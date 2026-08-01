// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

/*
 * R3Comm ¡ª User-mode CLI tool for communicating with the MyMonitor
 * kernel driver.  Configures APC DLL injection, toggles the
 * process-creation callback, and manages the injection whitelist.
 */

#include "DriverComm.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>

static void PrintHelp() {
    wprintf(
        L"R3Comm ¡ª MyMonitor Driver Communication Tool\n"
        L"\n"
        L"COMMANDS:\n"
        L"  help                      Show this help\n"
        L"  status                    Check if driver is accessible\n"
        L"\n"
        L"  set-loadlib               Auto-resolve kernel32 / ntdll / LoadLibraryW\n"
        L"                            via GetModuleHandle + GetProcAddress and send\n"
        L"                            to driver. (No arguments.)\n"
        L"\n"
        L"  set-dll <path>            Set the DLL path for injection\n"
        L"       Example: C:\\Tools\\InjectDll.dll\n"
        L"\n"
        L"  callback-on               Enable process-creation callback (start injecting)\n"
        L"  callback-off              Disable process-creation callback (stop injecting)\n"
        L"\n"
        L"  whitelist-add    <path>   Add a file path to the injection whitelist\n"
        L"  whitelist-remove <path>   Remove a file path from the whitelist\n"
        L"  whitelist-query  <path>   Query whether a path is in the whitelist\n"
        L"\n"
        L"TYPICAL WORKFLOW:\n"
        L"  R3Comm.exe set-loadlib\n"
        L"  R3Comm.exe set-dll C:\\Tools\\InjectDll.dll\n"
        L"  R3Comm.exe callback-on\n"
        L"  (driver now injects DLL into every new process)\n"
        L"\n"
    );
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        PrintHelp();
        return 1;
    }

    const wchar_t* cmd = argv[1];

    // ©¤©¤ help ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"help") || 0 == _wcsicmp(cmd, L"-h")) {
        PrintHelp();
        return 0;
    }

    // ©¤©¤ status (no driver handle needed) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"status")) {
        DriverComm comm;
        wprintf(L"[*] Driver device : %s\n", DEVICE_SYMLINK);
        if (comm.Open()) {
            wprintf(L"[+] Driver is accessible.\n");
        } else {
            wprintf(L"[!] Cannot open driver. Is it loaded?\n");
        }
        return 0;
    }

    // ©¤©¤ All other commands need a driver handle ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    DriverComm comm;
    if (!comm.Open()) {
        wprintf(L"[!] Failed to open driver device.\n");
        return 2;
    }

    // ©¤©¤ set-loadlib ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"set-loadlib")) {
        return comm.AutoResolveAndSetAddresses() ? 0 : 3;
    }

    // ©¤©¤ set-dll ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"set-dll")) {
        if (argc < 3) {
            wprintf(L"[!] Usage: R3Comm.exe set-dll <dllPath>\n");
            return 1;
        }
        return comm.SetDllPath(argv[2]) ? 0 : 3;
    }

    // ©¤©¤ callback-on / callback-off ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"callback-on")) {
        return comm.TurnOnProcessCallback() ? 0 : 3;
    }
    if (0 == _wcsicmp(cmd, L"callback-off")) {
        return comm.TurnOffProcessCallback() ? 0 : 3;
    }

    // ©¤©¤ whitelist-add ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"whitelist-add")) {
        if (argc < 3) {
            wprintf(L"[!] Usage: R3Comm.exe whitelist-add <filePath>\n");
            return 1;
        }
        return comm.AddToWhitelist(argv[2]) ? 0 : 3;
    }

    // ©¤©¤ whitelist-remove ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"whitelist-remove")) {
        if (argc < 3) {
            wprintf(L"[!] Usage: R3Comm.exe whitelist-remove <filePath>\n");
            return 1;
        }
        return comm.RemoveFromWhitelist(argv[2]) ? 0 : 3;
    }

    // ©¤©¤ whitelist-query ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    if (0 == _wcsicmp(cmd, L"whitelist-query")) {
        if (argc < 3) {
            wprintf(L"[!] Usage: R3Comm.exe whitelist-query <filePath>\n");
            return 1;
        }
        auto result = comm.QueryWhitelist(argv[2]);
        if (!result.has_value()) return 3;
        wprintf(L"[+] %s\n", result.value() ? L"IN WHITELIST" : L"NOT IN WHITELIST");
        return 0;
    }

    // ©¤©¤ unknown ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    wprintf(L"[!] Unknown command: %s\n", cmd);
    wprintf(L"    Run 'R3Comm.exe help' for usage.\n");
    return 1;
}
