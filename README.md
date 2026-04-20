# TaskForge OS — Banking System on a Custom Operating System Kernel

A course project for **Operating Systems** that implements a complete OS kernel in C (with a Python/Flask port), and a **Banking Application** running on top of it through a system call interface. Every banking operation (deposit, withdraw, transfer) goes through the OS kernel — creating processes, allocating memory, locking resources, reading/writing files, and scheduling I/O — demonstrating how real operating systems work under the hood.

## Quick Start

The fastest way to see every OS concept in action is the web UI:

```bash
pip install flask
python web/app.py
# Open http://localhost:5000 in your browser
```

You get four tabs:
- **Banking** — create accounts, deposit, withdraw, transfer (with live kernel trace)
- **OS Dashboard** — live state of processes, memory, cache, deadlock, filesystem, I/O
- **OS Concepts** — syllabus unit → implementation file → live metric (course-project showcase)
- **Configuration** — switch scheduler / memory strategy / cache policy / disk algorithm on the fly

For the native C builds (CLI + Win32 GUI), see [Building](#building) below.

## Architecture

```
+----------------------------------------------+
|  BANKING APPLICATION           (app/bank.c)  |
|  Create Account | Deposit | Withdraw         |
|  Transfer | Balance | Statement              |
+----------------------------------------------+
|  SYSTEM CALL API            (os_syscall.h)   |
|  sys_fork  sys_alloc  sys_open  sys_lock     |
|  sys_read  sys_write  sys_close sys_unlock   |
+----------------------------------------------+
|  TASKFORGE OS KERNEL        (os_kernel.c)    |
|  Process Mgr | Scheduler | Memory Mgr       |
|  File System | Deadlock Mgr | I/O Subsystem  |
+----------------------------------------------+
```

## OS Concepts Covered

| Unit | Topic | Implementation |
|------|-------|----------------|
| I | System Calls, OS Types | `os_syscall.h` — 25+ system calls (sys_fork, sys_alloc, sys_open, sys_lock, ...) |
| II | Process Management, Threads, Concurrency | PCB with 5-state model, real pthreads, mutexes, semaphores. Producer-Consumer, Readers-Writers, Dining Philosophers demos |
| III | CPU Scheduling | FCFS, SJF, SRTF, Round Robin, Priority (preemptive & non-preemptive). Gantt charts and comparison mode |
| IV | Deadlocks | Banker's Algorithm for avoidance, Resource Allocation Graph, deadlock detection, resource ordering prevention |
| V | Memory Management | First/Best/Worst/Next Fit allocation, paging, segmentation, TLB simulation. Page replacement: FIFO, LRU, Optimal, Clock |
| VI | I/O & File Management | Disk scheduling (FCFS, SSTF, SCAN, C-SCAN), virtual file system, I/O buffering, free space management |

## How It Works

When you perform a banking operation, the OS kernel handles everything:

```
[BANK] Depositing $500.00 to Account #1...
[OS]   Process P5 'deposit' created (Priority: 4)
[OS]   P5 state: NEW -> READY -> RUNNING
[OS]   Memory allocated: 1 KB at address 128 (Best Fit)
[OS]   Resource lock on Account #1: GRANTED (Banker's: SAFE)
[OS]   Cache access page 1: HIT
[OS]   File write: 24 bytes to acc_001.dat
[OS]   Disk I/O: track 13, seek distance 37 (SCAN)
[OS]   Resource unlocked: Account #1
[OS]   Memory freed for P5
[OS]   P5 state: RUNNING -> TERMINATED
[BANK] Deposit complete. New balance: $1,500.00
```

**Transfers** demonstrate deadlock prevention — the system locks both accounts using resource ordering (lower ID first) and validates safety through the Banker's algorithm before proceeding.

## Building

### Prerequisites

- **Web UI:** Python 3.8+ and Flask (`pip install flask`)
- **Native C builds:** GCC (MinGW/MSYS2 on Windows, or GCC on Linux) + pthreads

### Build Commands

**Using build.bat (Windows):**
```batch
build.bat v2        # Banking System on OS Kernel (main project)
build.bat v1        # OS Simulator (standalone demos)
build.bat gui       # Win32 GUI versions
build.bat all       # Build everything
build.bat clean     # Remove build artifacts
```

**Manual compilation:**
```bash
# v2: Banking System on OS Kernel
gcc -Wall -Wextra -std=c11 -Ikernel -c kernel/os_kernel.c -o obj/os_kernel.o
gcc -Wall -Wextra -std=c11 -Ikernel -c app/bank.c -o obj/bank.o
gcc -Wall -Wextra -std=c11 -Ikernel -c main_v2.c -o obj/main_v2.o
gcc -static -o taskforge_v2.exe obj/main_v2.o obj/os_kernel.o obj/bank.o -lpthread

# GUI version
gcc -Wall -Wextra -std=c11 -Ikernel -o taskforge_gui_v2.exe gui/taskforge_gui_v2.c kernel/os_kernel.c -lcomctl32 -lgdi32 -lcomdlg32 -lpthread -mwindows -static
```

## Running

The project ships in three forms — pick whichever suits the demo.

### 1. Web UI (recommended for the course showcase)

```bash
pip install flask
python web/app.py
```

Open `http://localhost:5000`. The **OS Concepts** tab maps each syllabus unit to the exact file / function in the kernel, with live metrics that update as you perform banking operations.

### 2. Native CLI

```bash
./taskforge_v2.exe        # Banking on OS kernel (main project)
./taskforge.exe           # v1 standalone algorithm simulator
```

### 3. Native Win32 GUI

```bash
./taskforge_gui_v2.exe    # Banking + OS dashboard GUI
./taskforge_gui.exe       # v1 simulator GUI
```

## Project Structure

```
TaskForge/
├── kernel/                     # OS Kernel Layer (C)
│   ├── os_kernel.h             # Kernel types and API declarations
│   ├── os_kernel.c             # Kernel implementation
│   └── os_syscall.h            # System call interface for applications
├── app/                        # Application Layer (C)
│   ├── bank.h                  # Banking types and declarations
│   └── bank.c                  # Banking application
├── main_v2.c                   # v2 entry point (OS boot + banking menu)
├── gui/
│   ├── taskforge_gui_v2.c      # Win32 GUI for v2 (banking + OS dashboard)
│   └── taskforge_gui.c         # Win32 GUI for v1 (OS simulator)
├── web/                        # Python/Flask port with web dashboard
│   ├── app.py                  # Kernel + Banking + REST API
│   └── templates/index.html    # Dark-theme SPA (Banking / Dashboard / OS Concepts / Config / Trace)
├── include/                    # v1 module headers
├── src/                        # v1 module implementations
│   ├── process_mgmt.c          # Process states, concurrency demos
│   ├── scheduler.c             # CPU scheduling algorithms
│   ├── deadlock.c              # Banker's, RAG, detection
│   ├── memory_mgmt.c           # Partitioning, paging, page replacement
│   ├── io_file_mgmt.c          # Disk scheduling, VFS, buffering
│   └── task_ops.c              # Real file operations
├── main.c                      # v1 entry point
├── build.bat                   # Windows build script
├── Makefile                    # Make build system
├── CONTRIBUTIONS.md            # Team distribution of syllabus concepts
└── README.md
```

## Features

### Banking Application
- Create accounts with initial balance
- Deposit and withdraw funds
- Transfer between accounts (with deadlock prevention)
- Account statements and transaction history
- Live OS kernel dashboard showing system state

### OS Kernel Dashboard
- Process table with states and scheduling info
- Memory usage, fragmentation, and allocation strategy
- Page cache hit/miss ratio
- Deadlock status and resource allocation
- File system stats and open file descriptors
- Disk I/O metrics and scheduling algorithm

### OS Concepts Showcase (web only)
A dedicated tab that lists all six syllabus units, the exact file/function
that implements each topic, the team member responsible, and a live kernel
metric proving it works. Hit *Refresh* after any banking action to watch the
numbers update.

### v1 Simulator (Bonus)
Interactive demos for all OS algorithms — enter your own process sets, reference strings, and disk queues to see algorithms execute step by step with visual output.

## Tech Stack

- **Languages**: C (C11) for the kernel/banking/GUI; Python 3 + Flask for the web port
- **Threading**: POSIX threads (pthreads)
- **GUI**: Win32 API with Common Controls (native); HTML/CSS/JS SPA (web)
- **Build**: GCC (MinGW/MSYS2) for C; `pip install flask` for web
- **No external runtime dependencies** beyond standard C library, pthreads, Win32 API, and Flask

## Author

**Ameya Borkar** — architecture, kernel, banking application, web UI, integration.

Team distribution and per-concept ownership: [`CONTRIBUTIONS.md`](CONTRIBUTIONS.md).
