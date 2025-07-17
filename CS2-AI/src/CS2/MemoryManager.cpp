#include "CS2/MemoryManager.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <string>

// 字符串转换函数（UTF8 -> Unicode）
static std::wstring char_to_wstring(const char* str)
{
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (size_needed <= 1) return L"";
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstrTo[0], size_needed);
    wstrTo.pop_back(); // 移除最后的 '\0'
    return wstrTo;
}

MemoryManager::~MemoryManager() {}

bool MemoryManager::attach_to_process(const char* window_name)
{
    HWND handle = FindWindowA(nullptr, window_name);
    if (!handle)
        return false;

    DWORD process_ID = 0;
    GetWindowThreadProcessId(handle, &process_ID);
    m_pid = process_ID;

    if (!m_kernel.initialize()) {
        Logging::log_error("Failed to initialize kernel interface.");
        return false;
    }
    Logging::log_success("Process found process ID: " + std::to_string(process_ID));
    return true;
}

uintptr_t MemoryManager::get_module_address(const char* module_name)
{
    MODULEENTRY32W me32{};
    me32.dwSize = sizeof(MODULEENTRY32W);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (!Module32FirstW(hSnapshot, &me32)) {
        CloseHandle(hSnapshot);
        return 0;
    }
    std::wstring w_module_name = char_to_wstring(module_name);
    do {
        if (!_wcsicmp(me32.szModule, w_module_name.c_str())) {
            CloseHandle(hSnapshot);
            // 调试输出全部注释，避免Qt或非标准输出报错
            // if (debug_print) {
            //     printf("Module found!\n");
            // }
            return reinterpret_cast<uintptr_t>(me32.modBaseAddr);
        }
    } while (Module32NextW(hSnapshot, &me32));
    CloseHandle(hSnapshot);
    return 0;
}

void MemoryManager::print_4_byte_hex(uintptr_t number) const
{
#if defined(_WIN64)
    printf_s("0x%016llx", number);
#else
    printf_s("0x%08llx", number);
#endif
}

void MemoryManager::read_string_from_memory(uintptr_t address, char* buffer, size_t size, bool* success)
{
    if (!m_kernel.read_memory(m_pid, address, buffer, size)) {
        if (success) *success = false;
        if (debug_print) Logging::log_error("Kernel read_memory (string) failed!");
    }
    else if (success) {
        *success = true;
    }
}
