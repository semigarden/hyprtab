#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

inline HANDLE PHANDLE = nullptr;
inline CHyprSignalListener g_windowTitleListener;
inline CHyprSignalListener g_windowOpenListener;
inline std::string g_format = "{tab}";

static constexpr std::array<std::string_view, 3> SEPARATORS = {
    " - ",
    " | ",
    " \xE2\x80\x94 ",
};

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
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
    const char* home = std::getenv("HOME");

    if (!home)
        return;

    const std::filesystem::path hyprConfigDir = std::string(home) + "/.config/hypr";
    const std::filesystem::path pluginDir = hyprConfigDir / "plugins";
    const std::filesystem::path configPath = pluginDir / "hyprtab.conf";

    std::error_code ec;
    const bool hyprExists = std::filesystem::exists(hyprConfigDir, ec);
    if (hyprExists && !std::filesystem::exists(configPath, ec)) {
        std::filesystem::create_directories(pluginDir, ec);

        std::ofstream output(configPath);
        if (output.is_open()) {
            output << "# Variables:\n";
            output << "# - {default}\n";
            output << "# - {tab}\n";
            output << "# - {app}\n";
            output << "# - {class}\n";
            output << "# - {initialTitle}\n";
            output << "# - {initialClass}\n";
            output << "\n";
            output << "format = \"{default}\"\n";
        }
    }

    std::ifstream input(configPath);

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

static void rewriteExistingWindows() {
    if (!g_pCompositor) return;

    for (const auto& window : g_pCompositor->m_windows) {
        rewriteTitle(window);
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string compositorHash = __hyprland_api_get_hash();
    const std::string clientHash     = __hyprland_api_get_client_hash();

    if (compositorHash != clientHash) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprtab] Mismatched headers, plugin disabled", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprtab] Version mismatch");
    }

    loadConfig();

    g_windowTitleListener = Event::bus()->m_events.window.title.listen([](PHLWINDOW window) { rewriteTitle(window); });
    g_windowOpenListener  = Event::bus()->m_events.window.open.listen([](PHLWINDOW window) { rewriteTitle(window); });

    rewriteExistingWindows();
    HyprlandAPI::addNotification(PHANDLE, "[hyprtab] Loaded", CHyprColor{0.2, 0.8, 0.3, 1.0}, 2500);

    return {"hyprtab", "Window title formatter", "hyprtab", "0.0.2"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_windowTitleListener.reset();
    g_windowOpenListener.reset();
}
