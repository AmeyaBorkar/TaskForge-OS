# TaskForge — Team Contributions

**Course:** Operating Systems
**Project:** TaskForge OS — A Banking System running on a custom Operating System kernel
**Team Size:** 5
**Lead:** Ameya Borkar

---

## The Story — From Zero to a Full OS

TaskForge started with a simple question: *can we teach every concept in the OS syllabus by making them all run in one real application?* Textbook OS demos usually show one algorithm in isolation — a scheduler here, a disk algorithm there. We wanted something where every concept earns its place by doing actual work.

So we built a **Banking System on top of a Custom OS Kernel**.

When a user clicks *Deposit*, the request doesn't just update a number. It:

1. Issues a **system call** into the kernel (Unit I)
2. The kernel **creates a process**, moves it through `NEW → READY → RUNNING` (Unit II)
3. The **scheduler** picks which process runs next using FCFS / SJF / RR / Priority (Unit III)
4. A **resource lock** is requested on the account — the **Banker's algorithm** checks if the grant leaves the system in a safe state (Unit IV)
5. **Memory is allocated** using First/Best/Worst/Next Fit, and a **page cache** serves the account record using LRU/FIFO/Clock replacement (Unit V)
6. The **file system** reads/writes the account file, and the **disk scheduler** handles the I/O using FCFS/SSTF/SCAN/C-SCAN (Unit VI)
7. Resources are released, memory is freed, the process is terminated

Every banking action exercises the whole stack. Students can flip the scheduler to SJF, the memory strategy to Best Fit, the disk algorithm to C-SCAN — and watch the same deposit take a different path through the kernel, with live dashboards showing hits, misses, seek distance, fragmentation, and deadlock status.

The project ships in **three forms**:

- **v1 Simulator** (`main.c` + `src/*.c`) — standalone interactive demos of each algorithm (enter your own process set, reference string, or disk queue)
- **v2 CLI** (`main_v2.c` + `kernel/*` + `app/bank.c`) — the full banking system on the C kernel
- **v2 Web** (`web/app.py` + `web/templates/index.html`) — browser-accessible Flask port of v2 with a live OS dashboard

---

## Team Distribution

Every member owns **at least two** core concepts and their implementations. The lead owns cross-cutting pieces (syscall interface, process lifecycle, and system integration) so every other module plugs into one coherent API.

### Ameya Borkar — *Lead*
> Architecture, Unit I (OS & System Calls), Unit II (Process Management core), system integration

**Owned Concepts**
| # | Concept | Unit | Files |
|---|---------|------|-------|
| 1 | System Call Interface (`sys_fork`, `sys_alloc`, `sys_open`, `sys_lock`, `sys_read`, `sys_write`, `sys_close`, `sys_unlock` …) | I | `kernel/os_syscall.h` |
| 2 | OS Services & Goals (convenience, efficiency, evolution) wired as kernel entry points | I | `kernel/os_kernel.h`, `kernel/os_kernel.c` |
| 3 | Process Concept, PCB, 2/5/7-state model, Process Control transitions (`NEW → READY → RUNNING → WAITING/TERMINATED`) | II | `src/process_mgmt.c`, `kernel/os_kernel.c` (`create_process`, `set_state`), `web/app.py` (`Kernel.create_process`, `Kernel.set_state`) |
| 4 | Application Layer: Banking app that exercises every syscall | — | `app/bank.c`, `main_v2.c`, `web/app.py` (`BankApp`) |
| 5 | Overall integration, build system, web dashboard backend | — | `Makefile`, `build.bat`, `web/app.py` |

**Story contribution:** Designed the syscall boundary that the other four layers plug into, and owns the banking application that drives the whole demo.

---

### Ayush Agnihotri
> Unit III (Process Scheduling) + Unit II (Concurrency, Producer-Consumer)

**Owned Concepts**
| # | Concept | Unit | Files |
|---|---------|------|-------|
| 1 | CPU Scheduling criteria, Preemptive vs Non-preemptive, Long/Medium/Short-term schedulers | III | `src/scheduler.c`, `include/scheduler.h` |
| 2 | Scheduling Algorithms — **FCFS, SJF, SRTF, Round Robin, Priority** (both preemptive and non-preemptive variants) with Gantt charts and comparison mode | III | `src/scheduler.c`, `kernel/os_kernel.c` (`schedule_next`), `web/app.py` (`Kernel.schedule_next`) |
| 3 | Concurrency issues + **Producer-Consumer** problem using semaphores | II | `src/process_mgmt.c` (producer-consumer demo) |

**Story contribution:** Decides *which* process runs next. Every dispatcher decision in the banking demo goes through his scheduler.

---

### Arnav Gupta
> Unit V (Memory Management + Virtual Memory + Page Replacement)

**Owned Concepts**
| # | Concept | Unit | Files |
|---|---------|------|-------|
| 1 | Memory Partitioning (Fixed, Dynamic), Fragmentation, Placement strategies — **First Fit, Best Fit, Worst Fit, Next Fit** + coalescing | V | `src/memory_mgmt.c`, `include/memory_mgmt.h`, `kernel/os_kernel.c` (`mem_alloc`, `_coalesce`), `web/app.py` (`Kernel.mem_alloc`) |
| 2 | Virtual Memory concepts — Paging, Segmentation, TLB simulation, Page Table structure | V | `src/memory_mgmt.c` |
| 3 | **Page Replacement Policies — FIFO, LRU, Optimal, Clock** (the page cache that serves account-record reads) | V | `src/memory_mgmt.c`, `kernel/os_kernel.c` (`cache_access`, `_cache_victim`), `web/app.py` (`Kernel.cache_access`) |

**Story contribution:** Every time a banking operation touches an account, his cache and allocator decide whether it's a hit, which slot gets evicted, and where the new PCB/buffer lives.

---

### Aditya Chimurkar
> Unit IV (Deadlocks) + Unit II (Mutual Exclusion & Synchronization)

**Owned Concepts**
| # | Concept | Unit | Files |
|---|---------|------|-------|
| 1 | **Banker's Algorithm** for deadlock avoidance, Resource Allocation Graph, Deadlock Detection, Recovery (resource ordering prevention) | IV | `src/deadlock.c`, `include/deadlock.h`, `kernel/os_kernel.c` (`resource_request`, `banker_safe`, `deadlock_check`), `web/app.py` (`Kernel.resource_request`, `_safety_loop`) |
| 2 | Mutual Exclusion — H/W (test-and-set sim), S/W (Peterson's), OS support (Semaphores, Mutex, Monitors) | II | `src/process_mgmt.c` |
| 3 | Classical Synchronization — **Readers-Writers, Dining Philosophers** demos | II | `src/process_mgmt.c` |

**Story contribution:** A *Transfer* between two accounts locks both in a specific order — if that order would break safety, the Banker's algorithm rolls it back. That whole protection layer is Aditya's.

---

### Aarush Bakshi
> Unit VI (I/O and File Management)

**Owned Concepts**
| # | Concept | Unit | Files |
|---|---------|------|-------|
| 1 | **Disk Scheduling — FCFS, SSTF, SCAN, C-SCAN** with seek-distance accounting and head tracking | VI | `src/io_file_mgmt.c`, `include/io_file_mgmt.h`, `kernel/os_kernel.c` (`io_request`, `io_process`), `web/app.py` (`Kernel.io_process`) |
| 2 | I/O Subsystem — device types, buffering, OS design issues | VI | `src/io_file_mgmt.c`, `kernel/os_kernel.c` |
| 3 | File Management — Virtual File System, directories, file sharing, free-space management, record blocking (account files live here) | VI | `src/io_file_mgmt.c`, `src/task_ops.c`, `kernel/os_kernel.c` (`fs_create`, `fs_open`, `fs_read`, `fs_write`), `web/app.py` (`Kernel.fs_create`…) |

**Story contribution:** Every account is a file. Every deposit becomes a disk I/O. Aarush owns both — so the seek distances and read/write counts you see on the dashboard are his subsystems reporting home.

---

## Concept → Owner Quick Reference

| Syllabus Concept | Owner |
|------------------|-------|
| System Calls, OS Services, OS Types | Ameya |
| Process Concept, PCB, Process States | Ameya |
| CPU Scheduling (FCFS, SJF, RR, Priority) | Ayush |
| Concurrency, Producer-Consumer | Ayush |
| Memory Partitioning, Placement (First/Best/Worst/Next Fit) | Arnav |
| Virtual Memory, Paging, TLB | Arnav |
| Page Replacement (FIFO/LRU/Optimal/Clock) | Arnav |
| Deadlock Principles, Banker's, Detection, Recovery | Aditya |
| Mutual Exclusion, Semaphores, Mutex, Monitors | Aditya |
| Readers-Writers, Dining Philosophers | Aditya |
| Disk Scheduling (FCFS/SSTF/SCAN/C-SCAN) | Aarush |
| I/O Buffering, Device characteristics | Aarush |
| File Management, Directories, Free Space | Aarush |
| Integration, Banking App, Web Dashboard | Ameya (lead) |

---

## File-Level Summary

```
TaskForge/
├── kernel/
│   ├── os_kernel.h         — Kernel types and API              [Ameya]
│   ├── os_kernel.c         — Unified kernel implementation     [All members, integrated by Ameya]
│   └── os_syscall.h        — System call interface             [Ameya]
│
├── app/
│   ├── bank.h              — Banking types                     [Ameya]
│   └── bank.c              — Banking application               [Ameya]
│
├── src/                    — v1 standalone modules
│   ├── process_mgmt.c      — PCB, threads, sync demos         [Ameya + Aditya + Ayush]
│   ├── scheduler.c         — FCFS/SJF/RR/Priority             [Ayush]
│   ├── deadlock.c          — Banker's, RAG, detection         [Aditya]
│   ├── memory_mgmt.c       — Partitioning, paging, replace    [Arnav]
│   ├── io_file_mgmt.c      — Disk sched, VFS, buffering       [Aarush]
│   └── task_ops.c          — Real file ops                    [Aarush]
│
├── include/                — Matching headers                  [Module owners]
│
├── gui/
│   ├── taskforge_gui.c     — Win32 GUI v1                     [Ameya]
│   └── taskforge_gui_v2.c  — Win32 GUI v2 (banking + dash)    [Ameya]
│
├── web/
│   ├── app.py              — Flask backend (all subsystems)    [Ameya, ports everyone's logic]
│   └── templates/index.html — Dark-theme SPA + OS Concepts tab [Ameya]
│
├── main.c                  — v1 entry                          [Ameya]
├── main_v2.c               — v2 entry (boot + banking menu)    [Ameya]
├── build.bat / Makefile    — Build system                      [Ameya]
└── README.md               — Project overview                  [Ameya]
```

---

## Deliverable Summary

**Languages:** C11 (kernel, banking, GUI), Python 3 + Flask (web port), HTML/CSS/JS (dashboard)
**Platforms:** Windows (Win32 GUI), any OS with a browser (web), Linux/Windows/macOS (CLI)
**Total LOC:** ~2,500 (kernel) + ~800 (banking) + ~600 (web) + v1 simulator modules

**What the examiner sees:**
1. Open `http://localhost:5000` after `python web/app.py` — full banking UI
2. Perform a deposit / transfer — the *Operation Result* panel streams the kernel trace showing every Unit being exercised
3. The **OS Concepts** tab maps each syllabus unit to the file, function, and live metric proving it works
4. The **Configuration** tab lets the examiner change algorithms (scheduler, memory strategy, cache policy, disk algorithm) and re-run the same operation to see the path change

Every concept in the syllabus shows up in the live trace of a single banking transaction — that is the project.
