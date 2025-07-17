#include "CS2/KernelInterface.h"

KernelInterface::KernelInterface() : m_driver_handle(INVALID_HANDLE_VALUE) {}
KernelInterface::~KernelInterface() {
    if (m_driver_handle != INVALID_HANDLE_VALUE)
        CloseHandle(m_driver_handle);
}
bool KernelInterface::initialize() {
    m_driver_handle = CreateFileA("\\\\.\\CS2Reader", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    return m_driver_handle != INVALID_HANDLE_VALUE;
}
bool KernelInterface::read_memory(DWORD pid, uintptr_t address, void* buffer, SIZE_T size) {
    if (m_driver_handle == INVALID_HANDLE_VALUE)
        return false;
    KERNEL_READ_REQUEST req{};
    req.pid = pid;
    req.address = address;
    req.size = size;
    req.buffer = buffer;
    DWORD bytesReturned;
    return DeviceIoControl(m_driver_handle, IOCTL_READ_MEMORY, &req, sizeof(req), &req, sizeof(req), &bytesReturned, nullptr);
}
