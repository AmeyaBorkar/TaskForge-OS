/* ================================================================
 *  TaskForge OS -- System Call Interface
 *  This is the API that applications use to talk to the kernel.
 *  Applications NEVER touch kernel internals directly.
 * ================================================================ */
#ifndef OS_SYSCALL_H
#define OS_SYSCALL_H

#include "os_kernel.h"

/* Global kernel instance (initialized in main) */
extern Kernel g_kernel;

/* ============================================================
 *  System Call Wrappers
 *  Each maps to a kernel function with logging + validation.
 * ============================================================ */

/* -- Process syscalls -- */
static inline int sys_fork(const char *name, int priority, int burst,
                           void (*fn)(void*), void *arg) {
    kernel_log(&g_kernel, "SYSCALL: sys_fork('%s', pri=%d)", name, priority);
    return kernel_create_process(&g_kernel, name, priority, burst, fn, arg);
}

static inline int sys_kill(int pid) {
    kernel_log(&g_kernel, "SYSCALL: sys_kill(pid=%d)", pid);
    return kernel_kill_process(&g_kernel, pid);
}

static inline int sys_set_state(int pid, ProcessState st) {
    return kernel_set_state(&g_kernel, pid, st);
}

static inline int sys_schedule(void) {
    return kernel_schedule_next(&g_kernel);
}

static inline void sys_set_scheduler(SchedulerAlgo algo, int quantum) {
    const char *names[] = {"FCFS","SJF","PRIORITY","ROUND_ROBIN"};
    kernel_log(&g_kernel, "SYSCALL: sys_set_scheduler(%s, q=%d)", names[algo], quantum);
    kernel_set_scheduler(&g_kernel, algo, quantum);
}

/* -- Memory syscalls -- */
static inline int sys_alloc(int pid, int size_kb) {
    kernel_log(&g_kernel, "SYSCALL: sys_alloc(pid=%d, %d KB)", pid, size_kb);
    return kernel_mem_alloc(&g_kernel, pid, size_kb);
}

static inline int sys_free(int pid, int addr) {
    kernel_log(&g_kernel, "SYSCALL: sys_free(pid=%d, addr=%d)", pid, addr);
    return kernel_mem_free(&g_kernel, pid, addr);
}

static inline void sys_free_all(int pid) {
    kernel_mem_free_all(&g_kernel, pid);
}

static inline void sys_mem_stats(MemStats *s) {
    kernel_mem_stats(&g_kernel, s);
}

static inline void sys_set_mem_strategy(MemStrategy s) {
    const char *names[] = {"FIRST_FIT","BEST_FIT","WORST_FIT","NEXT_FIT"};
    kernel_log(&g_kernel, "SYSCALL: sys_set_mem_strategy(%s)", names[s]);
    kernel_set_mem_strategy(&g_kernel, s);
}

/* -- Cache syscalls -- */
static inline int sys_cache_access(int page_id) {
    return kernel_cache_access(&g_kernel, page_id);
}

static inline void sys_cache_stats(CacheStats *s) {
    kernel_cache_stats(&g_kernel, s);
}

static inline void sys_set_cache_algo(CacheAlgo algo) {
    const char *names[] = {"LRU","FIFO","CLOCK"};
    kernel_log(&g_kernel, "SYSCALL: sys_set_cache_algo(%s)", names[algo]);
    kernel_set_cache_algo(&g_kernel, algo);
}

/* -- File system syscalls -- */
static inline int sys_create(const char *name, int is_dir, int parent) {
    kernel_log(&g_kernel, "SYSCALL: sys_create('%s', dir=%d)", name, is_dir);
    return kernel_fs_create(&g_kernel, name, is_dir, parent);
}

static inline int sys_delete(int fid) {
    kernel_log(&g_kernel, "SYSCALL: sys_delete(fid=%d)", fid);
    return kernel_fs_delete(&g_kernel, fid);
}

static inline int sys_find(const char *name, int parent) {
    return kernel_fs_find(&g_kernel, name, parent);
}

static inline int sys_open(int file_id, int mode, int pid) {
    kernel_log(&g_kernel, "SYSCALL: sys_open(fid=%d, mode=%d, pid=%d)", file_id, mode, pid);
    return kernel_fs_open(&g_kernel, file_id, mode, pid);
}

static inline int sys_close(int fd) {
    return kernel_fs_close(&g_kernel, fd);
}

static inline int sys_read(int fd, char *buf, int size) {
    g_kernel.io.buf_reads++;
    return kernel_fs_read(&g_kernel, fd, buf, size);
}

static inline int sys_write(int fd, const char *buf, int size) {
    g_kernel.io.buf_writes++;
    return kernel_fs_write(&g_kernel, fd, buf, size);
}

static inline int sys_ls(int dir_id, int *out_ids, int max) {
    return kernel_fs_list(&g_kernel, dir_id, out_ids, max);
}

/* -- Deadlock / Resource syscalls -- */
static inline int sys_lock(int pid, int res_id, int count) {
    kernel_log(&g_kernel, "SYSCALL: sys_lock(pid=%d, res=%d, cnt=%d)", pid, res_id, count);
    return kernel_resource_request(&g_kernel, pid, res_id, count);
}

static inline int sys_unlock(int pid, int res_id, int count) {
    kernel_log(&g_kernel, "SYSCALL: sys_unlock(pid=%d, res=%d, cnt=%d)", pid, res_id, count);
    return kernel_resource_release(&g_kernel, pid, res_id, count);
}

static inline int sys_deadlock_check(int *dl_procs, int *dl_count) {
    kernel_log(&g_kernel, "SYSCALL: sys_deadlock_check()");
    return kernel_deadlock_check(&g_kernel, dl_procs, dl_count);
}

static inline int sys_is_safe(void) {
    return kernel_banker_safe(&g_kernel);
}

static inline void sys_set_max_need(int pid, int res_id, int max) {
    kernel_set_max_need(&g_kernel, pid, res_id, max);
}

/* -- I/O syscalls -- */
static inline int sys_io_request(int track) {
    return kernel_io_request(&g_kernel, track);
}

static inline int sys_io_process(void) {
    return kernel_io_process(&g_kernel);
}

static inline void sys_set_disk_algo(DiskAlgo algo) {
    const char *names[] = {"FCFS","SSTF","SCAN","C-SCAN"};
    kernel_log(&g_kernel, "SYSCALL: sys_set_disk_algo(%s)", names[algo]);
    kernel_set_disk_algo(&g_kernel, algo);
}

/* -- Kernel info -- */
static inline void sys_kernel_log(const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kernel_log(&g_kernel, "%s", buf);
}

#endif /* OS_SYSCALL_H */
