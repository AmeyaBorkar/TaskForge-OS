/* ================================================================
 *  TaskForge -- I/O and File Management Module
 *  io_file_mgmt.c
 *
 *  Covers: Disk Scheduling, I/O Buffering, Virtual File System,
 *          Free Space Management (Bitmap & Linked List).
 * ================================================================ */
#include "io_file_mgmt.h"
#include <limits.h>

/* ----------------------------------------------------------------
 *  Internal constants
 * ---------------------------------------------------------------- */
#define MAX_REQUESTS   30
#define MAX_FS_ENTRIES 100
#define MAX_BLOCKS     64
#define MAX_CMD        256

/* ================================================================
 *  1.  DISK SCHEDULING  (FCFS / SSTF / SCAN / C-SCAN)
 * ================================================================ */

/* Run a single disk-scheduling algorithm.
 * Fills |order| with the service sequence and returns total seek. */
static int disk_fcfs(const int *q, int n, int head, int *order) {
    int total = 0, cur = head;
    for (int i = 0; i < n; i++) {
        order[i] = q[i];
        total += abs(cur - q[i]);
        cur = q[i];
    }
    return total;
}

static int disk_sstf(const int *q, int n, int head, int *order) {
    int visited[MAX_REQUESTS] = {0};
    int total = 0, cur = head;
    for (int i = 0; i < n; i++) {
        int best = -1, bestd = INT_MAX;
        for (int j = 0; j < n; j++) {
            if (!visited[j] && abs(cur - q[j]) < bestd) {
                bestd = abs(cur - q[j]);
                best  = j;
            }
        }
        visited[best] = 1;
        order[i] = q[best];
        total += bestd;
        cur = q[best];
    }
    return total;
}

static int disk_scan(const int *q, int n, int head, int disk_sz,
                     int dir_right, int *order, int *cnt) {
    /* Build sorted copy */
    int sorted[MAX_REQUESTS];
    for (int i = 0; i < n; i++) sorted[i] = q[i];
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (sorted[i] > sorted[j]) {
                int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t;
            }

    int total = 0, cur = head;
    *cnt = 0;

    if (dir_right) {
        for (int i = 0; i < n; i++)
            if (sorted[i] >= head) { order[(*cnt)++] = sorted[i]; }
        /* Go to max boundary */
        if (*cnt == 0 || order[*cnt - 1] != disk_sz - 1)
            order[(*cnt)++] = disk_sz - 1;
        /* Reverse pass -- requests below head, descending */
        for (int i = n - 1; i >= 0; i--)
            if (sorted[i] < head) order[(*cnt)++] = sorted[i];
    } else {
        for (int i = n - 1; i >= 0; i--)
            if (sorted[i] <= head) order[(*cnt)++] = sorted[i];
        if (*cnt == 0 || order[*cnt - 1] != 0)
            order[(*cnt)++] = 0;
        for (int i = 0; i < n; i++)
            if (sorted[i] > head) order[(*cnt)++] = sorted[i];
    }

    cur = head;
    for (int i = 0; i < *cnt; i++) {
        total += abs(cur - order[i]);
        cur = order[i];
    }
    return total;
}

static int disk_cscan(const int *q, int n, int head, int disk_sz,
                      int dir_right, int *order, int *cnt) {
    int sorted[MAX_REQUESTS];
    for (int i = 0; i < n; i++) sorted[i] = q[i];
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (sorted[i] > sorted[j]) {
                int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t;
            }

    int total = 0, cur = head;
    *cnt = 0;

    if (dir_right) {
        for (int i = 0; i < n; i++)
            if (sorted[i] >= head) order[(*cnt)++] = sorted[i];
        if (*cnt == 0 || order[*cnt - 1] != disk_sz - 1)
            order[(*cnt)++] = disk_sz - 1;
        /* Jump to 0 */
        order[(*cnt)++] = 0;
        for (int i = 0; i < n; i++)
            if (sorted[i] < head) order[(*cnt)++] = sorted[i];
    } else {
        for (int i = n - 1; i >= 0; i--)
            if (sorted[i] <= head) order[(*cnt)++] = sorted[i];
        if (*cnt == 0 || order[*cnt - 1] != 0)
            order[(*cnt)++] = 0;
        order[(*cnt)++] = disk_sz - 1;
        for (int i = n - 1; i >= 0; i--)
            if (sorted[i] > head) order[(*cnt)++] = sorted[i];
    }

    cur = head;
    for (int i = 0; i < *cnt; i++) {
        total += abs(cur - order[i]);
        cur = order[i];
    }
    return total;
}

/* Pretty-print a seek trace table */
static void print_trace(const int *order, int cnt, int head) {
    printf("\n  " BWHITE "%-6s %-8s %-14s" RESET "\n", "Step", "Track", "Seek Distance");
    print_line();
    printf("  %-6d %-8d " DIM "- (start)" RESET "\n", 0, head);

    int cur = head, total = 0;
    for (int i = 0; i < cnt; i++) {
        int d = abs(cur - order[i]);
        total += d;
        printf("  %-6d %-8d %d\n", i + 1, order[i], d);
        cur = order[i];
    }
    print_line();
    printf("  " BGREEN "Total Seek Distance: %d" RESET "\n", total);
    printf("  " BCYAN  "Average Seek:        %.1f" RESET "\n",
           cnt > 0 ? (double)total / cnt : 0.0);
}

/* Gather disk-scheduling input once, reuse across calls */
static void get_disk_input(int *q, int *n, int *head, int *dsz) {
    *n    = get_int("Number of requests (1-30): ", 1, MAX_REQUESTS);
    *dsz  = get_pos_int("Disk size (cylinders): ");
    *head = get_int("Current head position: ", 0, *dsz - 1);
    printf("  Enter request queue:\n");
    for (int i = 0; i < *n; i++) {
        char prompt[48];
        sprintf(prompt, "  Request[%d]: ", i);
        q[i] = get_int(prompt, 0, *dsz - 1);
    }
}

static void run_single_disk_algo(void) {
    int q[MAX_REQUESTS], n, head, dsz;
    get_disk_input(q, &n, &head, &dsz);

    int algo = get_int("Algorithm -- 1)FCFS 2)SSTF 3)SCAN 4)C-SCAN: ", 1, 4);

    int order[MAX_REQUESTS + 2], cnt = n, total;
    const char *name = "";
    int dir = 1;

    if (algo >= 3)
        dir = get_int("Direction -- 1)Toward max  0)Toward 0: ", 0, 1);

    switch (algo) {
        case 1: name = "FCFS";   total = disk_fcfs(q, n, head, order);                  break;
        case 2: name = "SSTF";   total = disk_sstf(q, n, head, order);                  break;
        case 3: name = "SCAN";   total = disk_scan(q, n, head, dsz, dir, order, &cnt);  break;
        case 4: name = "C-SCAN"; total = disk_cscan(q, n, head, dsz, dir, order, &cnt); break;
        default: return;
    }

    print_subheader(name);
    printf("  Request queue: ");
    for (int i = 0; i < n; i++) printf("%d%s", q[i], i < n - 1 ? ", " : "\n");
    printf("  Service order: ");
    for (int i = 0; i < cnt; i++) printf("%d%s", order[i], i < cnt - 1 ? ", " : "\n");

    print_trace(order, cnt, head);
    (void)total;
}

static void compare_disk_algos(void) {
    int q[MAX_REQUESTS], n, head, dsz;
    get_disk_input(q, &n, &head, &dsz);
    int dir = get_int("Direction for SCAN/C-SCAN -- 1)Toward max 0)Toward 0: ", 0, 1);

    int order[MAX_REQUESTS + 2];
    int cnt;
    int totals[4];

    totals[0] = disk_fcfs(q, n, head, order);
    totals[1] = disk_sstf(q, n, head, order);
    totals[2] = disk_scan(q, n, head, dsz, dir, order, &cnt);
    totals[3] = disk_cscan(q, n, head, dsz, dir, order, &cnt);

    const char *names[] = {"FCFS", "SSTF", "SCAN", "C-SCAN"};

    int best = 0;
    for (int i = 1; i < 4; i++)
        if (totals[i] < totals[best]) best = i;

    print_subheader("Disk Scheduling Comparison");
    printf("\n  " BWHITE "%-12s %-14s %-10s" RESET "\n",
           "Algorithm", "Total Seek", "Avg Seek");
    print_line();
    for (int i = 0; i < 4; i++) {
        double avg = n > 0 ? (double)totals[i] / n : 0.0;
        if (i == best)
            printf("  " BGREEN "%-12s %-14d %-10.1f  <-- best" RESET "\n",
                   names[i], totals[i], avg);
        else
            printf("  %-12s %-14d %-10.1f\n", names[i], totals[i], avg);
    }
    print_line();
}

/* ================================================================
 *  2.  I/O BUFFERING SIMULATION
 * ================================================================ */

static void io_buffering_sim(void) {
    print_header("I/O BUFFERING SIMULATION");

    int blocks  = get_int("Number of data blocks (1-20): ", 1, 20);
    int t_xfer  = get_pos_int("Transfer time per block (ms): ");
    int t_proc  = get_pos_int("Process time per block (ms): ");

    /* ---- No Buffering ---- */
    int no_buf_total = blocks * (t_xfer + t_proc);

    print_subheader("No Buffering (baseline)");
    printf("  Each block: Transfer then Process sequentially.\n");
    printf("  Time: ");
    for (int i = 0; i < MIN(blocks, 6); i++)
        printf("|" CYAN "--T--" RESET "|" YELLOW "--P--" RESET "|");
    if (blocks > 6) printf(" ...");
    printf("\n  " BWHITE "Total: %d ms" RESET "\n", no_buf_total);

    /* ---- Single Buffering ---- */
    print_subheader("Single Buffering");
    /* First block: transfer only; middle: max(T,C); last: process only */
    int sb_total = t_xfer + (blocks - 1) * MAX(t_xfer, t_proc) + t_proc;
    printf("  Overlap: while process works on buffer, next transfer waits if busy.\n\n");
    printf("  Time:   ");
    for (int i = 0; i < MIN(blocks, 5); i++) {
        printf("|" CYAN "--Transfer--" RESET "|");
        if (i < MIN(blocks, 5) - 1) printf(YELLOW "--Process--|" RESET);
    }
    if (blocks > 5) printf(" ...");
    printf("\n  Buffer: ");
    for (int i = 0; i < MIN(blocks, 5); i++) {
        printf("|" BGREEN "====FULL====" RESET "|");
        if (i < MIN(blocks, 5) - 1) printf(DIM "===EMPTY===" RESET "|");
    }
    if (blocks > 5) printf(" ...");
    printf("\n\n  " BWHITE "Total: %d ms" RESET "\n", sb_total);
    printf("  " BGREEN "Speedup over no-buf: %.2fx" RESET "\n",
           (double)no_buf_total / sb_total);

    /* ---- Double Buffering ---- */
    print_subheader("Double Buffering");
    int db_total = t_xfer + blocks * MAX(t_xfer, t_proc);
    /* Extra process at end if T >= C already included */
    if (t_proc > t_xfer)
        db_total = t_xfer + (blocks - 1) * t_proc + t_proc;

    /* Recalculate cleanly: first transfer alone, then all overlap, last proc */
    db_total = t_xfer + (blocks - 1) * MAX(t_xfer, t_proc) + t_proc;
    /* Slightly tighter when t_proc <= t_xfer: no trailing proc needed */
    if (t_proc <= t_xfer)
        db_total = blocks * t_xfer + t_proc;

    printf("  Two buffers alternate: fill one while processing the other.\n\n");
    printf("  Buf1: ");
    for (int i = 0; i < MIN(blocks, 4); i++)
        printf("|" CYAN "==FILL==" RESET "|        ");
    printf("\n  Buf2:         ");
    for (int i = 0; i < MIN(blocks - 1, 3); i++)
        printf("|" CYAN "==FILL==" RESET "|        ");
    printf("\n  Proc:         ");
    for (int i = 0; i < MIN(blocks, 4); i++)
        printf("|" YELLOW "==PROC==" RESET "|");
    printf("\n\n  " BWHITE "Total: %d ms" RESET "\n", db_total);
    printf("  " BGREEN "Speedup over no-buf: %.2fx" RESET "\n",
           (double)no_buf_total / db_total);

    /* ---- Circular Buffering ---- */
    print_subheader("Circular Buffering");
    int nbuf = get_int("Number of buffers in ring (2-8): ", 2, 8);
    /* With enough buffers the slower side never stalls */
    int cb_total = t_xfer + (blocks - 1) * MAX(t_xfer, t_proc) + t_proc;
    /* With >= 2 buffers already handles full overlap, same as double for
       simple model; real advantage is reduced stall probability. */
    (void)nbuf;

    printf("  %d buffers in a ring.  Producer --> [", nbuf);
    for (int i = 0; i < nbuf; i++) {
        if (i == 0)       printf(BGREEN " P" RESET);
        else if (i == 1)  printf(BYELLOW " C" RESET);
        else              printf(DIM " _" RESET);
    }
    printf(" ] --> Consumer\n");
    printf("  " DIM "(P = producer/fill pointer, C = consumer/process pointer)" RESET "\n\n");
    printf("  " BWHITE "Total: %d ms" RESET "\n", cb_total);
    printf("  " BGREEN "Speedup over no-buf: %.2fx" RESET "\n",
           (double)no_buf_total / cb_total);

    /* Summary table */
    print_subheader("Buffering Summary");
    printf("  " BWHITE "%-22s %-12s %-10s" RESET "\n", "Scheme", "Total (ms)", "Speedup");
    print_line();
    printf("  %-22s %-12d %-10s\n", "No Buffering", no_buf_total, "1.00x");
    printf("  %-22s %-12d %.2fx\n", "Single Buffering", sb_total,
           (double)no_buf_total / sb_total);
    printf("  %-22s %-12d %.2fx\n", "Double Buffering", db_total,
           (double)no_buf_total / db_total);
    printf("  %-22s %-12d %.2fx\n", "Circular Buffering", cb_total,
           (double)no_buf_total / cb_total);
    print_line();
}

/* ================================================================
 *  3.  VIRTUAL FILE SYSTEM (in-memory shell)
 * ================================================================ */

typedef struct {
    int   id;
    char  name[MAX_NAME];
    int   is_directory;
    int   size;
    int   parent_id;
    int   is_active;
    time_t created;
    time_t modified;
} FSEntry;

static FSEntry fs[MAX_FS_ENTRIES];
static int     fs_count;
static int     cwd_id;          /* current working directory id */

static void vfs_init(void) {
    memset(fs, 0, sizeof(fs));
    fs_count = 1;
    fs[0].id           = 0;
    strcpy(fs[0].name, "/");
    fs[0].is_directory = 1;
    fs[0].parent_id    = -1;
    fs[0].is_active    = 1;
    fs[0].created      = time(NULL);
    fs[0].modified     = time(NULL);
    cwd_id = 0;
}

/* Find child by name in directory dir_id. Returns index or -1. */
static int vfs_find(int dir_id, const char *name) {
    for (int i = 0; i < fs_count; i++)
        if (fs[i].is_active && fs[i].parent_id == dir_id &&
            strcmp(fs[i].name, name) == 0)
            return i;
    return -1;
}

/* Check if directory is empty */
static int vfs_dir_empty(int dir_id) {
    for (int i = 0; i < fs_count; i++)
        if (fs[i].is_active && fs[i].parent_id == dir_id) return 0;
    return 1;
}

/* Build absolute path string for a given id */
static void vfs_path(int id, char *buf, int bufsz) {
    if (id == 0) { snprintf(buf, bufsz, "/"); return; }
    char parts[20][MAX_NAME];
    int depth = 0;
    for (int cur = id; cur > 0 && depth < 20; cur = fs[cur].parent_id)
        strncpy(parts[depth++], fs[cur].name, MAX_NAME - 1);
    buf[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        strncat(buf, "/", bufsz - (int)strlen(buf) - 1);
        strncat(buf, parts[i], bufsz - (int)strlen(buf) - 1);
    }
}

/* ---- ls ---- */
static void vfs_ls(void) {
    int found = 0;
    printf("\n  " BWHITE "%-20s %-8s %-8s %-20s" RESET "\n",
           "Name", "Type", "Size", "Modified");
    print_line();
    for (int i = 0; i < fs_count; i++) {
        if (!fs[i].is_active || fs[i].parent_id != cwd_id) continue;
        found = 1;
        char tbuf[32];
        struct tm *tm = localtime(&fs[i].modified);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", tm);
        if (fs[i].is_directory)
            printf("  " BBLUE "%-20s" RESET " %-8s %-8s %-20s\n",
                   fs[i].name, "DIR", "-", tbuf);
        else
            printf("  " WHITE "%-20s" RESET " %-8s %-8d %-20s\n",
                   fs[i].name, "FILE", fs[i].size, tbuf);
    }
    if (!found) printf("  " DIM "(empty directory)" RESET "\n");
}

/* ---- mkdir ---- */
static void vfs_mkdir(const char *name) {
    if (strlen(name) == 0) { printf(BRED "  Usage: mkdir <name>\n" RESET); return; }
    if (vfs_find(cwd_id, name) >= 0) {
        printf(BRED "  '%s' already exists.\n" RESET, name); return;
    }
    if (fs_count >= MAX_FS_ENTRIES) {
        printf(BRED "  Filesystem full!\n" RESET); return;
    }
    int id = fs_count++;
    fs[id].id = id;
    strncpy(fs[id].name, name, MAX_NAME - 1);
    fs[id].is_directory = 1;
    fs[id].parent_id    = cwd_id;
    fs[id].is_active    = 1;
    fs[id].created      = time(NULL);
    fs[id].modified     = time(NULL);
    fs[cwd_id].modified = time(NULL);
    printf(BGREEN "  Directory '%s' created.\n" RESET, name);
}

/* ---- touch ---- */
static void vfs_touch(const char *name, int size) {
    if (strlen(name) == 0) { printf(BRED "  Usage: touch <name> <size>\n" RESET); return; }
    if (vfs_find(cwd_id, name) >= 0) {
        printf(BRED "  '%s' already exists.\n" RESET, name); return;
    }
    if (fs_count >= MAX_FS_ENTRIES) {
        printf(BRED "  Filesystem full!\n" RESET); return;
    }
    int id = fs_count++;
    fs[id].id = id;
    strncpy(fs[id].name, name, MAX_NAME - 1);
    fs[id].is_directory = 0;
    fs[id].size         = size;
    fs[id].parent_id    = cwd_id;
    fs[id].is_active    = 1;
    fs[id].created      = time(NULL);
    fs[id].modified     = time(NULL);
    fs[cwd_id].modified = time(NULL);
    printf(BGREEN "  File '%s' (%d B) created.\n" RESET, name, size);
}

/* ---- cd ---- */
static void vfs_cd(const char *name) {
    if (strcmp(name, "/") == 0)  { cwd_id = 0; return; }
    if (strcmp(name, "..") == 0) {
        if (fs[cwd_id].parent_id >= 0) cwd_id = fs[cwd_id].parent_id;
        return;
    }
    int idx = vfs_find(cwd_id, name);
    if (idx < 0)              { printf(BRED "  '%s' not found.\n" RESET, name); return; }
    if (!fs[idx].is_directory) { printf(BRED "  '%s' is not a directory.\n" RESET, name); return; }
    cwd_id = idx;
}

/* ---- rm ---- */
static void vfs_rm(const char *name) {
    if (strlen(name) == 0) { printf(BRED "  Usage: rm <name>\n" RESET); return; }
    int idx = vfs_find(cwd_id, name);
    if (idx < 0)  { printf(BRED "  '%s' not found.\n" RESET, name); return; }
    if (fs[idx].is_directory && !vfs_dir_empty(idx)) {
        printf(BRED "  Directory '%s' is not empty.\n" RESET, name); return;
    }
    fs[idx].is_active = 0;
    fs[cwd_id].modified = time(NULL);
    printf(BGREEN "  '%s' removed.\n" RESET, name);
}

/* ---- info ---- */
static void vfs_info(const char *name) {
    int idx = vfs_find(cwd_id, name);
    if (idx < 0) { printf(BRED "  '%s' not found.\n" RESET, name); return; }
    char cbuf[32], mbuf[32], pbuf[MAX_PATH_LEN];
    struct tm *tm;
    tm = localtime(&fs[idx].created);  strftime(cbuf, sizeof(cbuf), "%Y-%m-%d %H:%M:%S", tm);
    tm = localtime(&fs[idx].modified); strftime(mbuf, sizeof(mbuf), "%Y-%m-%d %H:%M:%S", tm);
    vfs_path(idx, pbuf, sizeof(pbuf));

    print_subheader("File / Directory Info");
    printf("  Name:     %s\n", fs[idx].name);
    printf("  Path:     %s\n", pbuf);
    printf("  Type:     %s\n", fs[idx].is_directory ? "Directory" : "File");
    if (!fs[idx].is_directory)
        printf("  Size:     %d B\n", fs[idx].size);
    printf("  Created:  %s\n", cbuf);
    printf("  Modified: %s\n", mbuf);
    printf("  ID:       %d\n", fs[idx].id);
}

/* ---- tree (recursive helper) ---- */
static void vfs_tree_r(int dir_id, const char *prefix, int is_last) {
    (void)is_last;
    /* Collect children */
    int children[MAX_FS_ENTRIES], cc = 0;
    for (int i = 0; i < fs_count; i++)
        if (fs[i].is_active && fs[i].parent_id == dir_id)
            children[cc++] = i;

    for (int c = 0; c < cc; c++) {
        int idx = children[c];
        int last = (c == cc - 1);
        printf("  %s%s ", prefix, last ? "+--" : "|--");
        if (fs[idx].is_directory)
            printf(BBLUE "%s/" RESET "\n", fs[idx].name);
        else
            printf(WHITE "%s" RESET " (%d B)\n", fs[idx].name, fs[idx].size);

        if (fs[idx].is_directory) {
            char new_prefix[MAX_PATH_LEN];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix,
                     last ? "    " : "|   ");
            vfs_tree_r(idx, new_prefix, last);
        }
    }
}

static void vfs_tree(void) {
    printf("\n  " BBLUE "/" RESET "\n");
    vfs_tree_r(0, "", 1);
}

/* ---- help ---- */
static void vfs_help(void) {
    print_subheader("VFS Commands");
    printf("  " BCYAN "ls" RESET "              List current directory\n");
    printf("  " BCYAN "mkdir <name>" RESET "    Create subdirectory\n");
    printf("  " BCYAN "touch <name> <sz>" RESET " Create file with given size\n");
    printf("  " BCYAN "cd <name|..>" RESET "    Change directory\n");
    printf("  " BCYAN "rm <name>" RESET "       Remove file or empty directory\n");
    printf("  " BCYAN "pwd" RESET "             Print working directory\n");
    printf("  " BCYAN "tree" RESET "            Show full directory tree\n");
    printf("  " BCYAN "info <name>" RESET "     Show metadata\n");
    printf("  " BCYAN "help" RESET "            Show this help\n");
    printf("  " BCYAN "exit" RESET "            Return to I/O menu\n");
}

/* VFS interactive shell loop */
static void vfs_shell(void) {
    print_header("VIRTUAL FILE SYSTEM SHELL");
    vfs_init();
    vfs_help();

    char line[MAX_CMD];
    while (1) {
        char pbuf[MAX_PATH_LEN];
        vfs_path(cwd_id, pbuf, sizeof(pbuf));
        printf("\n  " BGREEN "%s" RESET " $ ", pbuf);
        if (!fgets(line, sizeof(line), stdin)) break;
        /* strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        /* tokenize */
        char *cmd = strtok(line, " ");
        if (!cmd) continue;

        if (strcmp(cmd, "exit") == 0) break;
        else if (strcmp(cmd, "ls") == 0)    vfs_ls();
        else if (strcmp(cmd, "pwd") == 0) {
            printf("  %s\n", pbuf);
        }
        else if (strcmp(cmd, "tree") == 0)  vfs_tree();
        else if (strcmp(cmd, "help") == 0)  vfs_help();
        else if (strcmp(cmd, "mkdir") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) printf(BRED "  Usage: mkdir <name>\n" RESET);
            else      vfs_mkdir(arg);
        }
        else if (strcmp(cmd, "touch") == 0) {
            char *arg = strtok(NULL, " ");
            char *szs = strtok(NULL, " ");
            if (!arg || !szs) printf(BRED "  Usage: touch <name> <size>\n" RESET);
            else              vfs_touch(arg, atoi(szs));
        }
        else if (strcmp(cmd, "cd") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) printf(BRED "  Usage: cd <name|..>\n" RESET);
            else      vfs_cd(arg);
        }
        else if (strcmp(cmd, "rm") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) printf(BRED "  Usage: rm <name>\n" RESET);
            else      vfs_rm(arg);
        }
        else if (strcmp(cmd, "info") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) printf(BRED "  Usage: info <name>\n" RESET);
            else      vfs_info(arg);
        }
        else {
            printf(BRED "  Unknown command: '%s'. Type 'help'.\n" RESET, cmd);
        }
    }
}

/* ================================================================
 *  4.  FREE SPACE MANAGEMENT
 * ================================================================ */

/* ---- Bitmap (Bit-Vector) ---- */

static int bitmap[MAX_BLOCKS];  /* 1 = free, 0 = allocated */

static void bitmap_init(void) {
    for (int i = 0; i < MAX_BLOCKS; i++) bitmap[i] = 1;
}

static void bitmap_print(int total) {
    int free_cnt = 0;
    printf("\n  " BWHITE "Block bitmap (%d blocks):" RESET "\n  ", total);
    for (int i = 0; i < total; i++) {
        if (bitmap[i]) { printf(BGREEN "%d" RESET, bitmap[i]); free_cnt++; }
        else           printf(RED "0" RESET);
        if ((i + 1) % 4 == 0) printf(" ");
        if ((i + 1) % 32 == 0 && i + 1 < total) printf("\n  ");
    }
    printf("\n  " BCYAN "Free: %d/%d blocks (%.1f%%)" RESET "\n",
           free_cnt, total, 100.0 * free_cnt / total);
}

static void free_space_bitmap(void) {
    int total = get_int("Total blocks (1-64): ", 1, MAX_BLOCKS);
    bitmap_init();

    while (1) {
        bitmap_print(total);
        printf("\n  1) Allocate block(s)  2) Free block(s)  0) Back\n");
        int ch = get_int("  Choice: ", 0, 2);
        if (ch == 0) break;

        if (ch == 1) {
            int nb = get_int("Contiguous blocks needed: ", 1, total);
            /* First-fit search */
            int start = -1;
            for (int i = 0; i <= total - nb; i++) {
                int ok = 1;
                for (int j = 0; j < nb; j++)
                    if (!bitmap[i + j]) { ok = 0; break; }
                if (ok) { start = i; break; }
            }
            if (start < 0) {
                printf(BRED "  No contiguous free region of %d blocks!\n" RESET, nb);
            } else {
                for (int j = 0; j < nb; j++) bitmap[start + j] = 0;
                printf(BGREEN "  Allocated blocks %d-%d.\n" RESET,
                       start, start + nb - 1);
            }
        } else {
            int blk = get_int("Block number to free: ", 0, total - 1);
            if (bitmap[blk])
                printf(BYELLOW "  Block %d is already free.\n" RESET, blk);
            else {
                bitmap[blk] = 1;
                printf(BGREEN "  Block %d freed.\n" RESET, blk);
            }
        }
    }
}

/* ---- Linked List Free Space ---- */

typedef struct FreeNode {
    int block;
    struct FreeNode *next;
} FreeNode;

static FreeNode *free_list = NULL;

static void ll_init(int total) {
    /* Free any old list */
    while (free_list) { FreeNode *t = free_list; free_list = free_list->next; free(t); }
    /* Build list in order */
    FreeNode *tail = NULL;
    for (int i = 0; i < total; i++) {
        FreeNode *n = (FreeNode *)malloc(sizeof(FreeNode));
        n->block = i;
        n->next  = NULL;
        if (!free_list) free_list = n;
        else            tail->next = n;
        tail = n;
    }
}

static void ll_print(int total) {
    printf("\n  " BWHITE "Free list:" RESET " HEAD");
    int count = 0;
    for (FreeNode *p = free_list; p; p = p->next) {
        printf(" -> " BGREEN "[%d]" RESET, p->block);
        count++;
        if (count >= 20 && p->next) { printf(" -> ..."); break; }
    }
    printf(" -> NULL\n");
    printf("  " BCYAN "Free: %d/%d blocks" RESET "\n", count, total);
}

/* Remove first node from free list; return block number or -1. */
static int ll_alloc_one(void) {
    if (!free_list) return -1;
    FreeNode *t = free_list;
    int blk = t->block;
    free_list = t->next;
    free(t);
    return blk;
}

/* Insert block back into list (sorted insertion). */
static void ll_free_one(int blk) {
    FreeNode *n = (FreeNode *)malloc(sizeof(FreeNode));
    n->block = blk;
    n->next  = NULL;
    if (!free_list || blk < free_list->block) {
        n->next = free_list;
        free_list = n;
        return;
    }
    FreeNode *p = free_list;
    while (p->next && p->next->block < blk) p = p->next;
    n->next = p->next;
    p->next = n;
}

/* Check if block is in free list */
static int ll_is_free(int blk) {
    for (FreeNode *p = free_list; p; p = p->next)
        if (p->block == blk) return 1;
    return 0;
}

static void free_space_linked(void) {
    int total = get_int("Total blocks (1-64): ", 1, MAX_BLOCKS);
    ll_init(total);

    while (1) {
        ll_print(total);
        printf("\n  1) Allocate block  2) Free block  0) Back\n");
        int ch = get_int("  Choice: ", 0, 2);
        if (ch == 0) break;

        if (ch == 1) {
            int blk = ll_alloc_one();
            if (blk < 0)
                printf(BRED "  No free blocks!\n" RESET);
            else
                printf(BGREEN "  Allocated block %d.\n" RESET, blk);
        } else {
            int blk = get_int("Block number to free: ", 0, total - 1);
            if (ll_is_free(blk))
                printf(BYELLOW "  Block %d is already free.\n" RESET, blk);
            else {
                ll_free_one(blk);
                printf(BGREEN "  Block %d freed.\n" RESET, blk);
            }
        }
    }
    /* Cleanup */
    while (free_list) { FreeNode *t = free_list; free_list = free_list->next; free(t); }
}

static void free_space_menu(void) {
    while (1) {
        print_header("FREE SPACE MANAGEMENT");
        printf("  1. Bit Vector (Bitmap)\n");
        printf("  2. Linked List\n");
        printf("  0. Back\n");
        int ch = get_int("  Select: ", 0, 2);
        if (ch == 0) return;
        if (ch == 1) free_space_bitmap();
        else         free_space_linked();
    }
}

/* ================================================================
 *  PUBLIC MENU
 * ================================================================ */

void io_file_mgmt_menu(void) {
    while (1) {
        print_header("I/O AND FILE MANAGEMENT");
        printf("  1. Disk Scheduling (FCFS/SSTF/SCAN/C-SCAN)\n");
        printf("  2. Compare Disk Scheduling Algorithms\n");
        printf("  3. I/O Buffering Simulation\n");
        printf("  4. Virtual File System Shell\n");
        printf("  5. Free Space Management\n");
        printf("  0. Back to Main Menu\n");

        int ch = get_int("  Select: ", 0, 5);
        switch (ch) {
            case 0: return;
            case 1: print_header("DISK SCHEDULING"); run_single_disk_algo(); wait_enter(); break;
            case 2: print_header("DISK SCHEDULING COMPARISON"); compare_disk_algos(); wait_enter(); break;
            case 3: io_buffering_sim(); wait_enter(); break;
            case 4: vfs_shell(); break;
            case 5: free_space_menu(); break;
        }
    }
}
