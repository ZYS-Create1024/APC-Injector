// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once
#include <ntifs.h>
#include "AddressInfo.h"
#include "APC.h"
#include "ApcTypes.h"

// Global state shared across all modules

extern UNICODE_STRING DeviceName;
extern UNICODE_STRING SymbolicLinkName;
extern KSPIN_LOCK ProcessCallBackSpinLock;
extern BaseAddressInfo* AddressInfo;
extern UNICODE_STRING DllPath;
extern ULONG ThreadListHeadOffset;
extern ULONG ThreadListEntryOffset;
extern BOOLEAN IsProcessCallBack;
extern BOOLEAN WhitelistActive;
extern PVOID BitMapPoolAddress;
extern volatile LONG PendingApcCount;
extern KEVENT AllApcsCompletedEvent;

// Dynamically resolved kernel functions

typedef BOOLEAN(*PSISPROTECTEDPROCESSLIGHT)(PEPROCESS Process);

extern KEINITIALIZEAPC  KeInitializeApc;
extern KEINSERTQUEUEAPC KeInsertQueueApc;
extern PSISPROTECTEDPROCESSLIGHT PsIsProtectedProcessLight;
