// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#include "WhiteList.h"
KSPIN_LOCK BitMapAddressSpinLock{};

namespace {
    BOOLEAN IsInited{ FALSE };
}

NTSTATUS WhiteListInit(PVOID BitMapPoolAddress) { // Must be non-paged pool

    if (IsInited)return STATUS_ALREADY_COMPLETE;

    if ((ULONG64)BitMapPoolAddress < (ULONG64)MmSystemRangeStart) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!MmIsAddressValid((PVOID)BitMapPoolAddress)) {
        return STATUS_ACCESS_VIOLATION;
    }


    KeInitializeSpinLock(&BitMapAddressSpinLock);
    {
        SpinLockGuard Guard(&BitMapAddressSpinLock);
        memset(BitMapPoolAddress, 0, BIT_MAP_SIZE);
    }

    IsInited = TRUE;

    return STATUS_SUCCESS;
}



ULONG64 GetFileSizeFromFcb(PFILE_OBJECT FileObject) {
    // FsContext points to the FSRTL_ADVANCED_FCB_HEADER
    PFSRTL_ADVANCED_FCB_HEADER AdvancedFcb = (PFSRTL_ADVANCED_FCB_HEADER)FileObject->FsContext;
    if (!AdvancedFcb) return 0;
#ifdef _M_X64
    // Confirmed via WinDbg: 16-byte aligned, MOVDQA is atomic on Intel
    __m128i Temp = _mm_load_si128((const __m128i*) & AdvancedFcb->FileSize);
    FileData Data{};
    _mm_storeu_si128((__m128i*) & Data, Temp);
#else
    // ARM64: vld1q_u8 does not require alignment
    uint8x16_t Temp = vld1q_u8((const __m128i*) & AdvancedFcb->FileSize);
    FileData Data{};
    vst1q_u8((__m128i*) & Data, Temp);
#endif

    LONG64 FileSize = Data.FileSize.QuadPart;
    LONG64 ValidDataLen = Data.ValidDataLen.QuadPart;
    return FileSize ^ (ValidDataLen * FIBONACCI_HASH_CONSTANT);
}

// Hash a 64-bit file reference number (from FileInternalInformation)
// into a bitmap index.  The file reference number is NTFS-level unique
// and stable across all opens of the same file.
ULONG64 GetBitmapIndexFromFileId(ULONG64 FileId) noexcept {
	FileId ^= (FileId >> 32);
	FileId ^= (FileId >> 16);
	return FileId & (BIT_MAP_SIZE * BIT - 1);
}

// Hash a FileObject into a bitmap index.
// Uses SectionObjectPointer (shared across all FileObjects for the
// same data stream). Falls back to FsContext if NULL.
ULONG64 GetBitmapIndex(PFILE_OBJECT FileObject) noexcept {

	if (nullptr == FileObject)return 0UL;

	ULONG64 DiskId  = (ULONG64)(PVOID)FileObject->DeviceObject;
	ULONG64 FileKey = (ULONG64)(PVOID)FileObject->SectionObjectPointer;
	if (nullptr == (PVOID)FileKey) {
		FileKey = (ULONG64)(PVOID)FileObject->FsContext;
	}
	LONG64 Hash = (FileKey * FIBONACCI_HASH_CONSTANT)
	            ^ (DiskId  * FIBONACCI_HASH_CONSTANT)
	            ^ GetFileSizeFromFcb(FileObject);

	Hash ^= (Hash >> 32);
	Hash ^= (Hash >> 16);

	return (ULONG64)(Hash & (BIT_MAP_SIZE * BIT - 1));
}


VOID SetMap(PVOID BitMapAddress, ULONG64 Index, BOOLEAN IsSet) noexcept {
    ULONG64 QwordIndex = Index >> 6; //  Index / 64
    ULONG64 BitOffset = Index & 63;  //  Index % 64
    LONG64* Ptr64 = (LONG64*)BitMapAddress + QwordIndex;
    LONG64 bitMask = 1i64 << BitOffset;

    if (IsSet) {
        InterlockedOr64(Ptr64, bitMask);    // Set bit to 1
    }
    else {
        InterlockedAnd64(Ptr64, ~bitMask);  // Set bit to 0
    }
}


BOOLEAN QueryMap(PVOID BitMapAddress, ULONG64 Index) {
    ULONG64 QwordIndex = Index >> 6;   // Index / 64
    ULONG64 BitInQword = Index & 63;   // Index % 64
    LONG64* Ptr64 = (LONG64*)BitMapAddress + QwordIndex;
    LONG64 Data = InterlockedCompareExchange64(Ptr64, 0, 0);
    return (Data >> BitInQword) & 1;
}



NTSTATUS GetFileObject(UNICODE_STRING FilePath,
                       PFILE_OBJECT* OutFileObject) { // FilePath format: C:\Windows\xxx.exe
    NTSTATUS Status{ STATUS_SUCCESS };
    UNICODE_STRING TempPath;
    WCHAR Buffer[260]; // MAX_PATH
    IO_STATUS_BLOCK IOStatusBlock{};
    OBJECT_ATTRIBUTES ObjectAttributes{};
    HANDLE FileHandle{};

    if (0 == FilePath.Length) {
        Status = STATUS_OBJECT_NAME_INVALID;
        goto Cleanup;
    }


    __try {
        Status = RtlStringCbPrintfW(Buffer, sizeof(Buffer), L"\\??\\%wZ", &FilePath);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = STATUS_ACCESS_VIOLATION;
    }
    if (!NT_SUCCESS(Status)) {
        goto Cleanup;
    }
    // Prepend \??\ NT namespace prefix so ZwCreateFile can resolve the DOS path
    RtlInitUnicodeString(&TempPath, Buffer);


    InitializeObjectAttributes(
        &ObjectAttributes,
        &TempPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);


    Status = ZwCreateFile(
        &FileHandle,
        GENERIC_READ | SYNCHRONIZE,
        &ObjectAttributes,
        &IOStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT,
        nullptr,
        0
    );

    if (!NT_SUCCESS(Status)) {
        LOG_ERROR("Failed to Open File,Status: 0x%08X", Status);
        goto Cleanup;
    }


    Status = ObReferenceObjectByHandle(
        FileHandle,
        0,
        *IoFileObjectType,
        KernelMode,
        (PVOID*)OutFileObject,
        NULL
    );
    if (!NT_SUCCESS(Status)) {
        LOG_ERROR("Failed to get FileObject! Status: 0x%08X", Status);
        goto Cleanup;
    }

Cleanup:
    if (nullptr != FileHandle)ZwClose(FileHandle);
    return Status;
}

NTSTATUS PushRemoveListItem(PVOID BitMapPoolAddress, UNICODE_STRING FilePath, BOOLEAN IsPushRemove) {
    PFILE_OBJECT FileObject{};
    NTSTATUS Status = GetFileObject(FilePath, &FileObject);
    if (!NT_SUCCESS(Status)) {
        LOG_ERROR("[Whitelist] Failed to open file, status 0x%08X\n", Status);
        return Status;
    }

    ULONG64 Index = GetBitmapIndex(FileObject);
    SetMap(BitMapPoolAddress, Index, IsPushRemove);
    LOG_ERROR("[Whitelist] %s index 0x%llX\n",
             IsPushRemove ? "Added" : "Removed", Index);

    if (nullptr != FileObject)ObDereferenceObject(FileObject);
    return STATUS_SUCCESS;
}
