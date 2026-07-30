// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once
#include <ntifs.h>

extern ULONG ThreadListHeadOffset;

ULONG FindThreadListHeadOffset();
ULONG FindThreadListEntryOffset();