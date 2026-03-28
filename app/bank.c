/* ================================================================
 *  TaskForge Banking Application
 *  ---------------------------------------------------------------
 *  A complete banking system running ON TOP of the TaskForge OS
 *  kernel.  Every operation uses OS system calls: process creation,
 *  memory allocation, filesystem I/O, deadlock-safe locking, cache
 *  access, and disk scheduling.
 *
 *  The detailed OS trace printed for each banking operation shows
 *  the kernel working under the hood.
 * ================================================================ */

#include "bank.h"
#include <stdarg.h>

/* ============================================================
 *  Input Helpers
 * ============================================================ */
static void flush_in(void) { int c; while ((c = getchar()) != '\n' && c != EOF); }

static int get_int_input(const char *prompt, int lo, int hi) {
    int v;
    while (1) {
        printf("  %s", prompt);
        if (scanf("%d", &v) == 1 && v >= lo && v <= hi) { flush_in(); return v; }
        flush_in();
        printf(BRED "  Invalid! Enter %d-%d.\n" RESET, lo, hi);
    }
}

static double get_double_input(const char *prompt) {
    double v;
    while (1) {
        printf("  %s", prompt);
        if (scanf("%lf", &v) == 1 && v > 0) { flush_in(); return v; }
        flush_in();
        printf(BRED "  Invalid! Enter a positive amount.\n" RESET);
    }
}

static void get_string_input(const char *prompt, char *buf, int max) {
    printf("  %s", prompt);
    if (fgets(buf, max, stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    }
}

/* ============================================================
 *  Name Lookup Tables
 * ============================================================ */
static const char *MEM_NAMES[]   = {"FIRST_FIT","BEST_FIT","WORST_FIT","NEXT_FIT"};
static const char *CACHE_NAMES[] = {"LRU","FIFO","CLOCK"};
static const char *DISK_NAMES[]  = {"FCFS","SSTF","SCAN","C-SCAN"};
static const char *SCHED_NAMES[] = {"FCFS","SJF","PRIORITY","ROUND_ROBIN"};

/* ============================================================
 *  Trace + Display Helpers
 * ============================================================ */
static void os_trace(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf(DIM "  [OS] "); vprintf(fmt, ap); printf(RESET "\n");
    va_end(ap);
}

static void bank_trace(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf(BCYAN "  [BANK] "); vprintf(fmt, ap); printf(RESET "\n");
    va_end(ap);
}

static void print_separator(void) {
    printf(BCYAN);
    for (int i = 0; i < 62; i++) putchar('=');
    printf(RESET "\n");
}

static void print_thin_sep(void) {
    printf(DIM);
    for (int i = 0; i < 62; i++) putchar('-');
    printf(RESET "\n");
}

/* ============================================================
 *  Internal Helpers
 * ============================================================ */
static Account *find_account(BankState *bs, int acc_id) {
    for (int i = 0; i < bs->acc_count; i++)
        if (bs->accounts[i].id == acc_id && bs->accounts[i].active)
            return &bs->accounts[i];
    return NULL;
}

static void log_tx(BankState *bs, int from, int to, double amount,
                   const char *type, int pid, int mem) {
    if (bs->tx_count >= MAX_TRANSACTIONS) return;
    Transaction *t = &bs->txlog[bs->tx_count];
    t->id = bs->tx_count + 1;  t->from_acc = from;  t->to_acc = to;
    t->amount = amount;  t->timestamp = time(NULL);
    t->pid = pid;  t->mem_addr = mem;
    snprintf(t->type, sizeof(t->type), "%s", type);
    bs->tx_count++;
}

/* Spawn a process, move it to RUNNING, and trace all transitions */
static int proc_start(const char *name, int pri, int burst) {
    int pid = sys_fork(name, pri, burst, NULL, NULL);
    os_trace("Process P%d '%s' created (Priority: %d)", pid, name, pri);
    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    os_trace("P%d state: NEW -> READY -> RUNNING", pid);
    return pid;
}

/* Allocate memory for a process and trace it */
static int proc_mem_alloc(int pid, int kb) {
    int mem = sys_alloc(pid, kb);
    MemStats ms; sys_mem_stats(&ms);
    os_trace("Memory allocated: %d KB at address %d (%s)", kb, mem,
             MEM_NAMES[ms.strategy]);
    return mem;
}

/* Free memory, terminate process, trace it */
static void proc_cleanup(int pid) {
    sys_free_all(pid);
    os_trace("Memory freed for P%d", pid);
    sys_set_state(pid, PROC_TERMINATED);
    os_trace("P%d state: RUNNING -> TERMINATED", pid);
}

/* Write account record to its file and trace */
static void write_account_file(Account *a, int pid) {
    int fd = sys_open(a->file_id, 2, pid);
    os_trace("File opened (fd=%d, mode=WRITE)", fd);
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%d|%s|%.2f", a->id, a->holder,
                       a->balance);
    sys_write(fd, buf, len);
    os_trace("%d bytes written to fd=%d", len, fd);
    sys_close(fd);
    os_trace("File closed (fd=%d)", fd);
}

/* Read account file (for trace purposes) */
static void read_account_file(Account *a, int pid) {
    int fd = sys_open(a->file_id, 1, pid);
    os_trace("File opened (fd=%d, mode=READ)", fd);
    char buf[256]; memset(buf, 0, sizeof(buf));
    int rb = sys_read(fd, buf, (int)sizeof(buf) - 1);
    os_trace("%d bytes read from fd=%d", rb, fd);
    sys_close(fd);
    os_trace("File closed (fd=%d)", fd);
}

/* Simulate disk I/O for an account and trace */
static void disk_io_for_account(int acc_id) {
    int track = (acc_id * 10) % OS_DISK_SIZE;
    sys_io_request(track);
    os_trace("I/O request: disk track %d", track);
    sys_io_process();
    os_trace("I/O processed (disk head moved)");
}

/* ============================================================
 *  bank_init
 * ============================================================ */
void bank_init(BankState *bs)
{
    memset(bs, 0, sizeof(*bs));
    bs->accounts_dir = sys_create("accounts", 1, 0);
    bs->logs_dir     = sys_create("logs",     1, 0);
    os_trace("Directory '/accounts' created (id=%d)", bs->accounts_dir);
    os_trace("Directory '/logs' created (id=%d)", bs->logs_dir);
    bank_trace("Banking system initialized");
}

/* ============================================================
 *  bank_create_account
 * ============================================================ */
int bank_create_account(BankState *bs, const char *holder, double initial)
{
    if (bs->acc_count >= MAX_ACCOUNTS) {
        printf(BRED "  [BANK] ERROR: Maximum accounts (%d) reached.\n" RESET,
               MAX_ACCOUNTS);
        return -1;
    }
    int acc_id = bs->acc_count + 1;
    printf("\n");
    bank_trace("Creating account for \"%s\"...", holder);

    int pid = proc_start("create_account", 3, 10);
    int mem = proc_mem_alloc(pid, 1);

    /* Create account file in /accounts */
    char fname[64];
    snprintf(fname, sizeof(fname), "acc_%03d.dat", acc_id);
    int fid = sys_create(fname, 0, bs->accounts_dir);
    os_trace("File '%s' created in /accounts (id=%d)", fname, fid);

    /* Write initial data */
    int fd = sys_open(fid, 2, pid);
    os_trace("File opened (fd=%d, mode=WRITE)", fd);
    char data[256];
    int dlen = snprintf(data, sizeof(data), "%d|%s|%.2f", acc_id, holder,
                        initial);
    sys_write(fd, data, dlen);
    os_trace("%d bytes written to fd=%d", dlen, fd);
    sys_close(fd);
    os_trace("File closed (fd=%d)", fd);

    /* Register resource for Banker's algorithm */
    sys_set_max_need(pid, acc_id, 1);
    os_trace("Resource R%d registered (max_need=1 for P%d)", acc_id, pid);

    /* Cache the account page */
    int ch = sys_cache_access(acc_id);
    os_trace("Cache: page %d loaded (%s)", acc_id, ch ? "HIT" : "MISS");

    /* Store in BankState */
    Account *a = &bs->accounts[bs->acc_count];
    a->id = acc_id;
    snprintf(a->holder, OS_MAX_NAME, "%s", holder);
    a->balance = initial;  a->active = 1;
    a->resource_id = acc_id;  a->file_id = fid;
    bs->acc_count++;

    if (initial > 0.0) log_tx(bs, 0, acc_id, initial, "DEPOSIT", pid, mem);

    proc_cleanup(pid);
    printf(BGREEN "  [BANK] Account #%d created for \"%s\" with balance $%.2f\n"
           RESET, acc_id, holder, initial);
    return acc_id;
}

/* ============================================================
 *  bank_deposit
 * ============================================================ */
int bank_deposit(BankState *bs, int acc_id, double amount)
{
    Account *a = find_account(bs, acc_id);
    if (!a) { printf(BRED "  [BANK] ERROR: Account #%d not found.\n" RESET, acc_id); return -1; }

    printf("\n");
    bank_trace("Depositing $%.2f into Account #%d (%s)...", amount, acc_id, a->holder);

    int pid = proc_start("deposit", 4, 5);
    int mem = proc_mem_alloc(pid, 1);

    /* Lock account (Banker's algorithm) */
    sys_set_max_need(pid, acc_id, 1);
    os_trace("Requesting lock on Account #%d (resource R%d)...", acc_id, acc_id);
    int r = sys_lock(pid, acc_id, 1);
    if (r < 0) {
        printf(BRED "  [OS] DENIED by Banker's algorithm - unsafe state!\n" RESET);
        proc_cleanup(pid);
        return -1;
    }
    os_trace("Lock GRANTED on R%d (Banker's check: SAFE)", acc_id);

    int ch = sys_cache_access(acc_id);
    os_trace("Cache: page %d accessed (%s)", acc_id, ch ? "HIT" : "MISS");

    read_account_file(a, pid);

    double old = a->balance;
    a->balance += amount;
    os_trace("Balance updated: $%.2f -> $%.2f (+$%.2f)", old, a->balance, amount);

    write_account_file(a, pid);
    disk_io_for_account(acc_id);

    sys_unlock(pid, acc_id, 1);
    os_trace("Lock RELEASED on R%d", acc_id);

    log_tx(bs, 0, acc_id, amount, "DEPOSIT", pid, mem);
    proc_cleanup(pid);

    printf(BGREEN "  [BANK] Deposited $%.2f into Account #%d. New balance: $%.2f\n"
           RESET, amount, acc_id, a->balance);
    return 0;
}

/* ============================================================
 *  bank_withdraw
 * ============================================================ */
int bank_withdraw(BankState *bs, int acc_id, double amount)
{
    Account *a = find_account(bs, acc_id);
    if (!a) { printf(BRED "  [BANK] ERROR: Account #%d not found.\n" RESET, acc_id); return -1; }

    printf("\n");
    bank_trace("Withdrawing $%.2f from Account #%d (%s)...", amount, acc_id, a->holder);

    int pid = proc_start("withdraw", 4, 5);
    int mem = proc_mem_alloc(pid, 1);

    /* Lock account */
    sys_set_max_need(pid, acc_id, 1);
    os_trace("Requesting lock on Account #%d (resource R%d)...", acc_id, acc_id);
    int r = sys_lock(pid, acc_id, 1);
    if (r < 0) {
        printf(BRED "  [OS] DENIED by Banker's algorithm - unsafe state!\n" RESET);
        proc_cleanup(pid);
        return -1;
    }
    os_trace("Lock GRANTED on R%d (Banker's check: SAFE)", acc_id);

    int ch = sys_cache_access(acc_id);
    os_trace("Cache: page %d accessed (%s)", acc_id, ch ? "HIT" : "MISS");

    read_account_file(a, pid);

    /* Check sufficient balance */
    if (a->balance < amount) {
        printf(BRED "  [BANK] ERROR: Insufficient balance! Available: $%.2f, "
               "Requested: $%.2f\n" RESET, a->balance, amount);
        sys_unlock(pid, acc_id, 1);
        os_trace("Lock RELEASED on R%d (operation aborted)", acc_id);
        proc_cleanup(pid);
        return -1;
    }

    double old = a->balance;
    a->balance -= amount;
    os_trace("Balance updated: $%.2f -> $%.2f (-$%.2f)", old, a->balance, amount);

    write_account_file(a, pid);
    disk_io_for_account(acc_id);

    sys_unlock(pid, acc_id, 1);
    os_trace("Lock RELEASED on R%d", acc_id);

    log_tx(bs, acc_id, 0, amount, "WITHDRAW", pid, mem);
    proc_cleanup(pid);

    printf(BGREEN "  [BANK] Withdrew $%.2f from Account #%d. New balance: $%.2f\n"
           RESET, amount, acc_id, a->balance);
    return 0;
}

/* ============================================================
 *  bank_transfer -- deadlock-safe two-account operation
 *  Locks lower-numbered account first (resource ordering).
 * ============================================================ */
int bank_transfer(BankState *bs, int from_id, int to_id, double amount)
{
    Account *src = find_account(bs, from_id);
    Account *dst = find_account(bs, to_id);
    if (!src) { printf(BRED "  [BANK] ERROR: Source account #%d not found.\n" RESET, from_id); return -1; }
    if (!dst) { printf(BRED "  [BANK] ERROR: Dest account #%d not found.\n" RESET, to_id); return -1; }
    if (from_id == to_id) { printf(BRED "  [BANK] ERROR: Cannot transfer to same account.\n" RESET); return -1; }

    printf("\n");
    print_thin_sep();
    bank_trace("Transferring $%.2f: Account #%d (%s) -> Account #%d (%s)",
               amount, from_id, src->holder, to_id, dst->holder);
    print_thin_sep();

    int pid = proc_start("transfer", 2, 15);
    int mem = proc_mem_alloc(pid, 2);

    /* Resource ordering: lock lower ID first to prevent deadlock */
    int first  = (from_id < to_id) ? from_id : to_id;
    int second = (from_id < to_id) ? to_id   : from_id;
    os_trace("Deadlock prevention: locking in order R%d then R%d", first, second);

    sys_set_max_need(pid, first,  1);
    sys_set_max_need(pid, second, 1);

    /* Lock first (lower-numbered) account */
    printf(DIM "  [OS] Requesting lock on Account #%d...\n" RESET, first);
    int r1 = sys_lock(pid, first, 1);
    if (r1 < 0) {
        printf(BRED "  [OS] DENIED by Banker's algorithm - unsafe state!\n" RESET);
        proc_cleanup(pid);
        return -1;
    }
    os_trace("Lock GRANTED on R%d (Banker's check: SAFE)", first);

    /* Lock second (higher-numbered) account */
    printf(DIM "  [OS] Requesting lock on Account #%d...\n" RESET, second);
    int r2 = sys_lock(pid, second, 1);
    if (r2 < 0) {
        printf(BRED "  [OS] DENIED - potential deadlock detected!\n" RESET);
        sys_unlock(pid, first, 1);
        os_trace("Lock RELEASED on R%d (rollback)", first);
        proc_cleanup(pid);
        return -1;
    }
    os_trace("Lock GRANTED on R%d (Banker's check: SAFE)", second);
    os_trace("Both accounts locked -- transfer is safe to proceed");

    /* Cache access for both */
    int ch1 = sys_cache_access(from_id);
    os_trace("Cache: page %d accessed (%s)", from_id, ch1 ? "HIT" : "MISS");
    int ch2 = sys_cache_access(to_id);
    os_trace("Cache: page %d accessed (%s)", to_id, ch2 ? "HIT" : "MISS");

    read_account_file(src, pid);

    /* Check sufficient balance */
    if (src->balance < amount) {
        printf(BRED "  [BANK] ERROR: Insufficient balance in Account #%d! "
               "Available: $%.2f, Requested: $%.2f\n" RESET,
               from_id, src->balance, amount);
        sys_unlock(pid, second, 1);
        os_trace("Lock RELEASED on R%d", second);
        sys_unlock(pid, first, 1);
        os_trace("Lock RELEASED on R%d", first);
        proc_cleanup(pid);
        return -1;
    }

    /* Perform the transfer */
    double old_s = src->balance, old_d = dst->balance;
    src->balance -= amount;
    dst->balance += amount;
    os_trace("Debit:  Account #%d  $%.2f -> $%.2f", from_id, old_s, src->balance);
    os_trace("Credit: Account #%d  $%.2f -> $%.2f", to_id,   old_d, dst->balance);

    write_account_file(src, pid);
    write_account_file(dst, pid);

    /* Disk I/O for both accounts */
    int trk1 = (from_id * 10) % OS_DISK_SIZE;
    int trk2 = (to_id   * 10) % OS_DISK_SIZE;
    sys_io_request(trk1);
    os_trace("I/O request: disk track %d (source)", trk1);
    sys_io_request(trk2);
    os_trace("I/O request: disk track %d (destination)", trk2);
    sys_io_process();
    os_trace("I/O processed (disk head moved)");

    /* Unlock in reverse order */
    sys_unlock(pid, second, 1);
    os_trace("Lock RELEASED on R%d", second);
    sys_unlock(pid, first, 1);
    os_trace("Lock RELEASED on R%d", first);

    /* Log debit and credit transactions */
    log_tx(bs, from_id, to_id, amount, "TRANSFER", pid, mem);
    log_tx(bs, from_id, to_id, amount, "TRANSFER", pid, mem);

    proc_cleanup(pid);

    printf(BGREEN "  [BANK] Transfer complete: $%.2f  Account #%d -> Account #%d\n"
           RESET, amount, from_id, to_id);
    printf(BGREEN "  [BANK] Account #%d balance: $%.2f  |  Account #%d balance: $%.2f\n"
           RESET, from_id, src->balance, to_id, dst->balance);
    return 0;
}

/* ============================================================
 *  bank_get_balance
 * ============================================================ */
double bank_get_balance(BankState *bs, int acc_id)
{
    Account *a = find_account(bs, acc_id);
    if (!a) { printf(BRED "  [BANK] ERROR: Account #%d not found.\n" RESET, acc_id); return -1.0; }

    printf("\n");
    bank_trace("Checking balance for Account #%d (%s)...", acc_id, a->holder);

    int pid = proc_start("get_balance", 5, 2);

    int ch = sys_cache_access(acc_id);
    os_trace("Cache: page %d (%s)", acc_id, ch ? "HIT" : "MISS");
    if (ch) {
        os_trace("Balance served from cache (fast path)");
    } else {
        int fd = sys_open(a->file_id, 1, pid);
        os_trace("Cache MISS -- reading from file (fd=%d)", fd);
        char buf[256]; memset(buf, 0, sizeof(buf));
        sys_read(fd, buf, (int)sizeof(buf) - 1);
        sys_close(fd);
        os_trace("File read complete, fd closed");
    }

    sys_set_state(pid, PROC_TERMINATED);
    os_trace("P%d state: RUNNING -> TERMINATED", pid);

    printf(BWHITE "  +---------------------------------------+\n");
    printf("  | Account #%-4d                         |\n", a->id);
    printf("  | Holder: %-30s|\n", a->holder);
    printf("  | Balance: " BGREEN "$%-27.2f" BWHITE "|\n", a->balance);
    printf("  +---------------------------------------+\n" RESET);
    return a->balance;
}

/* ============================================================
 *  bank_print_statement
 * ============================================================ */
void bank_print_statement(BankState *bs, int acc_id)
{
    Account *a = find_account(bs, acc_id);
    if (!a) { printf(BRED "  [BANK] ERROR: Account #%d not found.\n" RESET, acc_id); return; }

    printf("\n");
    print_separator();
    printf(BWHITE "  ACCOUNT STATEMENT -- #%d (%s)\n" RESET, a->id, a->holder);
    printf(BWHITE "  Current Balance: " BGREEN "$%.2f\n" RESET, a->balance);
    print_separator();

    printf(DIM "  %-6s %-10s %-8s %-8s %-12s %-5s\n" RESET,
           "TxID", "Type", "From", "To", "Amount", "PID");
    print_thin_sep();

    int found = 0;
    for (int i = 0; i < bs->tx_count; i++) {
        Transaction *t = &bs->txlog[i];
        if (t->from_acc != acc_id && t->to_acc != acc_id) continue;

        const char *color = RESET;
        if      (strcmp(t->type, "DEPOSIT")  == 0) color = BGREEN;
        else if (strcmp(t->type, "WITHDRAW") == 0) color = BRED;
        else if (strcmp(t->type, "TRANSFER") == 0) color = BYELLOW;

        char fs[16], ts[16];
        snprintf(fs, sizeof(fs), t->from_acc ? "#%d" : "--", t->from_acc);
        snprintf(ts, sizeof(ts), t->to_acc   ? "#%d" : "--", t->to_acc);

        printf("  %s%-6d %-10s %-8s %-8s $%-11.2f P%-4d" RESET "\n",
               color, t->id, t->type, fs, ts, t->amount, t->pid);
        found++;
    }
    if (!found) printf(DIM "  (no transactions)\n" RESET);
    print_thin_sep();
    printf("  Total transactions: %d\n", found);
}

/* ============================================================
 *  bank_print_all_accounts
 * ============================================================ */
void bank_print_all_accounts(BankState *bs)
{
    printf("\n");
    print_separator();
    printf(BWHITE "  ALL ACCOUNTS (%d total)\n" RESET, bs->acc_count);
    print_separator();

    if (bs->acc_count == 0) { printf(DIM "  (no accounts)\n" RESET); return; }

    printf(DIM "  %-6s %-20s %-14s %-10s %-8s\n" RESET,
           "ID", "Holder", "Balance", "Status", "Cache");
    print_thin_sep();

    for (int i = 0; i < bs->acc_count; i++) {
        Account *a = &bs->accounts[i];
        if (!a->active) continue;
        int ch = sys_cache_access(a->id);
        printf("  %-6d %-20s " BGREEN "$%-13.2f" RESET " %-10s %s\n",
               a->id, a->holder, a->balance, "Active",
               ch ? BGREEN "HIT" RESET : BYELLOW "MISS" RESET);
    }
    print_thin_sep();
}

/* ============================================================
 *  bank_print_tx_log
 * ============================================================ */
void bank_print_tx_log(BankState *bs)
{
    printf("\n");
    print_separator();
    printf(BWHITE "  TRANSACTION LOG (%d entries)\n" RESET, bs->tx_count);
    print_separator();

    if (bs->tx_count == 0) { printf(DIM "  (no transactions)\n" RESET); return; }

    printf(DIM "  %-5s %-10s %-6s %-6s %-12s %-5s %-6s %-20s\n" RESET,
           "TxID", "Type", "From", "To", "Amount", "PID", "Mem", "Time");
    print_thin_sep();

    for (int i = 0; i < bs->tx_count; i++) {
        Transaction *t = &bs->txlog[i];
        const char *color = RESET;
        if      (strcmp(t->type, "DEPOSIT")  == 0) color = BGREEN;
        else if (strcmp(t->type, "WITHDRAW") == 0) color = BRED;
        else if (strcmp(t->type, "TRANSFER") == 0) color = BYELLOW;

        char fs[16], ts_acc[16];
        snprintf(fs, sizeof(fs), t->from_acc ? "#%d" : "--", t->from_acc);
        snprintf(ts_acc, sizeof(ts_acc), t->to_acc ? "#%d" : "--", t->to_acc);

        struct tm *ti = localtime(&t->timestamp);
        char ts[24];
        if (ti) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ti);
        else    snprintf(ts, sizeof(ts), "N/A");

        printf("  %s%-5d %-10s %-6s %-6s $%-11.2f P%-4d %-6d %s" RESET "\n",
               color, t->id, t->type, fs, ts_acc, t->amount, t->pid,
               t->mem_addr, ts);
    }
    print_thin_sep();
}

/* ============================================================
 *  OS Kernel Dashboard
 * ============================================================ */
static void bank_os_dashboard(void)
{
    printf("\n");
    print_separator();
    printf(BWHITE "  OS KERNEL STATUS\n" RESET);
    print_separator();

    /* Processes */
    int total = 0, active = 0, terminated = 0;
    for (int i = 0; i < g_kernel.proc_count; i++) {
        if (!g_kernel.procs[i].active) continue;
        total++;
        if (g_kernel.procs[i].state == PROC_TERMINATED) terminated++;
        else active++;
    }
    printf(BCYAN "  Processes:" RESET " %d created, %d active, %d terminated\n",
           total, active, terminated);
    printf(BCYAN "  Scheduler:" RESET " %s (quantum=%d)\n",
           SCHED_NAMES[g_kernel.sched_algo], g_kernel.sched_quantum);

    /* Memory */
    MemStats ms; sys_mem_stats(&ms);
    printf("\n" BCYAN "  Memory:" RESET " %d/%d KB used | Strategy: %s\n",
           ms.used_kb, ms.total_kb, MEM_NAMES[ms.strategy]);
    printf("    Blocks: %d allocated, %d free | Fragmentation: %.1f%%\n",
           ms.block_count - ms.free_blocks, ms.free_blocks, ms.fragmentation);

    /* Cache */
    CacheStats cs; sys_cache_stats(&cs);
    float ratio = (cs.hits + cs.misses > 0)
                  ? (float)cs.hits / (float)(cs.hits + cs.misses) * 100.0f
                  : 0.0f;
    printf("\n" BCYAN "  Cache:" RESET " %s | %d/%d entries | Hits: %d | "
           "Misses: %d | Ratio: %.1f%%\n",
           CACHE_NAMES[cs.algo], cs.entries_used, cs.cache_size,
           cs.hits, cs.misses, ratio);

    /* Deadlock */
    printf("\n" BCYAN "  Deadlocks:" RESET " %d detected | Prevention: %s\n",
           g_kernel.deadlock.deadlock_count,
           g_kernel.deadlock.prevention_on ? "ON" : "OFF");
    printf("    Resources locked:");
    int any = 0;
    for (int r = 0; r < OS_MAX_RESOURCES; r++) {
        int tot = 0;
        for (int p = 0; p < g_kernel.deadlock.n_proc; p++)
            tot += g_kernel.deadlock.allocation[p][r];
        if (tot > 0) { printf(" R%d(acc%d)", r, r); any = 1; }
    }
    if (!any) printf(" (none)");
    printf("\n");

    /* File System */
    int fcnt = 0, dcnt = 0, ofds = 0;
    for (int i = 0; i < g_kernel.fs_count; i++) {
        if (!g_kernel.fs_nodes[i].active) continue;
        if (g_kernel.fs_nodes[i].is_dir) dcnt++; else fcnt++;
    }
    for (int i = 0; i < g_kernel.fd_count; i++)
        if (g_kernel.fd_table[i].active) ofds++;
    printf("\n" BCYAN "  File System:" RESET " %d files, %d dirs | Open FDs: %d\n",
           fcnt, dcnt, ofds);

    /* I/O */
    printf("\n" BCYAN "  I/O:" RESET " %s | Total seek: %d | Ops: %d\n",
           DISK_NAMES[g_kernel.io.algo], g_kernel.io.total_seek,
           g_kernel.io.total_ops);
    printf("    Reads: %d | Writes: %d | Buf hits: %d\n",
           g_kernel.io.buf_reads, g_kernel.io.buf_writes,
           g_kernel.io.buf_hits);

    /* Recent Kernel Log */
    printf("\n" BCYAN "  Recent Kernel Log:\n" RESET);
    int start = g_kernel.log_count > 8 ? g_kernel.log_count - 8 : 0;
    int shown = 0;
    for (int i = start; i < g_kernel.log_count && i < OS_MAX_LOG; i++) {
        int idx = i % OS_MAX_LOG;
        if (g_kernel.log[idx].message[0] == '\0') continue;
        struct tm *ti = localtime(&g_kernel.log[idx].timestamp);
        char ts[16];
        if (ti) strftime(ts, sizeof(ts), "%H:%M:%S", ti);
        else    snprintf(ts, sizeof(ts), "--:--:--");
        printf(DIM "    [%s] %s\n" RESET, ts, g_kernel.log[idx].message);
        shown++;
    }
    if (!shown) printf(DIM "    (no log entries)\n" RESET);
    print_separator();
}

/* ============================================================
 *  bank_menu -- Interactive banking menu
 * ============================================================ */
void bank_menu(BankState *bs)
{
    int running = 1;
    while (running) {
        printf(BCYAN "\n"
            "  +=======================================================+\n"
            "  |" BWHITE "           TASKFORGE BANKING SYSTEM" BCYAN
                                                     "                  |\n"
            "  +=======================================================+\n"
            "  |                                                       |\n"
            "  |" RESET "   1. Create Account" BCYAN
                                            "                                  |\n"
            "  |" RESET "   2. Deposit" BCYAN
                                     "                                         |\n"
            "  |" RESET "   3. Withdraw" BCYAN
                                      "                                        |\n"
            "  |" RESET "   4. Transfer (between accounts)" BCYAN
                                                           "                 |\n"
            "  |" RESET "   5. Check Balance" BCYAN
                                             "                                   |\n"
            "  |" RESET "   6. Account Statement" BCYAN
                                                 "                               |\n"
            "  |" RESET "   7. View All Accounts" BCYAN
                                                 "                               |\n"
            "  |" RESET "   8. Transaction Log" BCYAN
                                               "                                 |\n"
            "  |" BYELLOW "   9. OS Kernel Dashboard" BCYAN
                                                    "                             |\n"
            "  |" BRED "   0. Exit Banking System" BCYAN
                                                  "                             |\n"
            "  |                                                       |\n"
            "  +=======================================================+\n"
            RESET);

        int ch = get_int_input("Enter choice [0-9]: ", 0, 9);
        switch (ch) {

        case 1: { /* Create Account */
            char name[OS_MAX_NAME];
            get_string_input("Account holder name: ", name, OS_MAX_NAME);
            if (strlen(name) == 0) { printf(BRED "  Name cannot be empty.\n" RESET); break; }
            double init = get_double_input("Initial deposit ($): ");
            bank_create_account(bs, name, init);
            break;
        }
        case 2: { /* Deposit */
            if (bs->acc_count == 0) { printf(BRED "  No accounts exist yet.\n" RESET); break; }
            bank_print_all_accounts(bs);
            int acc = get_int_input("Account ID: ", 1, bs->acc_count);
            double amt = get_double_input("Deposit amount ($): ");
            bank_deposit(bs, acc, amt);
            break;
        }
        case 3: { /* Withdraw */
            if (bs->acc_count == 0) { printf(BRED "  No accounts exist yet.\n" RESET); break; }
            bank_print_all_accounts(bs);
            int acc = get_int_input("Account ID: ", 1, bs->acc_count);
            double amt = get_double_input("Withdrawal amount ($): ");
            bank_withdraw(bs, acc, amt);
            break;
        }
        case 4: { /* Transfer */
            if (bs->acc_count < 2) { printf(BRED "  Need at least 2 accounts.\n" RESET); break; }
            bank_print_all_accounts(bs);
            int from = get_int_input("From Account ID: ", 1, bs->acc_count);
            int to   = get_int_input("To Account ID: ",   1, bs->acc_count);
            if (from == to) { printf(BRED "  Cannot transfer to same account.\n" RESET); break; }
            double amt = get_double_input("Transfer amount ($): ");
            bank_transfer(bs, from, to, amt);
            break;
        }
        case 5: { /* Check Balance */
            if (bs->acc_count == 0) { printf(BRED "  No accounts exist yet.\n" RESET); break; }
            int acc = get_int_input("Account ID: ", 1, bs->acc_count);
            bank_get_balance(bs, acc);
            break;
        }
        case 6: { /* Account Statement */
            if (bs->acc_count == 0) { printf(BRED "  No accounts exist yet.\n" RESET); break; }
            int acc = get_int_input("Account ID: ", 1, bs->acc_count);
            bank_print_statement(bs, acc);
            break;
        }
        case 7: bank_print_all_accounts(bs); break;
        case 8: bank_print_tx_log(bs); break;
        case 9: bank_os_dashboard(); break;
        case 0:
            printf(BYELLOW "\n  Exiting Banking System...\n" RESET);
            running = 0;
            break;
        default:
            printf(BRED "  Invalid choice.\n" RESET);
            break;
        }
    }
}
