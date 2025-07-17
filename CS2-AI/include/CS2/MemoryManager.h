#pragma once
#include <Windows.h>
#include <psapi.h>
#include <TlHelp32.h>
#include <string>
#include "Utility/Logging.h"
#include "CS2/Offsets.h"
#include "CS2/KernelInterface.h" // Ensure KernelInterface is included

class MemoryManager
{
public:
    MemoryManager(const Offsets& offs) : m_offsets(offs), m_kernel() {} // Initialize m_kernel
    MemoryManager() = default;
    ~MemoryManager();

    bool attach_to_process(const char* window_name);
    uintptr_t get_module_address(const char* module_name);
    void print_4_byte_hex(uintptr_t address) const;
    void read_string_from_memory(uintptr_t address, char* buffer, size_t size, bool* success = nullptr);

    template <typename type>
    [[nodiscard]] type read_memory(uintptr_t address, bool* success = nullptr)
    {
        type result{};
        if (!m_kernel.read_memory(m_pid, address, &result, sizeof(type))) { // Use m_kernel
            if (success) *success = false;
            if (debug_print) Logging::log_error("Kernel read_memory failed!");
        }
        else if (success) {
            *success = true;
        }
        return result;
    }

private:
    Offsets m_offsets;
    DWORD m_pid = 0;
    KernelInterface m_kernel; // Declare m_kernel as a member of type KernelInterface
    static constexpr bool debug_print = false;
};