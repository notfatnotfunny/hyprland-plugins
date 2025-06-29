#define WLR_USE_UNSTABLE

#include <unistd.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "globals.hpp"

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static bool pluginInitialized = false;
static bool overviewActive = false;
static std::unique_ptr<CFramebuffer> overviewFramebuffer = nullptr;
static CBox overviewBox = {0, 0, 0, 0};

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

    if (swipeActive || overviewActive)
        info.cancelled = true;

    if (!**PENABLE || e.fingers != **FINGERS || swipeDirection != 'v')
        return;

    info.cancelled = true;
    if (!swipeActive) {
        if (overviewActive && (**PPOSITIVE ? 1.0 : -1.0) * e.delta.y <= 0) {
            gestured = **PDISTANCE;
            swipeActive = true;
        }
        else if (!overviewActive && (**PPOSITIVE ? 1.0 : -1.0) * e.delta.y > 0) {
            overviewActive = true;
            gestured = 0;
            swipeActive = true;
            HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Overview activated via gesture", CHyprColor{0.2, 0.8, 0.2, 1.0}, 2000);
        }
        else {
            return;
        }
    }

    gestured += (**PPOSITIVE ? 1.0 : -1.0) * e.delta.y;
    if (gestured <= 0.01) {
        gestured = 0.01;
    }
    
    // Update overview based on gesture
    if (gestured >= **PDISTANCE * 0.8) {
        if (!overviewActive) {
            overviewActive = true;
            HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Overview fully activated", CHyprColor{0.2, 0.8, 0.2, 1.0}, 1000);
        }
    }
}

static void swipeEnd(void* self, SCallbackInfo& info, std::any param) {
    if (!overviewActive)
        return;

    swipeActive = false;
    info.cancelled = true;

    if (gestured >= 100) { // Threshold for selection
        HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Workspace would be selected here", CHyprColor{0.8, 0.8, 0.2, 1.0}, 2000);
    }
}

static void renderOverview() {
    if (!overviewActive || !overviewFramebuffer) return;
    
    const auto PMONITOR = g_pCompositor->m_lastMonitor.lock();
    if (!PMONITOR) return;
    
    // Get configuration
    static auto* const* PCOLUMNS = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:columns")->getDataStaticPtr();
    static auto* const* PGAPS    = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gap_size")->getDataStaticPtr();
    
    int SIDE_LENGTH = **PCOLUMNS;
    int GAP_WIDTH   = **PGAPS;
    
    // Calculate workspace grid
    Vector2D tileSize = PMONITOR->m_size / SIDE_LENGTH;
    Vector2D tileRenderSize = (PMONITOR->m_size - Vector2D{GAP_WIDTH * PMONITOR->m_scale, GAP_WIDTH * PMONITOR->m_scale} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    
    // Render each workspace
    for (int i = 0; i < SIDE_LENGTH * SIDE_LENGTH; ++i) {
        int workspaceID = i + 1; // Simple workspace numbering
        CBox workspaceBox = {
            (i % SIDE_LENGTH) * tileRenderSize.x + (i % SIDE_LENGTH) * GAP_WIDTH,
            (i / SIDE_LENGTH) * tileRenderSize.y + (i / SIDE_LENGTH) * GAP_WIDTH,
            tileRenderSize.x,
            tileRenderSize.y
        };
        
        // Get workspace
        auto PWORKSPACE = g_pCompositor->getWorkspaceByID(workspaceID);
        if (!PWORKSPACE) {
            PWORKSPACE = CWorkspace::create(workspaceID, PMONITOR, std::to_string(workspaceID));
        }
        
        // Highlight current workspace
        if (PWORKSPACE == PMONITOR->m_activeWorkspace) {
            // Draw border around current workspace
            g_pHyprOpenGL->renderRect(workspaceBox, CHyprColor{0.2, 0.8, 0.2, 1.0}, 0);
        }
        
        // Draw workspace background
        g_pHyprOpenGL->renderRect(workspaceBox, CHyprColor{0.1, 0.1, 0.1, 0.8}, 0);
        
        // Draw workspace number
        // (This would require text rendering which is complex, so we'll skip for now)
    }
}

static void onExpoDispatcher(std::string arg) {
    if (!pluginInitialized) return;

    if (swipeActive)
        return;
        
    if (arg == "select") { 
        if (overviewActive) {
            // Get mouse position and determine which workspace to select
            const auto mousePos = g_pInputManager->getMouseCoordsInternal();
            const auto PMONITOR = g_pCompositor->m_lastMonitor.lock();
            
            if (PMONITOR) {
                static auto* const* PCOLUMNS = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:columns")->getDataStaticPtr();
                static auto* const* PGAPS    = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gap_size")->getDataStaticPtr();
                
                int SIDE_LENGTH = **PCOLUMNS;
                int GAP_WIDTH   = **PGAPS;
                
                Vector2D tileSize = PMONITOR->m_size / SIDE_LENGTH;
                Vector2D tileRenderSize = (PMONITOR->m_size - Vector2D{GAP_WIDTH * PMONITOR->m_scale, GAP_WIDTH * PMONITOR->m_scale} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
                
                Vector2D localPos = mousePos - PMONITOR->m_position;
                int col = localPos.x / (tileRenderSize.x + GAP_WIDTH);
                int row = localPos.y / (tileRenderSize.y + GAP_WIDTH);
                int workspaceID = row * SIDE_LENGTH + col + 1;
                
                if (col >= 0 && col < SIDE_LENGTH && row >= 0 && row < SIDE_LENGTH) {
                    auto PWORKSPACE = g_pCompositor->getWorkspaceByID(workspaceID);
                    if (PWORKSPACE) {
                        g_pCompositor->changeWorkspaceByID(workspaceID);
                        HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Switched to workspace " + std::to_string(workspaceID), CHyprColor{0.2, 0.8, 0.2, 1.0}, 2000);
                    }
                }
            }
            
            overviewActive = false;
        }
        return;
    }
    
    if (arg == "toggle") {
        if (overviewActive) {
            overviewActive = false;
            HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Overview deactivated", CHyprColor{0.8, 0.2, 0.2, 1.0}, 2000);
        } else {
            overviewActive = true;
            HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Overview activated", CHyprColor{0.2, 0.8, 0.2, 1.0}, 2000);
        }
        return;
    }

    if (arg == "off" || arg == "close" || arg == "disable") {
        if (overviewActive) {
            overviewActive = false;
            HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Overview deactivated", CHyprColor{0.8, 0.2, 0.2, 1.0}, 2000);
        }
        return;
    }

    if (overviewActive)
        return;

    overviewActive = true;
    HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Overview activated", CHyprColor{0.2, 0.8, 0.2, 1.0}, 2000);
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
        // Visual ARM64-compatible initialization
        HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Initializing visual ARM64-compatible version...", CHyprColor{0.2, 0.8, 0.2, 1.0}, 2000);

        // Register callbacks
        static auto P = HyprlandAPI::registerCallbackDynamic(PHANDLE, "preRender", [](void* self, SCallbackInfo& info, std::any param) {
            if (overviewActive) {
                renderOverview();
            }
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

        HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Visual ARM64-compatible plugin initialized successfully!", CHyprColor{0.2, 0.8, 0.2, 1.0}, 3000);

        return {"hyprexpo", "A plugin for an overview (Visual ARM64 compatible)", "Vaxry", "1.0"};

    } catch (const std::exception& e) {
        failNotif("Exception during initialization: " + std::string(e.what()));
        throw;
    }
}

APICALL EXPORT void PLUGIN_EXIT() {
    pluginInitialized = false;
    overviewActive = false;
    overviewFramebuffer.reset();
} 