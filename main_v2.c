/* ================================================================
 *  TaskForge v2 -- Banking System running on TaskForge OS
 *
 *  Architecture:
 *    +------------------------------------------+
 *    |     BANKING APPLICATION  (app/bank.c)    |
 *    +------------------------------------------+
 *    |     SYSTEM CALL API  (os_syscall.h)      |
 *    +------------------------------------------+
 *    |     TASKFORGE OS KERNEL (os_kernel.c)    |
 *    |  Process | Scheduler | Memory | FS | I/O |
 *    +------------------------------------------+
 *
 *  Course Project -- Operating Systems
 * ================================================================ */

#include "os_kernel.h"
#include "os_syscall.h"
#include "../app/bank.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ============================================================
 *  Console Initialization (Windows ANSI color support)
 * ============================================================ */
static void init_console(void) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | 0x0004);
    }
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

/* ============================================================
 *  OS Kernel Dashboard
 * ============================================================ */
static void print_kernel_dashboard(void) {
    MemStats ms;
    CacheStats cs;

    sys_mem_stats(&ms);
    sys_cache_stats(&cs);

    printf("\n");
    printf(BCYAN "  +========================================================+\n");
    printf("  |              OS KERNEL DASHBOARD                        |\n");
    printf("  +========================================================+\n" RESET);

    /* Process info */
    int total = 0, active = 0, ready = 0, running = 0, terminated = 0;
    pthread_mutex_lock(&g_kernel.proc_lock);
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (g_kernel.procs[i].pid > 0) {
            total++;
            if (g_kernel.procs[i].active) {
                active++;
                if (g_kernel.procs[i].state == PROC_READY) ready++;
                if (g_kernel.procs[i].state == PROC_RUNNING) running++;
            } else {
                terminated++;
            }
        }
    }
    pthread_mutex_unlock(&g_kernel.proc_lock);

    const char *sched_names[] = {"FCFS", "SJF", "PRIORITY", "ROUND ROBIN"};
    printf(BYELLOW "  | PROCESSES" RESET "\n");
    printf("  |   Total: %d | Active: %d (Ready:%d Run:%d) | Done: %d\n",
           total, active, ready, running, terminated);
    printf("  |   Scheduler: %s", sched_names[g_kernel.sched_algo]);
    if (g_kernel.sched_algo == SCHED_ROUND_ROBIN)
        printf(" (quantum=%d)", g_kernel.sched_quantum);
    printf("\n");

    printf(BCYAN "  +--------------------------------------------------------+\n" RESET);

    /* Memory */
    printf(BYELLOW "  | MEMORY" RESET "\n");
    printf("  |   Used: %d / %d KB | Free: %d KB\n", ms.used_kb, ms.total_kb, ms.free_kb);
    const char *strat_names[] = {"FIRST FIT","BEST FIT","WORST FIT","NEXT FIT"};
    printf("  |   Strategy: %s | Blocks: %d alloc, %d free\n",
           strat_names[ms.strategy], ms.block_count - ms.free_blocks, ms.free_blocks);
    printf("  |   Fragmentation: %.1f%%\n", ms.fragmentation * 100.0f);

    printf(BCYAN "  +--------------------------------------------------------+\n" RESET);

    /* Cache */
    const char *cache_names[] = {"LRU","FIFO","CLOCK"};
    printf(BYELLOW "  | PAGE CACHE" RESET "\n");
    printf("  |   Algorithm: %s | Entries: %d / %d\n",
           cache_names[cs.algo], cs.entries_used, cs.cache_size);
    printf("  |   Hits: %d | Misses: %d | Hit Ratio: %.1f%%\n",
           cs.hits, cs.misses, cs.hit_ratio * 100.0f);

    printf(BCYAN "  +--------------------------------------------------------+\n" RESET);

    /* Deadlock */
    printf(BYELLOW "  | DEADLOCK MANAGER" RESET "\n");
    printf("  |   Prevention: %s | Deadlocks detected: %d\n",
           g_kernel.deadlock.prevention_on ? "ON (Banker's)" : "OFF",
           g_kernel.deadlock.deadlock_count);
    printf("  |   Resources available: ");
    for (int i = 0; i < 5 && i < g_kernel.deadlock.n_res; i++)
        printf("R%d=%d ", i, g_kernel.deadlock.available[i]);
    printf("\n");

    printf(BCYAN "  +--------------------------------------------------------+\n" RESET);

    /* File System */
    int files = 0, dirs = 0;
    pthread_mutex_lock(&g_kernel.fs_lock);
    for (int i = 0; i < g_kernel.fs_count; i++) {
        if (g_kernel.fs_nodes[i].active) {
            if (g_kernel.fs_nodes[i].is_dir) dirs++;
            else files++;
        }
    }
    int open_fds = 0;
    for (int i = 0; i < g_kernel.fd_count; i++)
        if (g_kernel.fd_table[i].active) open_fds++;
    pthread_mutex_unlock(&g_kernel.fs_lock);

    printf(BYELLOW "  | FILE SYSTEM" RESET "\n");
    printf("  |   Files: %d | Directories: %d | Open FDs: %d\n", files, dirs, open_fds);

    printf(BCYAN "  +--------------------------------------------------------+\n" RESET);

    /* I/O */
    const char *disk_names[] = {"FCFS","SSTF","SCAN","C-SCAN"};
    printf(BYELLOW "  | I/O SUBSYSTEM" RESET "\n");
    printf("  |   Disk: %s | Head: track %d | Queue: %d pending\n",
           disk_names[g_kernel.io.algo], g_kernel.io.head_pos, g_kernel.io.queue_len);
    printf("  |   Total seek: %d | Ops: %d\n",
           g_kernel.io.total_seek, g_kernel.io.total_ops);
    printf("  |   Buffered reads: %d | writes: %d\n",
           g_kernel.io.buf_reads, g_kernel.io.buf_writes);

    printf(BCYAN "  +--------------------------------------------------------+\n" RESET);

    /* Recent log */
    printf(BYELLOW "  | KERNEL LOG (recent)" RESET "\n");
    pthread_mutex_lock(&g_kernel.log_lock);
    int count = g_kernel.log_count < 10 ? g_kernel.log_count : 10;
    int start = g_kernel.log_count <= OS_MAX_LOG
                ? (g_kernel.log_count - count)
                : ((g_kernel.log_head - count + OS_MAX_LOG) % OS_MAX_LOG);
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % OS_MAX_LOG;
        struct tm *t = localtime(&g_kernel.log[idx].timestamp);
        printf("  |   " DIM "[%02d:%02d:%02d]" RESET " %s\n",
               t->tm_hour, t->tm_min, t->tm_sec,
               g_kernel.log[idx].message);
    }
    pthread_mutex_unlock(&g_kernel.log_lock);

    printf(BCYAN "  +========================================================+\n" RESET);
}

/* ============================================================
 *  OS Configuration Menu
 * ============================================================ */
static void os_config_menu(void) {
    int choice;
    while (1) {
        printf("\n");
        printf(BCYAN "  +--- OS Configuration ---+\n" RESET);
        printf("  | 1. Change Scheduler      |\n");
        printf("  | 2. Change Memory Strategy |\n");
        printf("  | 3. Change Cache Algorithm |\n");
        printf("  | 4. Change Disk Scheduler  |\n");
        printf("  | 5. Run Deadlock Check     |\n");
        printf("  | 6. View Kernel Dashboard  |\n");
        printf("  | 7. View Kernel Log (full) |\n");
        printf("  | 0. Back                   |\n");
        printf(BCYAN "  +-------------------------+\n" RESET);

        printf("  Choice [0-7]: ");
        if (scanf("%d", &choice) != 1) { int c; while((c=getchar())!='\n'&&c!=EOF); continue; }
        int c; while((c=getchar())!='\n'&&c!=EOF);

        switch (choice) {
        case 1: {
            printf("  Scheduler: 0)FCFS 1)SJF 2)PRIORITY 3)RR: ");
            int a; scanf("%d", &a); while((c=getchar())!='\n'&&c!=EOF);
            if (a >= 0 && a <= 3) {
                int q = 5;
                if (a == 3) { printf("  Quantum: "); scanf("%d", &q); while((c=getchar())!='\n'&&c!=EOF); }
                sys_set_scheduler((SchedulerAlgo)a, q);
                printf(BGREEN "  Scheduler updated.\n" RESET);
            }
            break;
        }
        case 2: {
            printf("  Strategy: 0)FirstFit 1)BestFit 2)WorstFit 3)NextFit: ");
            int s; scanf("%d", &s); while((c=getchar())!='\n'&&c!=EOF);
            if (s >= 0 && s <= 3) {
                sys_set_mem_strategy((MemStrategy)s);
                printf(BGREEN "  Memory strategy updated.\n" RESET);
            }
            break;
        }
        case 3: {
            printf("  Cache: 0)LRU 1)FIFO 2)CLOCK: ");
            int a; scanf("%d", &a); while((c=getchar())!='\n'&&c!=EOF);
            if (a >= 0 && a <= 2) {
                sys_set_cache_algo((CacheAlgo)a);
                printf(BGREEN "  Cache algorithm updated.\n" RESET);
            }
            break;
        }
        case 4: {
            printf("  Disk: 0)FCFS 1)SSTF 2)SCAN 3)C-SCAN: ");
            int a; scanf("%d", &a); while((c=getchar())!='\n'&&c!=EOF);
            if (a >= 0 && a <= 3) {
                sys_set_disk_algo((DiskAlgo)a);
                printf(BGREEN "  Disk algorithm updated.\n" RESET);
            }
            break;
        }
        case 5: {
            int dl_procs[OS_MAX_PROCESSES], dl_count = 0;
            int result = sys_deadlock_check(dl_procs, &dl_count);
            if (result) {
                printf(BRED "  DEADLOCK DETECTED! %d processes involved:\n" RESET, dl_count);
                for (int i = 0; i < dl_count; i++)
                    printf("    P%d\n", dl_procs[i]);
            } else {
                printf(BGREEN "  No deadlock detected. System is safe.\n" RESET);
            }
            break;
        }
        case 6:
            print_kernel_dashboard();
            break;
        case 7: {
            printf("\n  " BYELLOW "--- Full Kernel Log ---" RESET "\n");
            pthread_mutex_lock(&g_kernel.log_lock);
            int cnt = g_kernel.log_count < OS_MAX_LOG ? g_kernel.log_count : OS_MAX_LOG;
            int st = g_kernel.log_count <= OS_MAX_LOG ? 0 : g_kernel.log_head;
            for (int i = 0; i < cnt; i++) {
                int idx = (st + i) % OS_MAX_LOG;
                struct tm *t = localtime(&g_kernel.log[idx].timestamp);
                printf("  " DIM "[%02d:%02d:%02d]" RESET " %s\n",
                       t->tm_hour, t->tm_min, t->tm_sec,
                       g_kernel.log[idx].message);
            }
            pthread_mutex_unlock(&g_kernel.log_lock);
            break;
        }
        case 0: return;
        }
    }
}

/* ============================================================
 *  Banner
 * ============================================================ */
static void print_banner(void) {
    printf(BCYAN "\n");
    printf("  ============================================================\n");
    printf("  |                                                          |\n");
    printf("  |" BWHITE "   TASKFORGE -- Banking System on TaskForge OS v2.0     " BCYAN "|\n");
    printf("  |" BYELLOW "   OS Kernel + Application Layer Architecture           " BCYAN "|\n");
    printf("  |                                                          |\n");
    printf("  ============================================================\n");
    printf(RESET "\n");
}

/* ============================================================
 *  Main Menu
 * ============================================================ */
static void main_menu(BankState *bs) {
    int choice;
    while (1) {
        printf("\n");
        printf(BCYAN "  +======================================================+\n");
        printf("  |                  MAIN MENU                            |\n");
        printf("  +======================================================+\n" RESET);
        printf(BYELLOW "  |  [A] BANKING APPLICATION" RESET "\n");
        printf("  |    1. Enter Banking System\n");
        printf(BYELLOW "  |  [B] OS KERNEL" RESET "\n");
        printf("  |    2. Kernel Dashboard\n");
        printf("  |    3. OS Configuration\n");
        printf(BYELLOW "  |  [C] INFO" RESET "\n");
        printf("  |    4. About / Architecture\n");
        printf("  |    0. Shutdown\n");
        printf(BCYAN "  +======================================================+\n" RESET);

        printf("\n  Choice [0-4]: ");
        if (scanf("%d", &choice) != 1) { int c; while((c=getchar())!='\n'&&c!=EOF); continue; }
        int c; while((c=getchar())!='\n'&&c!=EOF);

        switch (choice) {
        case 1:
            bank_menu(bs);
            break;
        case 2:
            print_kernel_dashboard();
            printf("\n" BYELLOW "  [Press Enter]" RESET);
            { int ch; while((ch=getchar())!='\n'&&ch!=EOF); }
            break;
        case 3:
            os_config_menu();
            break;
        case 4:
            printf("\n");
            printf(BCYAN "  ============================================================\n" RESET);
            printf(BWHITE "  TASKFORGE v2 -- Architecture\n\n" RESET);
            printf("    +----------------------------------------------+\n");
            printf("    |" BGREEN "     BANKING APPLICATION" RESET "  (app/bank.c)       |\n");
            printf("    |  Create Account | Deposit | Withdraw         |\n");
            printf("    |  Transfer | Balance | Statement              |\n");
            printf("    +----------------------------------------------+\n");
            printf("    |" BYELLOW "     SYSTEM CALL API" RESET "  (os_syscall.h)         |\n");
            printf("    |  sys_fork  sys_alloc  sys_open  sys_lock     |\n");
            printf("    |  sys_read  sys_write  sys_close sys_unlock   |\n");
            printf("    +----------------------------------------------+\n");
            printf("    |" BMAGENTA "     TASKFORGE OS KERNEL" RESET "  (os_kernel.c)      |\n");
            printf("    |  Process Mgr | Scheduler | Memory Mgr       |\n");
            printf("    |  File System | Deadlock Mgr | I/O Subsystem |\n");
            printf("    +----------------------------------------------+\n");
            printf("\n");
            printf("  " BYELLOW "OS Concepts Used:" RESET "\n");
            printf("    Unit I   - System calls (sys_fork, sys_open, ...)\n");
            printf("    Unit II  - Process states, PCB, mutexes, semaphores\n");
            printf("    Unit III - Scheduling (FCFS, SJF, Priority, RR)\n");
            printf("    Unit IV  - Deadlock (Banker's algorithm, detection)\n");
            printf("    Unit V   - Memory (First/Best/Worst Fit, LRU cache)\n");
            printf("    Unit VI  - File system, disk scheduling, I/O buffering\n");
            printf("\n" BYELLOW "  [Press Enter]" RESET);
            { int ch; while((ch=getchar())!='\n'&&ch!=EOF); }
            break;
        case 0:
            return;
        }
    }
}

/* ============================================================
 *  Entry Point
 * ============================================================ */
int main(void) {
    BankState bank;

    init_console();
    srand((unsigned int)time(NULL));

    /* Boot the OS kernel */
    print_banner();
    printf(BYELLOW "  Booting TaskForge OS...\n" RESET);
    kernel_init(&g_kernel);
    printf(BGREEN "  [OK]" RESET " Kernel initialized\n");
    printf(BGREEN "  [OK]" RESET " Process Manager ready (scheduler: FCFS)\n");
    printf(BGREEN "  [OK]" RESET " Memory Manager ready (%d KB, First Fit)\n", OS_MEMORY_SIZE);
    printf(BGREEN "  [OK]" RESET " File System ready (root mounted)\n");
    printf(BGREEN "  [OK]" RESET " Deadlock Manager ready (Banker's prevention: ON)\n");
    printf(BGREEN "  [OK]" RESET " I/O Subsystem ready (disk: FCFS)\n");
    printf(BGREEN "  [OK]" RESET " Cache ready (LRU, %d pages)\n", OS_CACHE_SIZE);
    printf("\n");

    /* Initialize banking application */
    printf(BYELLOW "  Starting Banking Application...\n" RESET);
    bank_init(&bank);
    printf(BGREEN "  [OK]" RESET " Banking system ready\n");
    printf("\n" BYELLOW "  [Press Enter to continue]" RESET);
    { int c; while((c=getchar())!='\n'&&c!=EOF); }

    /* Run main menu */
    main_menu(&bank);

    /* Shutdown */
    printf("\n" BYELLOW "  Shutting down TaskForge OS...\n" RESET);
    kernel_shutdown(&g_kernel);
    printf(BGREEN "  Goodbye!\n" RESET "\n");

    return 0;
}
