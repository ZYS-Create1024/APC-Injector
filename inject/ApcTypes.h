// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once
#include <ntifs.h>

// KAPC environment

typedef enum _KAPC_ENVIRONMENT {
	OriginalApcEnvironment,
	AttachedApcEnvironment,
	CurrentApcEnvironment,
	InsertApcEnvironment
} KAPC_ENVIRONMENT;

// APC routine typedefs

typedef
_Function_class_(KNORMAL_ROUTINE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
NTAPI
KNORMAL_ROUTINE(
	_In_opt_ PVOID NormalContext,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2
);
typedef KNORMAL_ROUTINE* PKNORMAL_ROUTINE;

typedef
_Function_class_(KKERNEL_ROUTINE)
_IRQL_requires_(APC_LEVEL)
_IRQL_requires_same_
VOID
NTAPI
KKERNEL_ROUTINE(
	_In_ PRKAPC Apc,
	_Inout_ _Deref_pre_maybenull_ PKNORMAL_ROUTINE* NormalRoutine,
	_Inout_ _Deref_pre_maybenull_ PVOID* NormalContext,
	_Inout_ _Deref_pre_maybenull_ PVOID* SystemArgument1,
	_Inout_ _Deref_pre_maybenull_ PVOID* SystemArgument2
);
typedef KKERNEL_ROUTINE* PKKERNEL_ROUTINE;

typedef
_Function_class_(KRUNDOWN_ROUTINE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
NTAPI
KRUNDOWN_ROUTINE(
	_In_ PRKAPC Apc
);
typedef KRUNDOWN_ROUTINE* PKRUNDOWN_ROUTINE;

// Dynamically resolved APC functions

typedef VOID(NTAPI* KEINITIALIZEAPC)(
	IN PKAPC Apc,
	IN PKTHREAD Thread,
	IN KAPC_ENVIRONMENT TargetEnvironment,
	IN PKKERNEL_ROUTINE KernelRoutine,
	IN PKRUNDOWN_ROUTINE RundownRoutine,
	IN PKNORMAL_ROUTINE NormalRoutine,
	IN KPROCESSOR_MODE Mode,
	IN PVOID Context
	);

typedef BOOLEAN(NTAPI* KEINSERTQUEUEAPC)(
	_Inout_ PRKAPC Apc,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2,
	_In_ KPRIORITY Increment
	);

// APC routine declarations

VOID ApcKernelRoutine(PKAPC Apc, PKNORMAL_ROUTINE* NormalRoutine,
	PVOID* NormalContext, PVOID* SystemArgument1,
	PVOID* SystemArgument2);

VOID ApcRundownRoutine(PKAPC Apc);
