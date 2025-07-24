#include "CS2/KernelInterface.h"
#include <cstdio>

KernelInterface::KernelInterface() = default;

KernelInterface::~KernelInterface() {
    if (m_driver_handle != INVALID_HANDLE_VALUE)
        CloseHandle(m_driver_handle);
}

bool KernelInterface::initialize() {
    m_driver_handle = CreateFileA("\\\\.\\AIsys", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (m_driver_handle == INVALID_HANDLE_VALUE) {
        printf("[错误] 打开驱动失败，GetLastError=%lu\n", GetLastError());
        return false;
    }
    printf("[信息] 驱动打开成功\n");
    return true;
}

// 关键！输入结构体，输出纯buffer，不带结构体头
bool KernelInterface::read_memory(DWORD pid, uintptr_t address, void* buffer, SIZE_T size) {
    if (m_driver_handle == INVALID_HANDLE_VALUE) {
        printf("[错误] 驱动句柄无效\n");
        return false;
    }
    if (!buffer || size == 0) {
        printf("[错误] buffer为空或者size为0\n");
        return false;
    }

    KERNEL_READ_REQUEST req{ pid, address, size };

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(
        m_driver_handle,
        IOCTL_READ_MEMORY,
        &req, sizeof(req),  // 输入结构体
        buffer, size,       // 输出buffer
        &bytesReturned, nullptr
    );
    /*if (!ok) {
        printf("[错误] DeviceIoControl 调用失败，GetLastError=%lu\n", GetLastError());
        return false;
    }
    if (bytesReturned != size) {
        printf("[警告] 实际返回的字节数: %lu (期望: %llu)\n", bytesReturned, size);
    }*/
    return true;
}
