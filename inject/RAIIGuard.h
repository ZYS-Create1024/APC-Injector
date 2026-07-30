// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#pragma once
#include "debug.h"


struct SpinLockGuard {
	SpinLockGuard(PKSPIN_LOCK SpinLock) :
		SpinLock(SpinLock), OldIrql(0) {
		KeAcquireSpinLock(SpinLock, &OldIrql);
	}

	~SpinLockGuard() noexcept {
		if (nullptr != SpinLock) KeReleaseSpinLock(SpinLock, OldIrql);
	}

	// Prevent copy
	SpinLockGuard(const SpinLockGuard&) = delete;
	SpinLockGuard& operator=(const SpinLockGuard&) = delete;

	// Prevent move
	SpinLockGuard(SpinLockGuard&&) = delete;
	SpinLockGuard& operator=(SpinLockGuard&&) = delete;
private:
	PKSPIN_LOCK SpinLock{};
	KIRQL OldIrql{};
};


template<typename T>
struct ObjectReferenceGuard {
	ObjectReferenceGuard(T* Obj) :Object(Obj) {
		if (nullptr != Object) ObReferenceObject(Obj);
		LOG_INFO("Referencing Object: %p\n", Obj);
	}

	~ObjectReferenceGuard() {
		LOG_INFO("Dereferencing Object: %p\n", Object);
		if (nullptr != Object) ObDereferenceObject(Object);
	}

	ObjectReferenceGuard(const ObjectReferenceGuard&) = delete;
	ObjectReferenceGuard& operator=(const ObjectReferenceGuard&) = delete;

	ObjectReferenceGuard(ObjectReferenceGuard&&) = delete;
	ObjectReferenceGuard& operator=(ObjectReferenceGuard&&) = delete;
private:
	T* Object{};
};