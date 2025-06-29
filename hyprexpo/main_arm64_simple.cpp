#define WLR_USE_UNSTABLE

#include <unistd.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "globals.hpp"
#include "overview.hpp"

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static bool renderingOverview = false;
static bool pluginInitialized = false;

static float gestured       = 0;
bool         swipeActive    = false;
char         swipeDirection = 0; // 0 = no direction, 'v' = vertical, 'h' = horizontal

static void  swipeBegin(void* self, SCallbackInfo& info, std::any param) {
    swipeActive    = false;
    swipeDirection = 0;
}

static void swipeUpdate(void* self, SCallbackInfo& info, std::any param) {
    if (!pluginInitialized) return;
    
    static auto* const* PENABLE   = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:enable_gesture")->getDataStaticPtr();
    static auto* const* FINGERS   = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gesture_fingers")->getDataStaticPtr();
    static auto* const* PPOSITIVE = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gesture_positive")->getDataStaticPtr();
    static auto* const* PDISTANCE = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gesture_distance")->getDataStaticPtr();
    auto                e         = std::any_cast<IPointer::SSwipeUpdateEvent>(param);

    if (!swipeDirection) {
        if (std::abs(e.delta.x) > std::abs(e.delta.y))
            swipeDirection = 'h';
        else if (std::abs(e.delta.y) > std::abs(e.delta.x))
            swipeDirection = 'v';
        else
            swipeDirection = 0;
    }

    if (swipeActive || g_pOverview)
        info.cancelled = true;

    if (!**PENABLE || e.fingers != **FINGERS || swipeDirection != 'v')
        return;

    info.cancelled = true;
    if (!swipeActive) {
        if (g_pOverview && (**PPOSITIVE ? 1.0 : -1.0) * e.delta.y <= 0) {
            renderingOverview = true;
            g_pOverview       = std::make_unique<COverview>(g_pCompositor->m_lastMonitor->m_activeWorkspace, true);
            renderingOverview = false;
            gestured          = **PDISTANCE;
            swipeActive       = true;
        }

        else if (!g_pOverview && (**PPOSITIVE ? 1.0 : -1.0) * e.delta.y > 0) {
            renderingOverview = true;
            g_pOverview       = std::make_unique<COverview>(g_pCompositor->m_lastMonitor->m_activeWorkspace, true);
            renderingOverview = false;
            gestured          = 0;
            swipeActive       = true;
        }

        else {
            return;
        }
    }

    gestured += (**PPOSITIVE ? 1.0 : -1.0) * e.delta.y;
    if (gestured <= 0.01) // plugin will crash if swipe ends at <= 0
        gestured = 0.01;
    g_pOverview->onSwipeUpdate(gestured);
}

static void swipeEnd(void* self, SCallbackInfo& info, std::any param) {
    if (!g_pOverview)
        return;

    swipeActive    = false;
    info.cancelled = true;

    g_pOverview->onSwipeEnd();
}

static void onExpoDispatcher(std::string arg) {
    if (!pluginInitialized) return;

    if (swipeActive)
        return;
    if (arg == "select") { 
        if (g_pOverview) {
            g_pOverview->selectHoveredWorkspace();
            g_pOverview->close();
        }
        return;
    }
    if (arg == "toggle") {
        if (g_pOverview)
            g_pOverview->close();
        else {
            renderingOverview = true;
            g_pOverview       = std::make_unique<COverview>(g_pCompositor->m_lastMonitor->m_activeWorkspace);
            renderingOverview = false;
        }
        return;
    }

    if (arg == "off" || arg == "close" || arg == "disable") {
        if (g_pOverview)
            g_pOverview->close();
        return;
    }

    if (g_pOverview)
        return;

    renderingOverview = true;
    g_pOverview       = std::make_unique<COverview>(g_pCompositor->m_lastMonitor->m_activeWorkspace);
    renderingOverview = false;
}

static void failNotif(const std::string& reason) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Failure in initialization: " + reason, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH) {
        failNotif("Version mismatch (headers ver is not equal to running hyprland ver)");
        throw std::runtime_error("[he] Version mismatch");
    }

    try {
        // Simple ARM64-compatible initialization without hooks
        HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Initializing simple ARM64-compatible version...", CHyprColor{0.2, 0.8, 0.2, 1.0}, 2000);

        // Register callbacks only (no hooks)
        static auto P = HyprlandAPI::registerCallbackDynamic(PHANDLE, "preRender", [](void* self, SCallbackInfo& info, std::any param) {
            if (!g_pOverview)
                return;
            g_pOverview->onPreRender();
        });

        static auto P2 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeBegin", [](void* self, SCallbackInfo& info, std::any data) { swipeBegin(self, info, data); });
        static auto P3 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeEnd", [](void* self, SCallbackInfo& info, std::any data) { swipeEnd(self, info, data); });
        static auto P4 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeUpdate", [](void* self, SCallbackInfo& info, std::any data) { swipeUpdate(self, info, data); });

        HyprlandAPI::addDispatcher(PHANDLE, "hyprexpo:expo", onExpoDispatcher);

        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:columns", Hyprlang::INT{3});
        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:gap_size", Hyprlang::INT{5});
        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:bg_col", Hyprlang::INT{0xFF111111});
        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:workspace_method", Hyprlang::STRING{"center current"});

        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:enable_gesture", Hyprlang::INT{1});
        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:gesture_distance", Hyprlang::INT{200});
        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:gesture_positive", Hyprlang::INT{1});
        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprexpo:gesture_fingers", Hyprlang::INT{4});

        HyprlandAPI::reloadConfig();

        pluginInitialized = true;

        HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Simple ARM64-compatible plugin initialized successfully!", CHyprColor{0.2, 0.8, 0.2, 1.0}, 3000);

        return {"hyprexpo", "A plugin for an overview (Simple ARM64 compatible)", "Vaxry", "1.0"};

    } catch (const std::exception& e) {
        failNotif("Exception during initialization: " + std::string(e.what()));
        throw;
    }
}

APICALL EXPORT void PLUGIN_EXIT() {
    pluginInitialized = false;
    
    if (g_pOverview) {
        g_pOverview->close();
        g_pOverview.reset();
    }
    
    g_pHyprRenderer->m_renderPass.removeAllOfType("COverviewPassElement");
} 