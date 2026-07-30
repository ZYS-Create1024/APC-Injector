![Buid status](https://img.shields.io/badge/build-passing-brightgreen?style=plastic&logo=C)
# APC-Injector

A Windows kernel-mode DLL injection framework that uses **kernel APC (Asynchronous Procedure Call)** to inject a DLL into newly created processes.


| Component | Type | Description |
|-----------|------|-------------|
| `inject` | Kernel Driver | `MyMonitor.sys` — registers a process-creation callback, queues user-mode APCs into new processes to call `LoadLibraryW` |
| `InjectDll` | User-mode DLL | The payload DLL injected into target processes (customize `DllMain` with your own logic) |
| `R3Comm` | User-mode CLI | `R3Comm.exe` — configures the driver via IOCTL: sets addresses, DLL path, toggles callback, manages whitelist |

## How It Works

1. **Load the driver** — `MyMonitor.sys` registers as a kernel driver and creates a device object (`\Device\MyMonitor`) with a symbolic link (`\\.\MyMonitorLink`).

2. **Configure addresses** — `R3Comm.exe set-loadlib` resolves `kernel32.dll` / `ntdll.dll` base addresses and `LoadLibraryW` via `GetModuleHandle` + `GetProcAddress`, then sends them to the driver. On x64 Windows these addresses are system-wide (same in every process).

3. **Set the DLL path** — `R3Comm.exe set-dll <path>` tells the driver which DLL to inject.

4. **Enable the callback** — `R3Comm.exe callback-on` registers `PsSetCreateProcessNotifyRoutineEx`. From this point on, every new process triggers the driver's callback.

5. **APC injection** — When a new process is created (and passes the whitelist check if active):
   - The callback queues a **work item** to switch to `PASSIVE_LEVEL`
   - The work item attaches to the target process's address space (`KeStackAttachProcess`)
   - Allocates memory in the target via `ZwAllocateVirtualMemory`, copies the DLL path
   - Iterates the target's thread list (using dynamically resolved `EPROCESS.ThreadListHead` / `ETHREAD.ThreadListEntry` offsets)
   - Queues a user-mode APC on a thread with `LoadLibraryW` as the normal routine and the DLL path as the argument
   - When the thread enters alertable state, `LoadLibraryW(dllPath)` executes, loading the DLL

6. **Whitelist (optional)** — Use `whitelist-add` / `whitelist-remove` to restrict injection to specific executables. The whitelist uses a bitmap with Fibonacci hashing over file identity (SectionObjectPointer + DeviceObject + FileSize).

## Quick Start

### Prerequisites

- Visual Studio 2022 with **Windows Driver Kit (WDK)** and **Windows SDK**
- Target system: **Windows 10/11 x64** or **ARM64**
- **Test signing mode** enabled (or a valid kernel signing certificate):
  ```cmd
  bcdedit /set testsigning on
  ```

### Build

1. Open each `.sln` in Visual Studio:
   - `inject` → builds `MyMonitor.sys`
   - `InjectDll` → builds `InjectDll.dll`
   - `R3Comm` → builds `R3Comm.exe`
2. Build in **Release** or **Debug** for your target architecture (x64 / ARM64).

### Deploy

1. Copy `MyMonitor.sys` to the target machine.
2. Create a kernel service:
   ```cmd
   sc create MyMonitor type= kernel binPath= C:\path\to\MyMonitor.sys
   sc start MyMonitor
   ```
3. Verify the driver is running:
   ```cmd
   R3Comm.exe status
   ```

### Usage

```cmd
# 1. Resolve addresses automatically
R3Comm.exe set-loadlib

# 2. Set the DLL to inject
R3Comm.exe set-dll C:\Tools\InjectDll.dll

# 3. Start injecting into every new process
R3Comm.exe callback-on

# 4. (Optional) Restrict injection to specific executables
R3Comm.exe whitelist-add C:\Windows\notepad.exe
R3Comm.exe whitelist-add C:\Program Files\MyApp\app.exe

# 5. Stop injecting
R3Comm.exe callback-off

### Full Command Reference

| Command | Description |
|---------|-------------|
| `help` | Show help |
| `status` | Check if driver is accessible |
| `set-loadlib` | Auto-resolve kernel32/ntdll/LoadLibraryW addresses |
| `set-dll <path>` | Set the DLL path for injection |
| `callback-on` | Enable process-creation callback (start injecting) |
| `callback-off` | Disable process-creation callback (stop injecting) |
| `whitelist-add <path>` | Add a file path to the whitelist |
| `whitelist-remove <path>` | Remove a file path from the whitelist |
| `whitelist-query <path>` | Query whether a path is in the whitelist |

## Security Design

- **SeDebugPrivilege required** — all IOCTLs verify the caller holds `SeDebugPrivilege`
- **PPL bypass prevention** — `PsIsProtectedProcessLight` check skips protected processes
- **System process exclusion** — processes with PID ≤ 4 are never injected
- **Spin-lock synchronization** — all global state (address info, DLL path, callback flag) is protected by `ProcessCallBackSpinLock`
- **RAII guards** — `SpinLockGuard` and `ObjectReferenceGuard` prevent leaks from early returns
- **Graceful shutdown** — `DriverUnload` drains in-flight work items and pending APCs before freeing resources

## Customizing the Payload DLL

Edit `InjectDll/dllmain.cpp` and add your logic inside `DLL_PROCESS_ATTACH`:

```cpp
case DLL_PROCESS_ATTACH:
    // Your code runs inside every injected process
    OutputDebugStringW(L"[InjectDll] Injected successfully");
    // MessageBoxW(nullptr, L"Injected!", L"APC-Injector", MB_OK);
    break;
```

## Project Structure

```
APC-Injector/
├── inject/                  # Kernel driver (MyMonitor.sys)
│   ├── Injector.cpp         # DriverEntry, IOCTL dispatch, process callback
│   ├── APC.cpp              # APC injection logic, work item routine
│   ├── APC.h                # APC header aggregation
│   ├── ApcTypes.h           # KAPC type definitions, function typedefs
│   ├── WhiteList.cpp        # Bitmap whitelist (Fibonacci hash, file identity)
│   ├── WhiteList.h          # Whitelist declarations
│   ├── ThreadOffset.cpp     # Dynamic EPROCESS/ETHREAD offset discovery
│   ├── ThreadOffset.h       # Thread offset declarations
│   ├── Common.h             # Shared global state, function typedefs
│   ├── Injection.h          # INJECT_CONTEXT, DEVICE_EXTENSION structs
│   ├── IOCTL.h              # IOCTL code definitions
│   ├── AddressInfo.h        # BaseAddressInfo struct
│   ├── RAIIGuard.h          # SpinLockGuard, ObjectReferenceGuard
│   └── debug.h              # LOG_INFO / LOG_ERROR macros
│
├── InjectDll/               # Payload DLL (injected into targets)
│   ├── dllmain.cpp          # DllMain entry point
│   ├── pch.h / pch.cpp      # Precompiled headers
│   └── framework.h          # Windows header includes
│
├── R3Comm/                  # User-mode driver communication tool
│   ├── main.cpp             # CLI command parser
│   ├── DriverComm.cpp       # DeviceIoControl wrapper, IOCTL handlers
│   ├── DriverComm.h         # DriverComm class declaration
│   └── Common.h             # Shared IOCTL/struct definitions (mirrors driver)
│
├── LICENSE                  # MIT License
└── README.md
```

## Technical Details

### Dynamic Offset Discovery

Instead of hardcoding EPROCESS/ETHREAD structure offsets (which change between Windows builds), the driver scans memory at runtime:
- **`FindThreadListHeadOffset`** — walks EPROCESS from offset `0x200` to `0x1000`, locating the `ThreadListHead` by verifying doubly-linked list integrity
- **`FindThreadListEntryOffset`** — uses the discovered `ThreadListHead` to find which field inside ETHREAD contains the `ThreadListEntry`, verified by checking `IoThreadToProcess`

### Whitelist Hashing

The whitelist identifies files by a composite hash of:
- `SectionObjectPointer` (shared across all FileObjects for the same data stream)
- `DeviceObject` (disk identifier)
- `FileSize` XOR `ValidDataLength * FibonacciConstant` (from the FSRTL_ADVANCED_FCB_HEADER)

This provides a stable, collision-resistant identity that survives path changes. The bitmap is **256 KiB** (≈ 2 million bits).

### APC Lifecycle

- Each queued APC increments `PendingApcCount`
- `ApcKernelRoutine` (runs at `APC_LEVEL` after the APC fires) decrements the count and frees the KAPC
- `ApcRundownRoutine` (runs if the thread terminates before the APC fires) handles cleanup identically
- `DriverUnload` waits on `AllApcsCompletedEvent` before freeing driver .text, ensuring no in-flight APCs reference freed code

### Platform Support

| Architecture | Status |
|-------------|--------|
| x64 | Supported |
| ARM64 | Supported (uses `vld1q_u8`/`vst1q_u8` for atomic FCB reads) |

## README.zh-cn

> **中文版摘要请查看**：[README.zh-CN.md](./README.zh-CN.md)

## License

MIT License — see [LICENSE](LICENSE) for full text.

Copyright (c) 2026 [@ZYS-Create1024](https://github.com/ZYS-Create1024)

> **Disclaimer**: This project is for educational and authorized security research purposes only. Injecting code into processes without consent may violate laws and software terms of service. Use responsibly.
