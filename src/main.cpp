#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

inline HANDLE PHANDLE = nullptr;
inline CHyprSignalListener g_windowTitleListener;
inline CHyprSignalListener g_windowOpenListener;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());

    return value;
}

static std::string cleanTitle(const std::string& title) {
    static constexpr std::array<std::string_view, 3> SEPARATORS = {
        " - ",
        " | ",
        " \xE2\x80\x94 ",
    };

    size_t bestPos = std::string::npos;
    size_t bestLen = 0;

    for (const auto& sep : SEPARATORS) {
        const size_t pos = title.rfind(sep);

        if (pos == std::string::npos)
            continue;

        if (bestPos == std::string::npos || pos > bestPos) {
            bestPos = pos;
            bestLen = sep.size();
        }
    }

    if (bestPos == std::string::npos) return title;

    const std::string left = trim(title.substr(0, bestPos));
    const std::string right = trim(title.substr(bestPos + bestLen));

    if (left.empty() || right.empty()) return title;

    return left;
}

static bool rewriteTitle(PHLWINDOW window) {
    if (!window) return false;

    const std::string original = window->m_title.empty() ? window->fetchTitle() : window->m_title;

    if (original.empty()) return false;

    const std::string cleaned  = cleanTitle(original);

    if (cleaned == original) return false;

    window->m_title = cleaned;
    window->updateWindowDecos();
    window->updateToplevel();
    Event::bus()->m_events.window.title.emit(window);

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

    g_windowTitleListener = Event::bus()->m_events.window.title.listen([](PHLWINDOW window) { rewriteTitle(window); });
    g_windowOpenListener  = Event::bus()->m_events.window.open.listen([](PHLWINDOW window) { rewriteTitle(window); });

    rewriteExistingWindows();
    HyprlandAPI::addNotification(PHANDLE, "[hyprtab] Loaded", CHyprColor{0.2, 0.8, 0.3, 1.0}, 2500);

    return {"hyprtab", "Groupbar title formatter", "hyprtab", "0.0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_windowTitleListener.reset();
    g_windowOpenListener.reset();
}
