// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once
#include <ntifs.h>

// ©¤©¤ Injection context passed to work items

typedef struct INJECT_CONTEXT {
	PEPROCESS Process;
	HANDLE ProcessId;
} INJECT_CONTEXT, *PINJECT_CONTEXT;

// ©¤©¤ Device extension (work item + unload coordination)

typedef struct DEVICE_EXTENSION {
	KSPIN_LOCK StateLock;
	PDEVICE_OBJECT DeviceObject;
	PIO_WORKITEM WorkItem;
	KEVENT WorkItemCompletedEvent;
	BOOLEAN IsUnloading;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// ©¤©¤ Injection functions

VOID InjectDllViaAPC(PEPROCESS Process, HANDLE ProcessId);
VOID InjectWorkItemRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context);
NTSTATUS QueueInjectWorkItem(PEPROCESS Process, HANDLE ProcessId);
