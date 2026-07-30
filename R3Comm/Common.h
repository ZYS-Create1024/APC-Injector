#pragma once

#include <Windows.h>

// ©¤©¤ Device / symbolic link name ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
// Must match the driver's definitions in inject.cpp:
//   \\Device\\MyMonitor   /   \\??\\MyMonitorLink
#define DEVICE_SYMLINK L"\\\\.\\MyMonitorLink"

// ©¤©¤ Shared structure: matches AddressInfo.h in the driver ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
// Passed to IOCTL_SET_LOADLIBRARY_ADDRESS so the driver knows where
// LoadLibraryA lives in the target process.
typedef struct _BaseAddressInfo {
    ULONG64 LoadLibraryAddress;   // Address of LoadLibraryA/W in target
    ULONG64 Kernel32BaseAddress;  // Base of kernel32.dll
    ULONG64 NtdllBaseAddress;     // Base of ntdll.dll
} BaseAddressInfo;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
#ifdef MIDL_PASS
    [size_is(MaximumLength / 2), length_is((Length) / 2)] USHORT* Buffer;
#else // MIDL_PASS
    _Field_size_bytes_part_opt_(MaximumLength, Length) PWCH   Buffer;
#endif // MIDL_PASS
} UNICODE_STRING;
typedef UNICODE_STRING* PUNICODE_STRING;
typedef const UNICODE_STRING* PCUNICODE_STRING;

// ©¤©¤ IOCTL definitions ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
// Mirrors IOCTL.h in the driver.  CTL_CODE(DeviceType, Function, Method, Access)

#define IOCTL_SET_LOADLIBRARY_ADDRESS  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_SET_DLL_PATH             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_TURN_ON_PROCESS_CALLBACK  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_TURN_OFF_PROCESS_CALLBACK \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_PUSH_WHITE_LIST_ITEM      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_REMOVE_WHITE_LIST_ITEM    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_QUERY_WHITE_LIST_ITEM     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_READ_ACCESS)
