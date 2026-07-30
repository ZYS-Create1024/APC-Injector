// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once

typedef struct {
	ULONG64 LoadLibraryAddress;
	ULONG64 Kernel32BaseAddress;
	ULONG64 NtdllBaseAddress;
} BaseAddressInfo;