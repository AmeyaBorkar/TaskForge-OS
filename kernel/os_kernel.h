/* ================================================================
 *  TaskForge OS Kernel -- Header
 *  Provides: Process Management, Scheduler, Memory Manager,
 *            File System, Deadlock Manager, I/O Subsystem
 * ================================================================ */
#ifndef OS_KERNEL_H
#define OS_KERNEL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef _WIN32
#include <windows.h>
#define os_sleep(ms) Sleep(ms)
#else
#include <unistd.h>
#define os_sleep(ms) usleep((ms)*1000)
#endif

/* ============================================================
 *  ANSI Colors
 * ============================================================ */
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define BRED        "\033[1;31m"
#define BGREEN      "\033[1;32m"
#define BYELLOW     "\033[1;33m"
#define BBLUE       "\033[1;34m"
#define BMAGENTA    "\033[1;35m"
#define BCYAN       "\033[1;36m"
#define BWHITE      "\033[1;37m"

/* ============================================================
 *  Constants
 * ============================================================ */
#define OS_MAX_PROCESSES    64
#define OS_MAX_RESOURCES    32
#define OS_MAX_FILES        64
#define OS_MAX_OPEN_FDS     128
#define OS_MAX_MEM_BLOCKS   256
#define OS_MEMORY_SIZE      4096    /* KB */
#define OS_PAGE_SIZE        64      /* KB per page */
#define OS_NUM_FRAMES       32      /* physical frames */
#define OS_CACHE_SIZE       16      /* cached pages */
#define OS_DISK_SIZE        200     /* tracks */
#define OS_MAX_DISK_QUEUE   64
#define OS_IO_BUF_SIZE      4096
#define OS_MAX_NAME         64
#define OS_MAX_PATH         256
#define OS_MAX_LOG          256

/* ============================================================
 *  Process Management
 * ============================================================ */
typedef enum {
    PROC_NEW,
    PROC_READY,
    PROC_RUNNING,
    PROC_WAITING,
    PROC_TERMINATED
} ProcessState;

typedef enum {
    SCHED_FCFS,
    SCHED_SJF,
    SCHED_PRIORITY,
    SCHED_ROUND_ROBIN
} SchedulerAlgo;

typedef struct {
    int             pid;
    char            name[OS_MAX_NAME];
    ProcessState    state;
    int             priority;           /* 1=highest, 10=lowest */
    int             burst_time;         /* estimated ms */
    int             remaining_time;
    int             arrival_time;       /* relative ms since boot */
    int             start_time;
    int             completion_time;
    int             wait_time;
    int             turnaround_time;
    int             resources_held[OS_MAX_RESOURCES];   /* count per resource */
    int             resources_max[OS_MAX_RESOURCES];    /* max need */
    int             mem_allocated;      /* KB allocated from pool */
    int             active;
    void            (*entry)(void *);   /* function to run */
    void            *arg;
    pthread_t       thread;
} PCB;

/* ============================================================
 *  Memory Management
 * ============================================================ */
typedef enum {
    MEM_FIRST_FIT,
    MEM_BEST_FIT,
    MEM_WORST_FIT,
    MEM_NEXT_FIT
} MemStrategy;

typedef enum {
    CACHE_LRU,
    CACHE_FIFO,
    CACHE_CLOCK
} CacheAlgo;

typedef struct {
    int     id;
    int     start;      /* start address in KB */
    int     size;       /* size in KB */
    int     free;       /* 1=free, 0=allocated */
    int     owner_pid;  /* -1 if free */
} MemBlock;

typedef struct {
    int     page_id;        /* -1 if empty */
    int     ref_bit;        /* for clock algo */
    int     last_access;    /* tick for LRU */
    int     load_time;      /* tick for FIFO */
} CacheEntry;

typedef struct {
    int     total_kb;
    int     used_kb;
    int     free_kb;
    int     block_count;
    int     free_blocks;
    float   fragmentation;
    MemStrategy strategy;
} MemStats;

typedef struct {
    int     cache_size;
    int     entries_used;
    int     hits;
    int     misses;
    float   hit_ratio;
    CacheAlgo algo;
} CacheStats;

/* ============================================================
 *  File System
 * ============================================================ */
typedef struct {
    int         id;
    char        name[OS_MAX_NAME];
    int         is_dir;
    int         size;           /* bytes */
    int         parent_id;      /* -1 for root */
    int         active;
    time_t      created;
    time_t      modified;
    char        data[OS_IO_BUF_SIZE]; /* file content (simplified) */
    int         data_len;
} FSNode;

typedef struct {
    int         fd;
    int         file_id;        /* index into fs_nodes */
    int         mode;           /* 1=read, 2=write, 3=rw */
    int         offset;
    int         owner_pid;
    int         active;
} FileDescriptor;

/* ============================================================
 *  Deadlock Management
 * ============================================================ */
typedef struct {
    int     available[OS_MAX_RESOURCES];
    int     allocation[OS_MAX_PROCESSES][OS_MAX_RESOURCES];
    int     max_need[OS_MAX_PROCESSES][OS_MAX_RESOURCES];
    int     need[OS_MAX_PROCESSES][OS_MAX_RESOURCES];
    int     n_proc;
    int     n_res;
    int     deadlock_count;
    int     prevention_on;      /* resource ordering */
} DeadlockMgr;

/* ============================================================
 *  I/O Subsystem
 * ============================================================ */
typedef enum {
    DISK_FCFS,
    DISK_SSTF,
    DISK_SCAN,
    DISK_CSCAN
} DiskAlgo;

typedef struct {
    int     request_queue[OS_MAX_DISK_QUEUE];
    int     queue_len;
    int     head_pos;
    int     direction;          /* 1=toward max, 0=toward 0 */
    int     total_seek;
    int     total_ops;
    DiskAlgo algo;
    /* Buffering stats */
    int     buf_reads;
    int     buf_writes;
    int     buf_hits;
} IOSubsystem;

/* ============================================================
 *  OS Kernel Log
 * ============================================================ */
typedef struct {
    char    message[128];
    time_t  timestamp;
} LogEntry;

/* ============================================================
 *  Kernel State (the full OS)
 * ============================================================ */
typedef struct {
    /* Process table */
    PCB             procs[OS_MAX_PROCESSES];
    int             proc_count;
    int             next_pid;
    SchedulerAlgo   sched_algo;
    int             sched_quantum;      /* for RR, in ms */
    int             clock_tick;         /* global tick counter */
    pthread_mutex_t proc_lock;

    /* Memory */
    MemBlock        mem_blocks[OS_MAX_MEM_BLOCKS];
    int             mem_block_count;
    MemStrategy     mem_strategy;
    int             mem_next_fit_pos;   /* for next-fit */
    CacheEntry      cache[OS_CACHE_SIZE];
    CacheAlgo       cache_algo;
    int             cache_tick;
    int             cache_hits;
    int             cache_misses;
    int             cache_clock_hand;
    pthread_mutex_t mem_lock;

    /* File system */
    FSNode          fs_nodes[OS_MAX_FILES];
    int             fs_count;
    FileDescriptor  fd_table[OS_MAX_OPEN_FDS];
    int             fd_count;
    int             next_fd;
    int             fs_cwd;             /* current working directory id */
    pthread_mutex_t fs_lock;

    /* Deadlock */
    DeadlockMgr     deadlock;
    pthread_mutex_t dl_lock;

    /* I/O */
    IOSubsystem     io;
    pthread_mutex_t io_lock;

    /* Kernel log */
    LogEntry        log[OS_MAX_LOG];
    int             log_count;
    int             log_head;           /* circular buffer */
    pthread_mutex_t log_lock;

    /* Boot time */
    time_t          boot_time;
    int             running;
} Kernel;

/* ============================================================
 *  Kernel API -- called by syscall layer
 * ============================================================ */

/* Boot / Shutdown */
void    kernel_init(Kernel *k);
void    kernel_shutdown(Kernel *k);
void    kernel_log(Kernel *k, const char *fmt, ...);

/* Process Management */
int     kernel_create_process(Kernel *k, const char *name, int priority,
                              int burst, void (*entry)(void*), void *arg);
int     kernel_kill_process(Kernel *k, int pid);
int     kernel_set_state(Kernel *k, int pid, ProcessState state);
PCB*    kernel_get_pcb(Kernel *k, int pid);
int     kernel_schedule_next(Kernel *k);
void    kernel_set_scheduler(Kernel *k, SchedulerAlgo algo, int quantum);

/* Memory Management */
int     kernel_mem_alloc(Kernel *k, int pid, int size_kb);
int     kernel_mem_free(Kernel *k, int pid, int start_addr);
void    kernel_mem_free_all(Kernel *k, int pid);
void    kernel_mem_stats(Kernel *k, MemStats *stats);
void    kernel_set_mem_strategy(Kernel *k, MemStrategy s);
/* Cache */
int     kernel_cache_access(Kernel *k, int page_id);
void    kernel_cache_stats(Kernel *k, CacheStats *stats);
void    kernel_set_cache_algo(Kernel *k, CacheAlgo algo);

/* File System */
int     kernel_fs_create(Kernel *k, const char *name, int is_dir, int parent);
int     kernel_fs_delete(Kernel *k, int file_id);
int     kernel_fs_find(Kernel *k, const char *name, int parent);
int     kernel_fs_open(Kernel *k, int file_id, int mode, int pid);
int     kernel_fs_close(Kernel *k, int fd);
int     kernel_fs_read(Kernel *k, int fd, char *buf, int size);
int     kernel_fs_write(Kernel *k, int fd, const char *buf, int size);
int     kernel_fs_list(Kernel *k, int dir_id, int *out_ids, int max);

/* Deadlock Management */
int     kernel_resource_request(Kernel *k, int pid, int res_id, int count);
int     kernel_resource_release(Kernel *k, int pid, int res_id, int count);
int     kernel_deadlock_check(Kernel *k, int *deadlocked, int *dl_count);
int     kernel_banker_safe(Kernel *k);
void    kernel_set_max_need(Kernel *k, int pid, int res_id, int max);

/* I/O Subsystem */
int     kernel_io_request(Kernel *k, int track);
int     kernel_io_process(Kernel *k);
void    kernel_set_disk_algo(Kernel *k, DiskAlgo algo);

#endif /* OS_KERNEL_H */
