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
        printf("[����] ������ʧ�ܣ�GetLastError=%lu\n", GetLastError());
        return false;
    }
    printf("[��Ϣ] �����򿪳ɹ�\n");
    return true;
}

// �ؼ�������ṹ�壬�����buffer�������ṹ��ͷ
bool KernelInterface::read_memory(DWORD pid, uintptr_t address, void* buffer, SIZE_T size) {
    if (m_driver_handle == INVALID_HANDLE_VALUE) {
        printf("[����] ���������Ч\n");
        return false;
    }
    if (!buffer || size == 0) {
        printf("[����] bufferΪ�ջ���sizeΪ0\n");
        return false;
    }

    KERNEL_READ_REQUEST req{ pid, address, size };

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(
        m_driver_handle,
        IOCTL_READ_MEMORY,
        &req, sizeof(req),  // ����ṹ��
        buffer, size,       // ���buffer
        &bytesReturned, nullptr
    );
    /*if (!ok) {
        printf("[����] DeviceIoControl ����ʧ�ܣ�GetLastError=%lu\n", GetLastError());
        return false;
    }
    if (bytesReturned != size) {
        printf("[����] ʵ�ʷ��ص��ֽ���: %lu (����: %llu)\n", bytesReturned, size);
    }*/
    return true;
}
