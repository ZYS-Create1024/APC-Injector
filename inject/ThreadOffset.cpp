// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#include "ThreadOffset.h"

ULONG FindThreadListHeadOffset() {

	if (KeGetCurrentIrql() > APC_LEVEL) return 0UL;

	ULONG Offset{ 0UL };

	extern PEPROCESS PsInitialSystemProcess; // System process EPROCESS pointer
	PUCHAR ProcessEPROCESSBaseAddress{ (PUCHAR)(PVOID)PsInitialSystemProcess };

	// Search range and step ¡ª may need tuning per target system
	const ULONG StartOffset{ 0x200UL };
	const ULONG EndOffset{ 0x1000UL };
	const ULONG Step{ sizeof(PVOID) };
	const ULONG MaxWalkSteps{ 1024UL };


	for (ULONG offset = StartOffset; offset < EndOffset; offset += Step) {
		PLIST_ENTRY List{ (PLIST_ENTRY)(ProcessEPROCESSBaseAddress + offset) };

		if (!MmIsAddressValid(List)) continue;

		// Validate Flink/Blink readability
		PLIST_ENTRY Flink{};
		PLIST_ENTRY Blink{};
		__try {
			Flink = List->Flink;
			Blink = List->Blink;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			continue;
		}

		if (!MmIsAddressValid(Flink) || !MmIsAddressValid(Blink)) continue;

		if ((ULONG64)Flink < (ULONG64)MmSystemRangeStart || (ULONG64)Blink < (ULONG64)MmSystemRangeStart) continue;


		// Verify Flink->Blink points back to List, and Blink->Flink points back to List
		BOOLEAN FlinkIsValid{ FALSE };
		__try {
			if (MmIsAddressValid(Flink->Blink) && Flink->Blink == List) FlinkIsValid = TRUE;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			continue;
		}

		BOOLEAN BlinkIsValid{ FALSE };
		__try {
			if (MmIsAddressValid(Blink->Flink) && Blink->Flink == List) BlinkIsValid = TRUE;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			continue;
		}



		// Walk forward along the list ¡ª can we loop back to List?
		PLIST_ENTRY Temp{ Flink };
		ULONG WalkCount{};
		BOOLEAN ValueStatus{ TRUE };

		while (Temp != List && WalkCount < MaxWalkSteps) {
			if (!MmIsAddressValid(Temp)) { ValueStatus = FALSE; break; }

			PLIST_ENTRY Next{};
			__try {
				Next = Temp->Flink;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				ValueStatus = FALSE;
				break;
			}

			if (!MmIsAddressValid(Next)) { ValueStatus = FALSE; break; }
			Temp = Next;
			WalkCount++;
		}

		if (ValueStatus && Temp == List && WalkCount > 0UL) {
			Offset = offset; // Probable thread list head offset
			goto ReturnOffset;
		}
	}

ReturnOffset:
	return Offset;
}

ULONG FindThreadListEntryOffset() {

	ULONG Offset{ 0UL };

	if (ThreadListHeadOffset == 0UL || KeGetCurrentIrql() > APC_LEVEL) goto ReturnEntryOffset;

	extern PEPROCESS PsInitialSystemProcess;
	PLIST_ENTRY Head = (PLIST_ENTRY)((PUCHAR)PsInitialSystemProcess + ThreadListHeadOffset);

	if (!MmIsAddressValid(Head)) return 0UL;

	PLIST_ENTRY Flink{};
	__try {
		Flink = Head->Flink;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0UL;
	}

	// Flink points to the first thread's ThreadListEntry ¡ª naturally inside a System process thread
	if ((ULONG64)Flink < (ULONG64)MmSystemRangeStart) return 0UL;

	// Approximate offset range of ThreadListEntry within ETHREAD
	const ULONG StartOffset{ 0x400UL };
	const ULONG EndOffset{ 0x800UL };
	const ULONG Step{ sizeof(PVOID) };

	for (ULONG TryOffset = StartOffset; TryOffset < EndOffset; TryOffset += Step) {
		PETHREAD Candidate = (PETHREAD)((PUCHAR)Flink - TryOffset);

		if (!MmIsAddressValid(Candidate)) continue;
		if ((ULONG64)Candidate < (ULONG64)MmSystemRangeStart) continue;

		__try {
			PLIST_ENTRY CandidateEntry = (PLIST_ENTRY)((PUCHAR)Candidate + TryOffset);

			if (!MmIsAddressValid(CandidateEntry)) continue;
			// Should point to the same Flink address
			if (CandidateEntry != Flink) continue;

			// Verify list integrity: CandidateEntry->Blink should point to Head
			if (!MmIsAddressValid(CandidateEntry->Blink)) continue;
			if (CandidateEntry->Blink != Head) continue;

			// Secondary verification: the candidate thread should belong to the System process
			if (IoThreadToProcess(Candidate) == PsInitialSystemProcess) {
				Offset = TryOffset;
				goto ReturnEntryOffset;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			continue;
		}
	}

ReturnEntryOffset:
	return Offset;
}