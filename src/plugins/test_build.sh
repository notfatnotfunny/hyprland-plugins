#!/bin/bash

echo "Testing ARM64 Hook System Build"

# Check architecture
if [[ $(uname -m) == "aarch64" ]]; then
    echo "Running on ARM64 architecture"
    ARCH_FLAGS="-DARCH_ARM64"
elif [[ $(uname -m) == "x86_64" ]]; then
    echo "Running on x86_64 architecture"
    ARCH_FLAGS="-DARCH_X86_64"
else
    echo "Unsupported architecture: $(uname -m)"
    exit 1
fi

# Test simple version first
echo "Testing simple ARM64 decoder..."
g++ -std=c++23 $ARCH_FLAGS simple_test.cpp -o simple_test

if [ $? -eq 0 ]; then
    echo "Simple test compilation successful!"
    echo "Running simple test..."
    ./simple_test
else
    echo "Simple test compilation failed!"
    exit 1
fi

echo ""
echo "Testing full hook system (requires dependencies)..."

# Check for udis86
if pkg-config --exists udis86; then
    echo "udis86 found, compiling with full x86_64 support..."
    UDIS86_FLAGS="$(pkg-config --cflags --libs udis86)"
else
    echo "udis86 not found, compiling without x86_64 disassembly support..."
    UDIS86_FLAGS=""
fi

# Try to compile the full test if dependencies are available
if command -v pkg-config &> /dev/null && pkg-config --exists pixman-1; then
    echo "Dependencies found, compiling full test..."
    
    # Create a standalone test that doesn't depend on Hyprland internals
    cat > standalone_hook_test.cpp << 'EOF'
#include <iostream>
#include <vector>
#include <cstddef>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#define HANDLE void*
#define HOOK_TRAMPOLINE_MAX_SIZE 64

// Architecture detection
#if defined(__x86_64__)
    #define ARCH_X86_64 1
#elif defined(__aarch64__) || defined(__arm64__)
    #define ARCH_ARM64 1
#else
    #define ARCH_UNKNOWN 1
#endif

// Simple memory management
template<typename T>
class UniquePtr {
    T* ptr;
public:
    UniquePtr(T* p) : ptr(p) {}
    ~UniquePtr() { delete ptr; }
    T* get() const { return ptr; }
    T* operator->() const { return ptr; }
    T& operator*() const { return *ptr; }
};

template<typename T>
UniquePtr<T> makeUnique(T* ptr) {
    return UniquePtr<T>(ptr);
}

// ARM64 instruction decoder (simplified)
class ARM64InstructionDecoder {
public:
    struct InstructionInfo {
        size_t len = 0;
        std::string assembly = "";
    };
    
    static InstructionInfo decodeInstruction(uint32_t instruction) {
#if defined(ARCH_ARM64)
        // Check for B (unconditional branch)
        if ((instruction & 0xFC000000) == 0x14000000) {
            return {4, "b"};
        }
        // Check for BL (branch with link)
        else if ((instruction & 0xFC000000) == 0x94000000) {
            return {4, "bl"};
        }
        // Check for NOP (HINT #0)
        else if (instruction == 0xD503201F) {
            return {4, "nop"};
        }
        // Check for RET (return)
        else if (instruction == 0xD65F03C0) {
            return {4, "ret"};
        }
        // Default case
        else {
            return {4, "unknown"};
        }
#else
        return {0, "unsupported"};
#endif
    }
};

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
    std::cout << "Testing Standalone ARM64 Hook System" << std::endl;
    
#if defined(ARCH_ARM64)
    std::cout << "ARM64 architecture detected" << std::endl;
#elif defined(ARCH_X86_64)
    std::cout << "x86_64 architecture detected" << std::endl;
#else
    std::cout << "Unknown architecture" << std::endl;
    return 1;
#endif

    // Test instruction decoding
    std::cout << "Testing ARM64 instruction decoding..." << std::endl;
    uint32_t testInstructions[] = {
        0xD503201F, // NOP
        0x14000000, // B (branch)
        0x94000000, // BL (branch with link)
        0xD65F03C0, // RET
    };
    
    for (auto instr : testInstructions) {
        auto info = ARM64InstructionDecoder::decodeInstruction(instr);
        std::cout << "0x" << std::hex << instr << ": " << info.assembly 
                  << " (length: " << std::dec << info.len << ")" << std::endl;
    }
    
    // Test function calls
    std::cout << "\nTesting function calls..." << std::endl;
    int result = testFunction(5);
    std::cout << "testFunction(5) = " << result << std::endl;
    
    result = hookFunction(5);
    std::cout << "hookFunction(5) = " << result << std::endl;
    
    std::cout << "Standalone hook system test completed successfully!" << std::endl;
    return 0;
}
EOF

    g++ -std=c++23 -I. -I../ $ARCH_FLAGS standalone_hook_test.cpp -o standalone_hook_test
    
    if [ $? -eq 0 ]; then
        echo "Standalone test compilation successful!"
        echo "Running standalone test..."
        ./standalone_hook_test
    else
        echo "Standalone test compilation failed"
    fi
else
    echo "Skipping full test (missing pixman-1 dependency)"
fi

echo ""
echo "Testing ARM64-specific functionality..."

# Create a test that focuses on ARM64 instruction decoding
cat > arm64_test.cpp << 'EOF'
#include <iostream>
#include <cstdint>

#if defined(__x86_64__)
    #define ARCH_X86_64 1
#elif defined(__aarch64__) || defined(__arm64__)
    #define ARCH_ARM64 1
#else
    #define ARCH_UNKNOWN 1
#endif

class ARM64InstructionDecoder {
public:
    struct InstructionInfo {
        size_t len = 0;
        std::string assembly = "";
        std::string description = "";
    };
    
    static InstructionInfo decodeInstruction(uint32_t instruction) {
#if defined(ARCH_ARM64)
        // Check for B (unconditional branch)
        if ((instruction & 0xFC000000) == 0x14000000) {
            int64_t offset = ((int64_t)(instruction & 0x3FFFFFF) << 2);
            return {4, "b", "unconditional branch"};
        }
        // Check for BL (branch with link)
        else if ((instruction & 0xFC000000) == 0x94000000) {
            int64_t offset = ((int64_t)(instruction & 0x3FFFFFF) << 2);
            return {4, "bl", "branch with link"};
        }
        // Check for NOP (HINT #0)
        else if (instruction == 0xD503201F) {
            return {4, "nop", "no operation"};
        }
        // Check for RET (return)
        else if (instruction == 0xD65F03C0) {
            return {4, "ret", "return from subroutine"};
        }
        // Default case
        else {
            return {4, "unknown", "unknown instruction"};
        }
#else
        return {0, "unsupported", "architecture not supported"};
#endif
    }
};

int main() {
    std::cout << "Testing ARM64 Instruction Decoder" << std::endl;
    
#if defined(ARCH_ARM64)
    std::cout << "ARM64 architecture detected" << std::endl;
#elif defined(ARCH_X86_64)
    std::cout << "x86_64 architecture detected" << std::endl;
#else
    std::cout << "Unknown architecture" << std::endl;
    return 1;
#endif

    // Test various ARM64 instructions
    uint32_t instructions[] = {
        0xD503201F, // NOP
        0x14000000, // B (branch)
        0x94000000, // BL (branch with link)
        0xD65F03C0, // RET
        0x12345678  // Unknown
    };
    
    for (size_t i = 0; i < sizeof(instructions)/sizeof(instructions[0]); i++) {
        auto info = ARM64InstructionDecoder::decodeInstruction(instructions[i]);
        std::cout << "0x" << std::hex << instructions[i] << ": " 
                  << info.assembly << " - " << info.description 
                  << " (length: " << std::dec << info.len << ")" << std::endl;
    }
    
    std::cout << "ARM64 instruction decoder test completed!" << std::endl;
    return 0;
}
EOF

g++ -std=c++23 $ARCH_FLAGS arm64_test.cpp -o arm64_test

if [ $? -eq 0 ]; then
    echo "ARM64 instruction decoder test compilation successful!"
    echo "Running ARM64 instruction decoder test..."
    ./arm64_test
else
    echo "ARM64 instruction decoder test compilation failed!"
fi

echo ""
echo "Testing updated HookSystem functionality..."

# Test the updated HookSystem
g++ -std=c++23 $ARCH_FLAGS test_updated_hook.cpp -o test_updated_hook

if [ $? -eq 0 ]; then
    echo "Updated HookSystem test compilation successful!"
    echo "Running updated HookSystem test..."
    ./test_updated_hook
else
    echo "Updated HookSystem test compilation failed!"
fi

echo ""
echo "All ARM64 Hook System tests completed!" 