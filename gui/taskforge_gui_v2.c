/* ================================================================
 *  TaskForge v2 -- Banking System on OS Kernel (Win32 GUI)
 *
 *  Clean, tabbed interface:
 *    Tab 1: Banking       -- account operations, transfers, list
 *    Tab 2: OS Dashboard  -- live kernel state overview
 *    Tab 3: OS Config     -- scheduler, memory, cache, disk settings
 *    Tab 4: Trace Log     -- detailed syscall trace from operations
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

#include "../kernel/os_kernel.h"
#include "../kernel/os_syscall.h"

/* ============================================================
 *  Global kernel instance (defined in os_kernel.c)
 * ============================================================ */
/* g_kernel is already defined in os_kernel.c and declared
   extern in os_syscall.h -- no redefinition needed here. */

/* ============================================================
 *  Window constants
 * ============================================================ */
#define WND_W           800
#define WND_H           650
#define WND_MIN_W       700
#define WND_MIN_H       550

#define TAB_TOP_PAD     30      /* space below tab headers */
#define PAD             12      /* general padding */
#define LABEL_H         18
#define EDIT_H          24
#define BTN_H           30
#define COMBO_H         24
#define GROUP_PAD       22
#define VGAP            30      /* vertical gap between sections */
#define HGAP            12      /* horizontal gap between controls */

/* ============================================================
 *  Control IDs
 * ============================================================ */
enum {
    /* Tab control */
    IDC_TABCTRL = 100,

    /* Banking -- inputs */
    IDC_EDIT_NAME = 1001,
    IDC_EDIT_AMOUNT,
    IDC_EDIT_ACCID,
    IDC_EDIT_FROM_ACC,
    IDC_EDIT_TO_ACC,
    IDC_EDIT_TRANS_AMT,

    /* Banking -- buttons */
    IDC_BTN_CREATE = 1100,
    IDC_BTN_BALANCE,
    IDC_BTN_DEPOSIT,
    IDC_BTN_WITHDRAW,
    IDC_BTN_TRANSFER,

    /* Banking -- display */
    IDC_ACCOUNTS_LIST = 1200,

    /* Banking -- group boxes */
    IDC_GRP_OPS = 1250,
    IDC_GRP_TRANSFER,
    IDC_GRP_ACCLIST,

    /* Dashboard */
    IDC_DASH_TEXT = 1300,
    IDC_BTN_REFRESH,

    /* Config */
    IDC_COMBO_SCHED = 1400,
    IDC_COMBO_MEM,
    IDC_COMBO_CACHE,
    IDC_COMBO_DISK,
    IDC_EDIT_QUANTUM,
    IDC_BTN_APPLY,

    /* Trace */
    IDC_TRACE_TEXT = 1500,
    IDC_BTN_CLEAR_LOG
};

/* ============================================================
 *  Window handles
 * ============================================================ */
static HINSTANCE g_hInst;
static HWND g_hWnd;

/* Tab control and panels */
static HWND hTabCtrl;
static HWND hPanelBank, hPanelDash, hPanelConfig, hPanelTrace;

/* Banking controls */
static HWND hEditName, hEditAmount, hEditAccId;
static HWND hEditFromAcc, hEditToAcc, hEditTransAmt;
static HWND hAccountsList;

/* Dashboard */
static HWND hDashText;

/* Config */
static HWND hComboSched, hComboMem, hComboCache, hComboDisk;
static HWND hEditQuantum;

/* Trace */
static HWND hTraceText;

/* Fonts */
static HFONT hFontUI;
static HFONT hFontMono;

/* Dark background brush for dashboard and trace */
static HBRUSH hBrushDark;
#define CLR_DARK_BG     RGB(20, 20, 30)
#define CLR_LIGHT_TEXT  RGB(210, 220, 230)
#define CLR_MONO_GREEN  RGB(140, 220, 140)

/* ============================================================
 *  Trace Buffer
 * ============================================================ */
#define TRACE_BUF_SIZE  65536

static char trace_buf[TRACE_BUF_SIZE];
static int  trace_len = 0;

static void trace_clear(void)
{
    trace_buf[0] = '\0';
    trace_len = 0;
}

static void trace_log(const char *fmt, ...)
{
    if (trace_len >= TRACE_BUF_SIZE - 256)
        return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    trace_len += snprintf(trace_buf + trace_len,
                          (size_t)(TRACE_BUF_SIZE - trace_len),
                          "[%02d:%02d:%02d] ",
                          t->tm_hour, t->tm_min, t->tm_sec);

    va_list ap;
    va_start(ap, fmt);
    trace_len += vsnprintf(trace_buf + trace_len,
                           (size_t)(TRACE_BUF_SIZE - trace_len),
                           fmt, ap);
    va_end(ap);

    trace_len += snprintf(trace_buf + trace_len,
                          (size_t)(TRACE_BUF_SIZE - trace_len),
                          "\r\n");
}

/* ============================================================
 *  Banking State (local to GUI)
 * ============================================================ */
typedef struct {
    int    id;
    char   holder[64];
    double balance;
    int    active;
    int    file_id;
} GuiAccount;

static GuiAccount accounts[20];
static int acc_count = 0;
static int accounts_dir = -1;
static int logs_dir = -1;

/* ============================================================
 *  Forward declarations
 * ============================================================ */
static void create_tabs(HWND hwnd);
static void create_bank_panel(HWND parent);
static void create_dash_panel(HWND parent);
static void create_config_panel(HWND parent);
static void create_trace_panel(HWND parent);
static void switch_tab(int idx);
static void resize_panels(int w, int h);

static void refresh_accounts(void);
static void refresh_dashboard(void);
static void update_trace_display(void);
static void apply_config(void);

static void do_create_account(const char *name, double initial);
static void do_deposit(int acc_id, double amount);
static void do_withdraw(int acc_id, double amount);
static void do_transfer(int from_id, int to_id, double amount);
static void do_check_balance(int acc_id);

static GuiAccount *find_account(int id);

static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

/* ============================================================
 *  Helpers
 * ============================================================ */
static HWND make_label(HWND parent, const char *text,
                       int x, int y, int w, int h)
{
    HWND hw = CreateWindowExA(0, "STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, NULL, g_hInst, NULL);
    SendMessageA(hw, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return hw;
}

static HWND make_edit(HWND parent, int id,
                      int x, int y, int w, int h, DWORD extra)
{
    HWND hw = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(hw, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return hw;
}

static HWND make_button(HWND parent, const char *text, int id,
                        int x, int y, int w, int h)
{
    HWND hw = CreateWindowExA(0, "BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(hw, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return hw;
}

static HWND make_combo(HWND parent, int id,
                       int x, int y, int w, int drop_h)
{
    HWND hw = CreateWindowExA(0, "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, drop_h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(hw, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return hw;
}

static HWND make_groupbox(HWND parent, const char *text, int id,
                          int x, int y, int w, int h)
{
    HWND hw = CreateWindowExA(0, "BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(hw, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return hw;
}

static HWND make_multiline(HWND parent, int id,
                           int x, int y, int w, int h, HFONT font)
{
    HWND hw = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

/* ============================================================
 *  WinMain
 * ============================================================ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;
    g_hInst = hInst;

    /* Initialize kernel */
    kernel_init(&g_kernel);

    /* Create filesystem directories for banking */
    accounts_dir = sys_create("accounts", 1, 0);
    logs_dir     = sys_create("logs",     1, 0);

    /* Init common controls (for tab) */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC  = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    /* Create fonts */
    hFontUI = CreateFontA(
        -MulDiv(9, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    hFontMono = CreateFontA(
        -MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    hBrushDark = CreateSolidBrush(CLR_DARK_BG);

    /* Register window class */
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName  = "TaskForgeV2Class";
    wc.hIcon          = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm        = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    /* Create main window */
    g_hWnd = CreateWindowExA(0, "TaskForgeV2Class",
        "TaskForge - Banking System on OS Kernel",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, WND_W, WND_H,
        NULL, NULL, hInst, NULL);

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    /* Message loop */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(g_hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    /* Cleanup */
    kernel_shutdown(&g_kernel);
    DeleteObject(hFontUI);
    DeleteObject(hFontMono);
    DeleteObject(hBrushDark);

    return (int)msg.wParam;
}

/* ============================================================
 *  Tab Creation
 * ============================================================ */
static void create_tabs(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom;

    /* Tab control fills the whole client area */
    hTabCtrl = CreateWindowExA(0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, cw, ch,
        hwnd, (HMENU)IDC_TABCTRL, g_hInst, NULL);
    SendMessageA(hTabCtrl, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    /* Insert tabs */
    TCITEMA tie;
    ZeroMemory(&tie, sizeof(tie));
    tie.mask = TCIF_TEXT;

    tie.pszText = "Banking";
    TabCtrl_InsertItem(hTabCtrl, 0, &tie);

    tie.pszText = "OS Dashboard";
    TabCtrl_InsertItem(hTabCtrl, 1, &tie);

    tie.pszText = "OS Config";
    TabCtrl_InsertItem(hTabCtrl, 2, &tie);

    tie.pszText = "Trace Log";
    TabCtrl_InsertItem(hTabCtrl, 3, &tie);

    /* Get display area inside tab control */
    RECT tabrc;
    GetClientRect(hTabCtrl, &tabrc);
    TabCtrl_AdjustRect(hTabCtrl, FALSE, &tabrc);

    int px = tabrc.left;
    int py = tabrc.top;
    int pw = tabrc.right - tabrc.left;
    int ph = tabrc.bottom - tabrc.top;

    /* Create panels (static child windows that hold each tab's controls) */
    hPanelBank = CreateWindowExA(0, "STATIC", "",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        px, py, pw, ph, hTabCtrl, NULL, g_hInst, NULL);

    hPanelDash = CreateWindowExA(0, "STATIC", "",
        WS_CHILD | WS_CLIPCHILDREN,
        px, py, pw, ph, hTabCtrl, NULL, g_hInst, NULL);

    hPanelConfig = CreateWindowExA(0, "STATIC", "",
        WS_CHILD | WS_CLIPCHILDREN,
        px, py, pw, ph, hTabCtrl, NULL, g_hInst, NULL);

    hPanelTrace = CreateWindowExA(0, "STATIC", "",
        WS_CHILD | WS_CLIPCHILDREN,
        px, py, pw, ph, hTabCtrl, NULL, g_hInst, NULL);

    /* Populate each panel */
    create_bank_panel(hPanelBank);
    create_dash_panel(hPanelDash);
    create_config_panel(hPanelConfig);
    create_trace_panel(hPanelTrace);
}

/* ============================================================
 *  Banking Panel (Tab 1)
 * ============================================================ */
static void create_bank_panel(HWND parent)
{
    RECT rc;
    GetClientRect(parent, &rc);
    int pw = rc.right;
    int lbl_w = 120;
    int field_w = 220;
    int grp_w = pw - 2 * PAD;
    int x0 = PAD;

    /* === Group: Account Operations === */
    int gy = PAD;
    int grp_ops_h = 210;
    make_groupbox(parent, "Account Operations", IDC_GRP_OPS,
                  x0, gy, grp_w, grp_ops_h);

    int ix = x0 + GROUP_PAD;
    int iy = gy + GROUP_PAD + 4;

    /* Account Holder */
    make_label(parent, "Account Holder:", ix, iy + 3, lbl_w, LABEL_H);
    hEditName = make_edit(parent, IDC_EDIT_NAME,
                          ix + lbl_w + HGAP, iy, field_w, EDIT_H, 0);

    iy += EDIT_H + 10;

    /* Amount */
    make_label(parent, "Amount ($):", ix, iy + 3, lbl_w, LABEL_H);
    hEditAmount = make_edit(parent, IDC_EDIT_AMOUNT,
                            ix + lbl_w + HGAP, iy, field_w, EDIT_H, 0);

    iy += EDIT_H + 10;

    /* Account ID */
    make_label(parent, "Account ID:", ix, iy + 3, lbl_w, LABEL_H);
    hEditAccId = make_edit(parent, IDC_EDIT_ACCID,
                           ix + lbl_w + HGAP, iy, field_w, EDIT_H, 0);

    iy += EDIT_H + 18;

    /* Buttons row 1 */
    int btn_w = 130;
    make_button(parent, "Create Account", IDC_BTN_CREATE,
                ix, iy, btn_w, BTN_H);
    make_button(parent, "Check Balance", IDC_BTN_BALANCE,
                ix + btn_w + HGAP, iy, btn_w, BTN_H);

    iy += BTN_H + 8;

    /* Buttons row 2 */
    make_button(parent, "Deposit", IDC_BTN_DEPOSIT,
                ix, iy, btn_w, BTN_H);
    make_button(parent, "Withdraw", IDC_BTN_WITHDRAW,
                ix + btn_w + HGAP, iy, btn_w, BTN_H);

    /* === Group: Transfer === */
    gy += grp_ops_h + VGAP - 10;
    int grp_trans_h = 150;
    make_groupbox(parent, "Transfer", IDC_GRP_TRANSFER,
                  x0, gy, grp_w, grp_trans_h);

    ix = x0 + GROUP_PAD;
    iy = gy + GROUP_PAD + 4;

    /* From / To / Amount on one row each */
    int small_lbl = 100;
    int small_field = 100;

    make_label(parent, "From Account:", ix, iy + 3, small_lbl, LABEL_H);
    hEditFromAcc = make_edit(parent, IDC_EDIT_FROM_ACC,
                             ix + small_lbl + HGAP, iy,
                             small_field, EDIT_H, 0);

    make_label(parent, "To Account:",
               ix + small_lbl + HGAP + small_field + 20, iy + 3,
               small_lbl, LABEL_H);
    hEditToAcc = make_edit(parent, IDC_EDIT_TO_ACC,
                           ix + 2 * (small_lbl + HGAP) + small_field + 20,
                           iy, small_field, EDIT_H, 0);

    iy += EDIT_H + 10;

    make_label(parent, "Amount ($):", ix, iy + 3, small_lbl, LABEL_H);
    hEditTransAmt = make_edit(parent, IDC_EDIT_TRANS_AMT,
                              ix + small_lbl + HGAP, iy,
                              small_field, EDIT_H, 0);

    make_button(parent, "Transfer", IDC_BTN_TRANSFER,
                ix + small_lbl + HGAP + small_field + 20, iy,
                btn_w, BTN_H);

    /* === Group: Accounts List === */
    gy += grp_trans_h + VGAP - 10;
    int list_h = rc.bottom - gy - PAD;
    if (list_h < 80) list_h = 80;
    make_groupbox(parent, "Accounts", IDC_GRP_ACCLIST,
                  x0, gy, grp_w, list_h);

    hAccountsList = make_multiline(parent, IDC_ACCOUNTS_LIST,
        x0 + GROUP_PAD, gy + GROUP_PAD + 2,
        grp_w - 2 * GROUP_PAD, list_h - GROUP_PAD - 10,
        hFontMono);
}

/* ============================================================
 *  Dashboard Panel (Tab 2)
 * ============================================================ */
static void create_dash_panel(HWND parent)
{
    RECT rc;
    GetClientRect(parent, &rc);
    int pw = rc.right;
    int ph = rc.bottom;

    /* Refresh button at top */
    make_button(parent, "Refresh", IDC_BTN_REFRESH,
                PAD, PAD, 100, BTN_H);

    /* Big multiline edit for dashboard output */
    hDashText = make_multiline(parent, IDC_DASH_TEXT,
        PAD, PAD + BTN_H + 10,
        pw - 2 * PAD, ph - BTN_H - PAD - 20,
        hFontMono);
}

/* ============================================================
 *  Config Panel (Tab 3)
 * ============================================================ */
static void create_config_panel(HWND parent)
{
    int lbl_w = 170;
    int field_w = 180;
    int ix = PAD + 30;
    int iy = PAD + 20;

    /* Scheduler Algorithm */
    make_label(parent, "Scheduler Algorithm:", ix, iy + 3, lbl_w, LABEL_H);
    hComboSched = make_combo(parent, IDC_COMBO_SCHED,
                             ix + lbl_w + HGAP, iy, field_w, 150);
    SendMessageA(hComboSched, CB_ADDSTRING, 0, (LPARAM)"FCFS");
    SendMessageA(hComboSched, CB_ADDSTRING, 0, (LPARAM)"SJF");
    SendMessageA(hComboSched, CB_ADDSTRING, 0, (LPARAM)"Priority");
    SendMessageA(hComboSched, CB_ADDSTRING, 0, (LPARAM)"Round Robin");
    SendMessageA(hComboSched, CB_SETCURSEL, (WPARAM)g_kernel.sched_algo, 0);

    iy += COMBO_H + 20;

    /* Time Quantum */
    make_label(parent, "Time Quantum (RR):", ix, iy + 3, lbl_w, LABEL_H);
    hEditQuantum = make_edit(parent, IDC_EDIT_QUANTUM,
                             ix + lbl_w + HGAP, iy, field_w, EDIT_H, 0);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", g_kernel.sched_quantum);
        SetWindowTextA(hEditQuantum, buf);
    }

    iy += EDIT_H + 20;

    /* Memory Strategy */
    make_label(parent, "Memory Strategy:", ix, iy + 3, lbl_w, LABEL_H);
    hComboMem = make_combo(parent, IDC_COMBO_MEM,
                           ix + lbl_w + HGAP, iy, field_w, 150);
    SendMessageA(hComboMem, CB_ADDSTRING, 0, (LPARAM)"First Fit");
    SendMessageA(hComboMem, CB_ADDSTRING, 0, (LPARAM)"Best Fit");
    SendMessageA(hComboMem, CB_ADDSTRING, 0, (LPARAM)"Worst Fit");
    SendMessageA(hComboMem, CB_ADDSTRING, 0, (LPARAM)"Next Fit");
    SendMessageA(hComboMem, CB_SETCURSEL, (WPARAM)g_kernel.mem_strategy, 0);

    iy += COMBO_H + 20;

    /* Cache Algorithm */
    make_label(parent, "Cache Algorithm:", ix, iy + 3, lbl_w, LABEL_H);
    hComboCache = make_combo(parent, IDC_COMBO_CACHE,
                             ix + lbl_w + HGAP, iy, field_w, 150);
    SendMessageA(hComboCache, CB_ADDSTRING, 0, (LPARAM)"LRU");
    SendMessageA(hComboCache, CB_ADDSTRING, 0, (LPARAM)"FIFO");
    SendMessageA(hComboCache, CB_ADDSTRING, 0, (LPARAM)"Clock");
    SendMessageA(hComboCache, CB_SETCURSEL, (WPARAM)g_kernel.cache_algo, 0);

    iy += COMBO_H + 20;

    /* Disk Scheduling */
    make_label(parent, "Disk Scheduling:", ix, iy + 3, lbl_w, LABEL_H);
    hComboDisk = make_combo(parent, IDC_COMBO_DISK,
                            ix + lbl_w + HGAP, iy, field_w, 150);
    SendMessageA(hComboDisk, CB_ADDSTRING, 0, (LPARAM)"FCFS");
    SendMessageA(hComboDisk, CB_ADDSTRING, 0, (LPARAM)"SSTF");
    SendMessageA(hComboDisk, CB_ADDSTRING, 0, (LPARAM)"SCAN");
    SendMessageA(hComboDisk, CB_ADDSTRING, 0, (LPARAM)"C-SCAN");
    SendMessageA(hComboDisk, CB_SETCURSEL, (WPARAM)g_kernel.io.algo, 0);

    iy += COMBO_H + 35;

    /* Apply button */
    make_button(parent, "Apply Changes", IDC_BTN_APPLY,
                ix, iy, 140, BTN_H);
}

/* ============================================================
 *  Trace Panel (Tab 4)
 * ============================================================ */
static void create_trace_panel(HWND parent)
{
    RECT rc;
    GetClientRect(parent, &rc);
    int pw = rc.right;
    int ph = rc.bottom;

    /* Clear Log button at top */
    make_button(parent, "Clear Log", IDC_BTN_CLEAR_LOG,
                PAD, PAD, 100, BTN_H);

    /* Big multiline edit for trace output */
    hTraceText = make_multiline(parent, IDC_TRACE_TEXT,
        PAD, PAD + BTN_H + 10,
        pw - 2 * PAD, ph - BTN_H - PAD - 20,
        hFontMono);
}

/* ============================================================
 *  Tab Switching
 * ============================================================ */
static void switch_tab(int idx)
{
    ShowWindow(hPanelBank,   (idx == 0) ? SW_SHOW : SW_HIDE);
    ShowWindow(hPanelDash,   (idx == 1) ? SW_SHOW : SW_HIDE);
    ShowWindow(hPanelConfig, (idx == 2) ? SW_SHOW : SW_HIDE);
    ShowWindow(hPanelTrace,  (idx == 3) ? SW_SHOW : SW_HIDE);

    /* Auto-refresh when switching to Dashboard or Trace */
    if (idx == 1) refresh_dashboard();
    if (idx == 3) update_trace_display();
}

/* ============================================================
 *  Resize Handling
 * ============================================================ */
static void resize_panels(int w, int h)
{
    if (!hTabCtrl) return;

    MoveWindow(hTabCtrl, 0, 0, w, h, TRUE);

    RECT tabrc;
    GetClientRect(hTabCtrl, &tabrc);
    TabCtrl_AdjustRect(hTabCtrl, FALSE, &tabrc);

    int px = tabrc.left;
    int py = tabrc.top;
    int pw = tabrc.right - tabrc.left;
    int ph = tabrc.bottom - tabrc.top;

    MoveWindow(hPanelBank,   px, py, pw, ph, TRUE);
    MoveWindow(hPanelDash,   px, py, pw, ph, TRUE);
    MoveWindow(hPanelConfig, px, py, pw, ph, TRUE);
    MoveWindow(hPanelTrace,  px, py, pw, ph, TRUE);

    /* Resize the full-panel multiline edits */
    if (hDashText) {
        MoveWindow(hDashText, PAD, PAD + BTN_H + 10,
                   pw - 2 * PAD, ph - BTN_H - PAD - 20, TRUE);
    }
    if (hTraceText) {
        MoveWindow(hTraceText, PAD, PAD + BTN_H + 10,
                   pw - 2 * PAD, ph - BTN_H - PAD - 20, TRUE);
    }

    /* Resize accounts list in banking panel to fill remaining space */
    if (hAccountsList) {
        HWND grp = GetDlgItem(hPanelBank, IDC_GRP_ACCLIST);
        if (grp) {
            RECT grp_rc;
            GetWindowRect(grp, &grp_rc);
            MapWindowPoints(NULL, hPanelBank, (LPPOINT)&grp_rc, 2);

            int new_h = ph - grp_rc.top - PAD;
            if (new_h < 80) new_h = 80;
            int grp_w = pw - 2 * PAD;
            MoveWindow(grp, PAD, grp_rc.top, grp_w, new_h, TRUE);
            MoveWindow(hAccountsList,
                       PAD + GROUP_PAD, grp_rc.top + GROUP_PAD + 2,
                       grp_w - 2 * GROUP_PAD, new_h - GROUP_PAD - 10,
                       TRUE);
        }
    }
}

/* ============================================================
 *  Refresh: Accounts List
 * ============================================================ */
static void refresh_accounts(void)
{
    char buf[4096];
    int len = 0;

    if (acc_count == 0) {
        len += snprintf(buf + len, sizeof(buf) - (size_t)len,
                        "  (no accounts yet)\r\n");
    } else {
        len += snprintf(buf + len, sizeof(buf) - (size_t)len,
            "  %-4s  %-20s  %12s  %s\r\n",
            "ID", "Holder", "Balance", "Status");
        len += snprintf(buf + len, sizeof(buf) - (size_t)len,
            "  %-4s  %-20s  %12s  %s\r\n",
            "----", "--------------------", "------------", "------");

        for (int i = 0; i < acc_count; i++) {
            len += snprintf(buf + len, sizeof(buf) - (size_t)len,
                "  #%-3d  %-20s  $%11.2f  %s\r\n",
                accounts[i].id,
                accounts[i].holder,
                accounts[i].balance,
                accounts[i].active ? "ACTIVE" : "CLOSED");
        }
    }

    SetWindowTextA(hAccountsList, buf);
}

/* ============================================================
 *  Refresh: Dashboard
 * ============================================================ */
static void refresh_dashboard(void)
{
    char buf[8192];
    int len = 0;

    /* Gather stats */
    MemStats ms;
    sys_mem_stats(&ms);
    CacheStats cs;
    sys_cache_stats(&cs);

    /* Count processes */
    int total = 0, active = 0, terminated = 0;
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (g_kernel.procs[i].pid > 0) {
            total++;
            if (g_kernel.procs[i].active)
                active++;
            else
                terminated++;
        }
    }

    /* Count filesystem entries */
    int file_count = 0, dir_count = 0, open_fds = 0;
    for (int i = 0; i < OS_MAX_FILES; i++) {
        if (g_kernel.fs_nodes[i].active) {
            if (g_kernel.fs_nodes[i].is_dir)
                dir_count++;
            else
                file_count++;
        }
    }
    for (int i = 0; i < OS_MAX_OPEN_FDS; i++) {
        if (g_kernel.fd_table[i].active)
            open_fds++;
    }

    /* Name tables */
    const char *sn[] = {"FCFS", "SJF", "PRIORITY", "ROUND ROBIN"};
    const char *mn[] = {"FIRST FIT", "BEST FIT", "WORST FIT", "NEXT FIT"};
    const char *cn[] = {"LRU", "FIFO", "CLOCK"};
    const char *dn[] = {"FCFS", "SSTF", "SCAN", "C-SCAN"};

    /* Build dashboard text */
    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
        "PROCESSES\r\n"
        "  Total: %d   Active: %d   Terminated: %d\r\n"
        "  Scheduler: %s",
        total, active, terminated, sn[g_kernel.sched_algo]);

    if (g_kernel.sched_algo == SCHED_ROUND_ROBIN) {
        len += snprintf(buf + len, sizeof(buf) - (size_t)len,
            "   Quantum: %d ms", g_kernel.sched_quantum);
    }
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "\r\n\r\n");

    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
        "MEMORY\r\n"
        "  Used: %d / %d KB   Free: %d KB\r\n"
        "  Strategy: %s   Fragmentation: %.1f%%\r\n"
        "  Blocks: %d allocated, %d free\r\n\r\n",
        ms.used_kb, ms.total_kb, ms.free_kb,
        mn[ms.strategy], ms.fragmentation * 100.0f,
        ms.block_count - ms.free_blocks, ms.free_blocks);

    float ratio = (cs.hits + cs.misses > 0)
        ? (float)cs.hits / (float)(cs.hits + cs.misses) * 100.0f
        : 0.0f;
    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
        "CACHE\r\n"
        "  Algorithm: %s   Entries: %d / %d\r\n"
        "  Hits: %d   Misses: %d   Ratio: %.1f%%\r\n\r\n",
        cn[g_kernel.cache_algo],
        cs.entries_used, cs.cache_size,
        cs.hits, cs.misses, ratio);

    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
        "DEADLOCK\r\n"
        "  Prevention: %s   Detected: %d\r\n\r\n",
        g_kernel.deadlock.prevention_on ? "ON (Banker's)" : "OFF",
        g_kernel.deadlock.deadlock_count);

    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
        "FILE SYSTEM\r\n"
        "  Files: %d   Directories: %d   Open FDs: %d\r\n\r\n",
        file_count, dir_count, open_fds);

    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
        "I/O SUBSYSTEM\r\n"
        "  Disk: %s   Head: track %d   Queue: %d\r\n"
        "  Total seek: %d   Operations: %d\r\n"
        "  Buffer reads: %d   writes: %d   hits: %d\r\n\r\n",
        dn[g_kernel.io.algo],
        g_kernel.io.head_pos, g_kernel.io.queue_len,
        g_kernel.io.total_seek, g_kernel.io.total_ops,
        g_kernel.io.buf_reads, g_kernel.io.buf_writes,
        g_kernel.io.buf_hits);

    /* Uptime */
    time_t now = time(NULL);
    int uptime = (int)(now - g_kernel.boot_time);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
        "SYSTEM\r\n"
        "  Uptime: %dm %ds   Clock ticks: %d\r\n"
        "  Bank accounts: %d / 20\r\n",
        uptime / 60, uptime % 60, g_kernel.clock_tick,
        acc_count);

    SetWindowTextA(hDashText, buf);
}

/* ============================================================
 *  Update: Trace Display
 * ============================================================ */
static void update_trace_display(void)
{
    SetWindowTextA(hTraceText, trace_buf);
    /* Scroll to bottom */
    int len = GetWindowTextLengthA(hTraceText);
    SendMessageA(hTraceText, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(hTraceText, EM_SCROLLCARET, 0, 0);
}

/* ============================================================
 *  Apply Config
 * ============================================================ */
static void apply_config(void)
{
    int sched_idx = (int)SendMessageA(hComboSched, CB_GETCURSEL, 0, 0);
    int mem_idx   = (int)SendMessageA(hComboMem,   CB_GETCURSEL, 0, 0);
    int cache_idx = (int)SendMessageA(hComboCache, CB_GETCURSEL, 0, 0);
    int disk_idx  = (int)SendMessageA(hComboDisk,  CB_GETCURSEL, 0, 0);

    char qbuf[32];
    GetWindowTextA(hEditQuantum, qbuf, sizeof(qbuf));
    int quantum = atoi(qbuf);
    if (quantum < 1) quantum = 5;

    if (sched_idx >= 0)
        sys_set_scheduler((SchedulerAlgo)sched_idx, quantum);
    if (mem_idx >= 0)
        sys_set_mem_strategy((MemStrategy)mem_idx);
    if (cache_idx >= 0)
        sys_set_cache_algo((CacheAlgo)cache_idx);
    if (disk_idx >= 0)
        sys_set_disk_algo((DiskAlgo)disk_idx);

    trace_log("CONFIG: Scheduler=%d, Quantum=%d, Mem=%d, Cache=%d, Disk=%d",
              sched_idx, quantum, mem_idx, cache_idx, disk_idx);

    MessageBoxA(g_hWnd, "Configuration applied successfully.",
                "OS Config", MB_ICONINFORMATION);
}

/* ============================================================
 *  Find Account Helper
 * ============================================================ */
static GuiAccount *find_account(int id)
{
    for (int i = 0; i < acc_count; i++) {
        if (accounts[i].id == id && accounts[i].active)
            return &accounts[i];
    }
    return NULL;
}

/* ============================================================
 *  Banking: Create Account
 * ============================================================ */
static void do_create_account(const char *name, double initial)
{
    if (acc_count >= 20) {
        MessageBoxA(g_hWnd, "Maximum of 20 accounts reached.",
                    "Error", MB_ICONERROR);
        return;
    }
    if (strlen(name) == 0) {
        MessageBoxA(g_hWnd, "Please enter an account holder name.",
                    "Error", MB_ICONERROR);
        return;
    }
    if (initial < 0.0) {
        MessageBoxA(g_hWnd, "Initial balance cannot be negative.",
                    "Error", MB_ICONERROR);
        return;
    }

    int id = acc_count + 1;

    /* Create a process for this operation */
    int pid = sys_fork("create_account", 3, 10, NULL, NULL);
    trace_log("SYSCALL: sys_fork('create_account', pri=3)");
    trace_log("Process P%d created (NEW)", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_log("P%d: NEW -> READY -> RUNNING", pid);

    /* Allocate memory */
    int mem = sys_alloc(pid, 1);
    trace_log("Memory: 1 KB allocated at addr %d (%s)",
              mem,
              g_kernel.mem_strategy == MEM_BEST_FIT  ? "Best Fit"  :
              g_kernel.mem_strategy == MEM_FIRST_FIT  ? "First Fit" :
              g_kernel.mem_strategy == MEM_WORST_FIT  ? "Worst Fit" :
                                                        "Next Fit");

    /* Create file in /accounts */
    char fname[64];
    snprintf(fname, sizeof(fname), "acc_%03d", id);
    int fid = sys_create(fname, 0, accounts_dir);
    trace_log("File '%s' created in /accounts (node %d)", fname, fid);

    /* Write account data */
    int fd = sys_open(fid, 2, pid);
    trace_log("sys_open(fid=%d, mode=WRITE, pid=%d) -> fd=%d", fid, pid, fd);

    char data[256];
    int dlen = snprintf(data, sizeof(data), "%d|%s|%.2f", id, name, initial);
    sys_write(fd, data, dlen);
    trace_log("sys_write: %d bytes to fd=%d", dlen, fd);

    sys_close(fd);
    trace_log("sys_close(fd=%d)", fd);

    /* Cache the new account page */
    int cache_hit = sys_cache_access(id);
    trace_log("Cache page %d: %s", id, cache_hit ? "HIT" : "MISS (loaded)");

    /* Disk I/O simulation */
    int track = (id * 7) % OS_DISK_SIZE;
    sys_io_request(track);
    sys_io_process();
    trace_log("Disk I/O: wrote to track %d (%s)",
              track,
              g_kernel.io.algo == DISK_SCAN  ? "SCAN"  :
              g_kernel.io.algo == DISK_FCFS  ? "FCFS"  :
              g_kernel.io.algo == DISK_SSTF  ? "SSTF"  :
                                                "C-SCAN");

    /* Set max need for Banker's algorithm */
    sys_set_max_need(pid, id, 1);
    trace_log("Banker's: max_need[P%d][res_%d] = 1", pid, id);

    /* Store in local state */
    accounts[acc_count].id       = id;
    strncpy(accounts[acc_count].holder, name, 63);
    accounts[acc_count].holder[63] = '\0';
    accounts[acc_count].balance  = initial;
    accounts[acc_count].active   = 1;
    accounts[acc_count].file_id  = fid;
    acc_count++;

    /* Cleanup process */
    sys_free_all(pid);
    trace_log("Memory freed for P%d", pid);

    sys_set_state(pid, PROC_TERMINATED);
    trace_log("P%d TERMINATED. Account #%d created for '%s' ($%.2f)",
              pid, id, name, initial);
    trace_log("---");

    refresh_accounts();
    update_trace_display();

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Account #%d created for %s\nBalance: $%.2f", id, name, initial);
    MessageBoxA(g_hWnd, msg, "Account Created", MB_ICONINFORMATION);
}

/* ============================================================
 *  Banking: Deposit
 * ============================================================ */
static void do_deposit(int acc_id, double amount)
{
    GuiAccount *acc = find_account(acc_id);
    if (!acc) {
        MessageBoxA(g_hWnd, "Account not found or inactive.",
                    "Error", MB_ICONERROR);
        return;
    }
    if (amount <= 0.0) {
        MessageBoxA(g_hWnd, "Deposit amount must be positive.",
                    "Error", MB_ICONERROR);
        return;
    }

    int pid = sys_fork("deposit", 4, 8, NULL, NULL);
    trace_log("SYSCALL: sys_fork('deposit', pri=4)");
    trace_log("Process P%d created (NEW)", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_log("P%d: NEW -> READY -> RUNNING", pid);

    int mem = sys_alloc(pid, 1);
    trace_log("Memory: 1 KB at addr %d", mem);

    /* Lock account resource (Banker's) */
    sys_set_max_need(pid, acc_id, 1);
    int r = sys_lock(pid, acc_id, 1);
    trace_log("sys_lock(P%d, res=%d): %s",
              pid, acc_id, r == 0 ? "GRANTED (Banker's: SAFE)" : "DENIED");
    if (r < 0) {
        sys_free_all(pid);
        sys_set_state(pid, PROC_TERMINATED);
        trace_log("P%d TERMINATED (lock denied).", pid);
        trace_log("---");
        update_trace_display();
        MessageBoxA(g_hWnd, "Could not acquire lock on account.",
                    "Error", MB_ICONERROR);
        return;
    }

    /* Cache access */
    int cache_hit = sys_cache_access(acc_id);
    trace_log("Cache page %d: %s", acc_id, cache_hit ? "HIT" : "MISS");

    /* Update file */
    int fd = sys_open(acc->file_id, 2, pid);
    char data[256];
    acc->balance += amount;
    int dlen = snprintf(data, sizeof(data), "%d|%s|%.2f",
                        acc->id, acc->holder, acc->balance);
    sys_write(fd, data, dlen);
    sys_close(fd);
    trace_log("File write: %d bytes to fd=%d (balance updated)", dlen, fd);

    /* Disk I/O */
    int track = (acc_id * 7 + 3) % OS_DISK_SIZE;
    sys_io_request(track);
    sys_io_process();
    trace_log("Disk I/O: track %d", track);

    /* Release lock */
    sys_unlock(pid, acc_id, 1);
    trace_log("sys_unlock(P%d, res=%d): released", pid, acc_id);

    /* Cleanup */
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_log("P%d TERMINATED. Deposited $%.2f to Account #%d (new bal: $%.2f)",
              pid, amount, acc_id, acc->balance);
    trace_log("---");

    refresh_accounts();
    update_trace_display();

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Deposited $%.2f to Account #%d\nNew Balance: $%.2f",
             amount, acc_id, acc->balance);
    MessageBoxA(g_hWnd, msg, "Deposit Successful", MB_ICONINFORMATION);
}

/* ============================================================
 *  Banking: Withdraw
 * ============================================================ */
static void do_withdraw(int acc_id, double amount)
{
    GuiAccount *acc = find_account(acc_id);
    if (!acc) {
        MessageBoxA(g_hWnd, "Account not found or inactive.",
                    "Error", MB_ICONERROR);
        return;
    }
    if (amount <= 0.0) {
        MessageBoxA(g_hWnd, "Withdrawal amount must be positive.",
                    "Error", MB_ICONERROR);
        return;
    }
    if (acc->balance < amount) {
        MessageBoxA(g_hWnd, "Insufficient funds.",
                    "Error", MB_ICONERROR);
        return;
    }

    int pid = sys_fork("withdraw", 4, 8, NULL, NULL);
    trace_log("SYSCALL: sys_fork('withdraw', pri=4)");
    trace_log("Process P%d created (NEW)", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_log("P%d: NEW -> READY -> RUNNING", pid);

    int mem = sys_alloc(pid, 1);
    trace_log("Memory: 1 KB at addr %d", mem);

    /* Lock account */
    sys_set_max_need(pid, acc_id, 1);
    int r = sys_lock(pid, acc_id, 1);
    trace_log("sys_lock(P%d, res=%d): %s",
              pid, acc_id, r == 0 ? "GRANTED (Banker's: SAFE)" : "DENIED");
    if (r < 0) {
        sys_free_all(pid);
        sys_set_state(pid, PROC_TERMINATED);
        trace_log("P%d TERMINATED (lock denied).", pid);
        trace_log("---");
        update_trace_display();
        MessageBoxA(g_hWnd, "Could not acquire lock on account.",
                    "Error", MB_ICONERROR);
        return;
    }

    /* Cache */
    int cache_hit = sys_cache_access(acc_id);
    trace_log("Cache page %d: %s", acc_id, cache_hit ? "HIT" : "MISS");

    /* Update file */
    int fd = sys_open(acc->file_id, 2, pid);
    char data[256];
    acc->balance -= amount;
    int dlen = snprintf(data, sizeof(data), "%d|%s|%.2f",
                        acc->id, acc->holder, acc->balance);
    sys_write(fd, data, dlen);
    sys_close(fd);
    trace_log("File write: %d bytes (balance updated)", dlen);

    /* Disk I/O */
    int track = (acc_id * 7 + 5) % OS_DISK_SIZE;
    sys_io_request(track);
    sys_io_process();
    trace_log("Disk I/O: track %d", track);

    /* Release */
    sys_unlock(pid, acc_id, 1);
    trace_log("sys_unlock(P%d, res=%d): released", pid, acc_id);

    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_log("P%d TERMINATED. Withdrew $%.2f from Account #%d (new bal: $%.2f)",
              pid, amount, acc_id, acc->balance);
    trace_log("---");

    refresh_accounts();
    update_trace_display();

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Withdrew $%.2f from Account #%d\nNew Balance: $%.2f",
             amount, acc_id, acc->balance);
    MessageBoxA(g_hWnd, msg, "Withdrawal Successful", MB_ICONINFORMATION);
}

/* ============================================================
 *  Banking: Transfer (with resource ordering for deadlock prevention)
 * ============================================================ */
static void do_transfer(int from_id, int to_id, double amount)
{
    if (from_id == to_id) {
        MessageBoxA(g_hWnd, "Cannot transfer to the same account.",
                    "Error", MB_ICONERROR);
        return;
    }
    GuiAccount *from_acc = find_account(from_id);
    GuiAccount *to_acc   = find_account(to_id);
    if (!from_acc || !to_acc) {
        MessageBoxA(g_hWnd, "One or both accounts not found.",
                    "Error", MB_ICONERROR);
        return;
    }
    if (amount <= 0.0) {
        MessageBoxA(g_hWnd, "Transfer amount must be positive.",
                    "Error", MB_ICONERROR);
        return;
    }
    if (from_acc->balance < amount) {
        MessageBoxA(g_hWnd, "Insufficient funds in source account.",
                    "Error", MB_ICONERROR);
        return;
    }

    int pid = sys_fork("transfer", 2, 15, NULL, NULL);
    trace_log("SYSCALL: sys_fork('transfer', pri=2)");
    trace_log("Process P%d created (NEW)", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_log("P%d: NEW -> READY -> RUNNING", pid);

    int mem = sys_alloc(pid, 2);
    trace_log("Memory: 2 KB at addr %d", mem);

    /* Resource ordering: lock lower-numbered account first */
    int first  = (from_id < to_id) ? from_id : to_id;
    int second = (from_id < to_id) ? to_id   : from_id;

    trace_log("Resource ordering: locking #%d then #%d", first, second);

    sys_set_max_need(pid, first, 1);
    sys_set_max_need(pid, second, 1);

    int r1 = sys_lock(pid, first, 1);
    trace_log("sys_lock(P%d, res=%d): %s",
              pid, first,
              r1 == 0 ? "GRANTED (Banker's: SAFE)" : "DENIED");
    if (r1 < 0) {
        sys_free_all(pid);
        sys_set_state(pid, PROC_TERMINATED);
        trace_log("P%d TERMINATED (lock denied on first resource).", pid);
        trace_log("---");
        update_trace_display();
        MessageBoxA(g_hWnd, "Could not acquire lock. Transfer aborted.",
                    "Error", MB_ICONERROR);
        return;
    }

    int r2 = sys_lock(pid, second, 1);
    trace_log("sys_lock(P%d, res=%d): %s",
              pid, second,
              r2 == 0 ? "GRANTED" : "DENIED - potential deadlock!");
    if (r2 < 0) {
        sys_unlock(pid, first, 1);
        trace_log("sys_unlock(P%d, res=%d): released (rollback)", pid, first);
        sys_free_all(pid);
        sys_set_state(pid, PROC_TERMINATED);
        trace_log("P%d TERMINATED (lock denied on second resource).", pid);
        trace_log("---");
        update_trace_display();
        MessageBoxA(g_hWnd,
                    "Could not lock both accounts. Transfer aborted.",
                    "Error", MB_ICONERROR);
        return;
    }

    /* Cache both accounts */
    int c1 = sys_cache_access(from_id);
    trace_log("Cache page %d: %s", from_id, c1 ? "HIT" : "MISS");
    int c2 = sys_cache_access(to_id);
    trace_log("Cache page %d: %s", to_id, c2 ? "HIT" : "MISS");

    /* Perform transfer */
    from_acc->balance -= amount;
    to_acc->balance   += amount;

    /* Update source file */
    int fd1 = sys_open(from_acc->file_id, 2, pid);
    char data1[256];
    int dlen1 = snprintf(data1, sizeof(data1), "%d|%s|%.2f",
                         from_acc->id, from_acc->holder, from_acc->balance);
    sys_write(fd1, data1, dlen1);
    sys_close(fd1);
    trace_log("File write: Account #%d updated (%d bytes)", from_id, dlen1);

    /* Update destination file */
    int fd2 = sys_open(to_acc->file_id, 2, pid);
    char data2[256];
    int dlen2 = snprintf(data2, sizeof(data2), "%d|%s|%.2f",
                         to_acc->id, to_acc->holder, to_acc->balance);
    sys_write(fd2, data2, dlen2);
    sys_close(fd2);
    trace_log("File write: Account #%d updated (%d bytes)", to_id, dlen2);

    /* Disk I/O */
    int track1 = (from_id * 7 + 11) % OS_DISK_SIZE;
    int track2 = (to_id * 7 + 11) % OS_DISK_SIZE;
    sys_io_request(track1);
    sys_io_request(track2);
    sys_io_process();
    sys_io_process();
    trace_log("Disk I/O: tracks %d, %d", track1, track2);

    /* Release both locks (reverse order) */
    sys_unlock(pid, second, 1);
    trace_log("sys_unlock(P%d, res=%d): released", pid, second);
    sys_unlock(pid, first, 1);
    trace_log("sys_unlock(P%d, res=%d): released", pid, first);

    /* Cleanup */
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_log("P%d TERMINATED. Transferred $%.2f: #%d -> #%d",
              pid, amount, from_id, to_id);
    trace_log("  #%d new bal: $%.2f   #%d new bal: $%.2f",
              from_id, from_acc->balance, to_id, to_acc->balance);
    trace_log("---");

    refresh_accounts();
    update_trace_display();

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Transferred $%.2f\n"
             "From #%d (%s): $%.2f\n"
             "To #%d (%s): $%.2f",
             amount,
             from_id, from_acc->holder, from_acc->balance,
             to_id,   to_acc->holder,   to_acc->balance);
    MessageBoxA(g_hWnd, msg, "Transfer Successful", MB_ICONINFORMATION);
}

/* ============================================================
 *  Banking: Check Balance
 * ============================================================ */
static void do_check_balance(int acc_id)
{
    GuiAccount *acc = find_account(acc_id);
    if (!acc) {
        MessageBoxA(g_hWnd, "Account not found or inactive.",
                    "Error", MB_ICONERROR);
        return;
    }

    int pid = sys_fork("check_balance", 5, 4, NULL, NULL);
    trace_log("SYSCALL: sys_fork('check_balance', pri=5)");
    trace_log("Process P%d created (NEW)", pid);

    sys_set_state(pid, PROC_READY);
    sys_set_state(pid, PROC_RUNNING);
    trace_log("P%d: NEW -> READY -> RUNNING", pid);

    int mem = sys_alloc(pid, 1);
    trace_log("Memory: 1 KB at addr %d", mem);

    /* Cache access */
    int cache_hit = sys_cache_access(acc_id);
    trace_log("Cache page %d: %s", acc_id, cache_hit ? "HIT" : "MISS");

    /* Read file */
    int fd = sys_open(acc->file_id, 1, pid);
    char rbuf[256];
    int nread = sys_read(fd, rbuf, sizeof(rbuf) - 1);
    if (nread > 0) rbuf[nread] = '\0';
    else           rbuf[0] = '\0';
    sys_close(fd);
    trace_log("File read: %d bytes from fd=%d -> '%s'", nread, fd, rbuf);

    /* Disk I/O */
    int track = (acc_id * 7 + 1) % OS_DISK_SIZE;
    sys_io_request(track);
    sys_io_process();
    trace_log("Disk I/O: read track %d", track);

    /* Cleanup */
    sys_free_all(pid);
    sys_set_state(pid, PROC_TERMINATED);
    trace_log("P%d TERMINATED. Balance query for Account #%d", pid, acc_id);
    trace_log("---");

    update_trace_display();

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Account #%d\nHolder: %s\nBalance: $%.2f",
             acc->id, acc->holder, acc->balance);
    MessageBoxA(g_hWnd, msg, "Account Balance", MB_ICONINFORMATION);
}

/* ============================================================
 *  Command Handler
 * ============================================================ */
static void handle_command(HWND hwnd, int id)
{
    char buf_name[128], buf_amount[64], buf_accid[32];
    char buf_from[32], buf_to[32], buf_tamt[64];

    switch (id) {
    case IDC_BTN_CREATE:
        GetWindowTextA(hEditName, buf_name, sizeof(buf_name));
        GetWindowTextA(hEditAmount, buf_amount, sizeof(buf_amount));
        {
            double initial = atof(buf_amount);
            do_create_account(buf_name, initial);
        }
        break;

    case IDC_BTN_BALANCE:
        GetWindowTextA(hEditAccId, buf_accid, sizeof(buf_accid));
        {
            int acc_id = atoi(buf_accid);
            if (acc_id <= 0) {
                MessageBoxA(hwnd, "Please enter a valid Account ID.",
                            "Error", MB_ICONERROR);
                return;
            }
            do_check_balance(acc_id);
        }
        break;

    case IDC_BTN_DEPOSIT:
        GetWindowTextA(hEditAccId, buf_accid, sizeof(buf_accid));
        GetWindowTextA(hEditAmount, buf_amount, sizeof(buf_amount));
        {
            int acc_id = atoi(buf_accid);
            double amount = atof(buf_amount);
            if (acc_id <= 0) {
                MessageBoxA(hwnd, "Please enter a valid Account ID.",
                            "Error", MB_ICONERROR);
                return;
            }
            do_deposit(acc_id, amount);
        }
        break;

    case IDC_BTN_WITHDRAW:
        GetWindowTextA(hEditAccId, buf_accid, sizeof(buf_accid));
        GetWindowTextA(hEditAmount, buf_amount, sizeof(buf_amount));
        {
            int acc_id = atoi(buf_accid);
            double amount = atof(buf_amount);
            if (acc_id <= 0) {
                MessageBoxA(hwnd, "Please enter a valid Account ID.",
                            "Error", MB_ICONERROR);
                return;
            }
            do_withdraw(acc_id, amount);
        }
        break;

    case IDC_BTN_TRANSFER:
        GetWindowTextA(hEditFromAcc, buf_from, sizeof(buf_from));
        GetWindowTextA(hEditToAcc, buf_to, sizeof(buf_to));
        GetWindowTextA(hEditTransAmt, buf_tamt, sizeof(buf_tamt));
        {
            int from_id = atoi(buf_from);
            int to_id   = atoi(buf_to);
            double amount = atof(buf_tamt);
            if (from_id <= 0 || to_id <= 0) {
                MessageBoxA(hwnd,
                    "Please enter valid From and To account IDs.",
                    "Error", MB_ICONERROR);
                return;
            }
            do_transfer(from_id, to_id, amount);
        }
        break;

    case IDC_BTN_REFRESH:
        refresh_dashboard();
        break;

    case IDC_BTN_APPLY:
        apply_config();
        break;

    case IDC_BTN_CLEAR_LOG:
        trace_clear();
        update_trace_display();
        break;
    }
}

/* ============================================================
 *  Window Procedure
 * ============================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE:
        create_tabs(hwnd);
        refresh_accounts();
        trace_log("TaskForge v2 GUI started");
        trace_log("Kernel initialized. Memory: %d KB, Cache: %d entries",
                  OS_MEMORY_SIZE, OS_CACHE_SIZE);
        trace_log("Dirs created: /accounts (node %d), /logs (node %d)",
                  accounts_dir, logs_dir);
        trace_log("---");
        return 0;

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        resize_panels(w, h);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = WND_MIN_W;
        mmi->ptMinTrackSize.y = WND_MIN_H;
        return 0;
    }

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            handle_command(hwnd, LOWORD(wParam));
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lParam;
        if (nm->idFrom == IDC_TABCTRL && nm->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(hTabCtrl);
            switch_tab(sel);
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        /* Dark background for dashboard and trace multiline edits */
        if (hCtl == hDashText || hCtl == hTraceText) {
            SetBkColor(hdc, CLR_DARK_BG);
            SetTextColor(hdc, CLR_LIGHT_TEXT);
            return (LRESULT)hBrushDark;
        }
        /* Green text for accounts list on default bg */
        if (hCtl == hAccountsList) {
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, RGB(0, 80, 0));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        break;
    }

    /* Read-only edit controls send WM_CTLCOLORSTATIC,
       but just in case, also handle WM_CTLCOLOREDIT */
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        if (hCtl == hDashText || hCtl == hTraceText) {
            SetBkColor(hdc, CLR_DARK_BG);
            SetTextColor(hdc, CLR_LIGHT_TEXT);
            return (LRESULT)hBrushDark;
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
