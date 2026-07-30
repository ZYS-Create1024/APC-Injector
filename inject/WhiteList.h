// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once

#include <ntifs.h>
#include <fltkernel.h>
#include <immintrin.h>
#include <Ntstrsafe.h>
#include "RAIIGuard.h"
#include "debug.h"

constexpr ULONG64 BIT_MAP_SIZE{ 256UL * 1024UL }; //Unit:bytes
constexpr ULONG64 BIT{ 8ULL };
constexpr ULONG64 FIBONACCI_HASH_CONSTANT{ 0x9E3779B97F4A7C15ULL }; // 2^64 / ¦Õ

struct FileData {
    LARGE_INTEGER FileSize;
    LARGE_INTEGER ValidDataLen;
};

NTSTATUS WhiteListInit(PVOID BitMapPoolAddress);
ULONG64 GetBitmapIndexFromFileId(ULONG64 FileId) noexcept;
ULONG64 GetBitmapIndex(PFILE_OBJECT FileObject) noexcept;
VOID SetMap(PVOID BitMapAddress, ULONG64 Index, BOOLEAN IsSet) noexcept;
BOOLEAN QueryMap(PVOID BitMapAddress, ULONG64 Index);
NTSTATUS GetFileObject(UNICODE_STRING FilePath, PFILE_OBJECT* OutFileObject);
NTSTATUS PushRemoveListItem(PVOID BitMapPoolAddress, UNICODE_STRING FilePath, BOOLEAN IsPushRemove);