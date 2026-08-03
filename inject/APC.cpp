// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#include "Common.h"
#include "Injection.h"

// APC lifecycle tracking    see APC.h
volatile LONG PendingApcCount = 0;
KEVENT AllApcsCompletedEvent;

VOID ApcKernelRoutine(PKAPC Apc,
	PKNORMAL_ROUTINE* NormalRoutine,
	PVOID* NormalContext,
	PVOID* SystemArgument1,
	PVOID* SystemArgument2) {
	UNREFERENCED_PARAMETER(NormalRoutine);
	UNREFERENCED_PARAMETER(NormalContext);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	ExFreePool(Apc);

	if (0 == InterlockedDecrement(&PendingApcCount)) {
		KeSetEvent(&AllApcsCompletedEvent, IO_NO_INCREMENT, FALSE);
	}
}

VOID ApcRundownRoutine(PKAPC Apc)
{
	ExFreePool(Apc);

	if (0 == InterlockedDecrement(&PendingApcCount)) {
		KeSetEvent(&AllApcsCompletedEvent, IO_NO_INCREMENT, FALSE);
	}
}

VOID InjectDllViaAPC(PEPROCESS Process, HANDLE ProcessId) {

	// Local copies and temporary variables
	PVOID  LocalDllPathBuffer{};
	SIZE_T LocalPathSize{};
	ULONG64 LocalLoadLibraryAddr{};

	// Step 1: Copy global state under spin lock
	{
		SpinLockGuard Guard(&ProcessCallBackSpinLock);

		if (nullptr == DllPath.Buffer || nullptr == AddressInfo) {
			return;
		}

		LocalPathSize = DllPath.Length + sizeof(WCHAR);

		LocalDllPathBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, LocalPathSize, 'LDLL');
		if (nullptr == LocalDllPathBuffer) {
			return;
		}

		RtlCopyMemory(LocalDllPathBuffer, DllPath.Buffer, LocalPathSize);
		LocalLoadLibraryAddr = AddressInfo->LoadLibraryAddress;
	}

	KAPC_STATE ApcState{};
	__try {
		KeStackAttachProcess(Process, &ApcState);
	}__except (EXCEPTION_EXECUTE_HANDLER) {
		LOG_ERROR("[Inject] KeStackAttachProcess failed\n");
		ExFreePool(LocalDllPathBuffer);
		return;
	}

	SIZE_T RegionSize{ LocalPathSize };
	PVOID DllPathVA{};
	NTSTATUS Status = ZwAllocateVirtualMemory(
		ZwCurrentProcess(), &DllPathVA, 0, &RegionSize,
		MEM_COMMIT, PAGE_READWRITE);

	if (!NT_SUCCESS(Status)) {
		LOG_ERROR("[Inject] ZwAllocateVirtualMemory failed: 0x%08X\n", Status);
		ExFreePool(LocalDllPathBuffer);
		KeUnstackDetachProcess(&ApcState);
		return;
	}

	__try {
		RtlCopyMemory(DllPathVA, LocalDllPathBuffer, LocalPathSize);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LOG_ERROR("[Inject] Access violation copying DLL path\n");
		ZwFreeVirtualMemory(ZwCurrentProcess(), &DllPathVA, &RegionSize, MEM_RELEASE);
		ExFreePool(LocalDllPathBuffer);
		KeUnstackDetachProcess(&ApcState);
		return;
	}

	// Local buffer has been copied into the target process; free local memory
	ExFreePool(LocalDllPathBuffer);
	LocalDllPathBuffer = nullptr;

	// Iterate threads and insert APCs
	BOOLEAN Injected = FALSE;


	// Fallback: manually walk the thread list
	LOG_INFO("[Inject] PsGetNextProcessThread not available, falling back to list walk\n");

	PLIST_ENTRY Head = (PLIST_ENTRY)((PUCHAR)Process + ThreadListHeadOffset);
	if (!MmIsAddressValid(Head)) goto Done; // Invalid list head, exit
	if ((ULONG64)(PVOID)Head < (ULONG64)MmSystemRangeStart) goto Done;//Same as above

	PLIST_ENTRY Entry = nullptr;
	__try {
		Entry = Head->Flink;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		goto Done;
	}

	for (; Entry != Head; Entry = Entry->Flink) {
		PETHREAD Thread = (PETHREAD)((PUCHAR)Entry - ThreadListEntryOffset);

		if (!MmIsAddressValid(Thread)) continue;

		ObjectReferenceGuard<PETHREAD> ThreadGuard(&Thread);

		PKAPC Apc = (PKAPC)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KAPC), 'KAPC');
		if (nullptr == Apc) {
			continue;
		}
		RtlZeroMemory(Apc, sizeof(KAPC));

		KeInitializeApc(
			Apc,
			(PKTHREAD)Thread,
			OriginalApcEnvironment,
			ApcKernelRoutine,
			ApcRundownRoutine,
			(PKNORMAL_ROUTINE)(PVOID)LocalLoadLibraryAddr,
			UserMode,
			DllPathVA
		);

		if (KeInsertQueueApc(Apc, nullptr, nullptr, IO_NO_INCREMENT)) {
			InterlockedIncrement(&PendingApcCount);
			Injected = TRUE;
			break;
		}
		else {
			ExFreePool(Apc);
		}
	}


Done:
	// If no APC was successfully queued, free the virtual memory allocated in the target process
	if (!Injected) {
		if (DllPathVA) {
			NTSTATUS FreeStatus = ZwFreeVirtualMemory(ZwCurrentProcess(), &DllPathVA, &RegionSize, MEM_RELEASE);
			if (!NT_SUCCESS(FreeStatus)) LOG_ERROR("[Inject] ZwFreeVirtualMemory failed: 0x%08X\n", FreeStatus);
		}
		LOG_ERROR("[Inject] Failed to queue APC into any thread of process %p\n", ProcessId);
	}
	else {
		LOG_INFO("[Inject] Successfully queued APC into process %p\n", ProcessId);
	}

	KeUnstackDetachProcess(&ApcState);
}

VOID InjectWorkItemRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context) {
	PDEVICE_EXTENSION DeviceExtension{ (PDEVICE_EXTENSION)DeviceObject->DeviceExtension };
	PINJECT_CONTEXT InjectContext{ (PINJECT_CONTEXT)Context };

	// Read IsUnloading under lock to decide whether to inject or skip.
	BOOLEAN IsUnloading = FALSE;
	{
		SpinLockGuard Guard(&DeviceExtension->StateLock);
		IsUnloading = DeviceExtension->IsUnloading;
	}

	if (!IsUnloading) {
		// Normal path: perform APC injection (may block, requires PASSIVE_LEVEL)
		InjectDllViaAPC(InjectContext->Process, InjectContext->ProcessId);
	}

	// Dereference the process object that was referenced in QueueInjectWorkItem
	// to keep it alive until the work item completes.
	if (nullptr != InjectContext && nullptr != InjectContext->Process) {
		ObDereferenceObject(InjectContext->Process);
	}

	// Free the injection context
	if (nullptr != InjectContext) {
		ExFreePoolWithTag(InjectContext, 'InjD');
	}

	// Only free WorkItem when unloading; during normal operation the WorkItem
	// is reused across injections to avoid repeated alloc/free cycles.
	if (IsUnloading) {
		PIO_WORKITEM SavedWorkItem = nullptr;
		{
			SpinLockGuard Guard(&DeviceExtension->StateLock);
			SavedWorkItem = DeviceExtension->WorkItem;
			DeviceExtension->WorkItem = nullptr;
		}

		if (nullptr != SavedWorkItem) {
			IoFreeWorkItem(SavedWorkItem);
		}
	}

	// Always signal the completion event so DriverUnload can wait for any
	// in-flight work item to finish.
	KeSetEvent(&DeviceExtension->WorkItemCompletedEvent, IO_NO_INCREMENT, FALSE);
	LOG_INFO("[WorkItem] Work item routine completed");
}

NTSTATUS QueueInjectWorkItem(PEPROCESS Process, HANDLE ProcessId) {

	NTSTATUS Status = STATUS_SUCCESS;
	PFILE_OBJECT FileObject{};
	PDEVICE_OBJECT DeviceObject{};
	PINJECT_CONTEXT InjectContext{};
	PDEVICE_EXTENSION DeviceExtension{};

	NTSTATUS DeviceObjectStatus = IoGetDeviceObjectPointer(
		&DeviceName,
		FILE_READ_DATA,
		&FileObject,
		&DeviceObject
	);
	if (!NT_SUCCESS(DeviceObjectStatus)) {
		Status = DeviceObjectStatus;
		goto Cleanup;
	}

	if (nullptr != FileObject) {
		ObDereferenceObject(FileObject);
	}

	DeviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
	{
		SpinLockGuard Guard(&DeviceExtension->StateLock);
		if (DeviceExtension->IsUnloading) {
			goto Cleanup;
		}

		InjectContext = (PINJECT_CONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(INJECT_CONTEXT), 'InjD');
		if (nullptr == InjectContext) {
			Status = STATUS_INSUFFICIENT_RESOURCES;
			goto Cleanup;
		};

		InjectContext->Process = Process;
		InjectContext->ProcessId = ProcessId;

		// Reference the process object to keep it alive until the work item
		// completes. The matching ObDereferenceObject is in InjectWorkItemRoutine.
		ObReferenceObject(Process);
		if (nullptr == DeviceExtension->WorkItem) {
			DeviceExtension->WorkItem = IoAllocateWorkItem(DeviceObject);
			if (nullptr == DeviceExtension->WorkItem) {
				ObDereferenceObject(Process);
				ExFreePoolWithTag(InjectContext, 'InjD');
				Status = STATUS_INSUFFICIENT_RESOURCES;
				goto Cleanup;
			}
		}
	}

	IoQueueWorkItem(
		DeviceExtension->WorkItem,
		InjectWorkItemRoutine,
		CriticalWorkQueue,
		InjectContext
	);

Cleanup:
	return Status;
}