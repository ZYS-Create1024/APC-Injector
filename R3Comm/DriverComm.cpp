// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#include "DriverComm.h"

#include <cstdio>
#include <vector>

//                                                                                                                                           
//  Construction / Destruction
//                                                                                                                                           

DriverComm::DriverComm(DriverComm&& other) noexcept
    : m_hDevice(other.m_hDevice) {
    other.m_hDevice = INVALID_HANDLE_VALUE;
}

DriverComm& DriverComm::operator=(DriverComm&& other) noexcept {
    if (this != &other) {
        Close();
        m_hDevice = other.m_hDevice;
        other.m_hDevice = INVALID_HANDLE_VALUE;
    }
    return *this;
}

DriverComm::~DriverComm() {
    Close();
}

//                                                                                                                                           
//  Open / Close
//                                                                                                                                           

bool DriverComm::Open() {
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        return true;   // already open
    }

    if (!EnableDebugPrivilege()) {
        wprintf(L"[!] WARNING: Could not enable SeDebugPrivilege    "
            L"driver will reject IOCTLs.\n");
    }

    m_hDevice = CreateFileW(
        DEVICE_SYMLINK,
        GENERIC_READ | GENERIC_WRITE,
        0,                          // exclusive access
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        wprintf(L"[!] CreateFile failed (GLE = %lu). "
            L"Is the driver loaded?\n", GetLastError());
        return false;
    }

    wprintf(L"[+] Opened device %s\n", DEVICE_SYMLINK);
    return true;
}

void DriverComm::Close() {
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
        wprintf(L"[+] Closed device handle\n");
    }
}

bool DriverComm::IsOpen() const {
    return m_hDevice != INVALID_HANDLE_VALUE;
}

//                                                                                                                                           
//  SeDebugPrivilege
//                                                                                                                                           

bool DriverComm::EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
        &hToken)) {
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp,
        sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);
    return ok && GetLastError() == ERROR_SUCCESS;
}

//                                                                                                                                           
//  Core IOCTL sender
//                                                                                                                                           

bool DriverComm::SendIoctl(DWORD code,
    const void* inBuf, DWORD inSize,
    void* outBuf, DWORD outSize,
    DWORD* bytesReturned) {
    if (m_hDevice == INVALID_HANDLE_VALUE) {
        wprintf(L"[!] IOCTL 0x%X failed: device not opened\n", code);
        return false;
    }

    DWORD junk = 0;
    BOOL ok = DeviceIoControl(
        m_hDevice,
        code,
        const_cast<void*>(inBuf), inSize,
        outBuf, outSize,
        bytesReturned ? bytesReturned : &junk,
        nullptr);   // synchronous

    if (!ok) {
        wprintf(L"[!] IOCTL 0x%X failed (GLE = %lu)\n", code, GetLastError());
        return false;
    }
    return true;
}

//                                                                                                                                           
//  Injection configuration
//                                                                                                                                           

bool DriverComm::AutoResolveAndSetAddresses() {
    wprintf(L"[*] Auto-resolving addresses via GetModuleHandle + GetProcAddress...\n");

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");

    if (!hKernel32 || !hNtdll) {
        wprintf(L"[!] Failed to get module handles: k32=0x%p, ntdll=0x%p\n",
            hKernel32, hNtdll);
        return false;
    }

    // LoadLibraryA and LoadLibraryW share the same semantics for APC
    // injection    the driver passes a wide-char path, but LoadLibraryA
    // works as long as the path is within the same codepage.  For safety
    // we use LoadLibraryW.
    FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!pLoadLibrary) {
        wprintf(L"[!] GetProcAddress(LoadLibraryW) failed\n");
        return false;
    }

    BaseAddressInfo info = {};
    info.Kernel32BaseAddress = reinterpret_cast<ULONG64>(hKernel32);
    info.NtdllBaseAddress = reinterpret_cast<ULONG64>(hNtdll);
    info.LoadLibraryAddress = reinterpret_cast<ULONG64>(pLoadLibrary);

    wprintf(L"[+] kernel32 base : 0x%llX\n", info.Kernel32BaseAddress);
    wprintf(L"[+] ntdll base    : 0x%llX\n", info.NtdllBaseAddress);
    wprintf(L"[+] LoadLibraryW  : 0x%llX\n", info.LoadLibraryAddress);

    return SetLoadLibraryAddress(info);
}

bool DriverComm::SetLoadLibraryAddress(const BaseAddressInfo& info) {
    wprintf(L"[*] Setting LoadLibrary address: 0x%llX\n",
        info.LoadLibraryAddress);

    return SendIoctl(IOCTL_SET_LOADLIBRARY_ADDRESS,
        &info, sizeof(info),
        nullptr, 0);
}

bool DriverComm::SetDllPath(const std::wstring& path) {
    wprintf(L"[*] Setting DLL path: %s\n", path.c_str());

    // The driver expects a null-terminated wide string, byte length
    // includes the terminator.
    DWORD byteLen = static_cast<DWORD>((path.length() + 1) * sizeof(WCHAR));

    return SendIoctl(IOCTL_SET_DLL_PATH,
        path.c_str(), byteLen,
        nullptr, 0);
}

//                                                                                                                                           
//  Callback control
//                                                                                                                                           

bool DriverComm::TurnOnProcessCallback() {
    wprintf(L"[*] Turning ON process callback\n");

    if (!SendIoctl(IOCTL_TURN_ON_PROCESS_CALLBACK,
        nullptr, 0, nullptr, 0)) {
        wprintf(L"[!] Did you call SetLoadLibraryAddress + SetDllPath first?\n");
        return false;
    }

    wprintf(L"[+] Process callback is now ACTIVE\n");
    return true;
}

bool DriverComm::TurnOffProcessCallback() {
    wprintf(L"[*] Turning OFF process callback\n");

    if (!SendIoctl(IOCTL_TURN_OFF_PROCESS_CALLBACK,
        nullptr, 0, nullptr, 0)) {
        return false;
    }

    wprintf(L"[+] Process callback is now INACTIVE\n");
    return true;
}

//                                                                                                                                           
//  Whitelist management
//                                                                                                                                           

bool DriverComm::AddToWhitelist(const std::wstring& filePath) {
    wprintf(L"[*] Adding to whitelist: %s\n", filePath.c_str());

    // The driver IOCTL_PUSH_WHITE_LIST_ITEM expects a UNICODE_STRING.
    // On x64, UNICODE_STRING is 16 bytes:
    //   USHORT Length;
    //   USHORT MaximumLength;
    //   PWCH   Buffer;       // pointer to the actual string
    UNICODE_STRING us = {};
    us.Buffer = const_cast<PWCH>(filePath.c_str());
    us.Length = static_cast<USHORT>(filePath.length() * sizeof(WCHAR));
    us.MaximumLength = static_cast<USHORT>((filePath.length() + 1) * sizeof(WCHAR));

    return SendIoctl(IOCTL_PUSH_WHITE_LIST_ITEM,
        &us, sizeof(us),
        nullptr, 0);
}

bool DriverComm::RemoveFromWhitelist(const std::wstring& filePath) {
    wprintf(L"[*] Removing from whitelist: %s\n", filePath.c_str());

    UNICODE_STRING us = {};
    us.Buffer = const_cast<PWCH>(filePath.c_str());
    us.Length = static_cast<USHORT>(filePath.length() * sizeof(WCHAR));
    us.MaximumLength = static_cast<USHORT>((filePath.length() + 1) * sizeof(WCHAR));

    return SendIoctl(IOCTL_REMOVE_WHITE_LIST_ITEM,
        &us, sizeof(us),
        nullptr, 0);
}

std::optional<bool> DriverComm::QueryWhitelist(const std::wstring& filePath) {
    wprintf(L"[*] Querying whitelist: %s\n", filePath.c_str());

    UNICODE_STRING us = {};
    us.Buffer = const_cast<PWCH>(filePath.c_str());
    us.Length = static_cast<USHORT>(filePath.length() * sizeof(WCHAR));
    us.MaximumLength = static_cast<USHORT>((filePath.length() + 1) * sizeof(WCHAR));

    BOOLEAN present = FALSE;
    DWORD bytesRet = 0;
    if (!SendIoctl(IOCTL_QUERY_WHITE_LIST_ITEM,
        &us, sizeof(us),
        &present, sizeof(present),
        &bytesRet)) {
        return std::nullopt;
    }

    if (bytesRet == sizeof(BOOLEAN)) {
        wprintf(L"[+] Result: %s\n", present ? L"IN whitelist" : L"NOT in whitelist");
    }
    return static_cast<bool>(present);
}
