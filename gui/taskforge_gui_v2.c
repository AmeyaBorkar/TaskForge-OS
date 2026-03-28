/* ================================================================
 *  TaskForge v2 -- Banking System on OS Kernel (Win32 GUI)
 *
 *  A single, self-contained Win32 GUI that shows the banking
 *  application on the LEFT and the live OS kernel dashboard on
 *  the RIGHT, with a trace log at the bottom.
 *
 *  Build:
 *    gcc -Wall -Wextra -std=c11 -Ikernel -o taskforge_gui_v2.exe \
 *        gui/taskforge_gui_v2.c kernel/os_kernel.c                \
 *        -lcomctl32 -lgdi32 -lcomdlg32 -lpthread -mwindows -static
 * ================================================================ */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

/* Include the OS kernel directly */
#include "../kernel/os_kernel.h"
#include "../kernel/os_syscall.h"

/* ============================================================
 *  Window / layout constants
 * ============================================================ */
#define WND_WIDTH       1150
#define WND_HEIGHT      780
#define LEFT_PCT        60      /* left panel takes 60% */
#define BOTTOM_H        180     /* trace log height */
#define MARGIN          8
#define LABEL_H         18
#define EDIT_H          22
#define BTN_H           28
#define COMBO_H         24
#define GROUP_PAD       20

/* ============================================================
 *  Control IDs
 * ============================================================ */
enum {
    /* Input fields */
    ID_EDT_NAME = 1001,
    ID_EDT_AMOUNT,
    ID_EDT_ACCID,
    ID_EDT_TARGET,

    /* Buttons */
    ID_BTN_CREATE = 1100,
    ID_BTN_DEPOSIT,
    ID_BTN_WITHDRAW,
    ID_BTN_TRANSFER,
    ID_BTN_BALANCE,
    ID_BTN_APPLY,

    /* Combo boxes */
    ID_CMB_SCHED = 1200,
    ID_CMB_MEM,
    ID_CMB_CACHE,
    ID_CMB_DISK,

    /* Display panels */
    ID_DASHBOARD = 1300,
    ID_ACCOUNTS,
    ID_TRACE,

    /* Group boxes (purely visual) */
    ID_GRP_BANK = 1400,
    ID_GRP_CONFIG,
    ID_GRP_ACCTS,
    ID_GRP_DASH,
    ID_GRP_TRACE
};

/* ============================================================
 *  Banking data (mirrors bank.h structures)
 * ============================================================ */
#define MAX_ACCOUNTS     20
#define MAX_TRANSACTIONS 100

typedef struct {
    int     id;
    char    holder[OS_MAX_NAME];
    double  balance;
    int     active;
    int     resource_id;
    int     file_id;
} GuiAccount;

typedef struct {
    int     id;
    int     from_acc;
    int     to_acc;
    double  amount;
    char    type[16];
    time_t  timestamp;
    int     pid;
    int     mem_addr;
} GuiTransaction;

static GuiAccount   accounts[MAX_ACCOUNTS];
static int          acc_count  = 0;
static GuiTransaction txlog[MAX_TRANSACTIONS];
static int          tx_count   = 0;
static int          accounts_dir = -1;   /* FS dir id for /accounts */
static int          logs_dir     = -1;   /* FS dir id for /logs */

/* ============================================================
 *  Trace output buffer
 * ============================================================ */
#define TRACE_BUF_SIZE 65536
static char trace_buf[TRACE_BUF_SIZE];
static int  trace_len = 0;

static void trace_clear(void) {
    trace_buf[0] = '\0';
    trace_len    = 0;
}

static void trace_append(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int room = TRACE_BUF_SIZE - trace_len - 1;
    if (room > 0) {
        int n = vsnprintf(trace_buf + trace_len, room, fmt, ap);
        if (n > 0) trace_len += (n < room) ? n : room;
    }
    va_end(ap);
}

/* Prepend a timestamp to each trace line */
static void trace_ts(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[16];
    snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ",
             t->tm_hour, t->tm_min, t->tm_sec);

    trace_append("%s%s\r\n", ts, tmp);
}

/* ============================================================
 *  Global window handles
 * ============================================================ */
static HWND hMainWnd;

/* Input fields */
static HWND hEdtName, hEdtAmount, hEdtAccId, hEdtTarget;

/* Buttons */
static HWND hBtnCreate, hBtnDeposit, hBtnWithdraw;
static HWND hBtnTransfer, hBtnBalance, hBtnApply;

/* Combo boxes */
static HWND hCmbSched, hCmbMem, hCmbCache, hCmbDisk;

/* Display panels */
static HWND hDashboard, hAccounts, hTrace;

/* Group boxes */
static HWND hGrpBank, hGrpConfig, hGrpAccts, hGrpDash, hGrpTrace;

/* Labels */
static HWND hLblName, hLblAmount, hLblAccId, hLblTarget;
static HWND hLblSched, hLblMem, hLblCache, hLblDisk;

/* Font */
static HFONT hFontUI, hFontMono;

/* ============================================================
 *  Forward declarations
 * ============================================================ */
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void create_controls(HWND hwnd);
static void refresh_dashboard(void);
static void refresh_accounts_list(void);
static void post_trace(void);
static void init_banking_fs(void);

/* Banking operations */
static void gui_create_account(const char *name, double initial);
static void gui_deposit(int acc_id, double amount);
static void gui_withdraw(int acc_id, double amount);
static void gui_transfer(int from_id, int to_id, double amount);
static void gui_check_balance(int acc_id);
static void gui_apply_config(void);

/* Helpers */
static int  get_edit_int(HWND h);
static double get_edit_double(HWND h);
static void get_edit_text(HWND h, char *buf, int max);

/* ============================================================
 *  WinMain -- entry point
 * ============================================================ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;

    /* Initialize common controls (for group boxes, etc.) */
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    /* Boot the OS kernel */
    kernel_init(&g_kernel);

    /* Register window class */
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "TaskForgeV2";
    wc.hIconSm       = LoadIconA(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    /* Create main window */
    hMainWnd = CreateWindowExA(
        0, "TaskForgeV2",
        "TaskForge -- Banking System on OS Kernel",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WND_WIDTH, WND_HEIGHT,
        NULL, NULL, hInst, NULL);

    if (!hMainWnd) {
        MessageBoxA(NULL, "Failed to create main window.", "Error", MB_OK);
        return 1;
    }

    ShowWindow(hMainWnd, nShow);
    UpdateWindow(hMainWnd);

    /* Message loop */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}

/* ============================================================
 *  Helper: read text from an edit control
 * ============================================================ */
static void get_edit_text(HWND h, char *buf, int max) {
    GetWindowTextA(h, buf, max);
}

static int get_edit_int(HWND h) {
    char buf[64];
    GetWindowTextA(h, buf, sizeof(buf));
    return atoi(buf);
}

static double get_edit_double(HWND h) {
    char buf[64];
    GetWindowTextA(h, buf, sizeof(buf));
    return atof(buf);
}

/* ============================================================
 *  Push trace buffer to the trace edit control and scroll down
 * ============================================================ */
static void post_trace(void) {
    SetWindowTextA(hTrace, trace_buf);
    /* Scroll to bottom */
    int len = GetWindowTextLengthA(hTrace);
    SendMessageA(hTrace, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(hTrace, EM_SCROLLCARET, 0, 0);
}

/* ============================================================
 *  Initialize banking filesystem directories
 * ============================================================ */
static void init_banking_fs(void) {
    /* Create /accounts directory */
    accounts_dir = sys_create("accounts", 1, 0);
    trace_ts("BOOT: Created /accounts directory (id=%d)", accounts_dir);

    /* Create /logs directory */
    logs_dir = sys_create("logs", 1, 0);
    trace_ts("BOOT: Created /logs directory (id=%d)", logs_dir);

    /* Create a transaction log file */
    int logf = sys_create("tx_log", 0, logs_dir);
    trace_ts("BOOT: Created /logs/tx_log (id=%d)", logf);

    /* Enable deadlock prevention (resource ordering) */
    g_kernel.deadlock.prevention_on = 1;
    trace_ts("BOOT: Deadlock prevention ENABLED (resource ordering)");

    trace_ts("BOOT: Banking system ready");
}

/* ============================================================
 *  Record a transaction in the local log
 * ============================================================ */
static void record_tx(const char *type, int from, int to,
                      double amount, int pid, int mem) {
    if (tx_count >= MAX_TRANSACTIONS) return;
    GuiTransaction *tx = &txlog[tx_count];
    tx->id       = tx_count + 1;
    tx->from_acc = from;
    tx->to_acc   = to;
    tx->amount   = amount;
    strncpy(tx->type, type, sizeof(tx->type) - 1);
    tx->type[sizeof(tx->type) - 1] = '\0';
    tx->timestamp = time(NULL);
    tx->pid      = pid;
    tx->mem_addr = mem;
    tx_count++;
}

/* ============================================================
 *  Write account data to the OS filesystem
 * ============================================================ */
static void write_account_file(int acc_idx, int pid) {
    GuiAccount *a = &accounts[acc_idx];
    char fname[64], data[256];
    snprintf(fname, sizeof(fname), "acc_%03d", a->id);

    int fid = sys_find(fname, accounts_dir);
    if (fid < 0) {
        /* File does not exist yet -- create it */
        fid = sys_create(fname, 0, accounts_dir);
        trace_ts("OS: Created file '%s' (fid=%d)", fname, fid);
    }

    if (fid >= 0) {
        int fd = sys_open(fid, 2, pid);  /* mode 2 = write */
        if (fd >= 0) {
            int dlen = snprintf(data, sizeof(data),
                                "%d|%s|%.2f", a->id, a->holder, a->balance);
            int written = sys_write(fd, data, dlen);
            sys_close(fd);
            trace_ts("OS: File '%s' written (%d bytes, fd=%d)",
                     fname, written, fd);
        }
    }
}

/* ============================================================
 *  Banking Operation: Create Account
 * ============================================================ */
static void gui_create_account(const char *name, double initial) {
    trace_clear();

    if (acc_count >= MAX_ACCOUNTS) {
        trace_ts("BANK: ERROR -- account table full (max %d)", MAX_ACCOUNTS);
        post_trace();
        return;
    }
    if (strlen(name) == 0) {
        trace_ts("BANK: ERROR -- account holder name is empty");
        post_trace();
        return;
    }
    if (initial < 0) {
        trace_ts("BANK: ERROR -- initial deposit cannot be negative");
        post_trace();
        return;
    }

    trace_ts("BANK: Creating account for '%s' ($%.2f)", name, initial);

    /* 1. Fork a process for this operation */
    int pid = sys_fork("create_acc", 3, 4, NULL, NULL);
    trace_ts("OS: Process P%d 'create_acc' created (NEW)", pid);

    /* 2. Transition to RUNNING */
    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_ts("OS: P%d: NEW -> READY -> RUNNING", pid);

    /* 3. Allocate memory for the account record */
    int mem = sys_alloc(pid, 1);
    trace_ts("OS: Memory allocated: 1 KB at address %d (%s)",
             mem, mem >= 0 ? "OK" : "FAILED");

    /* 4. Populate local account */
    int idx = acc_count;
    GuiAccount *a = &accounts[idx];
    a->id          = acc_count + 1;
    strncpy(a->holder, name, OS_MAX_NAME - 1);
    a->holder[OS_MAX_NAME - 1] = '\0';
    a->balance     = initial;
    a->active      = 1;
    a->resource_id = a->id;  /* resource id = account id (for deadlock mgr) */

    /* 5. Set max need for this resource so Banker's algorithm works */
    sys_set_max_need(pid, a->resource_id, 2);

    /* 6. Cache access for the new account page */
    int cached = sys_cache_access(a->id);
    trace_ts("OS: Cache access page %d: %s", a->id, cached ? "HIT" : "MISS");

    /* 7. Create account file in OS filesystem */
    char fname[64];
    snprintf(fname, sizeof(fname), "acc_%03d", a->id);
    a->file_id = sys_create(fname, 0, accounts_dir);
    trace_ts("OS: Created file '/accounts/%s' (fid=%d)", fname, a->file_id);

    /* 8. Write initial data */
    if (a->file_id >= 0) {
        int fd = sys_open(a->file_id, 2, pid);
        char data[256];
        int dlen = snprintf(data, sizeof(data),
                            "%d|%s|%.2f", a->id, a->holder, a->balance);
        int written = sys_write(fd, data, dlen);
        sys_close(fd);
        trace_ts("OS: File write: %d bytes to fd=%d", written, fd);
    }

    /* 9. Disk I/O for persistence */
    sys_io_request(a->id * 17 % OS_DISK_SIZE);
    sys_io_process();
    trace_ts("OS: Disk I/O processed (track %d)", a->id * 17 % OS_DISK_SIZE);

    /* 10. Record transaction */
    acc_count++;
    record_tx("CREATE", 0, a->id, initial, pid, mem);

    /* 11. Cleanup: free memory, terminate process */
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_ts("OS: P%d: RUNNING -> TERMINATED (resources freed)", pid);

    trace_ts("BANK: Account #%d created for '%s' (balance: $%.2f)",
             a->id, a->holder, a->balance);

    post_trace();
    refresh_dashboard();
    refresh_accounts_list();
}

/* ============================================================
 *  Banking Operation: Deposit
 * ============================================================ */
static void gui_deposit(int acc_id, double amount) {
    trace_clear();

    /* Validate */
    if (acc_id < 1 || acc_id > acc_count) {
        trace_ts("BANK: ERROR -- invalid account ID %d", acc_id);
        post_trace();
        return;
    }
    if (amount <= 0) {
        trace_ts("BANK: ERROR -- deposit amount must be positive");
        post_trace();
        return;
    }

    int idx = acc_id - 1;
    trace_ts("BANK: Deposit $%.2f to Account #%d (%s)",
             amount, acc_id, accounts[idx].holder);

    /* Fork process */
    int pid = sys_fork("deposit", 4, 5, NULL, NULL);
    trace_ts("OS: Process P%d 'deposit' created", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_ts("OS: P%d: NEW -> READY -> RUNNING", pid);

    /* Allocate memory */
    int mem = sys_alloc(pid, 1);
    trace_ts("OS: Memory allocated: 1 KB at address %d", mem);

    /* Set max need and request resource lock */
    sys_set_max_need(pid, acc_id, 2);
    int r = sys_lock(pid, acc_id, 1);
    if (r == 0)
        trace_ts("OS: Resource lock on Account #%d: GRANTED (Banker's: SAFE)",
                 acc_id);
    else
        trace_ts("OS: Resource lock on Account #%d: DENIED (unsafe state!)",
                 acc_id);

    /* Cache access */
    int cached = sys_cache_access(acc_id);
    trace_ts("OS: Cache access page %d: %s", acc_id, cached ? "HIT" : "MISS");

    /* Update balance */
    double old_bal = accounts[idx].balance;
    accounts[idx].balance += amount;
    trace_ts("BANK: Balance updated: $%.2f -> $%.2f",
             old_bal, accounts[idx].balance);

    /* Write to filesystem */
    write_account_file(idx, pid);

    /* Disk I/O */
    sys_io_request(acc_id * 13 % OS_DISK_SIZE);
    sys_io_process();
    trace_ts("OS: Disk I/O processed");

    /* Record transaction */
    record_tx("DEPOSIT", 0, acc_id, amount, pid, mem);

    /* Cleanup */
    sys_unlock(pid, acc_id, 1);
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_ts("OS: P%d: RUNNING -> TERMINATED (resources freed)", pid);

    trace_ts("BANK: Deposit complete. New balance: $%.2f",
             accounts[idx].balance);

    post_trace();
    refresh_dashboard();
    refresh_accounts_list();
}

/* ============================================================
 *  Banking Operation: Withdraw
 * ============================================================ */
static void gui_withdraw(int acc_id, double amount) {
    trace_clear();

    if (acc_id < 1 || acc_id > acc_count) {
        trace_ts("BANK: ERROR -- invalid account ID %d", acc_id);
        post_trace();
        return;
    }
    if (amount <= 0) {
        trace_ts("BANK: ERROR -- withdrawal amount must be positive");
        post_trace();
        return;
    }

    int idx = acc_id - 1;

    if (accounts[idx].balance < amount) {
        trace_ts("BANK: ERROR -- insufficient funds ($%.2f < $%.2f)",
                 accounts[idx].balance, amount);
        post_trace();
        return;
    }

    trace_ts("BANK: Withdraw $%.2f from Account #%d (%s)",
             amount, acc_id, accounts[idx].holder);

    /* Fork process */
    int pid = sys_fork("withdraw", 4, 5, NULL, NULL);
    trace_ts("OS: Process P%d 'withdraw' created", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_ts("OS: P%d: NEW -> READY -> RUNNING", pid);

    /* Allocate memory */
    int mem = sys_alloc(pid, 1);
    trace_ts("OS: Memory allocated: 1 KB at address %d", mem);

    /* Resource lock */
    sys_set_max_need(pid, acc_id, 2);
    int r = sys_lock(pid, acc_id, 1);
    if (r == 0)
        trace_ts("OS: Resource lock on Account #%d: GRANTED (Banker's: SAFE)",
                 acc_id);
    else
        trace_ts("OS: Resource lock on Account #%d: DENIED (unsafe state!)",
                 acc_id);

    /* Cache access */
    int cached = sys_cache_access(acc_id);
    trace_ts("OS: Cache access page %d: %s", acc_id, cached ? "HIT" : "MISS");

    /* Update balance */
    double old_bal = accounts[idx].balance;
    accounts[idx].balance -= amount;
    trace_ts("BANK: Balance updated: $%.2f -> $%.2f",
             old_bal, accounts[idx].balance);

    /* Write to filesystem */
    write_account_file(idx, pid);

    /* Disk I/O */
    sys_io_request(acc_id * 13 % OS_DISK_SIZE);
    sys_io_process();
    trace_ts("OS: Disk I/O processed");

    /* Record transaction */
    record_tx("WITHDRAW", acc_id, 0, amount, pid, mem);

    /* Cleanup */
    sys_unlock(pid, acc_id, 1);
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_ts("OS: P%d: RUNNING -> TERMINATED (resources freed)", pid);

    trace_ts("BANK: Withdrawal complete. New balance: $%.2f",
             accounts[idx].balance);

    post_trace();
    refresh_dashboard();
    refresh_accounts_list();
}

/* ============================================================
 *  Banking Operation: Transfer
 * ============================================================ */
static void gui_transfer(int from_id, int to_id, double amount) {
    trace_clear();

    if (from_id < 1 || from_id > acc_count) {
        trace_ts("BANK: ERROR -- invalid source account ID %d", from_id);
        post_trace();
        return;
    }
    if (to_id < 1 || to_id > acc_count) {
        trace_ts("BANK: ERROR -- invalid target account ID %d", to_id);
        post_trace();
        return;
    }
    if (from_id == to_id) {
        trace_ts("BANK: ERROR -- source and target accounts are the same");
        post_trace();
        return;
    }
    if (amount <= 0) {
        trace_ts("BANK: ERROR -- transfer amount must be positive");
        post_trace();
        return;
    }

    int fi = from_id - 1;
    int ti = to_id - 1;

    if (accounts[fi].balance < amount) {
        trace_ts("BANK: ERROR -- insufficient funds in Account #%d ($%.2f < $%.2f)",
                 from_id, accounts[fi].balance, amount);
        post_trace();
        return;
    }

    trace_ts("BANK: Transfer $%.2f from Account #%d -> Account #%d",
             amount, from_id, to_id);

    /* Fork process */
    int pid = sys_fork("transfer", 2, 8, NULL, NULL);
    trace_ts("OS: Process P%d 'transfer' created", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_ts("OS: P%d: NEW -> READY -> RUNNING", pid);

    /* Allocate memory */
    int mem = sys_alloc(pid, 2);
    trace_ts("OS: Memory allocated: 2 KB at address %d", mem);

    /* Deadlock prevention: lock resources in ascending order */
    int first  = (from_id < to_id) ? from_id : to_id;
    int second = (from_id < to_id) ? to_id   : from_id;

    trace_ts("OS: Deadlock prevention -- locking in order: R%d then R%d",
             first, second);

    /* Set max need for both resources */
    sys_set_max_need(pid, first, 2);
    sys_set_max_need(pid, second, 2);

    /* Lock first resource */
    int r1 = sys_lock(pid, first, 1);
    if (r1 == 0)
        trace_ts("OS: Resource lock on Account #%d: GRANTED (Banker's: SAFE)",
                 first);
    else
        trace_ts("OS: Resource lock on Account #%d: DENIED!", first);

    /* Lock second resource */
    int r2 = sys_lock(pid, second, 1);
    if (r2 == 0)
        trace_ts("OS: Resource lock on Account #%d: GRANTED (Banker's: SAFE)",
                 second);
    else
        trace_ts("OS: Resource lock on Account #%d: DENIED!", second);

    /* Cache access for both accounts */
    int c1 = sys_cache_access(from_id);
    trace_ts("OS: Cache access page %d (source): %s",
             from_id, c1 ? "HIT" : "MISS");
    int c2 = sys_cache_access(to_id);
    trace_ts("OS: Cache access page %d (target): %s",
             to_id, c2 ? "HIT" : "MISS");

    /* Perform transfer */
    double old_from = accounts[fi].balance;
    double old_to   = accounts[ti].balance;
    accounts[fi].balance -= amount;
    accounts[ti].balance += amount;
    trace_ts("BANK: Account #%d: $%.2f -> $%.2f",
             from_id, old_from, accounts[fi].balance);
    trace_ts("BANK: Account #%d: $%.2f -> $%.2f",
             to_id, old_to, accounts[ti].balance);

    /* Write both account files */
    write_account_file(fi, pid);
    write_account_file(ti, pid);

    /* Disk I/O */
    sys_io_request(from_id * 11 % OS_DISK_SIZE);
    sys_io_request(to_id * 11 % OS_DISK_SIZE);
    sys_io_process();
    trace_ts("OS: Disk I/O processed (2 requests)");

    /* Record transaction */
    record_tx("TRANSFER", from_id, to_id, amount, pid, mem);

    /* Unlock in reverse order (good practice) */
    sys_unlock(pid, second, 1);
    sys_unlock(pid, first, 1);
    trace_ts("OS: Resources R%d, R%d released", first, second);

    /* Cleanup */
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_ts("OS: P%d: RUNNING -> TERMINATED (resources freed)", pid);

    trace_ts("BANK: Transfer complete");

    post_trace();
    refresh_dashboard();
    refresh_accounts_list();
}

/* ============================================================
 *  Banking Operation: Check Balance
 * ============================================================ */
static void gui_check_balance(int acc_id) {
    trace_clear();

    if (acc_id < 1 || acc_id > acc_count) {
        trace_ts("BANK: ERROR -- invalid account ID %d", acc_id);
        post_trace();
        return;
    }

    int idx = acc_id - 1;
    trace_ts("BANK: Balance inquiry for Account #%d (%s)",
             acc_id, accounts[idx].holder);

    /* Fork a lightweight process */
    int pid = sys_fork("balance_chk", 5, 2, NULL, NULL);
    trace_ts("OS: Process P%d 'balance_chk' created", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_ts("OS: P%d: NEW -> READY -> RUNNING", pid);

    /* Allocate memory */
    int mem = sys_alloc(pid, 1);
    trace_ts("OS: Memory allocated: 1 KB at address %d", mem);

    /* Cache access -- shows hit/miss */
    int cached = sys_cache_access(acc_id);
    trace_ts("OS: Cache access page %d: %s", acc_id, cached ? "HIT" : "MISS");

    /* Read from filesystem to show OS interaction */
    char fname[64];
    snprintf(fname, sizeof(fname), "acc_%03d", acc_id);
    int fid = sys_find(fname, accounts_dir);
    if (fid >= 0) {
        int fd = sys_open(fid, 1, pid);  /* mode 1 = read */
        if (fd >= 0) {
            char rbuf[256];
            int nread = sys_read(fd, rbuf, sizeof(rbuf) - 1);
            if (nread > 0) {
                rbuf[nread] = '\0';
                trace_ts("OS: File read: '%s' (%d bytes)", rbuf, nread);
            }
            sys_close(fd);
        }
    } else {
        trace_ts("OS: File '%s' not found in /accounts", fname);
    }

    /* Disk I/O for the read */
    sys_io_request(acc_id * 7 % OS_DISK_SIZE);
    sys_io_process();
    trace_ts("OS: Disk I/O processed (read)");

    /* Report balance */
    trace_ts("BANK: Account #%d (%s) -- Balance: $%.2f",
             acc_id, accounts[idx].holder, accounts[idx].balance);

    /* Show transaction count for this account */
    int tc = 0;
    for (int i = 0; i < tx_count; i++) {
        if (txlog[i].from_acc == acc_id || txlog[i].to_acc == acc_id)
            tc++;
    }
    trace_ts("BANK: Total transactions involving this account: %d", tc);

    /* Cleanup */
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_ts("OS: P%d: RUNNING -> TERMINATED (resources freed)", pid);

    post_trace();
    refresh_dashboard();
}

/* ============================================================
 *  Apply OS Configuration from combo boxes
 * ============================================================ */
static void gui_apply_config(void) {
    trace_clear();

    /* Scheduler */
    int si = (int)SendMessageA(hCmbSched, CB_GETCURSEL, 0, 0);
    if (si >= 0 && si <= 3) {
        SchedulerAlgo algo = (SchedulerAlgo)si;
        int quantum = (algo == SCHED_ROUND_ROBIN) ? 10 : 0;
        sys_set_scheduler(algo, quantum);
        const char *names[] = {"FCFS", "SJF", "Priority", "Round Robin"};
        trace_ts("CONFIG: Scheduler set to %s", names[si]);
    }

    /* Memory strategy */
    int mi = (int)SendMessageA(hCmbMem, CB_GETCURSEL, 0, 0);
    if (mi >= 0 && mi <= 3) {
        sys_set_mem_strategy((MemStrategy)mi);
        const char *names[] = {"First Fit", "Best Fit", "Worst Fit", "Next Fit"};
        trace_ts("CONFIG: Memory strategy set to %s", names[mi]);
    }

    /* Cache algorithm */
    int ci = (int)SendMessageA(hCmbCache, CB_GETCURSEL, 0, 0);
    if (ci >= 0 && ci <= 2) {
        sys_set_cache_algo((CacheAlgo)ci);
        const char *names[] = {"LRU", "FIFO", "Clock"};
        trace_ts("CONFIG: Cache algorithm set to %s", names[ci]);
    }

    /* Disk algorithm */
    int di = (int)SendMessageA(hCmbDisk, CB_GETCURSEL, 0, 0);
    if (di >= 0 && di <= 3) {
        sys_set_disk_algo((DiskAlgo)di);
        const char *names[] = {"FCFS", "SSTF", "SCAN", "C-SCAN"};
        trace_ts("CONFIG: Disk I/O algorithm set to %s", names[di]);
    }

    trace_ts("CONFIG: All settings applied successfully");

    post_trace();
    refresh_dashboard();
}

/* ============================================================
 *  Refresh the OS Kernel Dashboard (right panel)
 * ============================================================ */
static void refresh_dashboard(void) {
    char buf[4096];
    int len = 0;
    int bsz = (int)sizeof(buf);

    /* --- PROCESSES --- */
    int total = 0, active = 0, ready = 0, running = 0, terminated = 0;
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (g_kernel.procs[i].active) {
            total++;
            switch (g_kernel.procs[i].state) {
                case PROC_READY:      ready++;   active++; break;
                case PROC_RUNNING:    running++; active++; break;
                case PROC_NEW:        active++;            break;
                case PROC_WAITING:    active++;            break;
                case PROC_TERMINATED: terminated++;        break;
            }
        }
    }
    /* Also count non-active terminated */
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (!g_kernel.procs[i].active && g_kernel.procs[i].pid > 0)
            terminated++;
    }

    const char *sched_names[] = {"FCFS", "SJF", "PRIORITY", "ROUND ROBIN"};

    len += snprintf(buf + len, bsz - len,
        "========== PROCESSES ==========\r\n");
    len += snprintf(buf + len, bsz - len,
        "Created: %d   Active: %d   Done: %d\r\n",
        g_kernel.next_pid - 1, total, terminated);
    len += snprintf(buf + len, bsz - len,
        "Ready: %d   Running: %d\r\n", ready, running);
    len += snprintf(buf + len, bsz - len,
        "Scheduler: %s", sched_names[g_kernel.sched_algo]);
    if (g_kernel.sched_algo == SCHED_ROUND_ROBIN)
        len += snprintf(buf + len, bsz - len,
            " (q=%d)", g_kernel.sched_quantum);
    len += snprintf(buf + len, bsz - len, "\r\n");
    len += snprintf(buf + len, bsz - len,
        "Clock Tick: %d\r\n", g_kernel.clock_tick);

    /* --- MEMORY --- */
    MemStats ms;
    sys_mem_stats(&ms);
    const char *mem_names[] = {"FIRST FIT", "BEST FIT", "WORST FIT", "NEXT FIT"};

    len += snprintf(buf + len, bsz - len,
        "\r\n========== MEMORY ==========\r\n");
    len += snprintf(buf + len, bsz - len,
        "Used: %d / %d KB\r\n", ms.used_kb, ms.total_kb);
    len += snprintf(buf + len, bsz - len,
        "Free: %d KB (%d blocks)\r\n", ms.free_kb, ms.free_blocks);
    len += snprintf(buf + len, bsz - len,
        "Strategy: %s\r\n", mem_names[ms.strategy]);
    len += snprintf(buf + len, bsz - len,
        "Fragmentation: %.1f%%\r\n", ms.fragmentation * 100.0f);
    len += snprintf(buf + len, bsz - len,
        "Total blocks: %d\r\n", ms.block_count);

    /* --- CACHE --- */
    CacheStats cs;
    sys_cache_stats(&cs);
    const char *cache_names[] = {"LRU", "FIFO", "CLOCK"};

    len += snprintf(buf + len, bsz - len,
        "\r\n========== CACHE (%s) ==========\r\n",
        cache_names[cs.algo]);
    len += snprintf(buf + len, bsz - len,
        "Entries: %d / %d\r\n", cs.entries_used, cs.cache_size);
    len += snprintf(buf + len, bsz - len,
        "Hits: %d   Misses: %d\r\n", cs.hits, cs.misses);
    len += snprintf(buf + len, bsz - len,
        "Hit Ratio: %.1f%%\r\n", cs.hit_ratio * 100.0f);

    /* --- DEADLOCK --- */
    len += snprintf(buf + len, bsz - len,
        "\r\n========== DEADLOCK ==========\r\n");
    len += snprintf(buf + len, bsz - len,
        "Prevention: %s\r\n",
        g_kernel.deadlock.prevention_on ? "ON (resource ordering)" : "OFF");
    len += snprintf(buf + len, bsz - len,
        "Detected: %d\r\n", g_kernel.deadlock.deadlock_count);

    /* Show available resources (first 5 that are meaningful) */
    len += snprintf(buf + len, bsz - len, "Available: [");
    for (int r = 0; r < 5; r++) {
        len += snprintf(buf + len, bsz - len, "%s%d",
            r > 0 ? "," : "", g_kernel.deadlock.available[r]);
    }
    len += snprintf(buf + len, bsz - len, "]\r\n");

    /* --- FILE SYSTEM --- */
    int files = 0, dirs = 0;
    for (int i = 0; i < OS_MAX_FILES; i++) {
        if (g_kernel.fs_nodes[i].active) {
            if (g_kernel.fs_nodes[i].is_dir)
                dirs++;
            else
                files++;
        }
    }

    len += snprintf(buf + len, bsz - len,
        "\r\n========== FILE SYSTEM ==========\r\n");
    len += snprintf(buf + len, bsz - len,
        "Files: %d   Dirs: %d\r\n", files, dirs);
    len += snprintf(buf + len, bsz - len,
        "Open FDs: %d\r\n", g_kernel.fd_count);

    /* --- I/O --- */
    const char *disk_names[] = {"FCFS", "SSTF", "SCAN", "C-SCAN"};

    len += snprintf(buf + len, bsz - len,
        "\r\n========== I/O (%s) ==========\r\n",
        disk_names[g_kernel.io.algo]);
    len += snprintf(buf + len, bsz - len,
        "Head: track %d  Dir: %s\r\n",
        g_kernel.io.head_pos,
        g_kernel.io.direction ? "--->" : "<---");
    len += snprintf(buf + len, bsz - len,
        "Total seeks: %d\r\n", g_kernel.io.total_seek);
    len += snprintf(buf + len, bsz - len,
        "Total ops: %d\r\n", g_kernel.io.total_ops);
    len += snprintf(buf + len, bsz - len,
        "Queue: %d pending\r\n", g_kernel.io.queue_len);
    len += snprintf(buf + len, bsz - len,
        "Buf reads: %d  writes: %d  hits: %d\r\n",
        g_kernel.io.buf_reads, g_kernel.io.buf_writes,
        g_kernel.io.buf_hits);

    SetWindowTextA(hDashboard, buf);
}

/* ============================================================
 *  Refresh accounts list display
 * ============================================================ */
static void refresh_accounts_list(void) {
    char buf[4096];
    int len = 0;
    int bsz = (int)sizeof(buf);

    if (acc_count == 0) {
        snprintf(buf, bsz, "(no accounts yet)");
    } else {
        len += snprintf(buf + len, bsz - len,
            " ID  %-20s  %12s\r\n", "Account Holder", "Balance");
        len += snprintf(buf + len, bsz - len,
            " --- %-20s  %12s\r\n",
            "--------------------", "------------");
        for (int i = 0; i < acc_count; i++) {
            GuiAccount *a = &accounts[i];
            if (a->active) {
                len += snprintf(buf + len, bsz - len,
                    " #%-2d  %-20s  $%10.2f\r\n",
                    a->id, a->holder, a->balance);
            }
        }
        /* Show total */
        double total = 0;
        for (int i = 0; i < acc_count; i++)
            if (accounts[i].active)
                total += accounts[i].balance;
        len += snprintf(buf + len, bsz - len,
            " --- %-20s  %12s\r\n",
            "--------------------", "------------");
        len += snprintf(buf + len, bsz - len,
            "      %-20s  $%10.2f\r\n", "TOTAL", total);
        len += snprintf(buf + len, bsz - len,
            "\r\n Transactions: %d", tx_count);
    }

    SetWindowTextA(hAccounts, buf);
}

/* ============================================================
 *  Create all child controls in WM_CREATE
 * ============================================================ */
static void create_controls(HWND hwnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hwnd, GWLP_HINSTANCE);

    /* Create fonts */
    hFontUI = CreateFontA(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    hFontMono = CreateFontA(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom;

    int left_w  = (cw * LEFT_PCT) / 100;
    int right_w = cw - left_w;
    int top_h   = ch - BOTTOM_H;

    /* ========================================================
     *  LEFT PANEL -- Banking Operations
     * ======================================================== */
    int lx = MARGIN;
    int ly = MARGIN;
    int lw = left_w - 2 * MARGIN;

    /* -- Group: Banking Operations -- */
    hGrpBank = CreateWindowExA(0, "BUTTON", "Banking Operations",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        lx, ly, lw, 260,
        hwnd, (HMENU)ID_GRP_BANK, hInst, NULL);
    SendMessageA(hGrpBank, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    int fx = lx + 12;   /* fields x */
    int fy = ly + 22;   /* fields y */
    int flbl_w = 95;    /* label width */
    int fedt_w = 200;   /* edit width */

    /* Account Name */
    hLblName = CreateWindowExA(0, "STATIC", "Account Name:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        fx, fy + 3, flbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblName, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hEdtName = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        fx + flbl_w + 6, fy, fedt_w, EDIT_H,
        hwnd, (HMENU)ID_EDT_NAME, hInst, NULL);
    SendMessageA(hEdtName, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    fy += EDIT_H + 6;

    /* Amount */
    hLblAmount = CreateWindowExA(0, "STATIC", "Amount:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        fx, fy + 3, flbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblAmount, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hEdtAmount = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        fx + flbl_w + 6, fy, fedt_w, EDIT_H,
        hwnd, (HMENU)ID_EDT_AMOUNT, hInst, NULL);
    SendMessageA(hEdtAmount, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    fy += EDIT_H + 6;

    /* Account ID */
    hLblAccId = CreateWindowExA(0, "STATIC", "Account ID:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        fx, fy + 3, flbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblAccId, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hEdtAccId = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        fx + flbl_w + 6, fy, fedt_w, EDIT_H,
        hwnd, (HMENU)ID_EDT_ACCID, hInst, NULL);
    SendMessageA(hEdtAccId, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    fy += EDIT_H + 6;

    /* Target Account */
    hLblTarget = CreateWindowExA(0, "STATIC", "Target Acc:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        fx, fy + 3, flbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblTarget, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hEdtTarget = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        fx + flbl_w + 6, fy, fedt_w, EDIT_H,
        hwnd, (HMENU)ID_EDT_TARGET, hInst, NULL);
    SendMessageA(hEdtTarget, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    fy += EDIT_H + 12;

    /* Buttons row 1 */
    int bx = fx;
    int btn_w = 130;
    int btn_gap = 8;

    hBtnCreate = CreateWindowExA(0, "BUTTON", "Create Account",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx, fy, btn_w + 20, BTN_H,
        hwnd, (HMENU)ID_BTN_CREATE, hInst, NULL);
    SendMessageA(hBtnCreate, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    fy += BTN_H + 6;

    /* Buttons row 2 */
    hBtnDeposit = CreateWindowExA(0, "BUTTON", "Deposit",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx, fy, btn_w, BTN_H,
        hwnd, (HMENU)ID_BTN_DEPOSIT, hInst, NULL);
    SendMessageA(hBtnDeposit, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hBtnWithdraw = CreateWindowExA(0, "BUTTON", "Withdraw",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx + btn_w + btn_gap, fy, btn_w, BTN_H,
        hwnd, (HMENU)ID_BTN_WITHDRAW, hInst, NULL);
    SendMessageA(hBtnWithdraw, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    fy += BTN_H + 6;

    /* Buttons row 3 */
    hBtnTransfer = CreateWindowExA(0, "BUTTON", "Transfer",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx, fy, btn_w, BTN_H,
        hwnd, (HMENU)ID_BTN_TRANSFER, hInst, NULL);
    SendMessageA(hBtnTransfer, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hBtnBalance = CreateWindowExA(0, "BUTTON", "Check Balance",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx + btn_w + btn_gap, fy, btn_w, BTN_H,
        hwnd, (HMENU)ID_BTN_BALANCE, hInst, NULL);
    SendMessageA(hBtnBalance, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    /* ========================================================
     *  LEFT PANEL -- OS Configuration
     * ======================================================== */
    int cy_cfg = ly + 268;
    hGrpConfig = CreateWindowExA(0, "BUTTON", "OS Configuration",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        lx, cy_cfg, lw, 160,
        hwnd, (HMENU)ID_GRP_CONFIG, hInst, NULL);
    SendMessageA(hGrpConfig, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    int cfx = lx + 12;
    int cfy = cy_cfg + 22;
    int clbl_w = 80;
    int ccmb_w = 160;

    /* Scheduler combo */
    hLblSched = CreateWindowExA(0, "STATIC", "Scheduler:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        cfx, cfy + 3, clbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblSched, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hCmbSched = CreateWindowExA(0, "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        cfx + clbl_w + 6, cfy, ccmb_w, COMBO_H * 6,
        hwnd, (HMENU)ID_CMB_SCHED, hInst, NULL);
    SendMessageA(hCmbSched, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessageA(hCmbSched, CB_ADDSTRING, 0, (LPARAM)"FCFS");
    SendMessageA(hCmbSched, CB_ADDSTRING, 0, (LPARAM)"SJF");
    SendMessageA(hCmbSched, CB_ADDSTRING, 0, (LPARAM)"Priority");
    SendMessageA(hCmbSched, CB_ADDSTRING, 0, (LPARAM)"Round Robin");
    SendMessageA(hCmbSched, CB_SETCURSEL, 0, 0);
    cfy += COMBO_H + 4;

    /* Memory combo */
    hLblMem = CreateWindowExA(0, "STATIC", "Memory:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        cfx, cfy + 3, clbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblMem, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hCmbMem = CreateWindowExA(0, "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        cfx + clbl_w + 6, cfy, ccmb_w, COMBO_H * 6,
        hwnd, (HMENU)ID_CMB_MEM, hInst, NULL);
    SendMessageA(hCmbMem, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessageA(hCmbMem, CB_ADDSTRING, 0, (LPARAM)"First Fit");
    SendMessageA(hCmbMem, CB_ADDSTRING, 0, (LPARAM)"Best Fit");
    SendMessageA(hCmbMem, CB_ADDSTRING, 0, (LPARAM)"Worst Fit");
    SendMessageA(hCmbMem, CB_ADDSTRING, 0, (LPARAM)"Next Fit");
    SendMessageA(hCmbMem, CB_SETCURSEL, 0, 0);
    cfy += COMBO_H + 4;

    /* Cache combo */
    hLblCache = CreateWindowExA(0, "STATIC", "Cache:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        cfx, cfy + 3, clbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblCache, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hCmbCache = CreateWindowExA(0, "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        cfx + clbl_w + 6, cfy, ccmb_w, COMBO_H * 6,
        hwnd, (HMENU)ID_CMB_CACHE, hInst, NULL);
    SendMessageA(hCmbCache, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessageA(hCmbCache, CB_ADDSTRING, 0, (LPARAM)"LRU");
    SendMessageA(hCmbCache, CB_ADDSTRING, 0, (LPARAM)"FIFO");
    SendMessageA(hCmbCache, CB_ADDSTRING, 0, (LPARAM)"Clock");
    SendMessageA(hCmbCache, CB_SETCURSEL, 0, 0);
    cfy += COMBO_H + 4;

    /* Disk combo */
    hLblDisk = CreateWindowExA(0, "STATIC", "Disk:",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        cfx, cfy + 3, clbl_w, LABEL_H,
        hwnd, NULL, hInst, NULL);
    SendMessageA(hLblDisk, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hCmbDisk = CreateWindowExA(0, "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        cfx + clbl_w + 6, cfy, ccmb_w, COMBO_H * 6,
        hwnd, (HMENU)ID_CMB_DISK, hInst, NULL);
    SendMessageA(hCmbDisk, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessageA(hCmbDisk, CB_ADDSTRING, 0, (LPARAM)"FCFS");
    SendMessageA(hCmbDisk, CB_ADDSTRING, 0, (LPARAM)"SSTF");
    SendMessageA(hCmbDisk, CB_ADDSTRING, 0, (LPARAM)"SCAN");
    SendMessageA(hCmbDisk, CB_ADDSTRING, 0, (LPARAM)"C-SCAN");
    SendMessageA(hCmbDisk, CB_SETCURSEL, 0, 0);

    /* Apply button -- to the right of the combos */
    hBtnApply = CreateWindowExA(0, "BUTTON", "Apply Config",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cfx + clbl_w + ccmb_w + 20, cy_cfg + 64, 120, BTN_H,
        hwnd, (HMENU)ID_BTN_APPLY, hInst, NULL);
    SendMessageA(hBtnApply, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    /* ========================================================
     *  LEFT PANEL -- Accounts Table
     * ======================================================== */
    int cy_acc = cy_cfg + 168;
    int acc_h  = top_h - cy_acc - MARGIN;
    if (acc_h < 60) acc_h = 60;

    hGrpAccts = CreateWindowExA(0, "BUTTON", "Accounts",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        lx, cy_acc, lw, acc_h,
        hwnd, (HMENU)ID_GRP_ACCTS, hInst, NULL);
    SendMessageA(hGrpAccts, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hAccounts = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "(no accounts yet)",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
        ES_AUTOVSCROLL | WS_VSCROLL,
        lx + 6, cy_acc + 18, lw - 12, acc_h - 24,
        hwnd, (HMENU)ID_ACCOUNTS, hInst, NULL);
    SendMessageA(hAccounts, WM_SETFONT, (WPARAM)hFontMono, TRUE);

    /* ========================================================
     *  RIGHT PANEL -- OS Kernel Dashboard
     * ======================================================== */
    int rx = left_w + MARGIN;
    int ry = MARGIN;
    int rw = right_w - 2 * MARGIN;
    int rh = top_h - 2 * MARGIN;

    hGrpDash = CreateWindowExA(0, "BUTTON", "OS Kernel Dashboard",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        rx, ry, rw, rh,
        hwnd, (HMENU)ID_GRP_DASH, hInst, NULL);
    SendMessageA(hGrpDash, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hDashboard = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
        ES_AUTOVSCROLL | WS_VSCROLL,
        rx + 6, ry + 18, rw - 12, rh - 24,
        hwnd, (HMENU)ID_DASHBOARD, hInst, NULL);
    SendMessageA(hDashboard, WM_SETFONT, (WPARAM)hFontMono, TRUE);

    /* ========================================================
     *  BOTTOM PANEL -- OS Trace Log
     * ======================================================== */
    int tx_y = top_h;
    int tx_w = cw - 2 * MARGIN;
    int tx_h = BOTTOM_H - MARGIN;

    hGrpTrace = CreateWindowExA(0, "BUTTON", "OS Trace Log",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        MARGIN, tx_y, tx_w, tx_h,
        hwnd, (HMENU)ID_GRP_TRACE, hInst, NULL);
    SendMessageA(hGrpTrace, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hTrace = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
        ES_AUTOVSCROLL | WS_VSCROLL,
        MARGIN + 6, tx_y + 18, tx_w - 12, tx_h - 24,
        hwnd, (HMENU)ID_TRACE, hInst, NULL);
    SendMessageA(hTrace, WM_SETFONT, (WPARAM)hFontMono, TRUE);
}

/* ============================================================
 *  Handle WM_SIZE -- resize panels proportionally
 * ============================================================ */
static void handle_resize(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom;

    int left_w  = (cw * LEFT_PCT) / 100;
    int right_w = cw - left_w;
    int top_h   = ch - BOTTOM_H;

    /* ---- Left panel groups ---- */
    int lx = MARGIN;
    int lw = left_w - 2 * MARGIN;

    /* Banking Operations group stays fixed height */
    MoveWindow(hGrpBank, lx, MARGIN, lw, 260, TRUE);

    /* OS Configuration group stays fixed height */
    int cy_cfg = MARGIN + 268;
    MoveWindow(hGrpConfig, lx, cy_cfg, lw, 160, TRUE);

    /* Accounts group fills remaining vertical space in the left panel */
    int cy_acc = cy_cfg + 168;
    int acc_h  = top_h - cy_acc - MARGIN;
    if (acc_h < 60) acc_h = 60;

    MoveWindow(hGrpAccts, lx, cy_acc, lw, acc_h, TRUE);
    MoveWindow(hAccounts, lx + 6, cy_acc + 18, lw - 12, acc_h - 24, TRUE);

    /* ---- Right panel ---- */
    int rx = left_w + MARGIN;
    int rw = right_w - 2 * MARGIN;
    int rh = top_h - 2 * MARGIN;

    MoveWindow(hGrpDash, rx, MARGIN, rw, rh, TRUE);
    MoveWindow(hDashboard, rx + 6, MARGIN + 18, rw - 12, rh - 24, TRUE);

    /* ---- Bottom panel ---- */
    int tx_y = top_h;
    int tx_w = cw - 2 * MARGIN;
    int tx_h = BOTTOM_H - MARGIN;

    MoveWindow(hGrpTrace, MARGIN, tx_y, tx_w, tx_h, TRUE);
    MoveWindow(hTrace, MARGIN + 6, tx_y + 18, tx_w - 12, tx_h - 24, TRUE);

    InvalidateRect(hwnd, NULL, TRUE);
}

/* ============================================================
 *  Handle WM_COMMAND -- button clicks
 * ============================================================ */
static void handle_command(HWND hwnd, WPARAM wParam) {
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);

    (void)hwnd;
    (void)code;

    switch (id) {

    case ID_BTN_CREATE: {
        char name[OS_MAX_NAME];
        get_edit_text(hEdtName, name, sizeof(name));
        double amt = get_edit_double(hEdtAmount);
        gui_create_account(name, amt);
        /* Clear input fields */
        SetWindowTextA(hEdtName, "");
        SetWindowTextA(hEdtAmount, "");
        break;
    }

    case ID_BTN_DEPOSIT: {
        int acc = get_edit_int(hEdtAccId);
        double amt = get_edit_double(hEdtAmount);
        gui_deposit(acc, amt);
        SetWindowTextA(hEdtAmount, "");
        break;
    }

    case ID_BTN_WITHDRAW: {
        int acc = get_edit_int(hEdtAccId);
        double amt = get_edit_double(hEdtAmount);
        gui_withdraw(acc, amt);
        SetWindowTextA(hEdtAmount, "");
        break;
    }

    case ID_BTN_TRANSFER: {
        int from = get_edit_int(hEdtAccId);
        int to   = get_edit_int(hEdtTarget);
        double amt = get_edit_double(hEdtAmount);
        gui_transfer(from, to, amt);
        SetWindowTextA(hEdtAmount, "");
        break;
    }

    case ID_BTN_BALANCE: {
        int acc = get_edit_int(hEdtAccId);
        gui_check_balance(acc);
        break;
    }

    case ID_BTN_APPLY:
        gui_apply_config();
        break;
    }
}

/* ============================================================
 *  Custom paint -- draw a separator line between panels
 * ============================================================ */
static void handle_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int left_w = (rc.right * LEFT_PCT) / 100;
    int top_h  = rc.bottom - BOTTOM_H;

    /* Vertical separator between left and right panels */
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, left_w, 0, NULL);
    LineTo(hdc, left_w, top_h);
    SelectObject(hdc, hOld);
    DeleteObject(hPen);

    /* Horizontal separator above the trace log */
    hPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    hOld = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, 0, top_h, NULL);
    LineTo(hdc, rc.right, top_h);
    SelectObject(hdc, hOld);
    DeleteObject(hPen);

    EndPaint(hwnd, &ps);
}

/* ============================================================
 *  Set tab stops for the read-only edit controls
 * ============================================================ */
static void set_edit_tabs(HWND hEdit) {
    DWORD tabs[] = { 24, 48, 72, 96, 120 };
    SendMessageA(hEdit, EM_SETTABSTOPS, 5, (LPARAM)tabs);
}

/* ============================================================
 *  Window Procedure
 * ============================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE:
        create_controls(hwnd);
        init_banking_fs();

        /* Set tab stops on multiline edits */
        set_edit_tabs(hDashboard);
        set_edit_tabs(hAccounts);
        set_edit_tabs(hTrace);

        /* Show initial dashboard and boot trace */
        refresh_dashboard();
        refresh_accounts_list();
        post_trace();
        return 0;

    case WM_SIZE:
        handle_resize(hwnd);
        return 0;

    case WM_COMMAND:
        handle_command(hwnd, wParam);
        return 0;

    case WM_PAINT:
        handle_paint(hwnd);
        return 0;

    case WM_CTLCOLORSTATIC: {
        /* Make dashboard / accounts / trace backgrounds white
         * for better readability with Consolas font */
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == hDashboard || hCtrl == hAccounts || hCtrl == hTrace) {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, RGB(0, 0, 0));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == hDashboard || hCtrl == hAccounts || hCtrl == hTrace) {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(15, 15, 25));
            SetTextColor(hdc, RGB(0, 220, 100));
            static HBRUSH hDarkBrush = NULL;
            if (!hDarkBrush)
                hDarkBrush = CreateSolidBrush(RGB(15, 15, 25));
            return (LRESULT)hDarkBrush;
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 900;
        mmi->ptMinTrackSize.y = 600;
        return 0;
    }

    case WM_DESTROY:
        /* Shutdown the kernel */
        kernel_shutdown(&g_kernel);

        /* Clean up fonts */
        if (hFontUI)   DeleteObject(hFontUI);
        if (hFontMono) DeleteObject(hFontMono);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
