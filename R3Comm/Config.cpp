// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#include "Config.h"

#include <Windows.h>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <sstream>

namespace config {

// ---------------------------------------------------------------------------
//  DefaultPath  --  "<exe-dir>\\R3Comm.ini"
// ---------------------------------------------------------------------------
std::wstring DefaultPath() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(_countof(buf)));
    if (len == 0 || len >= _countof(buf)) {
        return L"R3Comm.ini";  // fallback: current directory
    }

    // Strip the executable filename, keep the directory.
    wchar_t* lastSlash = wcsrchr(buf, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }

    std::wstring path(buf);
    path += L"R3Comm.ini";
    return path;
}

// ---------------------------------------------------------------------------
//  ParseBool
// ---------------------------------------------------------------------------
bool ParseBool(const std::wstring& s, bool& out, std::wstring& err) {
    if (_wcsicmp(s.c_str(), L"1") == 0 ||
        _wcsicmp(s.c_str(), L"true") == 0 ||
        _wcsicmp(s.c_str(), L"yes") == 0 ||
        _wcsicmp(s.c_str(), L"on") == 0) {
        out = true;
        return true;
    }
    if (_wcsicmp(s.c_str(), L"0") == 0 ||
        _wcsicmp(s.c_str(), L"false") == 0 ||
        _wcsicmp(s.c_str(), L"no") == 0 ||
        _wcsicmp(s.c_str(), L"off") == 0) {
        out = false;
        return true;
    }
    err = L"Unrecognized boolean value: " + s;
    return false;
}

// ---------------------------------------------------------------------------
//  Helpers: whitespace trim
// ---------------------------------------------------------------------------
static std::wstring Trim(const std::wstring& s) {
    std::size_t first = 0;
    while (first < s.size() && iswspace(static_cast<wint_t>(s[first]))) {
        ++first;
    }
    if (first >= s.size()) return L"";

    std::size_t last = s.size() - 1;
    while (last > first && iswspace(static_cast<wint_t>(s[last]))) {
        --last;
    }
    return s.substr(first, last - first + 1);
}

// ---------------------------------------------------------------------------
//  Helpers: UTF-8 <-> UTF-16 conversion
// ---------------------------------------------------------------------------
static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     utf8.data(),
                                     static_cast<int>(utf8.size()),
                                     nullptr, 0);
    if (needed <= 0) return L"";
    std::wstring result(needed, L'\0');
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      utf8.data(), static_cast<int>(utf8.size()),
                                      &result[0], needed);
    if (written <= 0) return L"";
    return result;
}

static std::string WideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int needed = WideCharToMultiByte(CP_UTF8, 0,
                                     ws.data(), static_cast<int>(ws.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "";
    std::string result(needed, '\0');
    int written = WideCharToMultiByte(CP_UTF8, 0,
                                      ws.data(), static_cast<int>(ws.size()),
                                      &result[0], needed, nullptr, nullptr);
    if (written <= 0) return "";
    return result;
}

// ---------------------------------------------------------------------------
//  Load
// ---------------------------------------------------------------------------
bool Load(const std::wstring& path, AppConfig& cfg,
          std::wstring& err, std::size_t& errLine) {
    // Reset to defaults; only keys present in the file will override.
    cfg = AppConfig{};

    // Open as binary so we can handle BOM ourselves.
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        err = L"Cannot open config file: " + path;
        return false;
    }

    std::string raw((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
    file.close();

    // Strip UTF-8 BOM if present: EF BB BF
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }

    // Split into lines, handling \r\n, \n, \r.
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        char ch = raw[i];
        if (ch == '\r') {
            lines.push_back(current);
            current.clear();
            // Peek: if next is \n, skip it (CRLF).
            if (i + 1 < raw.size() && raw[i + 1] == '\n') {
                ++i;
            }
        } else if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    lines.push_back(current);  // final line (may be empty)

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::wstring wline = Utf8ToWide(lines[i]);
        if (wline.empty() && lines[i].empty()) {
            continue;  // truly empty line
        }
        if (wline.empty() && !lines[i].empty()) {
            wprintf(L"[!] Line %zu: invalid UTF-8, skipping\n", i + 1);
            continue;
        }

        std::wstring trimmed = Trim(wline);
        if (trimmed.empty()) continue;

        // Whole-line comment: first non-whitespace char is # or ;
        if (trimmed[0] == L'#' || trimmed[0] == L';') continue;

        // Split at first '='.
        std::size_t eqPos = trimmed.find(L'=');
        if (eqPos == std::wstring::npos) {
            wprintf(L"[!] Line %zu: no '=', skipping: %s\n", i + 1, trimmed.c_str());
            continue;
        }

        std::wstring key = Trim(trimmed.substr(0, eqPos));
        std::wstring val = Trim(trimmed.substr(eqPos + 1));

        // Strip optional surrounding quotes.
        if (val.size() >= 2 && val.front() == L'"' && val.back() == L'"') {
            val = val.substr(1, val.size() - 2);
        }

        if (key.empty()) {
            wprintf(L"[!] Line %zu: empty key, skipping\n", i + 1);
            continue;
        }

        // Match keys case-insensitively.
        if (_wcsicmp(key.c_str(), L"dll_path") == 0) {
            cfg.dllPath = val;
        } else if (_wcsicmp(key.c_str(), L"set_loadlib") == 0) {
            if (!ParseBool(val, cfg.setLoadlib, err)) {
                errLine = i + 1;
                return false;
            }
        } else if (_wcsicmp(key.c_str(), L"enable_callback") == 0) {
            if (!ParseBool(val, cfg.enableCallback, err)) {
                errLine = i + 1;
                return false;
            }
        } else if (_wcsicmp(key.c_str(), L"whitelist") == 0) {
            if (!val.empty()) {
                cfg.whitelist.push_back(val);
            }
        } else {
            wprintf(L"[!] Line %zu: unknown key '%s', ignoring\n", i + 1, key.c_str());
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
//  Save
// ---------------------------------------------------------------------------
bool Save(const std::wstring& path, const AppConfig& cfg, std::wstring& err) {
    // Build content as UTF-8.
    std::ostringstream oss;

    // UTF-8 BOM.
    oss << static_cast<char>(0xEF)
        << static_cast<char>(0xBB)
        << static_cast<char>(0xBF);

    oss << "# R3Comm configuration\n";

    // dll_path
    oss << "dll_path = \"";
    oss << WideToUtf8(cfg.dllPath);
    oss << "\"\n";

    // set_loadlib
    oss << "set_loadlib = " << (cfg.setLoadlib ? "true" : "false") << "\n";

    // enable_callback
    oss << "enable_callback = " << (cfg.enableCallback ? "true" : "false") << "\n";

    // whitelist (repeatable)
    for (const auto& entry : cfg.whitelist) {
        oss << "whitelist = \"";
        oss << WideToUtf8(entry);
        oss << "\"\n";
    }

    std::string content = oss.str();

    // Write file.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        err = L"Cannot write config file: " + path;
        return false;
    }
    file.write(content.data(), content.size());
    file.close();

    if (file.fail()) {
        err = L"Write error for config file: " + path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Dump  --  human-readable display of current config
// ---------------------------------------------------------------------------
std::wstring Dump(const AppConfig& cfg) {
    std::wstring result;
    result += L"dll_path        = \"";
    result += cfg.dllPath;
    result += L"\"\n";

    result += L"set_loadlib     = ";
    result += cfg.setLoadlib ? L"true" : L"false";
    result += L"\n";

    result += L"enable_callback = ";
    result += cfg.enableCallback ? L"true" : L"false";
    result += L"\n";

    for (std::size_t i = 0; i < cfg.whitelist.size(); ++i) {
        result += L"whitelist       = \"";
        result += cfg.whitelist[i];
        result += L"\"\n";
    }

    return result;
}

} // namespace config
