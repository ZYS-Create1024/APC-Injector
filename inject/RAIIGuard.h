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
	explicit ObjectReferenceGuard(T* Obj = nullptr) :Object(nullptr), IsReferenced(FALSE) {
		if (nullptr != Obj) {
			if(NT_SUCCESS(ObReferenceObjectSafe(Obj))) {
				Object = Obj;
				IsReferenced = TRUE;
				LOG_INFO("Referencing Object: %p\n", Obj);
			}
			else {
				LOG_ERROR("Failed to reference Object: %p\n", Obj);
			}
		}
	}

	[[deprecated("Use constructor instead. Unsafe reference may cause crashes.")]]
	static ObjectReferenceGuard<T> UnSafeGuard(T* Obj) noexcept {
		ObjectReferenceGuard<T> Guard;
		if (nullptr != Obj) {
			ObReferenceObject(Obj);
			Guard.Object = Obj;
			Guard.IsReferenced = TRUE;
			LOG_INFO("Unsafe referencing Object: %p\n", Obj);
		}
		return Guard;
	} // Not recommended. Use the constructor to create a safe reference guard unless 
	  // you are certain the object is safe. This exists only for compatibility.


	~ObjectReferenceGuard() noexcept {
		LOG_INFO("Dereferencing Object: %p\n", Object);
		if (nullptr != Object && IsReferenced) ObDereferenceObject(Object);
	}

	explicit operator bool() const { return Object != nullptr && IsReferenced; }
	T* Get() const { return Object; }

	ObjectReferenceGuard(const ObjectReferenceGuard&) = delete;
	ObjectReferenceGuard& operator=(const ObjectReferenceGuard&) = delete;

	ObjectReferenceGuard(ObjectReferenceGuard&&) = delete;
	ObjectReferenceGuard& operator=(ObjectReferenceGuard&&) = delete;
private:
	T* Object{};
	BOOLEAN IsReferenced{};
};
