// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#include <ntifs.h>
#include <fltkernel.h>
#include <immintrin.h>

#include "Common.h"
#include "Injection.h"
#include "IOCTL.h"
#include "debug.h"
#include "RAIIGuard.h"
#include "WhiteList.h"
#include "AddressInfo.h"
#include "ThreadOffset.h"


UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\MyMonitor");
UNICODE_STRING SymbolicLinkName = RTL_CONSTANT_STRING(L"\\??\\MyMonitorLink");

KEINITIALIZEAPC  KeInitializeApc{};
KEINSERTQUEUEAPC KeInsertQueueApc{};

PSISPROTECTEDPROCESSLIGHT PsIsProtectedProcessLight{};
KSPIN_LOCK ProcessCallBackSpinLock{};

BaseAddressInfo* AddressInfo{};        // prepared for APC injection
UNICODE_STRING DllPath{};

ULONG ThreadListHeadOffset{};   // EPROCESS.ThreadListHead offset
ULONG ThreadListEntryOffset{};  // ETHREAD.ThreadListEntry offset


BOOLEAN IsProcessCallBack{ FALSE };
BOOLEAN WhitelistActive{ FALSE };  // set to TRUE on first whitelist-add

PVOID BitMapPoolAddress{};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



VOID CreateProcessNotifyRoutineEx(
	PEPROCESS Process,
	HANDLE ProcessId,
	PPS_CREATE_NOTIFY_INFO CreateInfo) {

	if (NULL == CreateInfo) {
		LOG_INFO("[Inject] Process %p is exiting", ProcessId);
		return;
	}

	if (nullptr != CreateInfo->ImageFileName) {
		LOG_INFO("[Inject] Process %wZ is starting", CreateInfo->ImageFileName);
	}
	else {
		LOG_INFO("[Inject] Process %p is starting (no image name)", ProcessId);
	}

	// Read global state under spin lock to prevent races where
	// AddressInfo / DllPath.Buffer are freed or modified during an IOCTL.
	BOOLEAN ShouldInject{ FALSE };
	{
		SpinLockGuard Guard(&ProcessCallBackSpinLock);

		ShouldInject = (IsProcessCallBack &&
			nullptr != AddressInfo &&
			nullptr != DllPath.Buffer &&
			ThreadListHeadOffset != 0UL &&
			ThreadListEntryOffset != 0UL);
	}

	if (!ShouldInject) {
		return;
	}

	HANDLE Pid = PsGetProcessId(Process);
	if ((ULONG64)Pid <= 4ULL) {
		return;
	}

	if (nullptr == PsIsProtectedProcessLight) {
		LOG_ERROR("PsIsProtectedProcessLight is empty");
		return;
	}
	if (PsIsProtectedProcessLight(Process))return; //is PPL process

	// Whitelist check: if active, only inject whitelisted processes
	if (WhitelistActive && nullptr != BitMapPoolAddress) {
		ULONG64 Index = GetBitmapIndex(CreateInfo->FileObject);
		if (QueryMap(BitMapPoolAddress, Index)) {
			QueueInjectWorkItem(Process, ProcessId);
		}
		else {
			return;
		}
	}
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline NTSTATUS CheckCallerDebugPrivilege(KPROCESSOR_MODE Mode) noexcept {
	LUID DebugPrivilegeLuid{};
	DebugPrivilegeLuid.LowPart = SE_DEBUG_PRIVILEGE;
	DebugPrivilegeLuid.HighPart = 0L;
	if (!SeSinglePrivilegeCheck(DebugPrivilegeLuid, Mode)) return STATUS_ACCESS_DENIED;
	return STATUS_SUCCESS;
}



NTSTATUS DeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0ULL;

	KPROCESSOR_MODE CallerMode = Irp->RequestorMode;
	if (CallerMode != UserMode) {
		LOG_ERROR("Caller is not in user mode");
		Status = STATUS_ACCESS_DENIED;
		Irp->IoStatus.Information = 0ULL;
		goto ReturnStatus;
	}

	if (CheckCallerDebugPrivilege(CallerMode) != STATUS_SUCCESS) {
		LOG_ERROR("Caller does not have debug privilege");
		Status = STATUS_ACCESS_DENIED;
		Irp->IoStatus.Information = 0ULL;
		goto ReturnStatus;
	}

	PEPROCESS CallerProcess = IoGetRequestorProcess(Irp);
	ULONG_PTR CallerPid = (ULONG_PTR)(PVOID)PsGetProcessId(CallerProcess);
	UNREFERENCED_PARAMETER(CallerPid); // TODO: Reserved for future BYOVD prevention

	PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

	ULONG ControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;

	PVOID SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
	ULONG InputBufferLength = Stack->Parameters.DeviceIoControl.InputBufferLength;

	switch (ControlCode) {
	case IOCTL_SET_LOADLIBRARY_ADDRESS: {
		LOG_INFO("Received custom IOCTL command");
		if (nullptr == SystemBuffer || InputBufferLength < sizeof(BaseAddressInfo)) {
			LOG_ERROR("Invalid input buffer");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		auto APIAddressLength = InputBufferLength;

		{
			SpinLockGuard Guard(&ProcessCallBackSpinLock);

			if (nullptr != AddressInfo) {
				ExFreePool(AddressInfo);
				AddressInfo = nullptr;
			}

			AddressInfo = (BaseAddressInfo*)ExAllocatePool2(POOL_FLAG_NON_PAGED, APIAddressLength, 'ADDR');
			if (nullptr == AddressInfo) {
				LOG_ERROR("Failed to allocate memory for AddressInfo");
				Status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			RtlCopyMemory(AddressInfo, SystemBuffer, APIAddressLength);
		}

		LOG_INFO("LoadLibrary Address: 0x%llX", AddressInfo->LoadLibraryAddress);
		LOG_INFO("Kernel32 Base Address: 0x%llX", AddressInfo->Kernel32BaseAddress);
		LOG_INFO("Ntdll Base Address: 0x%llX", AddressInfo->NtdllBaseAddress);

		Irp->IoStatus.Information = sizeof(BaseAddressInfo);
		break;
	}
	case IOCTL_SET_DLL_PATH: {
		LOG_INFO("Received custom IOCTL command to set DLL path\n");

		if (nullptr == SystemBuffer
			|| InputBufferLength < sizeof(WCHAR)
			|| (InputBufferLength % sizeof(WCHAR)) != 0) {
			LOG_ERROR("Invalid input buffer for DLL path\n");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		PCWSTR DllTempPath = (PCWSTR)SystemBuffer;
		ULONG CharCount = InputBufferLength / sizeof(WCHAR);
		if (DllTempPath[CharCount - 1] != L'\0') {
			LOG_ERROR("DLL path is not null-terminated\n");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		LOG_INFO("DLL Path: %ws\n", DllTempPath);
		{
			SpinLockGuard Guard(&ProcessCallBackSpinLock);

			if (nullptr != DllPath.Buffer) {
				ExFreePool(DllPath.Buffer);
				DllPath.Buffer = nullptr;
				DllPath.Length = 0;
				DllPath.MaximumLength = 0;
			}

			USHORT ByteLength = (USHORT)(CharCount * sizeof(WCHAR));
			DllPath.Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_NON_PAGED, ByteLength, 'DLL ');
			if (nullptr == DllPath.Buffer) {
				LOG_ERROR("Failed to allocate memory for DLL path\n");
				Status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			RtlCopyMemory(DllPath.Buffer, DllTempPath, ByteLength);
			DllPath.Length = ByteLength - sizeof(WCHAR);
			DllPath.MaximumLength = ByteLength;
		}

		Irp->IoStatus.Information = InputBufferLength;
		break;
	}
	case IOCTL_TURN_ON_PROCESS_CALLBACK: {
		{
			SpinLockGuard Guard(&ProcessCallBackSpinLock);

			if (IsProcessCallBack) {
				Status = STATUS_SUCCESS;
				break;
			}

			if (nullptr == AddressInfo ||
				nullptr == (PVOID)AddressInfo->Kernel32BaseAddress ||
				nullptr == (PVOID)AddressInfo->NtdllBaseAddress ||
				nullptr == (PVOID)AddressInfo->LoadLibraryAddress) {
				Status = STATUS_INVALID_ADDRESS;
				break;
			}
		}
		LOG_INFO("Turning on process callback\n");
		Status = PsSetCreateProcessNotifyRoutineEx(CreateProcessNotifyRoutineEx, FALSE);

		if (!NT_SUCCESS(Status)) {
			LOG_ERROR("Failed to register process notify routine: 0x%08X\n", Status);
			break;
		}

		SpinLockGuard Guard(&ProcessCallBackSpinLock);
		IsProcessCallBack = TRUE;
		break;
	}
	case IOCTL_TURN_OFF_PROCESS_CALLBACK: {
		{
			SpinLockGuard Guard(&ProcessCallBackSpinLock);
			if (!IsProcessCallBack) {
				Status = STATUS_SUCCESS;
				break;
			}
		}

		LOG_INFO("Turning off process callback");

		Status = PsSetCreateProcessNotifyRoutineEx(CreateProcessNotifyRoutineEx, TRUE);
		if (!NT_SUCCESS(Status)) {
			LOG_ERROR("Failed to unregister process notify routine: 0x%08X", Status);
			break;
		}
		SpinLockGuard Guard(&ProcessCallBackSpinLock);
		IsProcessCallBack = FALSE;
		break;
	}
	case IOCTL_PUSH_WHITE_LIST_ITEM: {
		if (nullptr == SystemBuffer || InputBufferLength < sizeof(UNICODE_STRING)) {
			LOG_ERROR("Invalid input buffer for whitelist query");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		PUNICODE_STRING FilePath = (PUNICODE_STRING)SystemBuffer;
		Status = PushRemoveListItem(BitMapPoolAddress, *FilePath, TRUE);
		if (NT_SUCCESS(Status)) {
			WhitelistActive = TRUE;
		}
		break;
	}
	case IOCTL_REMOVE_WHITE_LIST_ITEM: {
		if (nullptr == SystemBuffer || InputBufferLength < sizeof(UNICODE_STRING)) {
			LOG_ERROR("Invalid input buffer for whitelist query");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		PUNICODE_STRING FilePath = (PUNICODE_STRING)SystemBuffer;
		Status = PushRemoveListItem(BitMapPoolAddress, *FilePath, FALSE);
		break;
	}
	case IOCTL_QUERY_WHITE_LIST_ITEM: {

		if (nullptr == SystemBuffer || InputBufferLength < sizeof(UNICODE_STRING)) {
			LOG_ERROR("Invalid input buffer for whitelist query");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		PUNICODE_STRING FilePath = (PUNICODE_STRING)SystemBuffer;
		if (nullptr == FilePath->Buffer) {
			LOG_ERROR("FilePath.Buffer is NULL");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		// Ensure bitmap pool exists
		if (nullptr == BitMapPoolAddress) {
			LOG_ERROR("BitMapPoolAddress is NULL");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		PFILE_OBJECT FileObject{};
		Status = GetFileObject(*FilePath, &FileObject);
		if (!NT_SUCCESS(Status)) {
			break;
		}

		ULONG64 Index = GetBitmapIndex(FileObject);
		LOG_ERROR("[Whitelist] Query index 0x%llX\n", Index);

		BOOLEAN Present = FALSE;
		Present = QueryMap(BitMapPoolAddress, Index);

		// Write result back to SystemBuffer if there's enough space
		if (InputBufferLength >= sizeof(BOOLEAN)) {
			*(BOOLEAN*)SystemBuffer = Present;
			Irp->IoStatus.Information = sizeof(BOOLEAN);
		}
		else {
			Irp->IoStatus.Information = 0;
		}

		Status = STATUS_SUCCESS;
		break;
	}
	default: {
		LOG_ERROR("Unknown IOCTL command: 0x%X\n", ControlCode);
		Status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	}
ReturnStatus:
	Irp->IoStatus.Status = Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Status;
}

NTSTATUS CreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) noexcept {
	UNREFERENCED_PARAMETER(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}





VOID DriverUnload(PDRIVER_OBJECT DriverObject) {

	// Stop new injections
	IsProcessCallBack = FALSE;
	PsSetCreateProcessNotifyRoutineEx(CreateProcessNotifyRoutineEx, TRUE);

	// Drain in-flight work item
	if (DriverObject->DeviceObject) {
		PDEVICE_EXTENSION Exit = (PDEVICE_EXTENSION)DriverObject->DeviceObject->DeviceExtension;
		Exit->IsUnloading = TRUE;
		if (Exit->WorkItem) {
			KeWaitForSingleObject(&Exit->WorkItemCompletedEvent,
				Executive, KernelMode, FALSE, NULL);
			IoFreeWorkItem(Exit->WorkItem);
		}
	}

	// Drain pending APCs (KernelRoutine/RundownRoutine in driver .text)
	if (InterlockedCompareExchange(&PendingApcCount, 0, 0) != 0) {
		KeWaitForSingleObject(&AllApcsCompletedEvent,
			Executive, KernelMode, FALSE, NULL);
	}

	// Free resources
	if (DllPath.Buffer)    ExFreePool(DllPath.Buffer);
	if (AddressInfo)       ExFreePool(AddressInfo);
	if (BitMapPoolAddress) ExFreePool(BitMapPoolAddress);

	IoDeleteSymbolicLink(&SymbolicLinkName);
	if (DriverObject->DeviceObject) {
		IoDeleteDevice(DriverObject->DeviceObject);
		DriverObject->DeviceObject = nullptr;
	}

	LOG_INFO("Unloaded\n");
}


extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);



	UNICODE_STRING FunctionName = RTL_CONSTANT_STRING(L"KeInitializeApc");
	UNICODE_STRING FunctionName1 = RTL_CONSTANT_STRING(L"KeInsertQueueApc");
	UNICODE_STRING FunctionName2 = RTL_CONSTANT_STRING(L"PsGetNextProcessThread");
	UNICODE_STRING FunctionName3 = RTL_CONSTANT_STRING(L"PsIsProtectedProcessLight");


	NTSTATUS Status = STATUS_SUCCESS;

	PDEVICE_OBJECT DeviceObject{};
	Status = IoCreateDevice(
		DriverObject,
		sizeof(DEVICE_EXTENSION),
		&DeviceName,
		FILE_DEVICE_UNKNOWN,
		0,
		FALSE,
		&DeviceObject
	);

	if (!NT_SUCCESS(Status)) {
		LOG_ERROR("Failed to create device: 0x%08X", Status);
		goto Cleanup;
	}

	DeviceObject->Flags |= DO_BUFFERED_IO;



	PDEVICE_EXTENSION DeviceExtension{ (PDEVICE_EXTENSION)DeviceObject->DeviceExtension };
	RtlZeroMemory(DeviceExtension, sizeof(DEVICE_EXTENSION));
	DeviceExtension->DeviceObject = DeviceObject;
	DeviceExtension->WorkItem = nullptr;
	DeviceExtension->IsUnloading = FALSE;
	KeInitializeEvent(&DeviceExtension->WorkItemCompletedEvent, NotificationEvent, FALSE);


	Status = IoCreateSymbolicLink(&SymbolicLinkName, &DeviceName);
	if (!NT_SUCCESS(Status)) {
		LOG_ERROR("Failed to create symbolic link: 0x%08X", Status);
		goto Cleanup;
	}

	//
	// Dynamically resolve required kernel functions
	//
	KeInitializeApc = (KEINITIALIZEAPC)MmGetSystemRoutineAddress(&FunctionName);
	KeInsertQueueApc = (KEINSERTQUEUEAPC)MmGetSystemRoutineAddress(&FunctionName1);
	PsIsProtectedProcessLight = (PSISPROTECTEDPROCESSLIGHT)MmGetSystemRoutineAddress(&FunctionName3);

	if (nullptr == KeInitializeApc || nullptr == KeInsertQueueApc || nullptr == PsIsProtectedProcessLight) {
		LOG_ERROR("Failed to get KeInitializeApc or KeInsertQueueApc or PsIsProtectedProcessLight address");
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto Cleanup;
	}


	ThreadListHeadOffset = FindThreadListHeadOffset();
	if (ThreadListHeadOffset != 0UL) {
		LOG_INFO("Found thread list head offset: 0x%X", ThreadListHeadOffset);
	}
	else {
		LOG_ERROR("Failed to find thread list head offset");
	}

	ThreadListEntryOffset = FindThreadListEntryOffset();
	if (ThreadListEntryOffset != 0UL) {
		LOG_INFO("Found thread list entry offset: 0x%X", ThreadListEntryOffset);
	}
	else {
		LOG_ERROR("Failed to find thread list entry offset");
	}

	BitMapPoolAddress = ExAllocatePool2(POOL_FLAG_NON_PAGED, BIT_MAP_SIZE, 'BITM');
	if (nullptr == BitMapPoolAddress) {
		LOG_ERROR("Failed to allocate memory");
		Status = STATUS_INVALID_PARAMETER;
		goto Cleanup;
	}
	WhiteListInit(BitMapPoolAddress);

	KeInitializeSpinLock(&ProcessCallBackSpinLock);
	KeInitializeEvent(&AllApcsCompletedEvent, NotificationEvent, FALSE);
	DriverObject->MajorFunction[IRP_MJ_CREATE] = CreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = CreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
	DriverObject->DriverUnload = DriverUnload;

	return Status;

Cleanup:
	IoDeleteSymbolicLink(&SymbolicLinkName);
	if (DeviceObject) {
		IoDeleteDevice(DeviceObject);
		DeviceObject = nullptr;
	}
	return Status;
}