/* ================================================================
 *  TaskForge OS Kernel -- Implementation
 *  Provides: Process Management, Scheduler, Memory Manager,
 *            File System, Deadlock Manager, I/O Subsystem
 * ================================================================ */

#include "os_kernel.h"
#include <stdarg.h>

/* ============================================================
 *  Global kernel instance (referenced by os_syscall.h)
 * ============================================================ */
Kernel g_kernel;

/* ============================================================
 *  Helper: absolute value for integers
 * ============================================================ */
static int int_abs(int x) {
    return x < 0 ? -x : x;
}

/* ============================================================
 *  Helper: find process table index by pid (-1 if not found)
 * ============================================================ */
static int find_proc_index(Kernel *k, int pid) {
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (k->procs[i].active && k->procs[i].pid == pid)
            return i;
    }
    return -1;
}

/* ================================================================
 *  SECTION 1 -- BOOT / SHUTDOWN / LOGGING
 * ================================================================ */

/* ---------------------------------------------------------------
 *  kernel_init -- bring the kernel to life
 * --------------------------------------------------------------- */
void kernel_init(Kernel *k) {
    /* Zero everything first */
    memset(k, 0, sizeof(Kernel));

    /* ---- Mutexes ---- */
    pthread_mutex_init(&k->proc_lock, NULL);
    pthread_mutex_init(&k->mem_lock,  NULL);
    pthread_mutex_init(&k->fs_lock,   NULL);
    pthread_mutex_init(&k->dl_lock,   NULL);
    pthread_mutex_init(&k->io_lock,   NULL);
    pthread_mutex_init(&k->log_lock,  NULL);

    /* ---- Memory: one big free block ---- */
    k->mem_blocks[0].id        = 0;
    k->mem_blocks[0].start     = 0;
    k->mem_blocks[0].size      = OS_MEMORY_SIZE;
    k->mem_blocks[0].free      = 1;
    k->mem_blocks[0].owner_pid = -1;
    k->mem_block_count         = 1;
    k->mem_next_fit_pos        = 0;

    /* ---- File system: root directory ---- */
    k->fs_nodes[0].id        = 0;
    strncpy(k->fs_nodes[0].name, "/", OS_MAX_NAME - 1);
    k->fs_nodes[0].is_dir    = 1;
    k->fs_nodes[0].parent_id = -1;
    k->fs_nodes[0].active    = 1;
    k->fs_nodes[0].size      = 0;
    k->fs_nodes[0].data_len  = 0;
    k->fs_nodes[0].created   = time(NULL);
    k->fs_nodes[0].modified  = k->fs_nodes[0].created;
    k->fs_count               = 1;
    k->fs_cwd                 = 0;

    /* ---- Defaults ---- */
    k->sched_algo    = SCHED_FCFS;
    k->sched_quantum = 10;
    k->mem_strategy  = MEM_FIRST_FIT;
    k->cache_algo    = CACHE_LRU;
    k->io.algo       = DISK_FCFS;
    k->io.head_pos   = OS_DISK_SIZE / 2;
    k->io.direction  = 1;

    /* ---- Cache: mark all slots empty ---- */
    for (int i = 0; i < OS_CACHE_SIZE; i++) {
        k->cache[i].page_id     = -1;
        k->cache[i].ref_bit     = 0;
        k->cache[i].last_access = 0;
        k->cache[i].load_time   = 0;
    }
    k->cache_tick       = 1;
    k->cache_clock_hand = 0;

    /* ---- Deadlock manager ---- */
    k->deadlock.n_res        = OS_MAX_RESOURCES;
    k->deadlock.n_proc       = 0;
    k->deadlock.deadlock_count = 0;
    k->deadlock.prevention_on = 0;
    for (int r = 0; r < OS_MAX_RESOURCES; r++) {
        k->deadlock.available[r] = (r < 5) ? 10 : 0;
    }

    /* ---- Misc ---- */
    k->boot_time  = time(NULL);
    k->running    = 1;
    k->next_pid   = 1;
    k->next_fd    = 1;
    k->clock_tick = 0;
    k->proc_count = 0;
    k->log_count  = 0;
    k->log_head   = 0;

    kernel_log(k, "Kernel initialized");
}

/* ---------------------------------------------------------------
 *  kernel_shutdown -- tear everything down
 * --------------------------------------------------------------- */
void kernel_shutdown(Kernel *k) {
    k->running = 0;

    kernel_log(k, "Kernel shutdown");

    pthread_mutex_destroy(&k->proc_lock);
    pthread_mutex_destroy(&k->mem_lock);
    pthread_mutex_destroy(&k->fs_lock);
    pthread_mutex_destroy(&k->dl_lock);
    pthread_mutex_destroy(&k->io_lock);
    pthread_mutex_destroy(&k->log_lock);
}

/* ---------------------------------------------------------------
 *  kernel_log -- thread-safe circular log buffer
 * --------------------------------------------------------------- */
void kernel_log(Kernel *k, const char *fmt, ...) {
    pthread_mutex_lock(&k->log_lock);

    va_list ap;
    va_start(ap, fmt);

    int idx = k->log_head;
    vsnprintf(k->log[idx].message, sizeof(k->log[idx].message), fmt, ap);
    k->log[idx].timestamp = time(NULL);

    k->log_head = (k->log_head + 1) % OS_MAX_LOG;
    if (k->log_count < OS_MAX_LOG)
        k->log_count++;

    va_end(ap);

    pthread_mutex_unlock(&k->log_lock);
}

/* ================================================================
 *  SECTION 2 -- PROCESS MANAGEMENT
 * ================================================================ */

/* ---------------------------------------------------------------
 *  kernel_create_process -- spawn a new process
 * --------------------------------------------------------------- */
int kernel_create_process(Kernel *k, const char *name, int priority,
                          int burst, void (*entry)(void *), void *arg) {
    pthread_mutex_lock(&k->proc_lock);

    /* Find an empty slot */
    int slot = -1;
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (!k->procs[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&k->proc_lock);
        kernel_log(k, "Process table full -- cannot create '%s'", name);
        return -1;
    }

    PCB *p = &k->procs[slot];
    memset(p, 0, sizeof(PCB));

    p->pid            = k->next_pid++;
    strncpy(p->name, name, OS_MAX_NAME - 1);
    p->name[OS_MAX_NAME - 1] = '\0';
    p->priority       = priority;
    p->burst_time     = burst;
    p->remaining_time = burst;
    p->state          = PROC_NEW;
    p->arrival_time   = k->clock_tick;
    p->start_time     = -1;
    p->completion_time = 0;
    p->wait_time      = 0;
    p->turnaround_time = 0;
    p->mem_allocated  = 0;
    p->active         = 1;
    p->entry          = entry;
    p->arg            = arg;

    k->proc_count++;

    int pid = p->pid;
    pthread_mutex_unlock(&k->proc_lock);

    kernel_log(k, "Process P%d '%s' created", pid, name);
    return pid;
}

/* ---------------------------------------------------------------
 *  kernel_kill_process -- terminate and clean up
 * --------------------------------------------------------------- */
int kernel_kill_process(Kernel *k, int pid) {
    pthread_mutex_lock(&k->proc_lock);

    int idx = find_proc_index(k, pid);
    if (idx < 0) {
        pthread_mutex_unlock(&k->proc_lock);
        kernel_log(k, "Kill failed -- P%d not found", pid);
        return -1;
    }

    PCB *p = &k->procs[idx];
    p->state  = PROC_TERMINATED;
    p->active = 0;
    k->proc_count--;

    pthread_mutex_unlock(&k->proc_lock);

    /* Free all memory owned by this process */
    kernel_mem_free_all(k, pid);

    /* Release all resources held by this process */
    pthread_mutex_lock(&k->dl_lock);
    for (int r = 0; r < k->deadlock.n_res; r++) {
        if (p->resources_held[r] > 0) {
            k->deadlock.available[r] += p->resources_held[r];
            k->deadlock.allocation[idx][r] = 0;
            k->deadlock.need[idx][r] = k->deadlock.max_need[idx][r];
            p->resources_held[r] = 0;
        }
    }
    pthread_mutex_unlock(&k->dl_lock);

    kernel_log(k, "Process P%d terminated", pid);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_set_state -- validated state transition
 * --------------------------------------------------------------- */
int kernel_set_state(Kernel *k, int pid, ProcessState new_state) {
    pthread_mutex_lock(&k->proc_lock);

    int idx = find_proc_index(k, pid);
    if (idx < 0) {
        pthread_mutex_unlock(&k->proc_lock);
        return -1;
    }

    PCB *p = &k->procs[idx];
    ProcessState old = p->state;

    /* Validate transition:
     *   NEW       -> READY
     *   READY     -> RUNNING
     *   RUNNING   -> READY, WAITING, TERMINATED
     *   WAITING   -> READY
     *   TERMINATED-> (none)
     */
    int valid = 0;
    switch (old) {
        case PROC_NEW:
            if (new_state == PROC_READY) valid = 1;
            break;
        case PROC_READY:
            if (new_state == PROC_RUNNING) valid = 1;
            break;
        case PROC_RUNNING:
            if (new_state == PROC_READY ||
                new_state == PROC_WAITING ||
                new_state == PROC_TERMINATED) valid = 1;
            break;
        case PROC_WAITING:
            if (new_state == PROC_READY) valid = 1;
            break;
        case PROC_TERMINATED:
            break;
    }

    if (!valid) {
        pthread_mutex_unlock(&k->proc_lock);
        kernel_log(k, "Invalid transition P%d: %d -> %d", pid, old, new_state);
        return -1;
    }

    p->state = new_state;

    /* Side effects */
    if (new_state == PROC_RUNNING) {
        if (p->start_time < 0)
            p->start_time = k->clock_tick;
        k->clock_tick++;
    }

    if (new_state == PROC_TERMINATED) {
        p->completion_time  = k->clock_tick;
        p->turnaround_time  = p->completion_time - p->arrival_time;
        p->wait_time        = p->turnaround_time - p->burst_time;
        if (p->wait_time < 0)
            p->wait_time = 0;
    }

    static const char *state_names[] = {
        "NEW", "READY", "RUNNING", "WAITING", "TERMINATED"
    };

    pthread_mutex_unlock(&k->proc_lock);

    kernel_log(k, "P%d: %s -> %s", pid, state_names[old], state_names[new_state]);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_get_pcb -- lookup by pid
 * --------------------------------------------------------------- */
PCB *kernel_get_pcb(Kernel *k, int pid) {
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (k->procs[i].active && k->procs[i].pid == pid)
            return &k->procs[i];
    }
    return NULL;
}

/* ---------------------------------------------------------------
 *  kernel_schedule_next -- pick the next READY process to run
 * --------------------------------------------------------------- */
int kernel_schedule_next(Kernel *k) {
    pthread_mutex_lock(&k->proc_lock);

    int chosen = -1;

    switch (k->sched_algo) {

    case SCHED_FCFS: {
        int best_arrival = __INT_MAX__;
        for (int i = 0; i < OS_MAX_PROCESSES; i++) {
            if (k->procs[i].active && k->procs[i].state == PROC_READY) {
                if (k->procs[i].arrival_time < best_arrival) {
                    best_arrival = k->procs[i].arrival_time;
                    chosen = i;
                }
            }
        }
        break;
    }

    case SCHED_SJF: {
        int shortest = __INT_MAX__;
        for (int i = 0; i < OS_MAX_PROCESSES; i++) {
            if (k->procs[i].active && k->procs[i].state == PROC_READY) {
                if (k->procs[i].remaining_time < shortest) {
                    shortest = k->procs[i].remaining_time;
                    chosen = i;
                }
            }
        }
        break;
    }

    case SCHED_PRIORITY: {
        int best_pri = __INT_MAX__;
        for (int i = 0; i < OS_MAX_PROCESSES; i++) {
            if (k->procs[i].active && k->procs[i].state == PROC_READY) {
                if (k->procs[i].priority < best_pri) {
                    best_pri = k->procs[i].priority;
                    chosen = i;
                }
            }
        }
        break;
    }

    case SCHED_ROUND_ROBIN: {
        /* Find the next READY process after the last-scheduled slot */
        static int last_slot = -1;
        int start = (last_slot + 1) % OS_MAX_PROCESSES;
        for (int n = 0; n < OS_MAX_PROCESSES; n++) {
            int i = (start + n) % OS_MAX_PROCESSES;
            if (k->procs[i].active && k->procs[i].state == PROC_READY) {
                chosen = i;
                break;
            }
        }
        if (chosen >= 0) {
            last_slot = chosen;
            /* Decrement quantum from remaining time */
            if (k->procs[chosen].remaining_time > k->sched_quantum)
                k->procs[chosen].remaining_time -= k->sched_quantum;
            else
                k->procs[chosen].remaining_time = 0;
        }
        break;
    }
    } /* end switch */

    if (chosen >= 0) {
        PCB *p = &k->procs[chosen];
        p->state = PROC_RUNNING;
        if (p->start_time < 0)
            p->start_time = k->clock_tick;
        k->clock_tick++;

        int pid = p->pid;
        pthread_mutex_unlock(&k->proc_lock);
        kernel_log(k, "Scheduled P%d '%s'", pid, p->name);
        return pid;
    }

    pthread_mutex_unlock(&k->proc_lock);
    return -1;
}

/* ---------------------------------------------------------------
 *  kernel_set_scheduler
 * --------------------------------------------------------------- */
void kernel_set_scheduler(Kernel *k, SchedulerAlgo algo, int quantum) {
    pthread_mutex_lock(&k->proc_lock);
    k->sched_algo    = algo;
    k->sched_quantum = quantum;
    pthread_mutex_unlock(&k->proc_lock);

    static const char *names[] = {"FCFS", "SJF", "PRIORITY", "ROUND_ROBIN"};
    kernel_log(k, "Scheduler set to %s (quantum=%d)", names[algo], quantum);
}

/* ================================================================
 *  SECTION 3 -- MEMORY MANAGEMENT
 * ================================================================ */

/* ---------------------------------------------------------------
 *  kernel_mem_alloc -- allocate contiguous KB from pool
 * --------------------------------------------------------------- */
int kernel_mem_alloc(Kernel *k, int pid, int size_kb) {
    if (size_kb <= 0) return -1;

    pthread_mutex_lock(&k->mem_lock);

    int best = -1;  /* index of chosen free block */

    switch (k->mem_strategy) {

    case MEM_FIRST_FIT:
        for (int i = 0; i < k->mem_block_count; i++) {
            if (k->mem_blocks[i].free && k->mem_blocks[i].size >= size_kb) {
                best = i;
                break;
            }
        }
        break;

    case MEM_BEST_FIT: {
        int best_size = __INT_MAX__;
        for (int i = 0; i < k->mem_block_count; i++) {
            if (k->mem_blocks[i].free && k->mem_blocks[i].size >= size_kb) {
                if (k->mem_blocks[i].size < best_size) {
                    best_size = k->mem_blocks[i].size;
                    best = i;
                }
            }
        }
        break;
    }

    case MEM_WORST_FIT: {
        int worst_size = -1;
        for (int i = 0; i < k->mem_block_count; i++) {
            if (k->mem_blocks[i].free && k->mem_blocks[i].size >= size_kb) {
                if (k->mem_blocks[i].size > worst_size) {
                    worst_size = k->mem_blocks[i].size;
                    best = i;
                }
            }
        }
        break;
    }

    case MEM_NEXT_FIT: {
        int start = k->mem_next_fit_pos;
        for (int n = 0; n < k->mem_block_count; n++) {
            int i = (start + n) % k->mem_block_count;
            if (k->mem_blocks[i].free && k->mem_blocks[i].size >= size_kb) {
                best = i;
                k->mem_next_fit_pos = (i + 1) % k->mem_block_count;
                break;
            }
        }
        break;
    }
    } /* end switch */

    if (best < 0) {
        pthread_mutex_unlock(&k->mem_lock);
        kernel_log(k, "mem_alloc FAILED: no block for %d KB (pid=%d)", size_kb, pid);
        return -1;
    }

    MemBlock *blk = &k->mem_blocks[best];
    int addr = blk->start;

    /* Split if block is larger than requested */
    if (blk->size > size_kb) {
        if (k->mem_block_count >= OS_MAX_MEM_BLOCKS) {
            pthread_mutex_unlock(&k->mem_lock);
            kernel_log(k, "mem_alloc FAILED: block table full");
            return -1;
        }
        /* Shift blocks after 'best' to make room for the remainder */
        for (int i = k->mem_block_count; i > best + 1; i--)
            k->mem_blocks[i] = k->mem_blocks[i - 1];

        /* Remainder block goes right after the allocated portion */
        MemBlock *rem = &k->mem_blocks[best + 1];
        rem->id        = best + 1;
        rem->start     = blk->start + size_kb;
        rem->size      = blk->size - size_kb;
        rem->free      = 1;
        rem->owner_pid = -1;

        blk->size = size_kb;
        k->mem_block_count++;

        /* Fix ids after shift */
        for (int i = 0; i < k->mem_block_count; i++)
            k->mem_blocks[i].id = i;
    }

    /* Mark allocated */
    blk->free      = 0;
    blk->owner_pid = pid;

    /* Update PCB */
    pthread_mutex_lock(&k->proc_lock);
    int pi = find_proc_index(k, pid);
    if (pi >= 0)
        k->procs[pi].mem_allocated += size_kb;
    pthread_mutex_unlock(&k->proc_lock);

    pthread_mutex_unlock(&k->mem_lock);

    /* Simulate page load: generate a disk I/O request */
    kernel_io_request(k, rand() % OS_DISK_SIZE);

    kernel_log(k, "mem_alloc: P%d got %d KB at addr %d", pid, size_kb, addr);
    return addr;
}

/* ---------------------------------------------------------------
 *  kernel_mem_free -- free a block and coalesce neighbours
 * --------------------------------------------------------------- */
int kernel_mem_free(Kernel *k, int pid, int start_addr) {
    pthread_mutex_lock(&k->mem_lock);

    int idx = -1;
    for (int i = 0; i < k->mem_block_count; i++) {
        if (!k->mem_blocks[i].free &&
            k->mem_blocks[i].start == start_addr &&
            k->mem_blocks[i].owner_pid == pid) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        pthread_mutex_unlock(&k->mem_lock);
        kernel_log(k, "mem_free FAILED: addr %d not owned by P%d", start_addr, pid);
        return -1;
    }

    int freed_size = k->mem_blocks[idx].size;
    k->mem_blocks[idx].free      = 1;
    k->mem_blocks[idx].owner_pid = -1;

    /* Coalesce with next block */
    if (idx + 1 < k->mem_block_count && k->mem_blocks[idx + 1].free) {
        k->mem_blocks[idx].size += k->mem_blocks[idx + 1].size;
        /* Remove block idx+1 */
        for (int i = idx + 1; i < k->mem_block_count - 1; i++)
            k->mem_blocks[i] = k->mem_blocks[i + 1];
        k->mem_block_count--;
    }

    /* Coalesce with previous block */
    if (idx > 0 && k->mem_blocks[idx - 1].free) {
        k->mem_blocks[idx - 1].size += k->mem_blocks[idx].size;
        /* Remove block idx */
        for (int i = idx; i < k->mem_block_count - 1; i++)
            k->mem_blocks[i] = k->mem_blocks[i + 1];
        k->mem_block_count--;
    }

    /* Fix ids */
    for (int i = 0; i < k->mem_block_count; i++)
        k->mem_blocks[i].id = i;

    /* Update PCB */
    pthread_mutex_lock(&k->proc_lock);
    int pi = find_proc_index(k, pid);
    if (pi >= 0) {
        k->procs[pi].mem_allocated -= freed_size;
        if (k->procs[pi].mem_allocated < 0)
            k->procs[pi].mem_allocated = 0;
    }
    pthread_mutex_unlock(&k->proc_lock);

    pthread_mutex_unlock(&k->mem_lock);

    kernel_log(k, "mem_free: P%d freed %d KB at addr %d", pid, freed_size, start_addr);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_mem_free_all -- release every block owned by pid
 * --------------------------------------------------------------- */
void kernel_mem_free_all(Kernel *k, int pid) {
    pthread_mutex_lock(&k->mem_lock);

    for (int i = 0; i < k->mem_block_count; i++) {
        if (!k->mem_blocks[i].free && k->mem_blocks[i].owner_pid == pid) {
            k->mem_blocks[i].free      = 1;
            k->mem_blocks[i].owner_pid = -1;
        }
    }

    /* Coalesce all adjacent free blocks */
    for (int i = 0; i < k->mem_block_count - 1; ) {
        if (k->mem_blocks[i].free && k->mem_blocks[i + 1].free) {
            k->mem_blocks[i].size += k->mem_blocks[i + 1].size;
            for (int j = i + 1; j < k->mem_block_count - 1; j++)
                k->mem_blocks[j] = k->mem_blocks[j + 1];
            k->mem_block_count--;
            /* Don't advance i -- check the new neighbour */
        } else {
            i++;
        }
    }

    /* Fix ids */
    for (int i = 0; i < k->mem_block_count; i++)
        k->mem_blocks[i].id = i;

    /* Update PCB */
    pthread_mutex_lock(&k->proc_lock);
    int pi = find_proc_index(k, pid);
    if (pi >= 0)
        k->procs[pi].mem_allocated = 0;
    pthread_mutex_unlock(&k->proc_lock);

    pthread_mutex_unlock(&k->mem_lock);

    kernel_log(k, "mem_free_all: freed all blocks for P%d", pid);
}

/* ---------------------------------------------------------------
 *  kernel_mem_stats
 * --------------------------------------------------------------- */
void kernel_mem_stats(Kernel *k, MemStats *stats) {
    pthread_mutex_lock(&k->mem_lock);

    int total    = 0;
    int used     = 0;
    int free_kb  = 0;
    int blocks   = k->mem_block_count;
    int free_blk = 0;
    int largest_free = 0;

    for (int i = 0; i < k->mem_block_count; i++) {
        total += k->mem_blocks[i].size;
        if (k->mem_blocks[i].free) {
            free_kb += k->mem_blocks[i].size;
            free_blk++;
            if (k->mem_blocks[i].size > largest_free)
                largest_free = k->mem_blocks[i].size;
        } else {
            used += k->mem_blocks[i].size;
        }
    }

    stats->total_kb      = total;
    stats->used_kb       = used;
    stats->free_kb       = free_kb;
    stats->block_count   = blocks;
    stats->free_blocks   = free_blk;
    stats->strategy      = k->mem_strategy;

    if (free_kb > 0)
        stats->fragmentation = 1.0f - (float)largest_free / (float)free_kb;
    else
        stats->fragmentation = 0.0f;

    pthread_mutex_unlock(&k->mem_lock);
}

/* ---------------------------------------------------------------
 *  kernel_set_mem_strategy
 * --------------------------------------------------------------- */
void kernel_set_mem_strategy(Kernel *k, MemStrategy s) {
    pthread_mutex_lock(&k->mem_lock);
    k->mem_strategy = s;
    pthread_mutex_unlock(&k->mem_lock);

    static const char *names[] = {"FIRST_FIT","BEST_FIT","WORST_FIT","NEXT_FIT"};
    kernel_log(k, "Memory strategy set to %s", names[s]);
}

/* ================================================================
 *  SECTION 4 -- CACHE (Page cache with replacement)
 * ================================================================ */

/* ---------------------------------------------------------------
 *  kernel_cache_access -- access a page; returns 1=hit, 0=miss
 * --------------------------------------------------------------- */
int kernel_cache_access(Kernel *k, int page_id) {
    pthread_mutex_lock(&k->mem_lock);

    /* Search for a hit */
    for (int i = 0; i < OS_CACHE_SIZE; i++) {
        if (k->cache[i].page_id == page_id) {
            /* HIT */
            k->cache_hits++;
            k->cache[i].last_access = k->cache_tick++;
            k->cache[i].ref_bit     = 1;
            pthread_mutex_unlock(&k->mem_lock);
            return 1;
        }
    }

    /* MISS */
    k->cache_misses++;

    int victim = -1;

    switch (k->cache_algo) {

    case CACHE_LRU: {
        int min_access = __INT_MAX__;
        for (int i = 0; i < OS_CACHE_SIZE; i++) {
            if (k->cache[i].page_id == -1) {
                victim = i;
                break;
            }
            if (k->cache[i].last_access < min_access) {
                min_access = k->cache[i].last_access;
                victim = i;
            }
        }
        break;
    }

    case CACHE_FIFO: {
        int min_load = __INT_MAX__;
        for (int i = 0; i < OS_CACHE_SIZE; i++) {
            if (k->cache[i].page_id == -1) {
                victim = i;
                break;
            }
            if (k->cache[i].load_time < min_load) {
                min_load = k->cache[i].load_time;
                victim = i;
            }
        }
        break;
    }

    case CACHE_CLOCK: {
        /* Clock sweep: look for ref_bit==0, clearing ref_bits as we go */
        for (int pass = 0; pass < 2 * OS_CACHE_SIZE; pass++) {
            int i = k->cache_clock_hand;
            if (k->cache[i].page_id == -1) {
                victim = i;
                k->cache_clock_hand = (i + 1) % OS_CACHE_SIZE;
                break;
            }
            if (k->cache[i].ref_bit == 0) {
                victim = i;
                k->cache_clock_hand = (i + 1) % OS_CACHE_SIZE;
                break;
            }
            /* Give second chance */
            k->cache[i].ref_bit = 0;
            k->cache_clock_hand = (i + 1) % OS_CACHE_SIZE;
        }
        /* Fallback: if somehow we didn't find one, evict at hand */
        if (victim < 0) {
            victim = k->cache_clock_hand;
            k->cache_clock_hand = (victim + 1) % OS_CACHE_SIZE;
        }
        break;
    }
    } /* end switch */

    if (victim < 0) victim = 0;

    /* Load the new page into the victim slot */
    k->cache[victim].page_id     = page_id;
    k->cache[victim].ref_bit     = 1;
    k->cache[victim].last_access = k->cache_tick++;
    k->cache[victim].load_time   = k->cache_tick;

    pthread_mutex_unlock(&k->mem_lock);

    kernel_log(k, "Cache MISS: loaded page %d into slot %d", page_id, victim);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_cache_stats
 * --------------------------------------------------------------- */
void kernel_cache_stats(Kernel *k, CacheStats *stats) {
    pthread_mutex_lock(&k->mem_lock);

    int used = 0;
    for (int i = 0; i < OS_CACHE_SIZE; i++) {
        if (k->cache[i].page_id != -1)
            used++;
    }

    stats->cache_size   = OS_CACHE_SIZE;
    stats->entries_used = used;
    stats->hits         = k->cache_hits;
    stats->misses       = k->cache_misses;
    stats->algo         = k->cache_algo;

    int total = k->cache_hits + k->cache_misses;
    if (total > 0)
        stats->hit_ratio = (float)k->cache_hits / (float)total;
    else
        stats->hit_ratio = 0.0f;

    pthread_mutex_unlock(&k->mem_lock);
}

/* ---------------------------------------------------------------
 *  kernel_set_cache_algo
 * --------------------------------------------------------------- */
void kernel_set_cache_algo(Kernel *k, CacheAlgo algo) {
    pthread_mutex_lock(&k->mem_lock);
    k->cache_algo = algo;
    pthread_mutex_unlock(&k->mem_lock);

    static const char *names[] = {"LRU", "FIFO", "CLOCK"};
    kernel_log(k, "Cache algorithm set to %s", names[algo]);
}

/* ================================================================
 *  SECTION 5 -- FILE SYSTEM
 * ================================================================ */

/* ---------------------------------------------------------------
 *  kernel_fs_create -- create a file or directory node
 * --------------------------------------------------------------- */
int kernel_fs_create(Kernel *k, const char *name, int is_dir, int parent) {
    pthread_mutex_lock(&k->fs_lock);

    /* Validate parent */
    if (parent >= 0 && parent < OS_MAX_FILES) {
        if (!k->fs_nodes[parent].active || !k->fs_nodes[parent].is_dir) {
            pthread_mutex_unlock(&k->fs_lock);
            kernel_log(k, "fs_create FAILED: parent %d invalid", parent);
            return -1;
        }
    }

    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < OS_MAX_FILES; i++) {
        if (!k->fs_nodes[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&k->fs_lock);
        kernel_log(k, "fs_create FAILED: file table full");
        return -1;
    }

    FSNode *node = &k->fs_nodes[slot];
    memset(node, 0, sizeof(FSNode));
    node->id        = slot;
    strncpy(node->name, name, OS_MAX_NAME - 1);
    node->name[OS_MAX_NAME - 1] = '\0';
    node->is_dir    = is_dir;
    node->parent_id = parent;
    node->active    = 1;
    node->size      = 0;
    node->data_len  = 0;
    node->created   = time(NULL);
    node->modified  = node->created;

    k->fs_count++;

    pthread_mutex_unlock(&k->fs_lock);

    kernel_log(k, "fs_create: '%s' (id=%d, dir=%d, parent=%d)",
               name, slot, is_dir, parent);
    return slot;
}

/* ---------------------------------------------------------------
 *  kernel_fs_delete -- remove a file or empty directory
 * --------------------------------------------------------------- */
int kernel_fs_delete(Kernel *k, int file_id) {
    pthread_mutex_lock(&k->fs_lock);

    if (file_id <= 0 || file_id >= OS_MAX_FILES ||
        !k->fs_nodes[file_id].active) {
        pthread_mutex_unlock(&k->fs_lock);
        kernel_log(k, "fs_delete FAILED: id %d invalid", file_id);
        return -1;
    }

    FSNode *node = &k->fs_nodes[file_id];

    /* If it's a directory, make sure it's empty */
    if (node->is_dir) {
        for (int i = 0; i < OS_MAX_FILES; i++) {
            if (k->fs_nodes[i].active && k->fs_nodes[i].parent_id == file_id) {
                pthread_mutex_unlock(&k->fs_lock);
                kernel_log(k, "fs_delete FAILED: dir %d not empty", file_id);
                return -1;
            }
        }
    }

    char name_copy[OS_MAX_NAME];
    strncpy(name_copy, node->name, OS_MAX_NAME - 1);
    name_copy[OS_MAX_NAME - 1] = '\0';

    node->active = 0;
    k->fs_count--;

    pthread_mutex_unlock(&k->fs_lock);

    kernel_log(k, "fs_delete: '%s' (id=%d)", name_copy, file_id);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_fs_find -- find a node by name within a parent
 * --------------------------------------------------------------- */
int kernel_fs_find(Kernel *k, const char *name, int parent) {
    pthread_mutex_lock(&k->fs_lock);

    for (int i = 0; i < OS_MAX_FILES; i++) {
        if (k->fs_nodes[i].active &&
            k->fs_nodes[i].parent_id == parent &&
            strcmp(k->fs_nodes[i].name, name) == 0) {
            pthread_mutex_unlock(&k->fs_lock);
            return i;
        }
    }

    pthread_mutex_unlock(&k->fs_lock);
    return -1;
}

/* ---------------------------------------------------------------
 *  kernel_fs_open -- open a file descriptor
 * --------------------------------------------------------------- */
int kernel_fs_open(Kernel *k, int file_id, int mode, int pid) {
    pthread_mutex_lock(&k->fs_lock);

    if (file_id < 0 || file_id >= OS_MAX_FILES ||
        !k->fs_nodes[file_id].active) {
        pthread_mutex_unlock(&k->fs_lock);
        kernel_log(k, "fs_open FAILED: file %d invalid", file_id);
        return -1;
    }

    /* Find empty fd slot */
    int slot = -1;
    for (int i = 0; i < OS_MAX_OPEN_FDS; i++) {
        if (!k->fd_table[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&k->fs_lock);
        kernel_log(k, "fs_open FAILED: fd table full");
        return -1;
    }

    FileDescriptor *fdp = &k->fd_table[slot];
    fdp->fd        = k->next_fd++;
    fdp->file_id   = file_id;
    fdp->mode      = mode;
    fdp->offset    = 0;
    fdp->owner_pid = pid;
    fdp->active    = 1;
    k->fd_count++;

    int fd_num = fdp->fd;

    pthread_mutex_unlock(&k->fs_lock);

    kernel_log(k, "fs_open: fd=%d file=%d mode=%d pid=%d",
               fd_num, file_id, mode, pid);
    return fd_num;
}

/* ---------------------------------------------------------------
 *  kernel_fs_close -- close a file descriptor
 * --------------------------------------------------------------- */
int kernel_fs_close(Kernel *k, int fd) {
    pthread_mutex_lock(&k->fs_lock);

    for (int i = 0; i < OS_MAX_OPEN_FDS; i++) {
        if (k->fd_table[i].active && k->fd_table[i].fd == fd) {
            k->fd_table[i].active = 0;
            k->fd_count--;
            pthread_mutex_unlock(&k->fs_lock);
            kernel_log(k, "fs_close: fd=%d", fd);
            return 0;
        }
    }

    pthread_mutex_unlock(&k->fs_lock);
    kernel_log(k, "fs_close FAILED: fd=%d not found", fd);
    return -1;
}

/* ---------------------------------------------------------------
 *  kernel_fs_read -- read from a file via its descriptor
 * --------------------------------------------------------------- */
int kernel_fs_read(Kernel *k, int fd, char *buf, int size) {
    pthread_mutex_lock(&k->fs_lock);

    /* Find fd entry */
    FileDescriptor *fdp = NULL;
    for (int i = 0; i < OS_MAX_OPEN_FDS; i++) {
        if (k->fd_table[i].active && k->fd_table[i].fd == fd) {
            fdp = &k->fd_table[i];
            break;
        }
    }
    if (!fdp) {
        pthread_mutex_unlock(&k->fs_lock);
        return -1;
    }

    /* Check read permission */
    if (!(fdp->mode & 1)) {
        pthread_mutex_unlock(&k->fs_lock);
        return -1;
    }

    FSNode *node = &k->fs_nodes[fdp->file_id];
    int avail = node->data_len - fdp->offset;
    if (avail <= 0) {
        pthread_mutex_unlock(&k->fs_lock);
        return 0;
    }

    int to_read = (size < avail) ? size : avail;
    memcpy(buf, node->data + fdp->offset, to_read);
    fdp->offset += to_read;

    int file_id = fdp->file_id;

    pthread_mutex_unlock(&k->fs_lock);

    /* Simulate cache access for the file's page */
    kernel_cache_access(k, file_id);

    return to_read;
}

/* ---------------------------------------------------------------
 *  kernel_fs_write -- write / append to a file via its descriptor
 * --------------------------------------------------------------- */
int kernel_fs_write(Kernel *k, int fd, const char *buf, int size) {
    pthread_mutex_lock(&k->fs_lock);

    /* Find fd entry */
    FileDescriptor *fdp = NULL;
    for (int i = 0; i < OS_MAX_OPEN_FDS; i++) {
        if (k->fd_table[i].active && k->fd_table[i].fd == fd) {
            fdp = &k->fd_table[i];
            break;
        }
    }
    if (!fdp) {
        pthread_mutex_unlock(&k->fs_lock);
        return -1;
    }

    /* Check write permission */
    if (!(fdp->mode & 2)) {
        pthread_mutex_unlock(&k->fs_lock);
        return -1;
    }

    FSNode *node = &k->fs_nodes[fdp->file_id];
    int space = OS_IO_BUF_SIZE - node->data_len;
    int to_write = (size < space) ? size : space;

    if (to_write <= 0) {
        pthread_mutex_unlock(&k->fs_lock);
        return 0;
    }

    memcpy(node->data + node->data_len, buf, to_write);
    node->data_len += to_write;
    node->size      = node->data_len;
    fdp->offset     = node->data_len;
    node->modified  = time(NULL);

    pthread_mutex_unlock(&k->fs_lock);

    /* Generate I/O request for disk write */
    kernel_io_request(k, rand() % OS_DISK_SIZE);

    return to_write;
}

/* ---------------------------------------------------------------
 *  kernel_fs_list -- list children of a directory
 * --------------------------------------------------------------- */
int kernel_fs_list(Kernel *k, int dir_id, int *out_ids, int max) {
    pthread_mutex_lock(&k->fs_lock);

    int count = 0;
    for (int i = 0; i < OS_MAX_FILES && count < max; i++) {
        if (k->fs_nodes[i].active && k->fs_nodes[i].parent_id == dir_id) {
            out_ids[count++] = i;
        }
    }

    pthread_mutex_unlock(&k->fs_lock);
    return count;
}

/* ================================================================
 *  SECTION 6 -- DEADLOCK MANAGEMENT
 * ================================================================ */

/* ---------------------------------------------------------------
 *  kernel_banker_safe -- Banker's safety algorithm
 *  Returns 1 if the current state is safe, 0 if unsafe.
 * --------------------------------------------------------------- */
int kernel_banker_safe(Kernel *k) {
    /* NOTE: caller must hold dl_lock */
    DeadlockMgr *dm = &k->deadlock;

    int work[OS_MAX_RESOURCES];
    int finish[OS_MAX_PROCESSES];

    for (int r = 0; r < dm->n_res; r++)
        work[r] = dm->available[r];
    for (int i = 0; i < OS_MAX_PROCESSES; i++)
        finish[i] = 0;

    /* Only consider active processes */
    int active_count = 0;
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (k->procs[i].active)
            active_count++;
        else
            finish[i] = 1; /* Inactive slots are trivially finished */
    }

    int found = 1;
    while (found) {
        found = 0;
        for (int i = 0; i < OS_MAX_PROCESSES; i++) {
            if (finish[i]) continue;

            /* Check if Need[i] <= Work */
            int can_run = 1;
            for (int r = 0; r < dm->n_res; r++) {
                if (dm->need[i][r] > work[r]) {
                    can_run = 0;
                    break;
                }
            }
            if (can_run) {
                for (int r = 0; r < dm->n_res; r++)
                    work[r] += dm->allocation[i][r];
                finish[i] = 1;
                found = 1;
            }
        }
    }

    /* Check if all finished */
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (!finish[i])
            return 0; /* unsafe */
    }
    return 1; /* safe */
}

/* ---------------------------------------------------------------
 *  kernel_resource_request -- request resources (with Banker's)
 * --------------------------------------------------------------- */
int kernel_resource_request(Kernel *k, int pid, int res_id, int count) {
    if (res_id < 0 || res_id >= OS_MAX_RESOURCES || count <= 0)
        return -1;

    pthread_mutex_lock(&k->dl_lock);

    int pi = find_proc_index(k, pid);
    if (pi < 0) {
        pthread_mutex_unlock(&k->dl_lock);
        kernel_log(k, "resource_request FAILED: P%d not found", pid);
        return -1;
    }

    DeadlockMgr *dm = &k->deadlock;

    /* Prevention: resource ordering -- only allow if res_id > all held */
    if (dm->prevention_on) {
        for (int r = res_id; r < dm->n_res; r++) {
            /* Allow res_id itself; deny if holding anything with id > res_id
             * Actually: only allow if res_id > all currently held resource ids */
        }
        /* Check: process must not hold any resource with id >= res_id */
        for (int r = res_id + 1; r < dm->n_res; r++) {
            if (dm->allocation[pi][r] > 0) {
                pthread_mutex_unlock(&k->dl_lock);
                kernel_log(k, "resource_request DENIED (ordering): P%d res=%d",
                           pid, res_id);
                return -1;
            }
        }
    }

    /* Check: request <= need */
    if (count > dm->need[pi][res_id]) {
        pthread_mutex_unlock(&k->dl_lock);
        kernel_log(k, "resource_request DENIED: P%d wants %d of R%d (need=%d)",
                   pid, count, res_id, dm->need[pi][res_id]);
        return -1;
    }

    /* Check: request <= available */
    if (count > dm->available[res_id]) {
        pthread_mutex_unlock(&k->dl_lock);
        kernel_log(k, "resource_request DENIED: P%d wants %d of R%d (avail=%d)",
                   pid, count, res_id, dm->available[res_id]);
        return -1;
    }

    /* Tentatively allocate */
    dm->available[res_id]      -= count;
    dm->allocation[pi][res_id] += count;
    dm->need[pi][res_id]       -= count;

    /* Banker's safety check */
    if (!kernel_banker_safe(k)) {
        /* Rollback */
        dm->available[res_id]      += count;
        dm->allocation[pi][res_id] -= count;
        dm->need[pi][res_id]       += count;

        pthread_mutex_unlock(&k->dl_lock);
        kernel_log(k, "resource_request DENIED (unsafe): P%d res=%d cnt=%d",
                   pid, res_id, count);
        return -1;
    }

    /* Commit: update PCB */
    k->procs[pi].resources_held[res_id] += count;

    pthread_mutex_unlock(&k->dl_lock);

    kernel_log(k, "resource_request OK: P%d got %d of R%d", pid, count, res_id);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_resource_release -- release resources
 * --------------------------------------------------------------- */
int kernel_resource_release(Kernel *k, int pid, int res_id, int count) {
    if (res_id < 0 || res_id >= OS_MAX_RESOURCES || count <= 0)
        return -1;

    pthread_mutex_lock(&k->dl_lock);

    int pi = find_proc_index(k, pid);
    if (pi < 0) {
        pthread_mutex_unlock(&k->dl_lock);
        kernel_log(k, "resource_release FAILED: P%d not found", pid);
        return -1;
    }

    DeadlockMgr *dm = &k->deadlock;

    if (dm->allocation[pi][res_id] < count) {
        pthread_mutex_unlock(&k->dl_lock);
        kernel_log(k, "resource_release FAILED: P%d holds %d of R%d, tried %d",
                   pid, dm->allocation[pi][res_id], res_id, count);
        return -1;
    }

    dm->allocation[pi][res_id] -= count;
    dm->available[res_id]      += count;
    dm->need[pi][res_id]       += count;

    k->procs[pi].resources_held[res_id] -= count;

    pthread_mutex_unlock(&k->dl_lock);

    kernel_log(k, "resource_release: P%d freed %d of R%d", pid, count, res_id);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_deadlock_check -- detect deadlocked processes
 *  Returns 1 if deadlock found, 0 if safe.
 * --------------------------------------------------------------- */
int kernel_deadlock_check(Kernel *k, int *deadlocked, int *dl_count) {
    pthread_mutex_lock(&k->dl_lock);

    DeadlockMgr *dm = &k->deadlock;

    int work[OS_MAX_RESOURCES];
    int finish[OS_MAX_PROCESSES];

    for (int r = 0; r < dm->n_res; r++)
        work[r] = dm->available[r];
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (k->procs[i].active)
            finish[i] = 0;
        else
            finish[i] = 1;
    }

    /* Detection: use Allocation (not Need) for the comparison.
     * A process can finish if its current Allocation can be recovered. */
    int found = 1;
    while (found) {
        found = 0;
        for (int i = 0; i < OS_MAX_PROCESSES; i++) {
            if (finish[i]) continue;

            /* Check if Allocation[i] <= Work (process can release) */
            int can_finish = 1;
            for (int r = 0; r < dm->n_res; r++) {
                /* Use need (outstanding requests) to see if process is waiting */
                if (dm->need[i][r] > work[r]) {
                    can_finish = 0;
                    break;
                }
            }
            if (can_finish) {
                for (int r = 0; r < dm->n_res; r++)
                    work[r] += dm->allocation[i][r];
                finish[i] = 1;
                found = 1;
            }
        }
    }

    /* Collect deadlocked processes */
    *dl_count = 0;
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (!finish[i]) {
            deadlocked[*dl_count] = k->procs[i].pid;
            (*dl_count)++;
        }
    }

    int has_deadlock = (*dl_count > 0) ? 1 : 0;
    if (has_deadlock)
        dm->deadlock_count++;

    pthread_mutex_unlock(&k->dl_lock);

    if (has_deadlock)
        kernel_log(k, "Deadlock detected: %d processes", *dl_count);
    else
        kernel_log(k, "No deadlock detected");

    return has_deadlock;
}

/* ---------------------------------------------------------------
 *  kernel_set_max_need -- set max resource need for a process
 * --------------------------------------------------------------- */
void kernel_set_max_need(Kernel *k, int pid, int res_id, int max) {
    if (res_id < 0 || res_id >= OS_MAX_RESOURCES) return;

    pthread_mutex_lock(&k->dl_lock);

    int pi = find_proc_index(k, pid);
    if (pi < 0) {
        pthread_mutex_unlock(&k->dl_lock);
        return;
    }

    DeadlockMgr *dm = &k->deadlock;
    dm->max_need[pi][res_id] = max;
    dm->need[pi][res_id]     = max - dm->allocation[pi][res_id];
    if (dm->need[pi][res_id] < 0)
        dm->need[pi][res_id] = 0;

    /* Also mirror into PCB */
    k->procs[pi].resources_max[res_id] = max;

    pthread_mutex_unlock(&k->dl_lock);
}

/* ================================================================
 *  SECTION 7 -- I/O SUBSYSTEM (Disk Scheduling)
 * ================================================================ */

/* ---------------------------------------------------------------
 *  kernel_io_request -- enqueue a track request
 * --------------------------------------------------------------- */
int kernel_io_request(Kernel *k, int track) {
    pthread_mutex_lock(&k->io_lock);

    if (k->io.queue_len >= OS_MAX_DISK_QUEUE) {
        pthread_mutex_unlock(&k->io_lock);
        kernel_log(k, "io_request FAILED: queue full");
        return -1;
    }

    k->io.request_queue[k->io.queue_len] = track;
    k->io.queue_len++;

    pthread_mutex_unlock(&k->io_lock);
    return 0;
}

/* ---------------------------------------------------------------
 *  kernel_io_process -- serve one I/O request based on disk_algo
 * --------------------------------------------------------------- */
int kernel_io_process(Kernel *k) {
    pthread_mutex_lock(&k->io_lock);

    if (k->io.queue_len <= 0) {
        pthread_mutex_unlock(&k->io_lock);
        return -1;
    }

    int chosen = -1; /* index into request_queue */

    switch (k->io.algo) {

    case DISK_FCFS:
        chosen = 0;
        break;

    case DISK_SSTF: {
        int min_dist = __INT_MAX__;
        for (int i = 0; i < k->io.queue_len; i++) {
            int dist = int_abs(k->io.request_queue[i] - k->io.head_pos);
            if (dist < min_dist) {
                min_dist = dist;
                chosen = i;
            }
        }
        break;
    }

    case DISK_SCAN: {
        /* Pick the nearest request in the current direction.
         * If none in that direction, reverse and pick nearest. */
        int best_dist = __INT_MAX__;
        /* direction 1 = toward higher tracks, 0 = toward lower */
        for (int i = 0; i < k->io.queue_len; i++) {
            int track = k->io.request_queue[i];
            int diff  = track - k->io.head_pos;
            /* In current direction? */
            int in_dir = (k->io.direction == 1) ? (diff >= 0) : (diff <= 0);
            if (in_dir) {
                int dist = int_abs(diff);
                if (dist < best_dist) {
                    best_dist = dist;
                    chosen = i;
                }
            }
        }
        if (chosen < 0) {
            /* Reverse direction */
            k->io.direction = 1 - k->io.direction;
            best_dist = __INT_MAX__;
            for (int i = 0; i < k->io.queue_len; i++) {
                int track = k->io.request_queue[i];
                int diff  = track - k->io.head_pos;
                int in_dir = (k->io.direction == 1) ? (diff >= 0) : (diff <= 0);
                if (in_dir) {
                    int dist = int_abs(diff);
                    if (dist < best_dist) {
                        best_dist = dist;
                        chosen = i;
                    }
                }
            }
            /* Ultimate fallback: just pick the nearest */
            if (chosen < 0) {
                best_dist = __INT_MAX__;
                for (int i = 0; i < k->io.queue_len; i++) {
                    int dist = int_abs(k->io.request_queue[i] - k->io.head_pos);
                    if (dist < best_dist) {
                        best_dist = dist;
                        chosen = i;
                    }
                }
            }
        }
        break;
    }

    case DISK_CSCAN: {
        /* Only serve in the current direction (toward max).
         * If none ahead, wrap to track 0 side and pick nearest. */
        int best_dist = __INT_MAX__;
        for (int i = 0; i < k->io.queue_len; i++) {
            int track = k->io.request_queue[i];
            if (track >= k->io.head_pos) {
                int dist = track - k->io.head_pos;
                if (dist < best_dist) {
                    best_dist = dist;
                    chosen = i;
                }
            }
        }
        if (chosen < 0) {
            /* Jump to beginning: pick lowest track */
            int min_track = __INT_MAX__;
            for (int i = 0; i < k->io.queue_len; i++) {
                if (k->io.request_queue[i] < min_track) {
                    min_track = k->io.request_queue[i];
                    chosen = i;
                }
            }
        }
        break;
    }
    } /* end switch */

    if (chosen < 0) {
        pthread_mutex_unlock(&k->io_lock);
        return -1;
    }

    int track = k->io.request_queue[chosen];
    int seek  = int_abs(track - k->io.head_pos);

    k->io.head_pos    = track;
    k->io.total_seek += seek;
    k->io.total_ops++;

    /* Remove served request by shifting the rest down */
    for (int i = chosen; i < k->io.queue_len - 1; i++)
        k->io.request_queue[i] = k->io.request_queue[i + 1];
    k->io.queue_len--;

    pthread_mutex_unlock(&k->io_lock);

    return seek;
}

/* ---------------------------------------------------------------
 *  kernel_set_disk_algo
 * --------------------------------------------------------------- */
void kernel_set_disk_algo(Kernel *k, DiskAlgo algo) {
    pthread_mutex_lock(&k->io_lock);
    k->io.algo = algo;
    pthread_mutex_unlock(&k->io_lock);

    static const char *names[] = {"FCFS", "SSTF", "SCAN", "C-SCAN"};
    kernel_log(k, "Disk algorithm set to %s", names[algo]);
}
