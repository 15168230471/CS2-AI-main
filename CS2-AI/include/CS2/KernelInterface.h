#pragma once
#include <Windows.h>
#include <cstdint>

#define IOCTL_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

struct KERNEL_READ_REQUEST {
    ULONG pid;
    uintptr_t address;
    SIZE_T size;
};

class KernelInterface {
public:
    KernelInterface();
    ~KernelInterface();

    bool initialize();
    bool read_memory(DWORD pid, uintptr_t address, void* buffer, SIZE_T size);

private:
    HANDLE m_driver_handle = INVALID_HANDLE_VALUE;
};
