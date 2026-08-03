// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

/*
 * R3Comm - User-mode CLI tool for communicating with the MyMonitor
 * kernel driver.  Configures APC DLL injection, toggles the
 * process-creation callback, and manages the injection whitelist.
 */

#include "DriverComm.h"
#include "Config.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>

// ---------------------------------------------------------------------------
//  ResolveConfigPath
// ---------------------------------------------------------------------------
static std::wstring ResolveConfigPath(const std::wstring* explicitPath) {
    if (explicitPath && !explicitPath->empty()) {
        return *explicitPath;
    }
    // Check R3COMM_CONFIG environment variable.
    wchar_t envBuf[MAX_PATH] = {};
    DWORD envLen = GetEnvironmentVariableW(L"R3COMM_CONFIG", envBuf,
                                           static_cast<DWORD>(_countof(envBuf)));
    if (envLen > 0 && envLen < _countof(envBuf)) {
        return std::wstring(envBuf);
    }
    if (envLen >= _countof(envBuf)) {
        wprintf(L"[!] R3COMM_CONFIG value exceeds %zu chars, using default config path.\n",
                _countof(envBuf) - 1);
    }
    return config::DefaultPath();
}

// ---------------------------------------------------------------------------
//  PrintHelp
// ---------------------------------------------------------------------------
static void PrintHelp() {
    wprintf(
        L"R3Comm - MyMonitor Driver Communication Tool\n"
        L"\n"
        L"USAGE:\n"
        L"  R3Comm.exe [--config <file>] <command> [args...]\n"
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
        L"  apply                     Load config and apply all settings to driver\n"
        L"  config-show               Display config file path and parsed contents\n"
        L"  config-set <key> <value>  Set a config value and save to file\n"
        L"                            Keys: dll_path, set_loadlib, enable_callback,\n"
        L"                                  whitelist (append), whitelist-remove,\n"
        L"                                  whitelist-clear\n"
        L"\n"
        L"ONE-STEP WORKFLOW:\n"
        L"  R3Comm.exe config-set dll_path C:\\Tools\\InjectDll.dll\n"
        L"  R3Comm.exe apply\n"
        L"\n"
        L"TYPICAL WORKFLOW (manual):\n"
        L"  R3Comm.exe set-loadlib\n"
        L"  R3Comm.exe set-dll C:\\Tools\\InjectDll.dll\n"
        L"  R3Comm.exe callback-on\n"
        L"  (driver now injects DLL into every new process)\n"
        L"\n"
    );
}

// ---------------------------------------------------------------------------
//  wmain
// ---------------------------------------------------------------------------
int wmain(int argc, wchar_t* argv[]) {
    // ---- Parse optional --config <path> / -c <path> ----
    std::wstring explicitConfigPath;
    int cmdIdx = 1;

    if (argc >= 3 && (0 == _wcsicmp(argv[1], L"--config") ||
                      0 == _wcsicmp(argv[1], L"-c"))) {
        explicitConfigPath = argv[2];
        cmdIdx = 3;
    }

    if (cmdIdx >= argc) {
        PrintHelp();
        return 1;
    }

    const wchar_t* cmd = argv[cmdIdx];

    // ---- help ----
    if (0 == _wcsicmp(cmd, L"help") || 0 == _wcsicmp(cmd, L"-h")) {
        PrintHelp();
        return 0;
    }

    // ---- status (no driver handle needed) ----
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

    // Resolve effective config path (used by config-* and apply).
    const std::wstring configPath = ResolveConfigPath(
        explicitConfigPath.empty() ? nullptr : &explicitConfigPath);

    // ---- config-show (no driver needed) ----
    if (0 == _wcsicmp(cmd, L"config-show")) {
        wprintf(L"[*] Config file: %s\n", configPath.c_str());

        AppConfig cfg;
        std::wstring err;
        std::size_t errLine = 0;

        if (!config::Load(configPath, cfg, err, errLine)) {
            if (errLine > 0) {
                wprintf(L"[!] Parse error at line %zu: %s\n", errLine, err.c_str());
                return 5;
            }
            // File not found is not an error for config-show; show defaults.
            wprintf(L"[!] %s\n", err.c_str());
            wprintf(L"[*] Showing defaults:\n");
        }

        wprintf(L"%s", config::Dump(cfg).c_str());
        return 0;
    }

    // ---- config-set <key> [value...] (no driver needed) ----
    if (0 == _wcsicmp(cmd, L"config-set")) {
        if (argc - cmdIdx < 2) {
            wprintf(L"[!] Usage: R3Comm.exe config-set <key> [value]\n");
            wprintf(L"    Keys: dll_path, set_loadlib, enable_callback,\n");
            wprintf(L"          whitelist, whitelist-remove, whitelist-clear\n");
            return 1;
        }

        const wchar_t* key = argv[cmdIdx + 1];

        // Join remaining args as the value (supports paths with spaces).
        std::wstring value;
        for (int i = cmdIdx + 2; i < argc; ++i) {
            if (i > cmdIdx + 2) value += L' ';
            value += argv[i];
        }

        // Load existing config (or defaults if file missing).
        AppConfig cfg;
        std::wstring err;
        std::size_t errLine = 0;
        if (!config::Load(configPath, cfg, err, errLine) && errLine > 0) {
            wprintf(L"[!] Parse error in existing config at line %zu: %s\n",
                    errLine, err.c_str());
            return 5;
        }

        // Update the matching key.
        // Whitelist add/remove also sync to driver immediately when possible.
        bool syncToDriver = false;
        bool syncIsRemove = false;
        std::wstring syncPath;
        if (_wcsicmp(key, L"dll_path") == 0) {
            cfg.dllPath = value;
            wprintf(L"[+] Set dll_path = \"%s\"\n", value.c_str());
        } else if (_wcsicmp(key, L"set_loadlib") == 0) {
            if (!config::ParseBool(value, cfg.setLoadlib, err)) {
                wprintf(L"[!] %s\n", err.c_str());
                return 1;
            }
            wprintf(L"[+] Set set_loadlib = %s\n",
                    cfg.setLoadlib ? L"true" : L"false");
        } else if (_wcsicmp(key, L"enable_callback") == 0) {
            if (!config::ParseBool(value, cfg.enableCallback, err)) {
                wprintf(L"[!] %s\n", err.c_str());
                return 1;
            }
            wprintf(L"[+] Set enable_callback = %s\n",
                    cfg.enableCallback ? L"true" : L"false");
        } else if (_wcsicmp(key, L"whitelist") == 0) {
            if (value.empty()) {
                wprintf(L"[!] whitelist requires a path value.\n");
                return 1;
            }
            cfg.whitelist.push_back(value);
            wprintf(L"[+] Added to whitelist: \"%s\" (%zu total)\n",
                    value.c_str(), cfg.whitelist.size());
            syncToDriver = true;
            syncIsRemove = false;
            syncPath = value;
        } else if (_wcsicmp(key, L"whitelist-remove") == 0) {
            auto& wl = cfg.whitelist;
            auto it = std::find_if(wl.begin(), wl.end(),
                [&value](const std::wstring& e) {
                    return _wcsicmp(e.c_str(), value.c_str()) == 0;
                });
            if (it != wl.end()) {
                wprintf(L"[+] Removed from whitelist: \"%s\"\n", it->c_str());
                wl.erase(it);
                syncToDriver = true;
                syncIsRemove = true;
                syncPath = value;
            } else {
                wprintf(L"[!] Not in whitelist: \"%s\"\n", value.c_str());
            }
        } else if (_wcsicmp(key, L"whitelist-clear") == 0) {
            std::size_t count = cfg.whitelist.size();
            cfg.whitelist.clear();
            wprintf(L"[+] Cleared %zu whitelist entries\n", count);
        } else {
            wprintf(L"[!] Unknown config key: %s\n", key);
            wprintf(L"    Valid keys: dll_path, set_loadlib, enable_callback,\n");
            wprintf(L"               whitelist, whitelist-remove, whitelist-clear\n");
            return 1;
        }

        // Save config file first.
        if (!config::Save(configPath, cfg, err)) {
            wprintf(L"[!] %s\n", err.c_str());
            return 6;
        }
        wprintf(L"[+] Config saved to: %s\n", configPath.c_str());

        // Sync whitelist add/remove to driver immediately.
        if (syncToDriver) {
            DriverComm comm;
            if (comm.Open()) {
                bool ok = syncIsRemove
                    ? comm.RemoveFromWhitelist(syncPath)
                    : comm.AddToWhitelist(syncPath);
                if (ok) {
                    wprintf(L"[+] Driver %s: %s\n",
                            syncIsRemove ? L"removed" : L"added",
                            syncPath.c_str());
                }
            } else {
                wprintf(L"[!] Driver not accessible; whitelist change saved to config only.\n");
            }
        }
        return 0;
    }

    // ---- apply (needs driver; opens it after successful config parse) ----
    if (0 == _wcsicmp(cmd, L"apply")) {
        // 1. Load config.
        AppConfig cfg;
        std::wstring err;
        std::size_t errLine = 0;

        if (!config::Load(configPath, cfg, err, errLine)) {
            if (errLine > 0) {
                wprintf(L"[!] Parse error at line %zu: %s\n", errLine, err.c_str());
                return 5;
            }
            wprintf(L"[!] %s\n", err.c_str());
            wprintf(L"[*] Use 'config-set' to create a config, or create '%s' manually.\n",
                    configPath.c_str());
            return 4;
        }

        wprintf(L"[*] Applying config from: %s\n", configPath.c_str());

        // 2. Open driver.
        DriverComm comm;
        if (!comm.Open()) {
            wprintf(L"[!] Failed to open driver device.\n");
            return 2;
        }

        // 3. Auto-resolve and set LoadLibrary addresses.
        if (cfg.setLoadlib) {
            if (!comm.AutoResolveAndSetAddresses()) {
                wprintf(L"[!] Failed to resolve and set LoadLibrary addresses.\n");
                return 3;
            }
        }

        // 4. Set DLL path.
        if (!cfg.dllPath.empty()) {
            if (!comm.SetDllPath(cfg.dllPath)) {
                wprintf(L"[!] Failed to set DLL path.\n");
                return 3;
            }
        } else {
            wprintf(L"[!] WARNING: dll_path is empty. No DLL will be injected.\n");
        }

        // 5. Add whitelist entries (best-effort).
        for (const auto& entry : cfg.whitelist) {
            if (!comm.AddToWhitelist(entry)) {
                wprintf(L"[!] WARNING: Failed to add whitelist entry: %s\n", entry.c_str());
                // Continue    don't abort on individual whitelist failure.
            }
        }

        // 6. Enable process-creation callback.
        if (cfg.enableCallback) {
            if (cfg.dllPath.empty() && !cfg.setLoadlib) {
                wprintf(L"[!] Cannot enable callback: dll_path is empty "
                        L"and set_loadlib is false.\n");
                return 3;
            }
            if (!comm.TurnOnProcessCallback()) {
                wprintf(L"[!] Failed to enable process callback.\n");
                return 3;
            }
        }

        wprintf(L"[+] Config applied successfully.\n");
        return 0;
    }

    // ---- All other commands need a driver handle ----
    DriverComm comm;
    if (!comm.Open()) {
        wprintf(L"[!] Failed to open driver device.\n");
        return 2;
    }

    // ---- set-loadlib ----
    if (0 == _wcsicmp(cmd, L"set-loadlib")) {
        return comm.AutoResolveAndSetAddresses() ? 0 : 3;
    }

    // ---- set-dll ----
    if (0 == _wcsicmp(cmd, L"set-dll")) {
        if (cmdIdx + 1 >= argc) {
            wprintf(L"[!] Usage: R3Comm.exe set-dll <dllPath>\n");
            return 1;
        }
        return comm.SetDllPath(argv[cmdIdx + 1]) ? 0 : 3;
    }

    // ---- callback-on / callback-off ----
    if (0 == _wcsicmp(cmd, L"callback-on")) {
        return comm.TurnOnProcessCallback() ? 0 : 3;
    }
    if (0 == _wcsicmp(cmd, L"callback-off")) {
        return comm.TurnOffProcessCallback() ? 0 : 3;
    }

    // ---- whitelist-add ----
    if (0 == _wcsicmp(cmd, L"whitelist-add")) {
        if (cmdIdx + 1 >= argc) {
            wprintf(L"[!] Usage: R3Comm.exe whitelist-add <filePath>\n");
            return 1;
        }
        return comm.AddToWhitelist(argv[cmdIdx + 1]) ? 0 : 3;
    }

    // ---- whitelist-remove ----
    if (0 == _wcsicmp(cmd, L"whitelist-remove")) {
        if (cmdIdx + 1 >= argc) {
            wprintf(L"[!] Usage: R3Comm.exe whitelist-remove <filePath>\n");
            return 1;
        }
        return comm.RemoveFromWhitelist(argv[cmdIdx + 1]) ? 0 : 3;
    }

    // ---- whitelist-query ----
    if (0 == _wcsicmp(cmd, L"whitelist-query")) {
        if (cmdIdx + 1 >= argc) {
            wprintf(L"[!] Usage: R3Comm.exe whitelist-query <filePath>\n");
            return 1;
        }
        auto result = comm.QueryWhitelist(argv[cmdIdx + 1]);
        if (!result.has_value()) return 3;
        wprintf(L"[+] %s\n", result.value() ? L"IN WHITELIST" : L"NOT IN WHITELIST");
        return 0;
    }

    // ---- unknown ----
    wprintf(L"[!] Unknown command: %s\n", cmd);
    wprintf(L"    Run 'R3Comm.exe help' for usage.\n");
    return 1;
}
