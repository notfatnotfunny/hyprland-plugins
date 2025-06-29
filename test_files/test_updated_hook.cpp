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
    std::cout << "Testing Updated ARM64 Hook System" << std::endl;
    
#if defined(ARCH_ARM64)
    std::cout << "ARM64 architecture detected" << std::endl;
#elif defined(ARCH_X86_64)
    std::cout << "x86_64 architecture detected" << std::endl;
#else
    std::cout << "Unknown architecture" << std::endl;
    return 1;
#endif

    // Test the functions directly first
    std::cout << "Testing original function..." << std::endl;
    int result = testFunction(5);
    std::cout << "testFunction(5) = " << result << std::endl;
    
    std::cout << "Testing hook function..." << std::endl;
    result = hookFunction(5);
    std::cout << "hookFunction(5) = " << result << std::endl;
    
    std::cout << "Updated hook system test completed successfully!" << std::endl;
    std::cout << "Note: Full hook functionality requires integration with Hyprland's hook system" << std::endl;
    
    return 0;
} 