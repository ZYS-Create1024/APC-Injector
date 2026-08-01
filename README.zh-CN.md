![Buid status](https://img.shields.io/badge/build-passing-brightgreen?style=plastic&logo=C)
![Platform](https://img.shields.io/badge/platform-Windows-blue)
# APC-Injector

```
       Ring 3                      Ring 0 
┌─────────────────┐          ┌─────────────────┐
│  R3Comm.exe     │  IOCTL   │  Injector.sys   │
│  (CLI Tool)     │ ───────► │  (Driver)       │
│                 │ ◄─────── │                 │
└─────────────────┘          └─────────────────┘
         │                           │
         ▼                           ▼
   CreateFile()             IRP Dispatch Routine
   DeviceIoControl()
```

一个 Windows 内核模式 DLL 注入框架，利用**内核 APC（异步过程调用）**将 DLL 注入到新创建的进程中。

| 组件 | 类型 | 描述 |
|-----------|------|-------------|
| `inject` | 内核驱动 | `Injector.sys` — 注册进程创建回调，向新进程排队用户模式 APC 以调用 `LoadLibraryW` |
| `InjectDll` | 用户态 DLL | 注入目标进程的载荷 DLL（在 `DllMain` 中自定义你的逻辑） |
| `R3Comm` | 用户态 CLI | `R3Comm.exe` — 通过 IOCTL 配置驱动：设置地址、DLL 路径、开关回调、管理白名单 |

## 工作原理

1. **加载驱动** — `Injector.sys` 以内核驱动身份注册，创建设备对象（`\Device\MyMonitor`）及符号链接（`\\.\MyMonitorLink`）。

2. **配置地址** — `R3Comm.exe set-loadlib` 通过 `GetModuleHandle` + `GetProcAddress` 解析 `kernel32.dll` / `ntdll.dll` 基址以及 `LoadLibraryW` 地址，然后发送给驱动。在 x64 Windows 上，这些地址是系统全局的（在所有进程中相同）。

3. **设置 DLL 路径** — `R3Comm.exe set-dll <path>` 告诉驱动要注入哪个 DLL。

4. **启用回调** — `R3Comm.exe callback-on` 注册 `PsSetCreateProcessNotifyRoutineEx`。从此开始，每个新创建的进程都会触发驱动的回调。

5. **APC 注入** — 当新进程创建（且通过白名单检查，如果启用的话）：
   - 回调排队一个**工作项**，以切换到 `PASSIVE_LEVEL`
   - 工作项附加到目标进程的地址空间（`KeStackAttachProcess`）
   - 通过 `ZwAllocateVirtualMemory` 在目标进程中分配内存，拷贝 DLL 路径
   - 遍历目标进程的线程列表（使用动态解析的 `EPROCESS.ThreadListHead` / `ETHREAD.ThreadListEntry` 偏移量）
   - 在目标线程上排队一个用户模式 APC，其中 `LoadLibraryW` 作为正常例程，DLL 路径作为参数
   - 当线程进入可警告（alertable）状态时，`LoadLibraryW(dllPath)` 被执行，DLL 被加载

6. **白名单** — 使用 `whitelist-add` / `whitelist-remove` 将注入限制到特定的可执行文件。白名单使用位图加 Fibonacci 哈希，基于文件身份（SectionObjectPointer + DeviceObject + FileSize）。

## 快速开始

### 前置条件

- Visual Studio 2022，含 **Windows Driver Kit (WDK)** 和 **Windows SDK**
- 目标系统：**Windows 10/11 x64** 或 **ARM64**
- **测试签名模式**已启用（或持有有效的内核签名证书）：
  ```cmd
  bcdedit /set testsigning on
  ```

### 构建

1. 在 Visual Studio 中打开各 `.sln` 文件：
   - `inject` → 生成 `Injector.sys`
   - `InjectDll` → 生成 `InjectDll.dll`
   - `R3Comm` → 生成 `R3Comm.exe`
2. 以 **Release** 或 **Debug** 配置、对应目标架构（x64 / ARM64）构建。

### 部署

1. 将 `Injector.sys` 拷贝到目标机器。
2. 创建内核服务：
   ```cmd
   sc create MyMonitor type= kernel binPath= C:\path\to\Injector.sys
   sc start MyMonitor
   ```
3. 验证驱动正在运行：
   ```cmd
   R3Comm.exe status
   ```

### 使用

```cmd
# 1. 自动解析地址
R3Comm.exe set-loadlib

# 2. 设置要注入的 DLL
R3Comm.exe set-dll C:\Tools\InjectDll.dll

# 3. 开始向每个新进程注入
R3Comm.exe callback-on

# 4. （可选）将注入限制到特定可执行文件
R3Comm.exe whitelist-add C:\Windows\notepad.exe
R3Comm.exe whitelist-add C:\Program Files\MyApp\app.exe

# 5. 停止注入
R3Comm.exe callback-off
```

### 完整命令参考

| 命令 | 描述 |
|---------|-------------|
| `help` | 显示帮助 |
| `status` | 检查驱动是否可访问 |
| `set-loadlib` | 自动解析 kernel32/ntdll/LoadLibraryW 地址 |
| `set-dll <path>` | 设置要注入的 DLL 路径 |
| `callback-on` | 启用进程创建回调（开始注入） |
| `callback-off` | 禁用进程创建回调（停止注入） |
| `whitelist-add <path>` | 将文件路径添加到白名单 |
| `whitelist-remove <path>` | 从白名单中移除文件路径 |
| `whitelist-query <path>` | 查询路径是否在白名单中 |

## 安全设计

- **要求 SeDebugPrivilege** — 所有 IOCTL 都会验证调用者是否持有 `SeDebugPrivilege`
- **防止 PPL 绕过** — `PsIsProtectedProcessLight` 检查会跳过受保护的进程
- **排除系统进程** — PID ≤ 4 的进程永远不会被注入
- **自旋锁同步** — 所有全局状态（地址信息、DLL 路径、回调标志）由 `ProcessCallBackSpinLock` 保护
- **RAII 守卫** — `SpinLockGuard` 和 `ObjectReferenceGuard` 防止因提前返回导致的资源泄漏
- **优雅关闭** — `DriverUnload` 排空进行中的工作项和待处理的 APC，然后再释放资源

## 自定义载荷 DLL

编辑 `InjectDll/dllmain.cpp` 并在 `DLL_PROCESS_ATTACH` 中添加你的逻辑：

```cpp
case DLL_PROCESS_ATTACH:
    // 你的代码将在每个注入的进程内运行
    OutputDebugStringW(L"[InjectDll] 注入成功");
    // MessageBoxW(nullptr, L"已注入！", L"APC-Injector", MB_OK);
    break;
```

## 项目结构

```
APC-Injector/
├── inject/                  # 内核驱动（MyMonitor.sys）
│   ├── Injector.cpp         # DriverEntry、IOCTL 分发、进程回调
│   ├── APC.cpp              # APC 注入逻辑、工作项例程
│   ├── APC.h                # APC 头文件聚合
│   ├── ApcTypes.h           # KAPC 类型定义、函数类型别名
│   ├── WhiteList.cpp        # 位图白名单（Fibonacci 哈希、文件身份）
│   ├── WhiteList.h          # 白名单声明
│   ├── ThreadOffset.cpp     # 动态 EPROCESS/ETHREAD 偏移量发现
│   ├── ThreadOffset.h       # 线程偏移量声明
│   ├── Common.h             # 共享全局状态、函数类型别名
│   ├── Injection.h          # INJECT_CONTEXT、DEVICE_EXTENSION 结构体
│   ├── IOCTL.h              # IOCTL 代码定义
│   ├── AddressInfo.h        # BaseAddressInfo 结构体
│   ├── RAIIGuard.h          # SpinLockGuard、ObjectReferenceGuard
│   └── debug.h              # LOG_INFO / LOG_ERROR 宏
│
├── InjectDll/               # 载荷 DLL（注入到目标进程）
│   ├── dllmain.cpp          # DllMain 入口点
│   ├── pch.h / pch.cpp      # 预编译头
│   └── framework.h          # Windows 头文件包含
│
├── R3Comm/                  # 用户态驱动通信工具
│   ├── main.cpp             # 命令行命令解析器
│   ├── DriverComm.cpp       # DeviceIoControl 封装、IOCTL 处理器
│   ├── DriverComm.h         # DriverComm 类声明
│   └── Common.h             # 共享 IOCTL/结构体定义（与驱动镜像）
│
├── LICENSE                  # MIT License
├── README.md
└── README.zh-CN.md
```

## 技术细节

### 动态偏移量发现

驱动在运行时扫描内存，而非硬编码 EPROCESS/ETHREAD 结构体偏移量（这些偏移量在不同 Windows 构建版本之间会发生变化）：

- **`FindThreadListHeadOffset`** — 从偏移 `0x200` 到 `0x1000` 遍历 EPROCESS，通过验证双向链表完整性定位 `ThreadListHead`
- **`FindThreadListEntryOffset`** — 利用已发现的 `ThreadListHead`，定位 ETHREAD 中哪个字段包含 `ThreadListEntry`，并通过 `IoThreadToProcess` 验证

### 白名单哈希

白名单通过以下字段的复合哈希来识别文件：

- `SectionObjectPointer`（同一数据流的所有 FileObject 共享）
- `DeviceObject`（磁盘标识符）
- `FileSize` XOR `ValidDataLength * FibonacciConstant`（来自 FSRTL_ADVANCED_FCB_HEADER）

这提供了稳定、抗碰撞的身份标识，不受路径变化影响。位图大小为 **256 KiB**（约 200 万位）。

### APC 生命周期

- 每个排队的 APC 递增 `PendingApcCount`
- `ApcKernelRoutine`（APC 触发后在 `APC_LEVEL` 执行）递减计数并释放 KAPC
- `ApcRundownRoutine`（在线程于 APC 触发前终止时执行）执行相同的清理操作
- `DriverUnload` 等待 `AllApcsCompletedEvent`，然后再释放驱动 .text 段，确保没有进行中的 APC 引用已释放的代码

### 平台支持

| 架构 | 状态 |
|-------------|--------|
| x64 | 支持 |
| ARM64 | 支持（使用 `vld1q_u8`/`vst1q_u8` 进行原子 FCB 读取） |

## 许可证

MIT 许可证 — 完整内容见 [LICENSE](LICENSE)。

版权所有 © 2026 [@ZYS-Create1024](https://github.com/ZYS-Create1024)

> **免责声明**：本项目仅供教育和授权的安全研究用途。未经同意向进程注入代码可能违反法律和软件服务条款。请负责任地使用。