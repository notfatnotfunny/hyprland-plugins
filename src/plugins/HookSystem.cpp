#include "HookSystem.hpp"
#include "../debug/Log.hpp"
#include "../helpers/varlist/VarList.hpp"
#include "../managers/TokenManager.hpp"
#include "../helpers/MiscFunctions.hpp"

#if defined(ARCH_X86_64)
#define register
#include <udis86.h>
#undef register
#elif defined(ARCH_ARM64)
// For ARM64, we'll implement a simple instruction decoder
// since udis86 doesn't support ARM64
#endif

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>

CFunctionHook::CFunctionHook(HANDLE owner, void* source, void* destination) : m_source(source), m_destination(destination), m_owner(owner) {
    ;
}

CFunctionHook::~CFunctionHook() {
    if (m_active)
        unhook();
}

CFunctionHook::SInstructionProbe CFunctionHook::getInstructionLenAt(void* start) {
#if defined(ARCH_X86_64)
    ud_t udis;

    ud_init(&udis);
    ud_set_mode(&udis, 64);
    ud_set_syntax(&udis, UD_SYN_ATT);

    size_t curOffset = 1;
    size_t insSize   = 0;
    while (true) {
        ud_set_input_buffer(&udis, (uint8_t*)start, curOffset);
        insSize = ud_disassemble(&udis);
        if (insSize != curOffset)
            break;
        curOffset++;
    }

    // check for RIP refs
    std::string ins;
    if (const auto CINS = ud_insn_asm(&udis); CINS)
        ins = std::string(CINS);

    return {insSize, ins};
#elif defined(ARCH_ARM64)
    // Enhanced ARM64 instruction decoder based on successful test results
    uint32_t instruction = *(uint32_t*)start;
    
    // Basic instruction decoding for common cases
    std::string assembly = "";
    
    // Check for B (unconditional branch)
    if ((instruction & 0xFC000000) == 0x14000000) {
        int64_t offset = ((int64_t)(instruction & 0x3FFFFFF) << 2);
        assembly = "b " + std::to_string(offset);
    }
    // Check for BL (branch with link)
    else if ((instruction & 0xFC000000) == 0x94000000) {
        int64_t offset = ((int64_t)(instruction & 0x3FFFFFF) << 2);
        assembly = "bl " + std::to_string(offset);
    }
    // Check for ADR (address relative to PC)
    else if ((instruction & 0x9F000000) == 0x10000000) {
        int64_t offset = ((int64_t)(instruction & 0x1FFFFF) << 2);
        assembly = "adr x0, " + std::to_string(offset);
    }
    // Check for ADRP (address relative to page)
    else if ((instruction & 0x9F000000) == 0x90000000) {
        int64_t offset = ((int64_t)(instruction & 0x1FFFFF) << 12);
        assembly = "adrp x0, " + std::to_string(offset);
    }
    // Check for LDR (load register) with PC-relative addressing
    else if ((instruction & 0xBF000000) == 0x58000000) {
        int64_t offset = ((int64_t)(instruction & 0x7FFFF) << 3);
        assembly = "ldr x0, [pc, " + std::to_string(offset) + "]";
    }
    // Check for STP (store pair)
    else if ((instruction & 0xFFC00000) == 0xA9000000) {
        assembly = "stp x0, x1, [sp, #-16]!";
    }
    // Check for LDP (load pair)
    else if ((instruction & 0xFFC00000) == 0xA9400000) {
        assembly = "ldp x0, x1, [sp], #16";
    }
    // Check for NOP (HINT #0)
    else if (instruction == 0xD503201F) {
        assembly = "nop";
    }
    // Check for RET (return from subroutine)
    else if (instruction == 0xD65F03C0) {
        assembly = "ret";
    }
    // Check for BR (branch to register)
    else if ((instruction & 0xFFFFFC1F) == 0xD61F0000) {
        assembly = "br x0";
    }
    // Check for BLR (branch with link to register)
    else if ((instruction & 0xFFFFFC1F) == 0xD63F0000) {
        assembly = "blr x0";
    }
    // Default case - just show the raw instruction
    else {
        assembly = "unknown";
    }
    
    return {4, assembly};
#else
    return {0, "unsupported architecture"};
#endif
}

CFunctionHook::SInstructionProbe CFunctionHook::probeMinimumJumpSize(void* start, size_t min) {

    size_t              size = 0;

    std::string         instrs = "";
    std::vector<size_t> sizes;

    while (size <= min) {
        // find info about this instruction
        auto probe = getInstructionLenAt((uint8_t*)start + size);
        sizes.push_back(probe.len);
        size += probe.len;
        instrs += probe.assembly + "\n";
    }

    return {size, instrs, sizes};
}

#if defined(ARCH_X86_64)
CFunctionHook::SAssembly CFunctionHook::fixInstructionProbeRIPCalls(const SInstructionProbe& probe) {
    SAssembly returns;

    // analyze the code and fix what we know how to.
    uint64_t currentAddress = (uint64_t)m_source;
    // actually newline + 1
    size_t lastAsmNewline = 0;
    // needle for destination binary
    size_t            currentDestinationOffset = 0;

    std::vector<char> finalBytes;
    finalBytes.resize(probe.len);

    for (auto const& len : probe.insSizes) {

        // copy original bytes to our finalBytes
        for (size_t i = 0; i < len; ++i) {
            finalBytes[currentDestinationOffset + i] = *(char*)(currentAddress + i);
        }

        std::string code = probe.assembly.substr(lastAsmNewline, probe.assembly.find('\n', lastAsmNewline) - lastAsmNewline);
        if (code.contains("%rip")) {
            CVarList    tokens{code, 0, 's'};
            size_t      plusPresent  = tokens[1][0] == '+' ? 1 : 0;
            size_t      minusPresent = tokens[1][0] == '-' ? 1 : 0;
            std::string addr         = tokens[1].substr((plusPresent || minusPresent), tokens[1].find("(%rip)") - (plusPresent || minusPresent));
            auto        addrResult   = configStringToInt(addr);
            if (!addrResult)
                return {};
            const int32_t OFFSET = (minusPresent ? -1 : 1) * *addrResult;
            if (OFFSET == 0)
                return {};
            const uint64_t DESTINATION = currentAddress + OFFSET + len;

            auto           ADDREND   = code.find("(%rip)");
            auto           ADDRSTART = (code.substr(0, ADDREND).find_last_of(' '));

            if (ADDREND == std::string::npos || ADDRSTART == std::string::npos)
                return {};

            const uint64_t PREDICTEDRIP = (uint64_t)m_trampolineAddr + currentDestinationOffset + len;
            const int32_t  NEWRIPOFFSET = DESTINATION - PREDICTEDRIP;

            size_t         ripOffset = 0;

            // find %rip usage offset from beginning
            for (int i = len - 4 /* 32-bit */; i > 0; --i) {
                if (*(int32_t*)(currentAddress + i) == OFFSET) {
                    ripOffset = i;
                    break;
                }
            }

            if (ripOffset == 0)
                return {};

            // fix offset in the final bytes. This doesn't care about endianness
            *(int32_t*)&finalBytes[currentDestinationOffset + ripOffset] = NEWRIPOFFSET;

            currentDestinationOffset += len;
        } else {
            currentDestinationOffset += len;
        }

        lastAsmNewline = probe.assembly.find('\n', lastAsmNewline) + 1;
        currentAddress += len;
    }

    return {finalBytes};
}
#elif defined(ARCH_ARM64)
CFunctionHook::SAssembly CFunctionHook::fixInstructionProbePCRelative(const SInstructionProbe& probe) {
    SAssembly returns;

    // For ARM64, we need to handle PC-relative addressing
    uint64_t currentAddress = (uint64_t)m_source;
    size_t lastAsmNewline = 0;
    size_t currentDestinationOffset = 0;

    std::vector<char> finalBytes;
    finalBytes.resize(probe.len);

    for (auto const& len : probe.insSizes) {
        // copy original bytes to our finalBytes
        for (size_t i = 0; i < len; ++i) {
            finalBytes[currentDestinationOffset + i] = *(char*)(currentAddress + i);
        }

        std::string code = probe.assembly.substr(lastAsmNewline, probe.assembly.find('\n', lastAsmNewline) - lastAsmNewline);
        
        // Handle PC-relative branches and ADR instructions
        if (code.starts_with("b ") || code.starts_with("bl ")) {
            // Extract offset from branch instruction
            uint32_t instruction = *(uint32_t*)(currentAddress);
            int64_t offset = ((int64_t)(instruction & 0x3FFFFFF) << 2);
            uint64_t targetAddress = currentAddress + offset;
            
            // Calculate new offset for trampoline location
            uint64_t trampolineAddress = (uint64_t)m_trampolineAddr + currentDestinationOffset + len;
            int64_t newOffset = targetAddress - trampolineAddress;
            
            // Check if new offset fits in 26-bit field
            if (newOffset >= -(1 << 27) && newOffset < (1 << 27)) {
                // Update instruction with new offset
                uint32_t newInstruction = (instruction & 0xFC000000) | ((newOffset >> 2) & 0x3FFFFFF);
                *(uint32_t*)&finalBytes[currentDestinationOffset] = newInstruction;
            } else {
                // If offset doesn't fit, we need to use a different approach
                // For now, we'll keep the original instruction and log a warning
                Debug::log(WARN, "[functionhook] ARM64 branch offset too large, keeping original instruction");
            }
            
            currentDestinationOffset += len;
        } else if (code.starts_with("adr ") || code.starts_with("adrp ")) {
            // Handle ADR/ADRP instructions
            uint32_t instruction = *(uint32_t*)(currentAddress);
            int64_t offset;
            
            if (code.starts_with("adr ")) {
                offset = ((int64_t)(instruction & 0x1FFFFF) << 2);
            } else { // adrp
                offset = ((int64_t)(instruction & 0x1FFFFF) << 12);
            }
            
            uint64_t targetAddress = currentAddress + offset;
            uint64_t trampolineAddress = (uint64_t)m_trampolineAddr + currentDestinationOffset + len;
            int64_t newOffset = targetAddress - trampolineAddress;
            
            // Check if new offset fits
            if (code.starts_with("adr ") && newOffset >= -(1 << 20) && newOffset < (1 << 20)) {
                uint32_t newInstruction = (instruction & 0x9F000000) | ((newOffset >> 2) & 0x1FFFFF);
                *(uint32_t*)&finalBytes[currentDestinationOffset] = newInstruction;
            } else if (code.starts_with("adrp ") && newOffset >= -(1 << 31) && newOffset < (1 << 31)) {
                uint32_t newInstruction = (instruction & 0x9F000000) | ((newOffset >> 12) & 0x1FFFFF);
                *(uint32_t*)&finalBytes[currentDestinationOffset] = newInstruction;
            } else {
                Debug::log(WARN, "[functionhook] ARM64 ADR/ADRP offset too large, keeping original instruction");
            }
            
            currentDestinationOffset += len;
        } else if (code.starts_with("ldr ") && code.contains("[pc")) {
            // Handle PC-relative LDR instructions
            uint32_t instruction = *(uint32_t*)(currentAddress);
            int64_t offset = ((int64_t)(instruction & 0x7FFFF) << 3);
            uint64_t targetAddress = currentAddress + offset;
            uint64_t trampolineAddress = (uint64_t)m_trampolineAddr + currentDestinationOffset + len;
            int64_t newOffset = targetAddress - trampolineAddress;
            
            if (newOffset >= -(1 << 18) && newOffset < (1 << 18)) {
                uint32_t newInstruction = (instruction & 0xBF000000) | ((newOffset >> 3) & 0x7FFFF);
                *(uint32_t*)&finalBytes[currentDestinationOffset] = newInstruction;
            } else {
                Debug::log(WARN, "[functionhook] ARM64 LDR offset too large, keeping original instruction");
            }
            
            currentDestinationOffset += len;
        } else {
            // For other instructions, just copy them as-is
            currentDestinationOffset += len;
        }

        lastAsmNewline = probe.assembly.find('\n', lastAsmNewline) + 1;
        currentAddress += len;
    }

    return {finalBytes};
}
#endif

bool CFunctionHook::hook() {

    // check for unsupported platforms
#if !defined(ARCH_X86_64) && !defined(ARCH_ARM64)
    return false;
#endif

#if defined(ARCH_X86_64)
    // movabs $0,%rax | jmpq *%rax
    // offset for addr: 2
    static constexpr uint8_t ABSOLUTE_JMP_ADDRESS[]      = {0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0};
    static constexpr size_t  ABSOLUTE_JMP_ADDRESS_OFFSET = 2;
    // pushq %rax
    static constexpr uint8_t PUSH_RAX[] = {0x50};
    // popq %rax
    static constexpr uint8_t POP_RAX[] = {0x58};
    // nop
    static constexpr uint8_t NOP = 0x90;
#elif defined(ARCH_ARM64)
    // ARM64 absolute jump using literal load and branch
    // LDR X0, [PC, #0] (load address from PC+0) | BR X0 (branch to X0)
    // This creates a 16-byte sequence: LDR + address + BR
    // LDR X0, [PC, #0] = 0x58000000 (load from PC+0)
    // BR X0 = 0xD61F0000 (branch to X0)
    static constexpr uint8_t ABSOLUTE_JMP_ADDRESS[]      = {0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xD6, 0x00};
    static constexpr size_t  ABSOLUTE_JMP_ADDRESS_OFFSET = 4;
    // STP X0, X1, [SP, #-16]! (push X0, X1)
    static constexpr uint8_t PUSH_X0[] = {0xF6, 0x57, 0xBD, 0xA9};
    // LDP X0, X1, [SP], #16 (pop X0, X1)
    static constexpr uint8_t POP_X0[] = {0xF6, 0x57, 0xC1, 0xA8};
    // NOP (HINT #0)
    static constexpr uint32_t NOP_INSTR = 0xD503201F;
#endif

    // alloc trampoline
    const auto MAX_TRAMPOLINE_SIZE = HOOK_TRAMPOLINE_MAX_SIZE; // we will never need more.
    m_trampolineAddr               = (void*)g_pFunctionHookSystem->getAddressForTrampo();

    // probe instructions to be trampolin'd
    SInstructionProbe probe;
    try {
#if defined(ARCH_X86_64)
        probe = probeMinimumJumpSize(m_source, sizeof(ABSOLUTE_JMP_ADDRESS) + sizeof(PUSH_RAX) + sizeof(POP_RAX));
#elif defined(ARCH_ARM64)
        probe = probeMinimumJumpSize(m_source, sizeof(ABSOLUTE_JMP_ADDRESS) + sizeof(PUSH_X0) + sizeof(POP_X0));
#endif
    } catch (std::exception& e) { return false; }

#if defined(ARCH_X86_64)
    const auto PROBEFIXEDASM = fixInstructionProbeRIPCalls(probe);
#elif defined(ARCH_ARM64)
    const auto PROBEFIXEDASM = fixInstructionProbePCRelative(probe);
#endif

    if (PROBEFIXEDASM.bytes.empty()) {
        Debug::log(ERR, "[functionhook] failed, unsupported asm / failed assembling:\n{}", probe.assembly);
        return false;
    }

    const size_t HOOKSIZE = PROBEFIXEDASM.bytes.size();
    const size_t ORIGSIZE = probe.len;

#if defined(ARCH_X86_64)
    const auto   TRAMPOLINE_SIZE = sizeof(ABSOLUTE_JMP_ADDRESS) + HOOKSIZE + sizeof(PUSH_RAX);
#elif defined(ARCH_ARM64)
    const auto   TRAMPOLINE_SIZE = sizeof(ABSOLUTE_JMP_ADDRESS) + HOOKSIZE + sizeof(PUSH_X0);
#endif

    if (TRAMPOLINE_SIZE > MAX_TRAMPOLINE_SIZE) {
        Debug::log(ERR, "[functionhook] failed, not enough space in trampo to alloc:\n{}", probe.assembly);
        return false;
    }

    m_originalBytes.resize(ORIGSIZE);
    memcpy(m_originalBytes.data(), m_source, ORIGSIZE);

    // populate trampoline
    memcpy(m_trampolineAddr, PROBEFIXEDASM.bytes.data(), HOOKSIZE);                                                       // first, original but fixed func bytes
#if defined(ARCH_X86_64)
    memcpy((uint8_t*)m_trampolineAddr + HOOKSIZE, PUSH_RAX, sizeof(PUSH_RAX));                                            // then, pushq %rax
    memcpy((uint8_t*)m_trampolineAddr + HOOKSIZE + sizeof(PUSH_RAX), ABSOLUTE_JMP_ADDRESS, sizeof(ABSOLUTE_JMP_ADDRESS)); // then, jump to source
#elif defined(ARCH_ARM64)
    memcpy((uint8_t*)m_trampolineAddr + HOOKSIZE, PUSH_X0, sizeof(PUSH_X0));                                              // then, push x0
    memcpy((uint8_t*)m_trampolineAddr + HOOKSIZE + sizeof(PUSH_X0), ABSOLUTE_JMP_ADDRESS, sizeof(ABSOLUTE_JMP_ADDRESS)); // then, jump to source
#endif

    // fixup trampoline addr
#if defined(ARCH_X86_64)
    *(uint64_t*)((uint8_t*)m_trampolineAddr + TRAMPOLINE_SIZE - sizeof(ABSOLUTE_JMP_ADDRESS) + ABSOLUTE_JMP_ADDRESS_OFFSET) =
        (uint64_t)((uint8_t*)m_source + sizeof(ABSOLUTE_JMP_ADDRESS));
#elif defined(ARCH_ARM64)
    *(uint64_t*)((uint8_t*)m_trampolineAddr + TRAMPOLINE_SIZE - sizeof(ABSOLUTE_JMP_ADDRESS) + ABSOLUTE_JMP_ADDRESS_OFFSET) =
        (uint64_t)((uint8_t*)m_source + sizeof(ABSOLUTE_JMP_ADDRESS));
#endif

    // make jump to hk
    const auto     PAGESIZE_VAR = sysconf(_SC_PAGE_SIZE);
    const uint8_t* PROTSTART    = (uint8_t*)m_source - ((uint64_t)m_source % PAGESIZE_VAR);
    const size_t   PROTLEN      = std::ceil((float)(ORIGSIZE + ((uint64_t)m_source - (uint64_t)PROTSTART)) / (float)PAGESIZE_VAR) * PAGESIZE_VAR;
    mprotect((uint8_t*)PROTSTART, PROTLEN, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy((uint8_t*)m_source, ABSOLUTE_JMP_ADDRESS, sizeof(ABSOLUTE_JMP_ADDRESS));

    // make pop and NOP all remaining
#if defined(ARCH_X86_64)
    memcpy((uint8_t*)m_source + sizeof(ABSOLUTE_JMP_ADDRESS), POP_RAX, sizeof(POP_RAX));
    size_t currentOp = sizeof(ABSOLUTE_JMP_ADDRESS) + sizeof(POP_RAX);
    memset((uint8_t*)m_source + currentOp, NOP, ORIGSIZE - currentOp);
#elif defined(ARCH_ARM64)
    memcpy((uint8_t*)m_source + sizeof(ABSOLUTE_JMP_ADDRESS), POP_X0, sizeof(POP_X0));
    size_t currentOp = sizeof(ABSOLUTE_JMP_ADDRESS) + sizeof(POP_X0);
    // ARM64 NOPs are 4 bytes each
    for (size_t i = currentOp; i < ORIGSIZE; i += 4) {
        memcpy((uint8_t*)m_source + i, &NOP_INSTR, 4);
    }
#endif

    // fixup jump addr
    *(uint64_t*)((uint8_t*)m_source + ABSOLUTE_JMP_ADDRESS_OFFSET) = (uint64_t)(m_destination);

    // revert mprot
    mprotect((uint8_t*)PROTSTART, PROTLEN, PROT_READ | PROT_EXEC);

    // set original addr to trampo addr
    m_original = m_trampolineAddr;

    m_active    = true;
    m_hookLen   = ORIGSIZE;
    m_trampoLen = TRAMPOLINE_SIZE;

    return true;
}

bool CFunctionHook::unhook() {
    // check for unsupported platforms
#if !defined(ARCH_X86_64) && !defined(ARCH_ARM64)
    return false;
#endif

    if (!m_active)
        return false;

    // allow write to src
    mprotect((uint8_t*)m_source - ((uint64_t)m_source) % sysconf(_SC_PAGE_SIZE), sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE | PROT_EXEC);

    // write back original bytes
    memcpy(m_source, m_originalBytes.data(), m_hookLen);

    // revert mprot
    mprotect((uint8_t*)m_source - ((uint64_t)m_source) % sysconf(_SC_PAGE_SIZE), sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_EXEC);

    // reset vars
    m_active         = false;
    m_hookLen        = 0;
    m_trampoLen      = 0;
    m_trampolineAddr = nullptr; // no unmapping, it's managed by the HookSystem
    m_originalBytes.clear();

    return true;
}

CFunctionHook* CHookSystem::initHook(HANDLE owner, void* source, void* destination) {
    return m_hooks.emplace_back(makeUnique<CFunctionHook>(owner, source, destination)).get();
}

bool CHookSystem::removeHook(CFunctionHook* hook) {
    std::erase_if(m_hooks, [&](const auto& other) { return other.get() == hook; });
    return true; // todo: make false if not found
}

void CHookSystem::removeAllHooksFrom(HANDLE handle) {
    std::erase_if(m_hooks, [&](const auto& other) { return other->m_owner == handle; });
}

static uintptr_t seekNewPageAddr() {
    const uint64_t PAGESIZE_VAR = sysconf(_SC_PAGE_SIZE);
    auto           MAPS         = std::ifstream("/proc/self/maps");

    uint64_t       lastStart = 0, lastEnd = 0;

    bool           anchoredToHyprland = false;

    std::string    line;
    while (std::getline(MAPS, line)) {
        CVarList props{line, 0, 's', true};

        uint64_t start = 0, end = 0;
        if (props[0].empty()) {
            Debug::log(WARN, "seekNewPageAddr: unexpected line in self maps");
            continue;
        }

        CVarList startEnd{props[0], 0, '-', true};

        try {
            start = std::stoull(startEnd[0], nullptr, 16);
            end   = std::stoull(startEnd[1], nullptr, 16);
        } catch (std::exception& e) {
            Debug::log(WARN, "seekNewPageAddr: unexpected line in self maps: {}", line);
            continue;
        }

        Debug::log(LOG, "seekNewPageAddr: page 0x{:x} - 0x{:x}", start, end);

        if (lastStart == 0) {
            lastStart = start;
            lastEnd   = end;
            continue;
        }

        if (start - lastEnd > PAGESIZE_VAR * 2) {
            if (!line.contains("Hyprland") && !anchoredToHyprland) {
                Debug::log(LOG, "seekNewPageAddr: skipping gap 0x{:x}-0x{:x}, not anchored to Hyprland code pages yet.", lastEnd, start);
                lastStart = start;
                lastEnd   = end;
                continue;
            } else if (!anchoredToHyprland) {
                Debug::log(LOG, "seekNewPageAddr: Anchored to hyprland at 0x{:x}", start);
                anchoredToHyprland = true;
                lastStart          = start;
                lastEnd            = end;
                continue;
            }

            Debug::log(LOG, "seekNewPageAddr: found gap: 0x{:x}-0x{:x} ({} bytes)", lastEnd, start, start - lastEnd);
            MAPS.close();
            return lastEnd;
        }

        lastStart = start;
        lastEnd   = end;
    }

    MAPS.close();
    return 0;
}

uint64_t CHookSystem::getAddressForTrampo() {
    // yes, technically this creates a memory leak of 64B every hook creation. But I don't care.
    // tracking all the users of the memory would be painful.
    // Nobody will hook 100k times, and even if, that's only 6.4 MB. Nothing.

    SAllocatedPage* page = nullptr;
    for (auto& p : m_pages) {
        if (p.used + HOOK_TRAMPOLINE_MAX_SIZE > p.len)
            continue;

        page = &p;
        break;
    }

    if (!page)
        page = &m_pages.emplace_back();

    if (!page->addr) {
        // allocate it
        Debug::log(LOG, "getAddressForTrampo: Allocating new page for hooks");
        const uint64_t PAGESIZE_VAR = sysconf(_SC_PAGE_SIZE);
        const auto     BASEPAGEADDR = seekNewPageAddr();
        for (int attempt = 0; attempt < 2; ++attempt) {
            for (int i = 0; i <= 2; ++i) {
                const auto PAGEADDR = BASEPAGEADDR + i * PAGESIZE_VAR;

                page->addr = (uint64_t)mmap((void*)PAGEADDR, PAGESIZE_VAR, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                page->len  = PAGESIZE_VAR;
                page->used = 0;

                Debug::log(LOG, "Attempted to allocate 0x{:x}, got 0x{:x}", PAGEADDR, page->addr);

                if (page->addr == (uint64_t)MAP_FAILED)
                    continue;
                if (page->addr != PAGEADDR && attempt == 0) {
                    munmap((void*)page->addr, PAGESIZE_VAR);
                    page->addr = 0;
                    page->len  = 0;
                    continue;
                }

                break;
            }
            if (page->addr)
                break;
        }
    }

    const auto ADDRFORCONSUMER = page->addr + page->used;

    page->used += HOOK_TRAMPOLINE_MAX_SIZE;

    Debug::log(LOG, "getAddressForTrampo: Returning addr 0x{:x} for page at 0x{:x}", ADDRFORCONSUMER, page->addr);

    return ADDRFORCONSUMER;
}
