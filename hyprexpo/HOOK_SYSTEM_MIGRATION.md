# Hyprexpo Hook System Migration

This document describes the changes made to adapt the hyprexpo plugin to use the new function hook system in Hyprland.

## Changes Made

### 1. Updated main.cpp

**Before (Old Hook System):**
```cpp
// Old approach using CFunctionHook
inline CFunctionHook* g_pRenderWorkspaceHook = nullptr;
inline CFunctionHook* g_pAddDamageHookA = nullptr;
inline CFunctionHook* g_pAddDamageHookB = nullptr;

// Hook creation
g_pRenderWorkspaceHook = HyprlandAPI::createFunctionHook(PHANDLE, FNS[0].address, (void*)hkRenderWorkspace);
bool success = g_pRenderWorkspaceHook->hook();
```

**After (New Hook System):**
```cpp
// New approach using CHookSystem
inline CHookSystem* g_pHookSystem = nullptr;
inline void* g_pRenderWorkspaceOriginal = nullptr;

// Hook creation
g_pHookSystem = new CHookSystem();
g_pRenderWorkspaceOriginal = FNS[0].address;
if (!g_pHookSystem->hookFunction(g_pRenderWorkspaceOriginal, (void*)hkRenderWorkspace)) {
    // Handle error
}
```

### 2. Updated Hook Function Calls

**Before:**
```cpp
((origRenderWorkspace)(g_pRenderWorkspaceHook->m_original))(thisptr, pMonitor, pWorkspace, now, geometry);
```

**After:**
```cpp
typedef void (*origRenderWorkspace)(void*, PHLMONITOR, PHLWORKSPACE, timespec*, const CBox&);
((origRenderWorkspace)g_pRenderWorkspaceOriginal)(thisptr, pMonitor, pWorkspace, now, geometry);
```

### 3. Updated CMakeLists.txt

Added include directories for the new hook system:
```cmake
target_include_directories(hyprexpo PRIVATE 
    ${CMAKE_SOURCE_DIR}/../src/plugins
    ${CMAKE_SOURCE_DIR}/../src
)
```

### 4. Added Cleanup

Added proper cleanup in PLUGIN_EXIT():
```cpp
APICALL EXPORT void PLUGIN_EXIT() {
    g_pHyprRenderer->m_renderPass.removeAllOfType("COverviewPassElement");
    
    // Clean up the hook system
    if (g_pHookSystem) {
        delete g_pHookSystem;
        g_pHookSystem = nullptr;
    }
}
```

## Benefits of the New Hook System

1. **Better ARM64 Support**: The new hook system has improved ARM64 instruction decoding and PC-relative addressing support.

2. **More Reliable**: Uses a more robust approach to function hooking that works better across different architectures.

3. **Cleaner API**: Simplified interface for creating and managing function hooks.

4. **Better Error Handling**: More explicit error checking and reporting.

## Building

To build the updated hyprexpo plugin:

```bash
cd hyprexpo
./test_build.sh
```

Or manually:
```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Testing

The plugin should now work with the new hook system and provide better compatibility with ARM64 architectures. The functionality remains the same, but the underlying hooking mechanism is now more robust and reliable. 