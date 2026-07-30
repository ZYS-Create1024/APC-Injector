#pragma once

#include "Common.h"
#include <string>
#include <optional>

// ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
//  DriverComm
//
//  User-mode (R3) communication channel to the MyMonitor kernel driver.
//  All methods use DeviceIoControl under the hood and require the
//  caller to hold SeDebugPrivilege (the driver enforces this).
//
//  Typical usage:
//    DriverComm comm;
//    if (!comm.Open()) { /* error */ }
//    comm.SetDllPath(L"C:\\Tools\\InjectDll.dll");
//    comm.SetLoadLibraryAddress(&info);
//    comm.TurnOnProcessCallback();
//    // ... driver now injects DLL into new processes ...
//    comm.Close();
// ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤

class DriverComm {
public:
    DriverComm() = default;

    // Non-copyable, movable.
    DriverComm(const DriverComm&) = delete;
    DriverComm& operator=(const DriverComm&) = delete;
    DriverComm(DriverComm&& other) noexcept;
    DriverComm& operator=(DriverComm&& other) noexcept;

    ~DriverComm();

    // ©¤©¤ Connection management ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤

    /// Open a handle to the driver device.  Enables SeDebugPrivilege
    /// automatically ¡ª the driver requires it.
    /// Returns true on success.
    bool Open();

    /// Close the handle.  Safe to call multiple times.
    void Close();

    /// Returns true when a valid handle to the driver is held.
    bool IsOpen() const;

    // ©¤©¤ Injection configuration ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤

    /// Auto-resolve kernel32 base, ntdll base, and LoadLibrary address
    /// via GetModuleHandle + GetProcAddress, then send to driver.
    /// On x64 Windows these addresses are system-wide (same in every process),
    /// so resolving from R3Comm itself is safe.
    bool AutoResolveAndSetAddresses();

    /// Send base addresses to the driver (LoadLibrary, kernel32, ntdll).
    /// Prefer AutoResolveAndSetAddresses() unless you need manual values.
    bool SetLoadLibraryAddress(const BaseAddressInfo& info);

    /// Send the full path of the DLL to inject.
    /// Example: L"C:\\Tools\\MyInjectDll.dll"
    bool SetDllPath(const std::wstring& path);

    // ©¤©¤ Callback control ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤

    /// Enable process-creation notifications.
    /// Requires SetLoadLibraryAddress + SetDllPath to have been called first.
    bool TurnOnProcessCallback();

    /// Disable process-creation notifications.
    bool TurnOffProcessCallback();

    // ©¤©¤ Whitelist management ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤

    /// Add a file path to the driver's injection whitelist.
    bool AddToWhitelist(const std::wstring& filePath);

    /// Remove a file path from the driver's injection whitelist.
    bool RemoveFromWhitelist(const std::wstring& filePath);

    /// Query whether a file path is in the whitelist.
    /// Returns std::nullopt on communication error.
    std::optional<bool> QueryWhitelist(const std::wstring& filePath);

private:
    /// Acquire SeDebugPrivilege for the current process.
    static bool EnableDebugPrivilege();

    /// Core DeviceIoControl wrapper.
    bool SendIoctl(DWORD code,
        const void* inBuf, DWORD inSize,
        void* outBuf, DWORD outSize,
        DWORD* bytesReturned = nullptr);

    HANDLE m_hDevice = INVALID_HANDLE_VALUE;
};
