#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <sys/inotify.h>
#include <unistd.h>

inline HANDLE PHANDLE = nullptr;
inline CHyprSignalListener g_windowTitleListener;
inline CHyprSignalListener g_windowOpenListener;
inline CHyprSignalListener g_configReloadedListener;
inline Hyprutils::OS::CFileDescriptor g_configWatchFD;
inline int g_configWatchWD = -1;
inline bool g_configWatchArmed = false;
inline std::string g_format = "{tab}";
inline std::optional<std::filesystem::file_time_type> g_configMtime;

static constexpr std::array<std::string_view, 3> SEPARATORS = {
    " - ",
    " | ",
    " \xE2\x80\x94 ",
};

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static std::filesystem::path configPath() {
    const char* home = std::getenv("HOME");
    if (!home)
        return {};
    return std::filesystem::path(home) / ".config" / "hypr" / "plugins" / "hyprtab.conf";
}

static std::filesystem::path configDir() {
    const auto cfg = configPath();
    if (cfg.empty())
        return {};
    return cfg.parent_path();
}

static std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());

    return value;
}

static std::string unquote(std::string value) {
    value = trim(std::move(value));

    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);

    return value;
}

static void replaceAll(std::string& text, const std::string_view from, const std::string_view to) {
    if (from.empty())
        return;

    size_t pos = 0;

    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

struct STitleParts {
    std::string full;
    std::string tab;
    std::string app;
};

static STitleParts splitTitle(const std::string& fullTitle) {
    STitleParts parts;
    parts.full = trim(fullTitle);
    parts.tab = parts.full;

    size_t bestPos = std::string::npos;
    size_t bestLen = 0;

    for (const auto& sep : SEPARATORS) {
        const size_t pos = parts.full.rfind(sep);

        if (pos == std::string::npos)
            continue;

        if (bestPos == std::string::npos || pos > bestPos) {
            bestPos = pos;
            bestLen = sep.size();
        }
    }

    if (bestPos == std::string::npos)
        return parts;

    const std::string left = trim(parts.full.substr(0, bestPos));
    const std::string right = trim(parts.full.substr(bestPos + bestLen));

    if (left.empty() || right.empty())
        return parts;

    parts.tab = left;
    parts.app = right;

    return parts;
}

static std::string formatTitle(const STitleParts& parts, PHLWINDOW window) {
    std::string formatted = g_format;

    replaceAll(formatted, "{default}", parts.full);
    replaceAll(formatted, "{tab}", parts.tab);
    replaceAll(formatted, "{app}", parts.app);
    replaceAll(formatted, "{class}", window ? window->m_class : "");
    replaceAll(formatted, "{initialTitle}", window ? window->m_initialTitle : "");
    replaceAll(formatted, "{initialClass}", window ? window->m_initialClass : "");

    formatted = trim(std::move(formatted));

    if (!formatted.empty())
        return formatted;

    if (!parts.tab.empty())
        return parts.tab;

    return parts.full;
}

static void loadConfig() {
    g_format = "{tab}";

    const std::filesystem::path cfg = configPath();
    if (cfg.empty())
        return;

    const std::filesystem::path hyprConfigDir = cfg.parent_path().parent_path();
    const std::filesystem::path pluginDir = hyprConfigDir / "plugins";

    std::error_code ec;
    const bool hyprExists = std::filesystem::exists(hyprConfigDir, ec) && std::filesystem::is_directory(hyprConfigDir, ec);
    if (hyprExists && !std::filesystem::exists(cfg, ec)) {
        std::filesystem::create_directories(pluginDir, ec);

        std::ofstream output(cfg);
        if (output.is_open()) {
            output << "# Variables:\n";
            output << "# - {default}\n";
            output << "# - {tab}\n";
            output << "# - {app}\n";
            output << "# - {class}\n";
            output << "# - {initialTitle}\n";
            output << "# - {initialClass}\n";
            output << "\n";
            output << "format = \"{tab}\"\n";
        }
    }

    std::ifstream input(cfg);

    if (!input.is_open())
        return;

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.starts_with('#'))
            continue;

        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;

        const std::string key = trim(line.substr(0, equals));
        if (key != "format")
            continue;

        const std::string value = unquote(line.substr(equals + 1));
        if (!value.empty())
            g_format = value;
    }

    g_configMtime = std::filesystem::last_write_time(cfg, ec);
}

static bool rewriteTitle(PHLWINDOW window) {
    if (!window) return false;

    const std::string source = window->fetchTitle().empty() ? window->m_title : window->fetchTitle();

    if (source.empty()) return false;

    const STitleParts parts = splitTitle(source);
    const std::string cleaned = formatTitle(parts, window);

    if (cleaned.empty() || cleaned == window->m_title) return false;

    window->m_title = cleaned;
    window->updateWindowDecos();
    window->updateToplevel();

    if (g_pHyprRenderer) g_pHyprRenderer->damageWindow(window, true);

    return true;
}

static void rewriteExistingWindows(bool refreshMetadata = false) {
    if (!g_pCompositor) return;

    for (const auto& window : g_pCompositor->m_windows) {
        if (refreshMetadata && window)
            window->onUpdateMeta();

        rewriteTitle(window);
    }
}

static void watchConfigAndReload() {
    const std::filesystem::path cfg = configPath();
    if (cfg.empty())
        return;

    std::error_code ec;
    if (!std::filesystem::exists(cfg, ec))
        return;

    const auto mtime = std::filesystem::last_write_time(cfg, ec);
    if (ec)
        return;

    if (!g_configMtime.has_value()) {
        g_configMtime = mtime;
        return;
    }

    if (mtime != *g_configMtime) {
        g_configMtime = mtime;
        loadConfig();
        rewriteExistingWindows(true);
    }
}

static void armConfigWatchReadable();

static void handleConfigWatchReadable() {
    if (!g_configWatchFD.isValid())
        return;

    constexpr size_t BUF_SIZE = 4096;
    alignas(inotify_event) char buffer[BUF_SIZE];
    bool maybeChanged = false;
    const auto cfgName = configPath().filename().string();

    while (true) {
        const ssize_t bytes = read(g_configWatchFD.get(), buffer, sizeof(buffer));
        if (bytes <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            break;
        }

        size_t offset = 0;
        while (offset < static_cast<size_t>(bytes)) {
            const auto* ev = reinterpret_cast<const inotify_event*>(buffer + offset);
            const std::string_view name = ev->len > 0 ? std::string_view(ev->name) : std::string_view{};

            if ((ev->mask & IN_Q_OVERFLOW) != 0 ||
                (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) != 0 ||
                (!name.empty() && name == cfgName))
                maybeChanged = true;

            offset += sizeof(inotify_event) + ev->len;
        }
    }

    if (maybeChanged)
        watchConfigAndReload();

    armConfigWatchReadable();
}

static void armConfigWatchReadable() {
    if (g_configWatchArmed || !g_pEventLoopManager || !g_configWatchFD.isValid())
        return;

    auto dup = g_configWatchFD.duplicate();
    if (!dup.isValid())
        return;

    g_configWatchArmed = true;
    g_pEventLoopManager->doOnReadable(std::move(dup), []() {
        g_configWatchArmed = false;
        handleConfigWatchReadable();
    });
}

static void setupConfigWatcher() {
    if (!g_pEventLoopManager)
        return;

    const auto dir = configDir();
    if (dir.empty())
        return;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0)
        return;

    g_configWatchFD = Hyprutils::OS::CFileDescriptor(fd);
    g_configWatchWD = inotify_add_watch(
        g_configWatchFD.get(),
        dir.c_str(),
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF
    );

    if (g_configWatchWD < 0) {
        g_configWatchFD.reset();
        return;
    }

    armConfigWatchReadable();
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string compositorHash = __hyprland_api_get_hash();
    const std::string clientHash     = __hyprland_api_get_client_hash();

    if (compositorHash != clientHash) {
        HyprlandAPI::addNotification(PHANDLE, "[Hyprtab] Mismatched headers, plugin disabled", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[Hyprtab] Version mismatch");
    }

    loadConfig();

    g_windowTitleListener = Event::bus()->m_events.window.title.listen([](PHLWINDOW window) { rewriteTitle(window); });
    g_windowOpenListener  = Event::bus()->m_events.window.open.listen([](PHLWINDOW window) { rewriteTitle(window); });
    g_configReloadedListener = Event::bus()->m_events.config.reloaded.listen([]() {
        loadConfig();
        rewriteExistingWindows(true);
    });
    setupConfigWatcher();

    rewriteExistingWindows(true);
    HyprlandAPI::addNotification(PHANDLE, "[Hyprtab] Loaded", CHyprColor{0.2, 0.8, 0.3, 1.0}, 2500);

    return {"hyprtab", "Format window titles", "hyprtab", "0.0.4"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_windowTitleListener.reset();
    g_windowOpenListener.reset();
    g_configReloadedListener.reset();
    if (g_configWatchFD.isValid() && g_configWatchWD >= 0)
        inotify_rm_watch(g_configWatchFD.get(), g_configWatchWD);
    g_configWatchWD = -1;
    g_configWatchFD.reset();
    g_configWatchArmed = false;
}
