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
