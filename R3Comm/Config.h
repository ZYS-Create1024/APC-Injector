// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Config structure: holds all user-configurable settings for R3Comm.
struct AppConfig {
    std::wstring dllPath;                       // Full path of DLL to inject
    bool setLoadlib = true;                     // Auto-resolve LoadLibrary / kernel32 / ntdll
    bool enableCallback = true;                 // Enable process-creation callback on apply
    std::vector<std::wstring> whitelist;        // Injection whitelist (repeatable key)
};

namespace config {

// Return the default config file path: "<exe-dir>\\R3Comm.ini".
std::wstring DefaultPath();

// Parse a boolean value: 1/0, true/false, yes/no, on/off (case-insensitive).
// Returns false and sets err on unrecognized input.
bool ParseBool(const std::wstring& s, bool& out, std::wstring& err);

// Load config from a UTF-8 file.  Unknown keys are warned and ignored.
// On parse error, returns false with err and the 1-based line number.
bool Load(const std::wstring& path, AppConfig& cfg,
          std::wstring& err, std::size_t& errLine);

// Save config to a UTF-8 file (with BOM).
bool Save(const std::wstring& path, const AppConfig& cfg, std::wstring& err);

// Format the config as display text (for config-show).
std::wstring Dump(const AppConfig& cfg);

} // namespace config
