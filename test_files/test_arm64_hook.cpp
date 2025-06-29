#include "HookSystem.hpp"
#include <iostream>
#include <cassert>

// Test function that we'll hook
int testFunction(int x) {
    return x * 2;
}

// Hook function that will replace testFunction
int hookFunction(int x) {
    std::cout << "Hook called with x = " << x << std::endl;
    return x * 3; // Different behavior to verify hook works
}

int main() {
    std::cout << "Testing ARM64 Hook System" << std::endl;
    
#if defined(ARCH_ARM64)
    std::cout << "ARM64 architecture detected" << std::endl;
#elif defined(ARCH_X86_64)
    std::cout << "x86_64 architecture detected" << std::endl;
#else
    std::cout << "Unknown architecture" << std::endl;
    return 1;
#endif

    // Initialize the hook system
    g_pFunctionHookSystem = makeUnique<CHookSystem>();
    
    // Test hooking using the public API
    CFunctionHook* hook = g_pFunctionHookSystem->initHook(nullptr, (void*)testFunction, (void*)hookFunction);
    
    if (hook) {
        std::cout << "Hook created successfully" << std::endl;
        
        // Test the hooked function
        int result = testFunction(5);
        std::cout << "testFunction(5) = " << result << std::endl;
        
        // Unhook
        bool unhookResult = g_pFunctionHookSystem->removeHook(hook);
        std::cout << "Unhook result: " << (unhookResult ? "SUCCESS" : "FAILED") << std::endl;
        
        // Test the original function
        int originalResult = testFunction(5);
        std::cout << "Original testFunction(5) = " << originalResult << std::endl;
    } else {
        std::cout << "Failed to create hook" << std::endl;
    }
    
    return 0;
} 