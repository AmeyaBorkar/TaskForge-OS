/*=============================================================================
 *  TaskForge - OS Concepts Engine (Win32 GUI)
 *  Single-file implementation with 5 module tabs:
 *    1. CPU Scheduling   2. Deadlock (Banker's)   3. Memory (Page Replacement)
 *    4. Disk Scheduling  5. File Operations
 *
 *  Compile:
 *    gcc -Wall -Wextra -std=c11 -o taskforge_gui.exe gui/taskforge_gui.c \
 *        -lcomctl32 -lgdi32 -lcomdlg32 -mwindows -static
 *=============================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")

/* Fallback for older MinGW headers that might not define WC_TABCONTROLA */
#ifndef WC_TABCONTROLA
#define WC_TABCONTROLA "SysTabControl32"
#endif

/* ---- Geometry constants ------------------------------------------------- */
#define WND_WIDTH   1100
#define WND_HEIGHT  720
#define TAB_HEIGHT  28
#define LEFT_W      420
#define MARGIN      10
#define LBL_H       18
#define ED_H        22
#define BTN_H       28
#define COMBO_H     200

/* ---- Control IDs -------------------------------------------------------- */
enum {
    IDC_TAB = 1000,
    /* CPU Scheduling */
    IDC_CPU_NPROC = 1100, IDC_CPU_PROCS, IDC_CPU_ALGO, IDC_CPU_QUANTUM,
    IDC_CPU_RUN,  IDC_CPU_OUTPUT,
    /* Deadlock */
    IDC_DL_N = 1200, IDC_DL_M, IDC_DL_AVAIL, IDC_DL_ALLOC, IDC_DL_MAX,
    IDC_DL_CHECK, IDC_DL_REQPID, IDC_DL_REQVEC, IDC_DL_REQUEST,
    IDC_DL_OUTPUT,
    /* Memory */
    IDC_MEM_FRAMES = 1300, IDC_MEM_REFSTR, IDC_MEM_ALGO, IDC_MEM_RUN,
    IDC_MEM_OUTPUT,
    /* Disk */
    IDC_DSK_SIZE = 1400, IDC_DSK_HEAD, IDC_DSK_QUEUE, IDC_DSK_DIR,
    IDC_DSK_ALGO, IDC_DSK_RUN, IDC_DSK_OUTPUT,
    /* File */
    IDC_FIL_OP = 1500, IDC_FIL_IN, IDC_FIL_BROWSE_IN, IDC_FIL_OUT,
    IDC_FIL_BROWSE_OUT, IDC_FIL_KEY, IDC_FIL_EXEC, IDC_FIL_OUTPUT
};

/* ---- Global state ------------------------------------------------------- */
static HINSTANCE g_hInst;
static HWND      g_hMain, g_hTab;
static HWND      g_hPanels[5];          /* one panel per tab */
static HFONT     g_hMonoFont;
static HFONT     g_hUIFont;

static char out_buf[65536];
static int  out_len;

static void out_clear(void) { out_buf[0] = '\0'; out_len = 0; }

static void out_append(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out_buf + out_len,
                      (int)sizeof(out_buf) - out_len, fmt, ap);
    if (n > 0) out_len += n;
    va_end(ap);
}

/* ---- Forward declarations ----------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void CreateTabs(HWND parent);
static void CreateCPUPanel(HWND parent);
static void CreateDeadlockPanel(HWND parent);
static void CreateMemoryPanel(HWND parent);
static void CreateDiskPanel(HWND parent);
static void CreateFilePanel(HWND parent);
static void SwitchTab(int idx);
static void ResizeAll(int cw, int ch);

static void RunCPUScheduling(void);
static void RunDeadlockCheck(void);
static void RunDeadlockRequest(void);
static void RunPageReplacement(void);
static void RunDiskScheduling(void);
static void RunFileOperation(void);

/* ---- Helper: create label (static text) --------------------------------- */
static HWND MakeLabel(HWND par, const char *txt, int x, int y, int w, int h) {
    HWND h2 = CreateWindowA("STATIC", txt,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, par, NULL, g_hInst, NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
    return h2;
}

/* ---- Helper: create single-line edit ------------------------------------ */
static HWND MakeEdit(HWND par, int id, int x, int y, int w, int h,
                     DWORD extra) {
    HWND h2 = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | extra,
        x, y, w, h, par, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
    return h2;
}

/* ---- Helper: create multiline edit -------------------------------------- */
static HWND MakeMultiEdit(HWND par, int id, int x, int y, int w, int h,
                          DWORD extra) {
    HWND h2 = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | extra,
        x, y, w, h, par, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
    return h2;
}

/* ---- Helper: create output (read-only multiline, monospaced) ------------ */
static HWND MakeOutput(HWND par, int id, int x, int y, int w, int h) {
    HWND h2 = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        x, y, w, h, par, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hMonoFont, TRUE);
    return h2;
}

/* ---- Helper: create combo box ------------------------------------------- */
static HWND MakeCombo(HWND par, int id, int x, int y, int w,
                      const char **items, int count) {
    HWND h2 = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, COMBO_H, par, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
    for (int i = 0; i < count; i++)
        SendMessageA(h2, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageA(h2, CB_SETCURSEL, 0, 0);
    return h2;
}

/* ---- Helper: create button ---------------------------------------------- */
static HWND MakeButton(HWND par, int id, const char *txt,
                       int x, int y, int w, int h, DWORD style) {
    HWND h2 = CreateWindowA("BUTTON", txt,
        WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, par, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
    return h2;
}

/* ---- Helper: create group box ------------------------------------------- */
static HWND MakeGroup(HWND par, const char *txt, int x, int y, int w, int h) {
    HWND h2 = CreateWindowA("BUTTON", txt,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, w, h, par, NULL, g_hInst, NULL);
    SendMessageA(h2, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
    return h2;
}

/* ---- Helper: get edit text ---------------------------------------------- */
static void GetEditText(HWND parent, int id, char *buf, int sz) {
    HWND hCtl = GetDlgItem(parent, id);
    if (hCtl) GetWindowTextA(hCtl, buf, sz);
    else buf[0] = '\0';
}

/* ---- Helper: get combo selection index ---------------------------------- */
static int GetComboSel(HWND parent, int id) {
    HWND hCtl = GetDlgItem(parent, id);
    if (!hCtl) return 0;
    int r = (int)SendMessageA(hCtl, CB_GETCURSEL, 0, 0);
    return (r == CB_ERR) ? 0 : r;
}

/* ---- Helper: browse file ------------------------------------------------ */
static void BrowseFile(HWND parent, int editId, BOOL forSave) {
    char path[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = parent;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = "All Files\0*.*\0Text Files\0*.txt\0";
    ofn.Flags       = OFN_EXPLORER | OFN_PATHMUSTEXIST;
    if (!forSave) ofn.Flags |= OFN_FILEMUSTEXIST;

    BOOL ok = forSave ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (ok) SetDlgItemTextA(parent, editId, path);
}

/*=============================================================================
 *  WinMain
 *===========================================================================*/
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;
    g_hInst = hInst;

    /* Common controls */
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icex);

    /* Register window class */
    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "TaskForgeGUI";
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    /* Create main window */
    g_hMain = CreateWindowA("TaskForgeGUI",
        "TaskForge - OS Concepts Engine",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, WND_WIDTH, WND_HEIGHT,
        NULL, NULL, hInst, NULL);

    ShowWindow(g_hMain, nShow);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(g_hMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return (int)msg.wParam;
}

/*=============================================================================
 *  Main WndProc
 *===========================================================================*/
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        /* Create fonts */
        g_hMonoFont = CreateFontA(
            16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        g_hUIFont = CreateFontA(
            15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        CreateTabs(hwnd);
        CreateCPUPanel(hwnd);
        CreateDeadlockPanel(hwnd);
        CreateMemoryPanel(hwnd);
        CreateDiskPanel(hwnd);
        CreateFilePanel(hwnd);
        SwitchTab(0);
        return 0;

    case WM_SIZE: {
        int cw = LOWORD(lParam), ch = HIWORD(lParam);
        ResizeAll(cw, ch);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lParam;
        if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) {
            int sel = (int)SendMessageA(g_hTab, TCM_GETCURSEL, 0, 0);
            SwitchTab(sel);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case IDC_CPU_RUN:        RunCPUScheduling();     break;
        case IDC_DL_CHECK:       RunDeadlockCheck();     break;
        case IDC_DL_REQUEST:     RunDeadlockRequest();   break;
        case IDC_MEM_RUN:        RunPageReplacement();   break;
        case IDC_DSK_RUN:        RunDiskScheduling();    break;
        case IDC_FIL_EXEC:       RunFileOperation();     break;
        case IDC_FIL_BROWSE_IN:  BrowseFile(g_hPanels[4], IDC_FIL_IN, FALSE);  break;
        case IDC_FIL_BROWSE_OUT: BrowseFile(g_hPanels[4], IDC_FIL_OUT, TRUE);  break;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_DESTROY:
        if (g_hMonoFont) DeleteObject(g_hMonoFont);
        if (g_hUIFont)   DeleteObject(g_hUIFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/*=============================================================================
 *  Tab creation and switching
 *===========================================================================*/
static void CreateTabs(HWND parent)
{
    g_hTab = CreateWindowA(WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, WND_WIDTH, TAB_HEIGHT + 4,
        parent, (HMENU)(INT_PTR)IDC_TAB, g_hInst, NULL);
    SendMessageA(g_hTab, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);

    const char *names[] = {
        "CPU Scheduling", "Deadlock (Banker's)",
        "Memory (Page Repl.)", "Disk Scheduling", "File Operations"
    };
    for (int i = 0; i < 5; i++) {
        TCITEMA ti = {0};
        ti.mask    = TCIF_TEXT;
        ti.pszText = (LPSTR)names[i];
        SendMessageA(g_hTab, TCM_INSERTITEMA, i, (LPARAM)&ti);
    }
}

static HWND MakePanel(HWND parent, int idx) {
    HWND h = CreateWindowA("STATIC", "",
        WS_CHILD | WS_CLIPCHILDREN | SS_LEFT,
        0, TAB_HEIGHT + 4, WND_WIDTH, WND_HEIGHT - TAB_HEIGHT - 4,
        parent, NULL, g_hInst, NULL);
    g_hPanels[idx] = h;
    return h;
}

static void SwitchTab(int idx) {
    for (int i = 0; i < 5; i++)
        ShowWindow(g_hPanels[i], (i == idx) ? SW_SHOW : SW_HIDE);
}

/*=============================================================================
 *  Resize handler
 *===========================================================================*/
static void ResizeAll(int cw, int ch)
{
    MoveWindow(g_hTab, 0, 0, cw, TAB_HEIGHT + 4, TRUE);

    int panelH = ch - TAB_HEIGHT - 4;
    for (int i = 0; i < 5; i++)
        MoveWindow(g_hPanels[i], 0, TAB_HEIGHT + 4, cw, panelH, TRUE);

    /* Resize output edit controls to fill right side */
    int outX = LEFT_W + MARGIN;
    int outW = cw - outX - MARGIN;
    int outH = panelH - MARGIN * 2;
    if (outW < 100) outW = 100;
    if (outH < 100) outH = 100;

    int outputIds[] = { IDC_CPU_OUTPUT, IDC_DL_OUTPUT, IDC_MEM_OUTPUT,
                        IDC_DSK_OUTPUT, IDC_FIL_OUTPUT };
    for (int i = 0; i < 5; i++) {
        HWND hOut = GetDlgItem(g_hPanels[i], outputIds[i]);
        if (hOut) MoveWindow(hOut, outX, MARGIN, outW, outH, TRUE);
    }
}

/*=============================================================================
 *  Tab 1: CPU Scheduling Panel
 *===========================================================================*/
static void CreateCPUPanel(HWND parent)
{
    HWND p = MakePanel(parent, 0);
    int x = MARGIN, y = MARGIN;

    MakeGroup(p, "CPU Scheduling Input", x, y, LEFT_W - MARGIN, 520);
    y += 20;

    MakeLabel(p, "Number of processes (1-20):", x+10, y, 250, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_CPU_NPROC, x+10, y, 80, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_CPU_NPROC, "3");
    y += ED_H + 8;

    MakeLabel(p, "Process data (one per line: arrival,burst,priority):",
              x+10, y, 380, LBL_H);
    y += LBL_H;
    MakeMultiEdit(p, IDC_CPU_PROCS, x+10, y, LEFT_W - 40, 140, 0);
    SetDlgItemTextA(p, IDC_CPU_PROCS, "0,5,2\r\n1,3,1\r\n2,8,3");
    y += 148;

    MakeLabel(p, "Algorithm:", x+10, y, 80, LBL_H);
    y += LBL_H;
    const char *cpuAlgos[] = {
        "FCFS", "SJF Non-Preemptive", "SRTF (SJF Preemptive)",
        "Round Robin", "Priority Non-Preemptive", "Priority Preemptive",
        "Compare All"
    };
    MakeCombo(p, IDC_CPU_ALGO, x+10, y, 250, cpuAlgos, 7);
    y += ED_H + 8;

    MakeLabel(p, "Time Quantum (for RR):", x+10, y, 200, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_CPU_QUANTUM, x+10, y, 80, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_CPU_QUANTUM, "2");
    y += ED_H + 14;

    MakeButton(p, IDC_CPU_RUN, "Run Scheduling", x+10, y, 160, BTN_H + 4,
               BS_DEFPUSHBUTTON);

    /* Output on the right */
    MakeOutput(p, IDC_CPU_OUTPUT, LEFT_W + MARGIN, MARGIN, 600, 500);
}

/*=============================================================================
 *  Tab 2: Deadlock (Banker's) Panel
 *===========================================================================*/
static void CreateDeadlockPanel(HWND parent)
{
    HWND p = MakePanel(parent, 1);
    int x = MARGIN, y = MARGIN;

    MakeGroup(p, "Banker's Algorithm Input", x, y, LEFT_W - MARGIN, 560);
    y += 20;

    MakeLabel(p, "Processes (n):", x+10, y, 120, LBL_H);
    MakeEdit(p, IDC_DL_N, x+130, y, 60, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_DL_N, "5");
    y += ED_H + 6;

    MakeLabel(p, "Resource types (m):", x+10, y, 140, LBL_H);
    MakeEdit(p, IDC_DL_M, x+160, y, 60, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_DL_M, "3");
    y += ED_H + 6;

    MakeLabel(p, "Available vector (comma-sep):", x+10, y, 220, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_DL_AVAIL, x+10, y, 250, ED_H, 0);
    SetDlgItemTextA(p, IDC_DL_AVAIL, "3,3,2");
    y += ED_H + 6;

    MakeLabel(p, "Allocation matrix (one row/line, comma-sep):", x+10, y,
              350, LBL_H);
    y += LBL_H;
    MakeMultiEdit(p, IDC_DL_ALLOC, x+10, y, LEFT_W - 40, 90, 0);
    SetDlgItemTextA(p, IDC_DL_ALLOC,
                    "0,1,0\r\n2,0,0\r\n3,0,2\r\n2,1,1\r\n0,0,2");
    y += 98;

    MakeLabel(p, "Max matrix (one row/line, comma-sep):", x+10, y, 350, LBL_H);
    y += LBL_H;
    MakeMultiEdit(p, IDC_DL_MAX, x+10, y, LEFT_W - 40, 90, 0);
    SetDlgItemTextA(p, IDC_DL_MAX,
                    "7,5,3\r\n3,2,2\r\n9,0,2\r\n2,2,2\r\n4,3,3");
    y += 98;

    MakeButton(p, IDC_DL_CHECK, "Check Safety", x+10, y, 140, BTN_H + 4,
               BS_DEFPUSHBUTTON);
    y += BTN_H + 14;

    MakeGroup(p, "Resource Request", x+10, y, LEFT_W - 30, 90);
    y += 20;
    MakeLabel(p, "Process ID:", x+20, y, 80, LBL_H);
    MakeEdit(p, IDC_DL_REQPID, x+105, y, 40, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_DL_REQPID, "1");
    y += ED_H + 4;
    MakeLabel(p, "Request vec:", x+20, y, 90, LBL_H);
    MakeEdit(p, IDC_DL_REQVEC, x+115, y, 150, ED_H, 0);
    SetDlgItemTextA(p, IDC_DL_REQVEC, "1,0,2");
    MakeButton(p, IDC_DL_REQUEST, "Request", x+280, y-2, 100, BTN_H,
               BS_PUSHBUTTON);

    MakeOutput(p, IDC_DL_OUTPUT, LEFT_W + MARGIN, MARGIN, 600, 500);
}

/*=============================================================================
 *  Tab 3: Memory (Page Replacement) Panel
 *===========================================================================*/
static void CreateMemoryPanel(HWND parent)
{
    HWND p = MakePanel(parent, 2);
    int x = MARGIN, y = MARGIN;

    MakeGroup(p, "Page Replacement Input", x, y, LEFT_W - MARGIN, 320);
    y += 20;

    MakeLabel(p, "Number of frames (1-10):", x+10, y, 200, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_MEM_FRAMES, x+10, y, 80, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_MEM_FRAMES, "3");
    y += ED_H + 8;

    MakeLabel(p, "Reference string (space-separated):", x+10, y, 300, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_MEM_REFSTR, x+10, y, LEFT_W - 40, ED_H, 0);
    SetDlgItemTextA(p, IDC_MEM_REFSTR, "7 0 1 2 0 3 0 4 2 3 0 3");
    y += ED_H + 8;

    MakeLabel(p, "Algorithm:", x+10, y, 80, LBL_H);
    y += LBL_H;
    const char *memAlgos[] = {
        "FIFO", "LRU", "Optimal", "Clock", "Compare All"
    };
    MakeCombo(p, IDC_MEM_ALGO, x+10, y, 220, memAlgos, 5);
    y += ED_H + 14;

    MakeButton(p, IDC_MEM_RUN, "Run Page Replacement", x+10, y,
               200, BTN_H + 4, BS_DEFPUSHBUTTON);

    MakeOutput(p, IDC_MEM_OUTPUT, LEFT_W + MARGIN, MARGIN, 600, 500);
}

/*=============================================================================
 *  Tab 4: Disk Scheduling Panel
 *===========================================================================*/
static void CreateDiskPanel(HWND parent)
{
    HWND p = MakePanel(parent, 3);
    int x = MARGIN, y = MARGIN;

    MakeGroup(p, "Disk Scheduling Input", x, y, LEFT_W - MARGIN, 380);
    y += 20;

    MakeLabel(p, "Disk size (cylinders):", x+10, y, 180, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_DSK_SIZE, x+10, y, 80, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_DSK_SIZE, "200");
    y += ED_H + 8;

    MakeLabel(p, "Initial head position:", x+10, y, 180, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_DSK_HEAD, x+10, y, 80, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_DSK_HEAD, "53");
    y += ED_H + 8;

    MakeLabel(p, "Request queue (comma-separated):", x+10, y, 300, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_DSK_QUEUE, x+10, y, LEFT_W - 40, ED_H, 0);
    SetDlgItemTextA(p, IDC_DSK_QUEUE, "98,183,37,122,14,124,65,67");
    y += ED_H + 8;

    MakeLabel(p, "Direction:", x+10, y, 80, LBL_H);
    y += LBL_H;
    const char *dirs[] = { "Toward 0", "Toward Max" };
    MakeCombo(p, IDC_DSK_DIR, x+10, y, 180, dirs, 2);
    y += ED_H + 8;

    MakeLabel(p, "Algorithm:", x+10, y, 80, LBL_H);
    y += LBL_H;
    const char *dskAlgos[] = {
        "FCFS", "SSTF", "SCAN", "C-SCAN", "Compare All"
    };
    MakeCombo(p, IDC_DSK_ALGO, x+10, y, 220, dskAlgos, 5);
    y += ED_H + 14;

    MakeButton(p, IDC_DSK_RUN, "Run Disk Scheduling", x+10, y,
               200, BTN_H + 4, BS_DEFPUSHBUTTON);

    MakeOutput(p, IDC_DSK_OUTPUT, LEFT_W + MARGIN, MARGIN, 600, 500);
}

/*=============================================================================
 *  Tab 5: File Operations Panel
 *===========================================================================*/
static void CreateFilePanel(HWND parent)
{
    HWND p = MakePanel(parent, 4);
    int x = MARGIN, y = MARGIN;

    MakeGroup(p, "File Operations Input", x, y, LEFT_W - MARGIN, 340);
    y += 20;

    MakeLabel(p, "Operation:", x+10, y, 80, LBL_H);
    y += LBL_H;
    const char *ops[] = {
        "Word Count", "Checksum (CRC-like)", "File Copy",
        "File Encrypt/Decrypt (XOR)"
    };
    MakeCombo(p, IDC_FIL_OP, x+10, y, 260, ops, 4);
    y += ED_H + 8;

    MakeLabel(p, "Input file path:", x+10, y, 120, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_FIL_IN, x+10, y, LEFT_W - 120, ED_H, 0);
    MakeButton(p, IDC_FIL_BROWSE_IN, "Browse...", LEFT_W - 100, y, 80,
               ED_H + 2, BS_PUSHBUTTON);
    y += ED_H + 8;

    MakeLabel(p, "Output file path (copy/encrypt):", x+10, y, 250, LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_FIL_OUT, x+10, y, LEFT_W - 120, ED_H, 0);
    MakeButton(p, IDC_FIL_BROWSE_OUT, "Browse...", LEFT_W - 100, y, 80,
               ED_H + 2, BS_PUSHBUTTON);
    y += ED_H + 8;

    MakeLabel(p, "Key (for encrypt, single byte 0-255):", x+10, y, 280,
              LBL_H);
    y += LBL_H;
    MakeEdit(p, IDC_FIL_KEY, x+10, y, 80, ED_H, ES_NUMBER);
    SetDlgItemTextA(p, IDC_FIL_KEY, "42");
    y += ED_H + 14;

    MakeButton(p, IDC_FIL_EXEC, "Execute", x+10, y, 140, BTN_H + 4,
               BS_DEFPUSHBUTTON);

    MakeOutput(p, IDC_FIL_OUTPUT, LEFT_W + MARGIN, MARGIN, 600, 500);
}

/*=============================================================================
 *                        ALGORITHM IMPLEMENTATIONS
 *===========================================================================*/

/* ---- Sorting helpers ---------------------------------------------------- */
static void swap_int(int *a, int *b) { int t = *a; *a = *b; *b = t; }

/* ---- Parsing helpers ---------------------------------------------------- */
static int parse_csv_int(const char *s, int *arr, int maxn)
{
    int cnt = 0;
    while (*s && cnt < maxn) {
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '\0' || *s == '\r' || *s == '\n') break;
        arr[cnt++] = atoi(s);
        while (*s && *s != ',' && *s != '\r' && *s != '\n') s++;
        if (*s == ',') s++;
    }
    return cnt;
}

static int parse_lines(const char *text, int mat[][20], int maxRows,
                       int *cols)
{
    int row = 0;
    const char *p = text;
    while (*p && row < maxRows) {
        while (*p == '\r' || *p == '\n') p++;
        if (*p == '\0') break;
        char line[256];
        int li = 0;
        while (*p && *p != '\r' && *p != '\n' && li < 255)
            line[li++] = *p++;
        line[li] = '\0';
        if (li == 0) continue;
        int c = parse_csv_int(line, mat[row], 20);
        if (row == 0 && cols) *cols = c;
        row++;
    }
    return row;
}

static int parse_space_int(const char *s, int *arr, int maxn)
{
    int cnt = 0;
    while (*s && cnt < maxn) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        if (*s == '\0') break;
        arr[cnt++] = atoi(s);
        while (*s && *s != ' ' && *s != '\t' && *s != ',') s++;
    }
    return cnt;
}

/*=============================================================================
 *  CPU SCHEDULING
 *===========================================================================*/
typedef struct {
    int pid, arrival, burst, priority;
    int remaining;
    int start, finish, wait, tat, response;
    int started;
} CPUProc;

/* Gantt entry */
typedef struct {
    int pid;  /* 0 = idle */
    int start, end;
} GanttEntry;

static void cpu_print_results(CPUProc *p, int n, GanttEntry *g, int gn,
                              const char *algoName)
{
    out_append("=== %s ===\r\n\r\n", algoName);

    /* Gantt chart */
    out_append("Gantt Chart:\r\n  |");
    for (int i = 0; i < gn; i++) {
        int len = g[i].end - g[i].start;
        int pad = (len < 2) ? 3 : (len + 1);
        if (pad > 8) pad = 8;
        if (pad < 3) pad = 3;
        char label[16];
        if (g[i].pid == 0)
            snprintf(label, sizeof(label), "idle");
        else
            snprintf(label, sizeof(label), "P%d", g[i].pid);
        int lbl = (int)strlen(label);
        int left = (pad - lbl) / 2;
        int right = pad - lbl - left;
        for (int j = 0; j < left; j++) out_append(" ");
        out_append("%s", label);
        for (int j = 0; j < right; j++) out_append(" ");
        out_append("|");
    }
    out_append("\r\n  ");
    for (int i = 0; i < gn; i++) {
        int len = g[i].end - g[i].start;
        int pad = (len < 2) ? 3 : (len + 1);
        if (pad > 8) pad = 8;
        if (pad < 3) pad = 3;
        char num[16];
        snprintf(num, sizeof(num), "%d", g[i].start);
        out_append("%s", num);
        int nl = (int)strlen(num);
        for (int j = nl; j < pad + 1; j++) out_append(" ");
    }
    if (gn > 0) {
        char num[16];
        snprintf(num, sizeof(num), "%d", g[gn-1].end);
        out_append("%s", num);
    }
    out_append("\r\n\r\n");

    /* Table */
    out_append("%-5s %-8s %-7s %-8s %-7s %-7s %-6s %-6s %-8s\r\n",
               "PID", "Arrival", "Burst", "Priority", "Start", "Finish",
               "Wait", "TAT", "Response");
    out_append("--------------------------------------------------------------"
               "---\r\n");
    double tw = 0, tt = 0, tr = 0;
    for (int i = 0; i < n; i++) {
        out_append("P%-4d %-8d %-7d %-8d %-7d %-7d %-6d %-6d %-8d\r\n",
                   p[i].pid, p[i].arrival, p[i].burst, p[i].priority,
                   p[i].start, p[i].finish, p[i].wait, p[i].tat,
                   p[i].response);
        tw += p[i].wait;
        tt += p[i].tat;
        tr += p[i].response;
    }
    out_append("\r\nAvg Wait = %.2f   Avg TAT = %.2f   Avg Response = %.2f"
               "\r\n\r\n", tw/n, tt/n, tr/n);
}

static void cpu_fcfs(CPUProc *procs, int n)
{
    CPUProc p[20];
    memcpy(p, procs, n * sizeof(CPUProc));
    /* Sort by arrival then pid */
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (p[i].arrival > p[j].arrival ||
                (p[i].arrival == p[j].arrival && p[i].pid > p[j].pid)) {
                CPUProc tmp = p[i]; p[i] = p[j]; p[j] = tmp;
            }

    GanttEntry g[200]; int gn = 0;
    int time = 0;
    for (int i = 0; i < n; i++) {
        if (time < p[i].arrival) {
            g[gn].pid = 0; g[gn].start = time;
            g[gn].end = p[i].arrival; gn++;
            time = p[i].arrival;
        }
        p[i].start = time;
        p[i].response = time - p[i].arrival;
        p[i].finish = time + p[i].burst;
        p[i].tat = p[i].finish - p[i].arrival;
        p[i].wait = p[i].tat - p[i].burst;
        g[gn].pid = p[i].pid; g[gn].start = time;
        g[gn].end = p[i].finish; gn++;
        time = p[i].finish;
    }
    cpu_print_results(p, n, g, gn, "FCFS");
}

static void cpu_sjf_np(CPUProc *procs, int n)
{
    CPUProc p[20];
    memcpy(p, procs, n * sizeof(CPUProc));
    int done[20] = {0};
    GanttEntry g[200]; int gn = 0;
    int time = 0, completed = 0;

    while (completed < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (done[i] || p[i].arrival > time) continue;
            if (best == -1 || p[i].burst < p[best].burst ||
                (p[i].burst == p[best].burst &&
                 p[i].arrival < p[best].arrival))
                best = i;
        }
        if (best == -1) {
            int earliest = INT_MAX;
            for (int i = 0; i < n; i++)
                if (!done[i] && p[i].arrival < earliest)
                    earliest = p[i].arrival;
            g[gn].pid = 0; g[gn].start = time; g[gn].end = earliest; gn++;
            time = earliest;
            continue;
        }
        p[best].start = time;
        p[best].response = time - p[best].arrival;
        p[best].finish = time + p[best].burst;
        p[best].tat = p[best].finish - p[best].arrival;
        p[best].wait = p[best].tat - p[best].burst;
        g[gn].pid = p[best].pid; g[gn].start = time;
        g[gn].end = p[best].finish; gn++;
        time = p[best].finish;
        done[best] = 1;
        completed++;
    }
    cpu_print_results(p, n, g, gn, "SJF Non-Preemptive");
}

static void cpu_srtf(CPUProc *procs, int n)
{
    CPUProc p[20];
    memcpy(p, procs, n * sizeof(CPUProc));
    for (int i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].started = 0;
        p[i].start = -1;
    }
    GanttEntry g[500]; int gn = 0;
    int time = 0, completed = 0;
    int lastPid = -1;

    while (completed < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (p[i].remaining <= 0 || p[i].arrival > time) continue;
            if (best == -1 || p[i].remaining < p[best].remaining ||
                (p[i].remaining == p[best].remaining &&
                 p[i].arrival < p[best].arrival))
                best = i;
        }
        if (best == -1) {
            int earliest = INT_MAX;
            for (int i = 0; i < n; i++)
                if (p[i].remaining > 0 && p[i].arrival < earliest)
                    earliest = p[i].arrival;
            if (gn > 0 && g[gn-1].pid == 0)
                g[gn-1].end = earliest;
            else {
                g[gn].pid = 0; g[gn].start = time;
                g[gn].end = earliest; gn++;
            }
            lastPid = 0;
            time = earliest;
            continue;
        }
        if (!p[best].started) {
            p[best].start = time;
            p[best].response = time - p[best].arrival;
            p[best].started = 1;
        }
        if (p[best].pid != lastPid) {
            g[gn].pid = p[best].pid; g[gn].start = time;
            g[gn].end = time + 1; gn++;
        } else {
            g[gn-1].end = time + 1;
        }
        lastPid = p[best].pid;
        p[best].remaining--;
        time++;
        if (p[best].remaining == 0) {
            p[best].finish = time;
            p[best].tat = p[best].finish - p[best].arrival;
            p[best].wait = p[best].tat - p[best].burst;
            completed++;
        }
    }
    cpu_print_results(p, n, g, gn, "SRTF (SJF Preemptive)");
}

static void cpu_rr(CPUProc *procs, int n, int quantum)
{
    CPUProc p[20];
    memcpy(p, procs, n * sizeof(CPUProc));
    for (int i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].started = 0;
        p[i].start = -1;
    }
    if (quantum < 1) quantum = 1;

    /* Sort by arrival for queue insertion order */
    int order[20];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (p[order[i]].arrival > p[order[j]].arrival ||
                (p[order[i]].arrival == p[order[j]].arrival &&
                 order[i] > order[j]))
                swap_int(&order[i], &order[j]);

    int queue[200], qf = 0, qr = 0;
    int inQueue[20] = {0};
    GanttEntry g[500]; int gn = 0;
    int time = 0, completed = 0, oi = 0;

    /* Add initially arrived processes */
    for (int i = 0; i < n; i++) {
        if (p[order[i]].arrival <= time) {
            queue[qr++] = order[i];
            inQueue[order[i]] = 1;
            oi = i + 1;
        }
    }

    while (completed < n) {
        if (qf == qr) {
            /* Queue empty - idle until next arrival */
            int earliest = INT_MAX;
            for (int i = 0; i < n; i++)
                if (p[i].remaining > 0 && p[i].arrival < earliest)
                    earliest = p[i].arrival;
            if (earliest == INT_MAX) break;
            g[gn].pid = 0; g[gn].start = time; g[gn].end = earliest; gn++;
            time = earliest;
            for (int i = oi; i < n; i++) {
                if (p[order[i]].arrival <= time && !inQueue[order[i]] &&
                    p[order[i]].remaining > 0) {
                    queue[qr++] = order[i];
                    inQueue[order[i]] = 1;
                    if (oi == i) oi = i + 1;
                }
            }
            continue;
        }

        int cur = queue[qf++];
        if (!p[cur].started) {
            p[cur].start = time;
            p[cur].response = time - p[cur].arrival;
            p[cur].started = 1;
        }

        int run = (p[cur].remaining < quantum) ? p[cur].remaining : quantum;
        g[gn].pid = p[cur].pid; g[gn].start = time;
        g[gn].end = time + run; gn++;
        p[cur].remaining -= run;
        time += run;

        /* Add new arrivals to queue (before re-adding current) */
        for (int i = oi; i < n; i++) {
            if (p[order[i]].arrival <= time && !inQueue[order[i]] &&
                p[order[i]].remaining > 0) {
                queue[qr++] = order[i];
                inQueue[order[i]] = 1;
                if (oi == i) oi = i + 1;
            }
        }

        if (p[cur].remaining == 0) {
            p[cur].finish = time;
            p[cur].tat = p[cur].finish - p[cur].arrival;
            p[cur].wait = p[cur].tat - p[cur].burst;
            completed++;
        } else {
            /* Re-add to queue */
            queue[qr++] = cur;
        }
    }
    char title[64];
    snprintf(title, sizeof(title), "Round Robin (Q=%d)", quantum);
    cpu_print_results(p, n, g, gn, title);
}

static void cpu_priority_np(CPUProc *procs, int n)
{
    CPUProc p[20];
    memcpy(p, procs, n * sizeof(CPUProc));
    int done[20] = {0};
    GanttEntry g[200]; int gn = 0;
    int time = 0, completed = 0;

    while (completed < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (done[i] || p[i].arrival > time) continue;
            if (best == -1 || p[i].priority < p[best].priority ||
                (p[i].priority == p[best].priority &&
                 p[i].arrival < p[best].arrival))
                best = i;
        }
        if (best == -1) {
            int earliest = INT_MAX;
            for (int i = 0; i < n; i++)
                if (!done[i] && p[i].arrival < earliest)
                    earliest = p[i].arrival;
            g[gn].pid = 0; g[gn].start = time; g[gn].end = earliest; gn++;
            time = earliest;
            continue;
        }
        p[best].start = time;
        p[best].response = time - p[best].arrival;
        p[best].finish = time + p[best].burst;
        p[best].tat = p[best].finish - p[best].arrival;
        p[best].wait = p[best].tat - p[best].burst;
        g[gn].pid = p[best].pid; g[gn].start = time;
        g[gn].end = p[best].finish; gn++;
        time = p[best].finish;
        done[best] = 1;
        completed++;
    }
    cpu_print_results(p, n, g, gn, "Priority Non-Preemptive (lower = higher)");
}

static void cpu_priority_p(CPUProc *procs, int n)
{
    CPUProc p[20];
    memcpy(p, procs, n * sizeof(CPUProc));
    for (int i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].started = 0;
        p[i].start = -1;
    }
    GanttEntry g[500]; int gn = 0;
    int time = 0, completed = 0;
    int lastPid = -1;

    while (completed < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (p[i].remaining <= 0 || p[i].arrival > time) continue;
            if (best == -1 || p[i].priority < p[best].priority ||
                (p[i].priority == p[best].priority &&
                 p[i].remaining < p[best].remaining))
                best = i;
        }
        if (best == -1) {
            int earliest = INT_MAX;
            for (int i = 0; i < n; i++)
                if (p[i].remaining > 0 && p[i].arrival < earliest)
                    earliest = p[i].arrival;
            if (earliest == INT_MAX) break;
            if (gn > 0 && g[gn-1].pid == 0)
                g[gn-1].end = earliest;
            else {
                g[gn].pid = 0; g[gn].start = time;
                g[gn].end = earliest; gn++;
            }
            lastPid = 0;
            time = earliest;
            continue;
        }
        if (!p[best].started) {
            p[best].start = time;
            p[best].response = time - p[best].arrival;
            p[best].started = 1;
        }
        if (p[best].pid != lastPid) {
            g[gn].pid = p[best].pid; g[gn].start = time;
            g[gn].end = time + 1; gn++;
        } else {
            g[gn-1].end = time + 1;
        }
        lastPid = p[best].pid;
        p[best].remaining--;
        time++;
        if (p[best].remaining == 0) {
            p[best].finish = time;
            p[best].tat = p[best].finish - p[best].arrival;
            p[best].wait = p[best].tat - p[best].burst;
            completed++;
        }
    }
    cpu_print_results(p, n, g, gn,
                      "Priority Preemptive (lower = higher)");
}

static void RunCPUScheduling(void)
{
    HWND panel = g_hPanels[0];
    char buf[4096];

    GetEditText(panel, IDC_CPU_NPROC, buf, sizeof(buf));
    int n = atoi(buf);
    if (n < 1 || n > 20) {
        SetDlgItemTextA(panel, IDC_CPU_OUTPUT,
                        "Error: Number of processes must be 1-20.");
        return;
    }

    GetEditText(panel, IDC_CPU_PROCS, buf, sizeof(buf));
    CPUProc procs[20];
    memset(procs, 0, sizeof(procs));
    const char *line = buf;
    int parsed = 0;
    while (*line && parsed < n) {
        while (*line == '\r' || *line == '\n') line++;
        if (*line == '\0') break;
        int vals[3] = {0, 0, 0};
        parse_csv_int(line, vals, 3);
        procs[parsed].pid = parsed + 1;
        procs[parsed].arrival = vals[0];
        procs[parsed].burst = vals[1];
        procs[parsed].priority = vals[2];
        if (procs[parsed].burst < 1) procs[parsed].burst = 1;
        parsed++;
        while (*line && *line != '\r' && *line != '\n') line++;
    }
    if (parsed < n) n = parsed;
    if (n < 1) {
        SetDlgItemTextA(panel, IDC_CPU_OUTPUT,
                        "Error: No valid process data found.");
        return;
    }

    int algo = GetComboSel(panel, IDC_CPU_ALGO);
    GetEditText(panel, IDC_CPU_QUANTUM, buf, sizeof(buf));
    int quantum = atoi(buf);
    if (quantum < 1) quantum = 1;

    out_clear();
    out_append("TaskForge CPU Scheduling Results\r\n");
    out_append("================================\r\n\r\n");

    switch (algo) {
    case 0: cpu_fcfs(procs, n); break;
    case 1: cpu_sjf_np(procs, n); break;
    case 2: cpu_srtf(procs, n); break;
    case 3: cpu_rr(procs, n, quantum); break;
    case 4: cpu_priority_np(procs, n); break;
    case 5: cpu_priority_p(procs, n); break;
    case 6: /* Compare All */
        cpu_fcfs(procs, n);
        cpu_sjf_np(procs, n);
        cpu_srtf(procs, n);
        cpu_rr(procs, n, quantum);
        cpu_priority_np(procs, n);
        cpu_priority_p(procs, n);
        break;
    }
    SetDlgItemTextA(panel, IDC_CPU_OUTPUT, out_buf);
}

/*=============================================================================
 *  BANKER'S ALGORITHM (Deadlock)
 *===========================================================================*/
static void bankers_safety(int n, int m, int avail[], int alloc[][20],
                           int maxm[][20], int need[][20], int showNeed)
{
    /* Compute Need */
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            need[i][j] = maxm[i][j] - alloc[i][j];

    if (showNeed) {
        out_append("Need Matrix (Max - Allocation):\r\n");
        out_append("      ");
        for (int j = 0; j < m; j++) out_append("R%-4d", j);
        out_append("\r\n");
        for (int i = 0; i < n; i++) {
            out_append("P%-4d ", i);
            for (int j = 0; j < m; j++) out_append("%-5d", need[i][j]);
            out_append("\r\n");
        }
        out_append("\r\n");
    }

    /* Safety algorithm */
    int work[20];
    for (int j = 0; j < m; j++) work[j] = avail[j];
    int finish[20] = {0};
    int safeSeq[20];
    int count = 0;

    out_append("Safety Algorithm Steps:\r\n");
    out_append("%-6s %-30s %-10s\r\n", "Step", "Work Vector", "Process");
    out_append("----------------------------------------------\r\n");

    while (count < n) {
        int found = -1;
        for (int i = 0; i < n; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < m; j++) {
                if (need[i][j] > work[j]) { ok = 0; break; }
            }
            if (ok) { found = i; break; }
        }
        if (found == -1) break;

        char wstr[128]; int wi = 0;
        wi += snprintf(wstr + wi, (int)sizeof(wstr) - wi, "[");
        for (int j = 0; j < m; j++) {
            if (j) wi += snprintf(wstr + wi, (int)sizeof(wstr) - wi, ",");
            wi += snprintf(wstr + wi, (int)sizeof(wstr) - wi, "%d", work[j]);
        }
        snprintf(wstr + wi, (int)sizeof(wstr) - wi, "]");

        out_append("%-6d %-30s P%d finishes\r\n", count + 1, wstr, found);

        for (int j = 0; j < m; j++)
            work[j] += alloc[found][j];
        finish[found] = 1;
        safeSeq[count++] = found;
    }

    out_append("\r\n");
    if (count == n) {
        out_append("SYSTEM IS IN A SAFE STATE.\r\n");
        out_append("Safe Sequence: <");
        for (int i = 0; i < n; i++) {
            if (i) out_append(", ");
            out_append("P%d", safeSeq[i]);
        }
        out_append(">\r\n\r\n");

        /* Show final Work */
        out_append("Final Work vector: [");
        for (int j = 0; j < m; j++) {
            if (j) out_append(",");
            out_append("%d", work[j]);
        }
        out_append("]\r\n");
    } else {
        out_append("*** SYSTEM IS UNSAFE! ***\r\n");
        out_append("Deadlock may occur. Could not finish all processes.\r\n");
        out_append("Finished %d of %d processes.\r\n", count, n);
        if (count > 0) {
            out_append("Partial safe sequence: <");
            for (int i = 0; i < count; i++) {
                if (i) out_append(", ");
                out_append("P%d", safeSeq[i]);
            }
            out_append(">\r\n");
        }
    }
}

static void RunDeadlockCheck(void)
{
    HWND panel = g_hPanels[1];
    char buf[4096];

    GetEditText(panel, IDC_DL_N, buf, sizeof(buf));
    int n = atoi(buf);
    GetEditText(panel, IDC_DL_M, buf, sizeof(buf));
    int m = atoi(buf);
    if (n < 1 || n > 20 || m < 1 || m > 20) {
        SetDlgItemTextA(panel, IDC_DL_OUTPUT,
                        "Error: n and m must be 1-20.");
        return;
    }

    int avail[20] = {0};
    GetEditText(panel, IDC_DL_AVAIL, buf, sizeof(buf));
    parse_csv_int(buf, avail, m);

    int alloc[20][20] = {{0}}, maxm[20][20] = {{0}};
    GetEditText(panel, IDC_DL_ALLOC, buf, sizeof(buf));
    int cols = 0;
    parse_lines(buf, alloc, n, &cols);
    GetEditText(panel, IDC_DL_MAX, buf, sizeof(buf));
    parse_lines(buf, maxm, n, &cols);

    out_clear();
    out_append("TaskForge Banker's Algorithm - Safety Check\r\n");
    out_append("============================================\r\n\r\n");

    /* Print input */
    out_append("Processes: %d   Resource types: %d\r\n", n, m);
    out_append("Available: [");
    for (int j = 0; j < m; j++) {
        if (j) out_append(",");
        out_append("%d", avail[j]);
    }
    out_append("]\r\n\r\n");

    out_append("Allocation Matrix:\r\n      ");
    for (int j = 0; j < m; j++) out_append("R%-4d", j);
    out_append("\r\n");
    for (int i = 0; i < n; i++) {
        out_append("P%-4d ", i);
        for (int j = 0; j < m; j++) out_append("%-5d", alloc[i][j]);
        out_append("\r\n");
    }
    out_append("\r\nMax Matrix:\r\n      ");
    for (int j = 0; j < m; j++) out_append("R%-4d", j);
    out_append("\r\n");
    for (int i = 0; i < n; i++) {
        out_append("P%-4d ", i);
        for (int j = 0; j < m; j++) out_append("%-5d", maxm[i][j]);
        out_append("\r\n");
    }
    out_append("\r\n");

    int need[20][20];
    bankers_safety(n, m, avail, alloc, maxm, need, 1);

    SetDlgItemTextA(panel, IDC_DL_OUTPUT, out_buf);
}

static void RunDeadlockRequest(void)
{
    HWND panel = g_hPanels[1];
    char buf[4096];

    GetEditText(panel, IDC_DL_N, buf, sizeof(buf));
    int n = atoi(buf);
    GetEditText(panel, IDC_DL_M, buf, sizeof(buf));
    int m = atoi(buf);
    if (n < 1 || n > 20 || m < 1 || m > 20) {
        SetDlgItemTextA(panel, IDC_DL_OUTPUT,
                        "Error: n and m must be 1-20.");
        return;
    }

    int avail[20] = {0};
    GetEditText(panel, IDC_DL_AVAIL, buf, sizeof(buf));
    parse_csv_int(buf, avail, m);

    int alloc[20][20] = {{0}}, maxm[20][20] = {{0}};
    GetEditText(panel, IDC_DL_ALLOC, buf, sizeof(buf));
    int cols = 0;
    parse_lines(buf, alloc, n, &cols);
    GetEditText(panel, IDC_DL_MAX, buf, sizeof(buf));
    parse_lines(buf, maxm, n, &cols);

    GetEditText(panel, IDC_DL_REQPID, buf, sizeof(buf));
    int reqPid = atoi(buf);
    if (reqPid < 0 || reqPid >= n) {
        SetDlgItemTextA(panel, IDC_DL_OUTPUT,
                        "Error: Invalid process ID for request.");
        return;
    }

    int req[20] = {0};
    GetEditText(panel, IDC_DL_REQVEC, buf, sizeof(buf));
    parse_csv_int(buf, req, m);

    int need[20][20];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            need[i][j] = maxm[i][j] - alloc[i][j];

    out_clear();
    out_append("TaskForge Banker's Algorithm - Resource Request\r\n");
    out_append("================================================\r\n\r\n");

    out_append("Process P%d requests: [", reqPid);
    for (int j = 0; j < m; j++) {
        if (j) out_append(",");
        out_append("%d", req[j]);
    }
    out_append("]\r\n\r\n");

    /* Check request <= need */
    for (int j = 0; j < m; j++) {
        if (req[j] > need[reqPid][j]) {
            out_append("DENIED: Request exceeds maximum claim (Need).\r\n");
            out_append("  Request[%d]=%d > Need[%d][%d]=%d\r\n",
                       j, req[j], reqPid, j, need[reqPid][j]);
            SetDlgItemTextA(panel, IDC_DL_OUTPUT, out_buf);
            return;
        }
    }

    /* Check request <= available */
    for (int j = 0; j < m; j++) {
        if (req[j] > avail[j]) {
            out_append("DENIED: Request exceeds available resources.\r\n");
            out_append("  Request[%d]=%d > Available[%d]=%d\r\n",
                       j, req[j], j, avail[j]);
            out_append("Process P%d must wait.\r\n", reqPid);
            SetDlgItemTextA(panel, IDC_DL_OUTPUT, out_buf);
            return;
        }
    }

    /* Pretend to allocate */
    int newAvail[20];
    int newAlloc[20][20], newMax[20][20];
    memcpy(newAlloc, alloc, sizeof(newAlloc));
    memcpy(newMax, maxm, sizeof(newMax));
    for (int j = 0; j < m; j++) {
        newAvail[j] = avail[j] - req[j];
        newAlloc[reqPid][j] += req[j];
    }

    out_append("Tentative allocation made. Checking safety...\r\n\r\n");

    int newNeed[20][20];
    bankers_safety(n, m, newAvail, newAlloc, newMax, newNeed, 1);

    /* Check if safe by looking at output for "SAFE STATE" */
    if (strstr(out_buf, "SAFE STATE")) {
        out_append("\r\n>>> REQUEST GRANTED for P%d. <<<\r\n", reqPid);
    } else {
        out_append("\r\n>>> REQUEST DENIED for P%d (would lead to unsafe "
                   "state). <<<\r\n", reqPid);
    }

    SetDlgItemTextA(panel, IDC_DL_OUTPUT, out_buf);
}

/*=============================================================================
 *  PAGE REPLACEMENT
 *===========================================================================*/
typedef struct {
    const char *name;
    int faults;
    int hits;
} PageResult;

static void page_print_header(int nf)
{
    out_append("%-6s %-5s ", "Step", "Ref");
    for (int f = 0; f < nf; f++) out_append("Frame%-2d ", f + 1);
    out_append("%-8s\r\n", "Status");
    for (int i = 0; i < 6 + 5 + nf * 8 + 8; i++) out_append("-");
    out_append("\r\n");
}

static void page_print_step(int step, int ref, int *frames, int nf,
                            int fault)
{
    out_append("%-6d %-5d ", step, ref);
    for (int f = 0; f < nf; f++) {
        if (frames[f] == -1)
            out_append("  -     ");
        else
            out_append("%-8d", frames[f]);
    }
    out_append("%s\r\n", fault ? "FAULT" : "HIT");
}

static PageResult page_fifo(int *refs, int nr, int nf, int verbose)
{
    int frames[10];
    for (int i = 0; i < nf; i++) frames[i] = -1;
    int ptr = 0, faults = 0, hits = 0;

    if (verbose) {
        out_append("=== FIFO Page Replacement ===\r\n\r\n");
        page_print_header(nf);
    }

    for (int i = 0; i < nr; i++) {
        int found = 0;
        for (int f = 0; f < nf; f++)
            if (frames[f] == refs[i]) { found = 1; break; }

        if (!found) {
            frames[ptr] = refs[i];
            ptr = (ptr + 1) % nf;
            faults++;
        } else {
            hits++;
        }
        if (verbose)
            page_print_step(i + 1, refs[i], frames, nf, !found);
    }

    if (verbose) {
        out_append("\r\nTotal faults: %d   Hits: %d   Hit ratio: %.2f%%"
                   "\r\n\r\n", faults, hits, 100.0 * hits / nr);
    }
    return (PageResult){"FIFO", faults, hits};
}

static PageResult page_lru(int *refs, int nr, int nf, int verbose)
{
    int frames[10], lastUsed[10];
    for (int i = 0; i < nf; i++) { frames[i] = -1; lastUsed[i] = -1; }
    int faults = 0, hits = 0;

    if (verbose) {
        out_append("=== LRU Page Replacement ===\r\n\r\n");
        page_print_header(nf);
    }

    for (int i = 0; i < nr; i++) {
        int found = -1;
        for (int f = 0; f < nf; f++)
            if (frames[f] == refs[i]) { found = f; break; }

        if (found >= 0) {
            lastUsed[found] = i;
            hits++;
        } else {
            /* Find empty or LRU */
            int victim = -1, oldest = INT_MAX;
            for (int f = 0; f < nf; f++) {
                if (frames[f] == -1) { victim = f; break; }
                if (lastUsed[f] < oldest) { oldest = lastUsed[f]; victim = f; }
            }
            frames[victim] = refs[i];
            lastUsed[victim] = i;
            faults++;
        }
        if (verbose)
            page_print_step(i + 1, refs[i], frames, nf, found < 0);
    }

    if (verbose) {
        out_append("\r\nTotal faults: %d   Hits: %d   Hit ratio: %.2f%%"
                   "\r\n\r\n", faults, hits, 100.0 * hits / nr);
    }
    return (PageResult){"LRU", faults, hits};
}

static PageResult page_optimal(int *refs, int nr, int nf, int verbose)
{
    int frames[10];
    for (int i = 0; i < nf; i++) frames[i] = -1;
    int faults = 0, hits = 0;

    if (verbose) {
        out_append("=== Optimal Page Replacement ===\r\n\r\n");
        page_print_header(nf);
    }

    for (int i = 0; i < nr; i++) {
        int found = 0;
        for (int f = 0; f < nf; f++)
            if (frames[f] == refs[i]) { found = 1; break; }

        if (!found) {
            /* Find empty slot */
            int victim = -1;
            for (int f = 0; f < nf; f++) {
                if (frames[f] == -1) { victim = f; break; }
            }
            if (victim == -1) {
                /* Find page used farthest in future */
                int farthest = -1;
                for (int f = 0; f < nf; f++) {
                    int nextUse = INT_MAX;
                    for (int j = i + 1; j < nr; j++) {
                        if (refs[j] == frames[f]) { nextUse = j; break; }
                    }
                    if (nextUse > farthest) {
                        farthest = nextUse;
                        victim = f;
                    }
                }
            }
            frames[victim] = refs[i];
            faults++;
        } else {
            hits++;
        }
        if (verbose)
            page_print_step(i + 1, refs[i], frames, nf, !found);
    }

    if (verbose) {
        out_append("\r\nTotal faults: %d   Hits: %d   Hit ratio: %.2f%%"
                   "\r\n\r\n", faults, hits, 100.0 * hits / nr);
    }
    return (PageResult){"Optimal", faults, hits};
}

static PageResult page_clock(int *refs, int nr, int nf, int verbose)
{
    int frames[10], useBit[10];
    for (int i = 0; i < nf; i++) { frames[i] = -1; useBit[i] = 0; }
    int ptr = 0, faults = 0, hits = 0;

    if (verbose) {
        out_append("=== Clock (Second Chance) Page Replacement ===\r\n\r\n");
        page_print_header(nf);
    }

    for (int i = 0; i < nr; i++) {
        int found = -1;
        for (int f = 0; f < nf; f++)
            if (frames[f] == refs[i]) { found = f; break; }

        if (found >= 0) {
            useBit[found] = 1;
            hits++;
        } else {
            /* Find empty slot first */
            int victim = -1;
            for (int f = 0; f < nf; f++) {
                if (frames[f] == -1) { victim = f; break; }
            }
            if (victim >= 0) {
                frames[victim] = refs[i];
                useBit[victim] = 1;
                ptr = (victim + 1) % nf;
            } else {
                while (useBit[ptr]) {
                    useBit[ptr] = 0;
                    ptr = (ptr + 1) % nf;
                }
                frames[ptr] = refs[i];
                useBit[ptr] = 1;
                ptr = (ptr + 1) % nf;
            }
            faults++;
        }
        if (verbose)
            page_print_step(i + 1, refs[i], frames, nf, found < 0);
    }

    if (verbose) {
        out_append("\r\nTotal faults: %d   Hits: %d   Hit ratio: %.2f%%"
                   "\r\n\r\n", faults, hits, 100.0 * hits / nr);
    }
    return (PageResult){"Clock", faults, hits};
}

static void RunPageReplacement(void)
{
    HWND panel = g_hPanels[2];
    char buf[4096];

    GetEditText(panel, IDC_MEM_FRAMES, buf, sizeof(buf));
    int nf = atoi(buf);
    if (nf < 1 || nf > 10) {
        SetDlgItemTextA(panel, IDC_MEM_OUTPUT,
                        "Error: Frames must be 1-10.");
        return;
    }

    GetEditText(panel, IDC_MEM_REFSTR, buf, sizeof(buf));
    int refs[500];
    int nr = parse_space_int(buf, refs, 500);
    if (nr < 1) {
        SetDlgItemTextA(panel, IDC_MEM_OUTPUT,
                        "Error: No reference string provided.");
        return;
    }

    int algo = GetComboSel(panel, IDC_MEM_ALGO);

    out_clear();
    out_append("TaskForge Page Replacement Results\r\n");
    out_append("===================================\r\n\r\n");
    out_append("Frames: %d   References: %d\r\n\r\n", nf, nr);

    if (algo == 4) {
        /* Compare All */
        PageResult results[4];
        results[0] = page_fifo(refs, nr, nf, 1);
        results[1] = page_lru(refs, nr, nf, 1);
        results[2] = page_optimal(refs, nr, nf, 1);
        results[3] = page_clock(refs, nr, nf, 1);

        out_append("=== Comparison Summary ===\r\n\r\n");
        out_append("%-12s %-10s %-10s %-12s\r\n",
                   "Algorithm", "Faults", "Hits", "Hit Ratio");
        out_append("--------------------------------------------\r\n");
        for (int i = 0; i < 4; i++) {
            out_append("%-12s %-10d %-10d %.2f%%\r\n",
                       results[i].name, results[i].faults, results[i].hits,
                       100.0 * results[i].hits / nr);
        }
    } else {
        switch (algo) {
        case 0: page_fifo(refs, nr, nf, 1); break;
        case 1: page_lru(refs, nr, nf, 1); break;
        case 2: page_optimal(refs, nr, nf, 1); break;
        case 3: page_clock(refs, nr, nf, 1); break;
        }
    }
    SetDlgItemTextA(panel, IDC_MEM_OUTPUT, out_buf);
}

/*=============================================================================
 *  DISK SCHEDULING
 *===========================================================================*/
typedef struct {
    const char *name;
    int totalSeek;
    int steps;
} DiskResult;

static DiskResult disk_fcfs(int *queue, int nq, int head, int diskSz,
                            int verbose)
{
    (void)diskSz;
    int total = 0, cur = head;

    if (verbose) {
        out_append("=== FCFS Disk Scheduling ===\r\n\r\n");
        out_append("%-6s %-10s %-14s\r\n", "Step", "Track", "Seek Distance");
        out_append("----------------------------------\r\n");
    }

    for (int i = 0; i < nq; i++) {
        int d = abs(queue[i] - cur);
        total += d;
        if (verbose)
            out_append("%-6d %-10d %-14d\r\n", i + 1, queue[i], d);
        cur = queue[i];
    }

    if (verbose) {
        out_append("\r\nTotal seek distance: %d\r\n", total);
        out_append("Average seek: %.2f\r\n\r\n", (double)total / nq);
    }
    return (DiskResult){"FCFS", total, nq};
}

static DiskResult disk_sstf(int *queue, int nq, int head, int diskSz,
                            int verbose)
{
    (void)diskSz;
    int visited[200] = {0};
    int total = 0, cur = head;

    if (verbose) {
        out_append("=== SSTF Disk Scheduling ===\r\n\r\n");
        out_append("%-6s %-10s %-14s\r\n", "Step", "Track", "Seek Distance");
        out_append("----------------------------------\r\n");
    }

    for (int step = 0; step < nq; step++) {
        int best = -1, bestDist = INT_MAX;
        for (int i = 0; i < nq; i++) {
            if (visited[i]) continue;
            int d = abs(queue[i] - cur);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        visited[best] = 1;
        total += bestDist;
        if (verbose)
            out_append("%-6d %-10d %-14d\r\n", step + 1, queue[best],
                       bestDist);
        cur = queue[best];
    }

    if (verbose) {
        out_append("\r\nTotal seek distance: %d\r\n", total);
        out_append("Average seek: %.2f\r\n\r\n", (double)total / nq);
    }
    return (DiskResult){"SSTF", total, nq};
}

static DiskResult disk_scan(int *queue, int nq, int head, int diskSz,
                            int towardZero, int verbose)
{
    /* Sort requests */
    int sorted[200];
    memcpy(sorted, queue, nq * sizeof(int));
    for (int i = 0; i < nq - 1; i++)
        for (int j = i + 1; j < nq; j++)
            if (sorted[i] > sorted[j]) swap_int(&sorted[i], &sorted[j]);

    int order[200], oc = 0;
    int total = 0, cur = head;

    if (towardZero) {
        /* Go toward 0 first */
        for (int i = nq - 1; i >= 0; i--)
            if (sorted[i] <= head) order[oc++] = sorted[i];
        order[oc++] = 0; /* Boundary */
        for (int i = 0; i < nq; i++)
            if (sorted[i] > head) order[oc++] = sorted[i];
    } else {
        /* Go toward max first */
        for (int i = 0; i < nq; i++)
            if (sorted[i] >= head) order[oc++] = sorted[i];
        order[oc++] = diskSz - 1; /* Boundary */
        for (int i = nq - 1; i >= 0; i--)
            if (sorted[i] < head) order[oc++] = sorted[i];
    }

    if (verbose) {
        out_append("=== SCAN (Elevator) Disk Scheduling ===\r\n\r\n");
        out_append("Direction: %s\r\n\r\n",
                   towardZero ? "Toward 0" : "Toward Max");
        out_append("%-6s %-10s %-14s\r\n", "Step", "Track", "Seek Distance");
        out_append("----------------------------------\r\n");
    }

    int step = 0;
    for (int i = 0; i < oc; i++) {
        int d = abs(order[i] - cur);
        total += d;
        step++;
        if (verbose)
            out_append("%-6d %-10d %-14d\r\n", step, order[i], d);
        cur = order[i];
    }

    if (verbose) {
        out_append("\r\nTotal seek distance: %d\r\n", total);
        out_append("Average seek: %.2f\r\n\r\n", (double)total / nq);
    }
    return (DiskResult){"SCAN", total, step};
}

static DiskResult disk_cscan(int *queue, int nq, int head, int diskSz,
                             int towardZero, int verbose)
{
    int sorted[200];
    memcpy(sorted, queue, nq * sizeof(int));
    for (int i = 0; i < nq - 1; i++)
        for (int j = i + 1; j < nq; j++)
            if (sorted[i] > sorted[j]) swap_int(&sorted[i], &sorted[j]);

    int order[200], oc = 0;
    int total = 0, cur = head;

    if (!towardZero) {
        /* Go toward max, jump to 0, continue toward max */
        for (int i = 0; i < nq; i++)
            if (sorted[i] >= head) order[oc++] = sorted[i];
        order[oc++] = diskSz - 1;
        order[oc++] = 0;
        for (int i = 0; i < nq; i++)
            if (sorted[i] < head) order[oc++] = sorted[i];
    } else {
        /* Go toward 0, jump to max, continue toward 0 */
        for (int i = nq - 1; i >= 0; i--)
            if (sorted[i] <= head) order[oc++] = sorted[i];
        order[oc++] = 0;
        order[oc++] = diskSz - 1;
        for (int i = nq - 1; i >= 0; i--)
            if (sorted[i] > head) order[oc++] = sorted[i];
    }

    if (verbose) {
        out_append("=== C-SCAN Disk Scheduling ===\r\n\r\n");
        out_append("Direction: %s\r\n\r\n",
                   towardZero ? "Toward 0" : "Toward Max");
        out_append("%-6s %-10s %-14s\r\n", "Step", "Track", "Seek Distance");
        out_append("----------------------------------\r\n");
    }

    int step = 0;
    for (int i = 0; i < oc; i++) {
        int d = abs(order[i] - cur);
        total += d;
        step++;
        if (verbose)
            out_append("%-6d %-10d %-14d\r\n", step, order[i], d);
        cur = order[i];
    }

    if (verbose) {
        out_append("\r\nTotal seek distance: %d\r\n", total);
        out_append("Average seek: %.2f\r\n\r\n", (double)total / nq);
    }
    return (DiskResult){"C-SCAN", total, step};
}

static void RunDiskScheduling(void)
{
    HWND panel = g_hPanels[3];
    char buf[4096];

    GetEditText(panel, IDC_DSK_SIZE, buf, sizeof(buf));
    int diskSz = atoi(buf);
    if (diskSz < 1) diskSz = 200;

    GetEditText(panel, IDC_DSK_HEAD, buf, sizeof(buf));
    int head = atoi(buf);

    GetEditText(panel, IDC_DSK_QUEUE, buf, sizeof(buf));
    int queue[200];
    int nq = parse_csv_int(buf, queue, 200);
    if (nq < 1) {
        SetDlgItemTextA(panel, IDC_DSK_OUTPUT,
                        "Error: No request queue provided.");
        return;
    }

    int dir = GetComboSel(panel, IDC_DSK_DIR);     /* 0=toward 0, 1=toward max */
    int algo = GetComboSel(panel, IDC_DSK_ALGO);
    int towardZero = (dir == 0);

    out_clear();
    out_append("TaskForge Disk Scheduling Results\r\n");
    out_append("==================================\r\n\r\n");
    out_append("Disk size: %d   Head: %d   Requests: %d\r\n\r\n",
               diskSz, head, nq);

    if (algo == 4) {
        /* Compare All */
        DiskResult results[4];
        results[0] = disk_fcfs(queue, nq, head, diskSz, 1);
        results[1] = disk_sstf(queue, nq, head, diskSz, 1);
        results[2] = disk_scan(queue, nq, head, diskSz, towardZero, 1);
        results[3] = disk_cscan(queue, nq, head, diskSz, towardZero, 1);

        out_append("=== Comparison Summary ===\r\n\r\n");
        out_append("%-12s %-15s %-15s\r\n",
                   "Algorithm", "Total Seek", "Avg Seek");
        out_append("------------------------------------------\r\n");
        for (int i = 0; i < 4; i++) {
            out_append("%-12s %-15d %.2f\r\n",
                       results[i].name, results[i].totalSeek,
                       (double)results[i].totalSeek / nq);
        }
    } else {
        switch (algo) {
        case 0: disk_fcfs(queue, nq, head, diskSz, 1); break;
        case 1: disk_sstf(queue, nq, head, diskSz, 1); break;
        case 2: disk_scan(queue, nq, head, diskSz, towardZero, 1); break;
        case 3: disk_cscan(queue, nq, head, diskSz, towardZero, 1); break;
        }
    }
    SetDlgItemTextA(panel, IDC_DSK_OUTPUT, out_buf);
}

/*=============================================================================
 *  FILE OPERATIONS
 *===========================================================================*/
static void file_word_count(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        out_append("Error: Cannot open file '%s'\r\n", path);
        return;
    }

    int lines = 0, words = 0, chars = 0, bytes = 0;
    int inWord = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        bytes++;
        chars++;
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inWord) { words++; inWord = 0; }
        } else {
            inWord = 1;
        }
    }
    if (inWord) words++;
    fclose(fp);

    out_append("=== Word Count Results ===\r\n\r\n");
    out_append("File: %s\r\n\r\n", path);
    out_append("  Lines:      %d\r\n", lines);
    out_append("  Words:      %d\r\n", words);
    out_append("  Characters: %d\r\n", chars);
    out_append("  Bytes:      %d\r\n", bytes);
}

static void file_checksum(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        out_append("Error: Cannot open file '%s'\r\n", path);
        return;
    }

    /* Simple CRC-like checksum (Adler-32 variant) */
    unsigned long a = 1, b = 0;
    unsigned long xorSum = 0;
    unsigned long addSum = 0;
    long fileSize = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        unsigned char byte = (unsigned char)c;
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
        xorSum ^= byte;
        addSum += byte;
        fileSize++;
    }
    fclose(fp);

    unsigned long adler = (b << 16) | a;

    out_append("=== Checksum Results ===\r\n\r\n");
    out_append("File: %s\r\n", path);
    out_append("Size: %ld bytes\r\n\r\n", fileSize);
    out_append("  Adler-32:       0x%08lX\r\n", adler);
    out_append("  XOR checksum:   0x%02lX\r\n", xorSum & 0xFF);
    out_append("  Additive sum:   0x%08lX (%lu)\r\n", addSum, addSum);
}

static void file_copy(const char *src, const char *dst)
{
    if (!dst || dst[0] == '\0') {
        out_append("Error: No output file path specified.\r\n");
        return;
    }

    FILE *fin = fopen(src, "rb");
    if (!fin) {
        out_append("Error: Cannot open source file '%s'\r\n", src);
        return;
    }

    FILE *fout = fopen(dst, "wb");
    if (!fout) {
        fclose(fin);
        out_append("Error: Cannot create destination file '%s'\r\n", dst);
        return;
    }

    unsigned char buf[8192];
    size_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        fwrite(buf, 1, n, fout);
        total += n;
    }

    fclose(fin);
    fclose(fout);

    out_append("=== File Copy Results ===\r\n\r\n");
    out_append("Source:      %s\r\n", src);
    out_append("Destination: %s\r\n", dst);
    out_append("Bytes copied: %lu\r\n", (unsigned long)total);
    out_append("\r\nCopy completed successfully.\r\n");
}

static void file_encrypt(const char *src, const char *dst, int key)
{
    if (!dst || dst[0] == '\0') {
        out_append("Error: No output file path specified.\r\n");
        return;
    }

    FILE *fin = fopen(src, "rb");
    if (!fin) {
        out_append("Error: Cannot open source file '%s'\r\n", src);
        return;
    }

    FILE *fout = fopen(dst, "wb");
    if (!fout) {
        fclose(fin);
        out_append("Error: Cannot create destination file '%s'\r\n", dst);
        return;
    }

    unsigned char k = (unsigned char)(key & 0xFF);
    unsigned char buf[8192];
    size_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        for (size_t i = 0; i < n; i++)
            buf[i] ^= k;
        fwrite(buf, 1, n, fout);
        total += n;
    }

    fclose(fin);
    fclose(fout);

    out_append("=== File Encrypt/Decrypt (XOR) Results ===\r\n\r\n");
    out_append("Source:      %s\r\n", src);
    out_append("Destination: %s\r\n", dst);
    out_append("Key:         %d (0x%02X)\r\n", key & 0xFF, key & 0xFF);
    out_append("Bytes processed: %lu\r\n", (unsigned long)total);
    out_append("\r\nXOR encryption/decryption completed.\r\n");
    out_append("(Apply the same key again to decrypt.)\r\n");
}

static void RunFileOperation(void)
{
    HWND panel = g_hPanels[4];
    char inPath[MAX_PATH], outPath[MAX_PATH], keyBuf[32];

    int op = GetComboSel(panel, IDC_FIL_OP);
    GetEditText(panel, IDC_FIL_IN, inPath, sizeof(inPath));
    GetEditText(panel, IDC_FIL_OUT, outPath, sizeof(outPath));
    GetEditText(panel, IDC_FIL_KEY, keyBuf, sizeof(keyBuf));
    int key = atoi(keyBuf);

    if (inPath[0] == '\0') {
        SetDlgItemTextA(panel, IDC_FIL_OUTPUT,
                        "Error: No input file path specified.");
        return;
    }

    out_clear();
    out_append("TaskForge File Operations\r\n");
    out_append("=========================\r\n\r\n");

    switch (op) {
    case 0: file_word_count(inPath); break;
    case 1: file_checksum(inPath); break;
    case 2: file_copy(inPath, outPath); break;
    case 3: file_encrypt(inPath, outPath, key); break;
    }

    SetDlgItemTextA(panel, IDC_FIL_OUTPUT, out_buf);
}

/* End of taskforge_gui.c */
