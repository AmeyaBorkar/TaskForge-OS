/* ================================================================
 *  scheduler.c  --  CPU Scheduling Algorithms Simulator
 *  Part of TaskForge: Interactive OS Concepts Simulator
 * ================================================================ */

#include "scheduler.h"

/* ----------------------------------------------------------------
 *  Data Structures
 * ---------------------------------------------------------------- */
typedef struct {
    int pid;
    char name[MAX_NAME];
    int arrival_time;
    int burst_time;
    int priority;        /* lower number = higher priority */
    int remaining_time;
    int start_time;      /* first time it got CPU (-1 if not started) */
    int completion_time;
    int waiting_time;
    int turnaround_time;
    int response_time;
    int started;         /* boolean */
} SchedProcess;

typedef struct {
    int pid;
    int start;
    int end;
} GanttEntry;

#define MAX_GANTT 500
#define MAX_PROCS 20

/* ----------------------------------------------------------------
 *  Module-level state
 * ---------------------------------------------------------------- */
static SchedProcess g_procs[MAX_PROCS];
static int          g_count = 0;
static int          g_data_loaded = 0;

/* ----------------------------------------------------------------
 *  Helper: deep-copy the process array and reset runtime fields
 * ---------------------------------------------------------------- */
static void copy_procs(SchedProcess *dst, const SchedProcess *src, int n)
{
    memcpy(dst, src, (size_t)n * sizeof(SchedProcess));
    for (int i = 0; i < n; i++) {
        dst[i].remaining_time  = dst[i].burst_time;
        dst[i].start_time      = -1;
        dst[i].completion_time = 0;
        dst[i].waiting_time    = 0;
        dst[i].turnaround_time = 0;
        dst[i].response_time   = 0;
        dst[i].started         = 0;
    }
}

/* ----------------------------------------------------------------
 *  Helper: check if all processes are complete
 * ---------------------------------------------------------------- */
static int all_done(const SchedProcess *p, int n)
{
    for (int i = 0; i < n; i++)
        if (p[i].remaining_time > 0) return 0;
    return 1;
}

/* ----------------------------------------------------------------
 *  Helper: compute final statistics from completion times
 * ---------------------------------------------------------------- */
static void calc_stats(SchedProcess *p, int n)
{
    for (int i = 0; i < n; i++) {
        p[i].turnaround_time = p[i].completion_time - p[i].arrival_time;
        p[i].waiting_time    = p[i].turnaround_time - p[i].burst_time;
        p[i].response_time   = p[i].start_time - p[i].arrival_time;
    }
}

/* ----------------------------------------------------------------
 *  Helper: merge consecutive Gantt entries for the same PID
 * ---------------------------------------------------------------- */
static int merge_gantt(GanttEntry *g, int len)
{
    if (len <= 1) return len;
    int w = 0;
    for (int r = 1; r < len; r++) {
        if (g[r].pid == g[w].pid && g[r].start == g[w].end) {
            g[w].end = g[r].end;
        } else {
            w++;
            g[w] = g[r];
        }
    }
    return w + 1;
}

/* ----------------------------------------------------------------
 *  Helper: print the Gantt chart
 * ---------------------------------------------------------------- */
static void print_gantt(const GanttEntry *g, int len)
{
    if (len == 0) return;

    printf("\n");
    print_subheader("Gantt Chart");

    /* --- top border --- */
    printf("  ");
    for (int i = 0; i < len; i++) {
        int dur = g[i].end - g[i].start;
        int width = (dur < 2) ? 5 : dur * 3;
        printf("+");
        for (int j = 0; j < width; j++) putchar('-');
    }
    printf("+\n");

    /* --- process names row --- */
    printf("  ");
    for (int i = 0; i < len; i++) {
        int dur = g[i].end - g[i].start;
        int width = (dur < 2) ? 5 : dur * 3;
        char label[16];
        if (g[i].pid == 0)
            snprintf(label, sizeof(label), "idle");
        else
            snprintf(label, sizeof(label), "P%d", g[i].pid);
        int llen = (int)strlen(label);
        int pad_left = (width - llen) / 2;
        int pad_right = width - llen - pad_left;
        printf("|");
        for (int j = 0; j < pad_left; j++) putchar(' ');
        if (g[i].pid == 0)
            printf(DIM "%s" RESET, label);
        else
            printf(BCYAN "%s" RESET, label);
        for (int j = 0; j < pad_right; j++) putchar(' ');
    }
    printf("|\n");

    /* --- bottom border --- */
    printf("  ");
    for (int i = 0; i < len; i++) {
        int dur = g[i].end - g[i].start;
        int width = (dur < 2) ? 5 : dur * 3;
        printf("+");
        for (int j = 0; j < width; j++) putchar('-');
    }
    printf("+\n");

    /* --- timeline numbers --- */
    printf("  ");
    for (int i = 0; i < len; i++) {
        int dur = g[i].end - g[i].start;
        int width = (dur < 2) ? 5 : dur * 3;
        char num[12];
        snprintf(num, sizeof(num), "%d", g[i].start);
        int nlen = (int)strlen(num);
        printf("%s", num);
        for (int j = 0; j < width - nlen + 1; j++) putchar(' ');
    }
    printf("%d\n", g[len - 1].end);
}

/* ----------------------------------------------------------------
 *  Helper: print per-process statistics table
 * ---------------------------------------------------------------- */
static void print_stats_table(const SchedProcess *p, int n)
{
    printf("\n");
    print_subheader("Per-Process Statistics");

    printf(BOLD "  %-5s %-9s %-7s %-10s %-7s %-8s %-6s %-6s %-9s\n" RESET,
           "PID", "Arrival", "Burst", "Priority", "Start",
           "Finish", "Wait", "TAT", "Response");
    print_line();

    for (int i = 0; i < n; i++) {
        printf("  " BCYAN "%-5s" RESET " %-9d %-7d %-10d %-7d %-8d",
               p[i].name, p[i].arrival_time, p[i].burst_time,
               p[i].priority, p[i].start_time, p[i].completion_time);

        /* color-code waiting time: green if 0, yellow if moderate, red if high */
        if (p[i].waiting_time == 0)
            printf(GREEN "%-6d" RESET " ", p[i].waiting_time);
        else if (p[i].waiting_time <= p[i].burst_time)
            printf(YELLOW "%-6d" RESET " ", p[i].waiting_time);
        else
            printf(RED "%-6d" RESET " ", p[i].waiting_time);

        printf("%-6d %-9d\n", p[i].turnaround_time, p[i].response_time);
    }
    print_line();
}

/* ----------------------------------------------------------------
 *  Helper: print averages
 * ---------------------------------------------------------------- */
static void print_averages(const SchedProcess *p, int n)
{
    double aw = 0, at = 0, ar = 0;
    for (int i = 0; i < n; i++) {
        aw += p[i].waiting_time;
        at += p[i].turnaround_time;
        ar += p[i].response_time;
    }
    aw /= n;  at /= n;  ar /= n;

    printf("\n  " BOLD "Averages:" RESET "\n");
    printf("    Avg Waiting Time    : " BGREEN "%.2f\n" RESET, aw);
    printf("    Avg Turnaround Time : " BGREEN "%.2f\n" RESET, at);
    printf("    Avg Response Time   : " BGREEN "%.2f\n" RESET, ar);
}

/* helper struct for compare-all */
typedef struct {
    double avg_wait;
    double avg_tat;
    double avg_resp;
} AlgoResult;

static AlgoResult get_result(const SchedProcess *p, int n)
{
    AlgoResult r = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        r.avg_wait += p[i].waiting_time;
        r.avg_tat  += p[i].turnaround_time;
        r.avg_resp += p[i].response_time;
    }
    r.avg_wait /= n;
    r.avg_tat  /= n;
    r.avg_resp /= n;
    return r;
}

/* ----------------------------------------------------------------
 *  Helper: display all results for a single algorithm run
 * ---------------------------------------------------------------- */
static void show_results(const char *title, SchedProcess *p, int n,
                         GanttEntry *g, int glen)
{
    print_header(title);
    print_gantt(g, glen);
    print_stats_table(p, n);
    print_averages(p, n);
}

/* ================================================================
 *  Algorithm 1: FCFS (First Come First Serve)
 * ================================================================ */
static void run_fcfs(SchedProcess *p, int n, GanttEntry *g, int *glen)
{
    copy_procs(p, g_procs, n);

    /* sort by arrival time, break ties by pid */
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (p[j].arrival_time < p[i].arrival_time ||
               (p[j].arrival_time == p[i].arrival_time && p[j].pid < p[i].pid)) {
                SchedProcess tmp = p[i]; p[i] = p[j]; p[j] = tmp;
            }

    int time = 0, gi = 0;
    for (int i = 0; i < n; i++) {
        if (time < p[i].arrival_time) {
            g[gi++] = (GanttEntry){0, time, p[i].arrival_time};
            time = p[i].arrival_time;
        }
        p[i].start_time = time;
        p[i].started = 1;
        g[gi++] = (GanttEntry){p[i].pid, time, time + p[i].burst_time};
        time += p[i].burst_time;
        p[i].completion_time = time;
        p[i].remaining_time = 0;
    }
    *glen = merge_gantt(g, gi);
    calc_stats(p, n);
}

/* ================================================================
 *  Algorithm 2: SJF Non-Preemptive
 * ================================================================ */
static void run_sjf_np(SchedProcess *p, int n, GanttEntry *g, int *glen)
{
    copy_procs(p, g_procs, n);

    int time = 0, done = 0, gi = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (p[i].remaining_time > 0 && p[i].arrival_time <= time) {
                if (best == -1 ||
                    p[i].burst_time < p[best].burst_time ||
                   (p[i].burst_time == p[best].burst_time && p[i].pid < p[best].pid))
                    best = i;
            }
        }
        if (best == -1) {
            /* find next arriving process */
            int next_arr = 1 << 30;
            for (int i = 0; i < n; i++)
                if (p[i].remaining_time > 0 && p[i].arrival_time < next_arr)
                    next_arr = p[i].arrival_time;
            g[gi++] = (GanttEntry){0, time, next_arr};
            time = next_arr;
            continue;
        }
        p[best].start_time = time;
        p[best].started = 1;
        g[gi++] = (GanttEntry){p[best].pid, time, time + p[best].burst_time};
        time += p[best].burst_time;
        p[best].completion_time = time;
        p[best].remaining_time = 0;
        done++;
    }
    *glen = merge_gantt(g, gi);
    calc_stats(p, n);
}

/* ================================================================
 *  Algorithm 3: SRTF (Shortest Remaining Time First) -- Preemptive
 * ================================================================ */
static void run_srtf(SchedProcess *p, int n, GanttEntry *g, int *glen)
{
    copy_procs(p, g_procs, n);

    int time = 0, gi = 0;

    /* find earliest arrival */
    int min_arr = p[0].arrival_time;
    for (int i = 1; i < n; i++)
        if (p[i].arrival_time < min_arr) min_arr = p[i].arrival_time;
    time = min_arr;

    while (!all_done(p, n)) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (p[i].remaining_time > 0 && p[i].arrival_time <= time) {
                if (best == -1 ||
                    p[i].remaining_time < p[best].remaining_time ||
                   (p[i].remaining_time == p[best].remaining_time &&
                    p[i].pid < p[best].pid))
                    best = i;
            }
        }
        if (best == -1) {
            int next_arr = 1 << 30;
            for (int i = 0; i < n; i++)
                if (p[i].remaining_time > 0 && p[i].arrival_time < next_arr)
                    next_arr = p[i].arrival_time;
            g[gi++] = (GanttEntry){0, time, next_arr};
            time = next_arr;
            continue;
        }
        if (!p[best].started) {
            p[best].start_time = time;
            p[best].started = 1;
        }
        g[gi++] = (GanttEntry){p[best].pid, time, time + 1};
        p[best].remaining_time--;
        time++;
        if (p[best].remaining_time == 0)
            p[best].completion_time = time;
    }
    *glen = merge_gantt(g, gi);
    calc_stats(p, n);
}

/* ================================================================
 *  Algorithm 4: Round Robin
 * ================================================================ */
static void run_rr(SchedProcess *p, int n, GanttEntry *g, int *glen, int quantum)
{
    copy_procs(p, g_procs, n);

    /* sort a copy of indices by arrival time for enqueue order */
    int order[MAX_PROCS];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (p[order[j]].arrival_time < p[order[i]].arrival_time ||
               (p[order[j]].arrival_time == p[order[i]].arrival_time &&
                p[order[j]].pid < p[order[i]].pid)) {
                int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
            }

    /* circular queue */
    int queue[MAX_PROCS * 20];
    int qf = 0, qr = 0;

    int time = p[order[0]].arrival_time;
    int gi = 0;
    int next_ord = 0; /* next index in order[] to enqueue */

    /* enqueue all processes arriving at time */
    while (next_ord < n && p[order[next_ord]].arrival_time <= time) {
        queue[qr++] = order[next_ord];
        next_ord++;
    }

    while (qf < qr || !all_done(p, n)) {
        if (qf == qr) {
            /* queue empty but work remains -- CPU idle until next arrival */
            int next_arr = 1 << 30;
            for (int i = 0; i < n; i++)
                if (p[i].remaining_time > 0 && p[i].arrival_time < next_arr)
                    next_arr = p[i].arrival_time;
            g[gi++] = (GanttEntry){0, time, next_arr};
            time = next_arr;
            while (next_ord < n && p[order[next_ord]].arrival_time <= time) {
                queue[qr++] = order[next_ord];
                next_ord++;
            }
            continue;
        }

        int idx = queue[qf++];
        if (!p[idx].started) {
            p[idx].start_time = time;
            p[idx].started = 1;
        }

        int run = (p[idx].remaining_time < quantum) ?
                   p[idx].remaining_time : quantum;
        g[gi++] = (GanttEntry){p[idx].pid, time, time + run};
        p[idx].remaining_time -= run;
        time += run;

        if (p[idx].remaining_time == 0)
            p[idx].completion_time = time;

        /* enqueue newly arrived processes (before re-enqueueing current) */
        while (next_ord < n && p[order[next_ord]].arrival_time <= time) {
            queue[qr++] = order[next_ord];
            next_ord++;
        }

        /* re-enqueue current process if it still has work */
        if (p[idx].remaining_time > 0)
            queue[qr++] = idx;
    }
    *glen = merge_gantt(g, gi);
    calc_stats(p, n);
}

/* ================================================================
 *  Algorithm 5: Priority Non-Preemptive
 * ================================================================ */
static void run_prio_np(SchedProcess *p, int n, GanttEntry *g, int *glen)
{
    copy_procs(p, g_procs, n);

    int time = 0, done = 0, gi = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (p[i].remaining_time > 0 && p[i].arrival_time <= time) {
                if (best == -1 ||
                    p[i].priority < p[best].priority ||
                   (p[i].priority == p[best].priority && p[i].pid < p[best].pid))
                    best = i;
            }
        }
        if (best == -1) {
            int next_arr = 1 << 30;
            for (int i = 0; i < n; i++)
                if (p[i].remaining_time > 0 && p[i].arrival_time < next_arr)
                    next_arr = p[i].arrival_time;
            g[gi++] = (GanttEntry){0, time, next_arr};
            time = next_arr;
            continue;
        }
        p[best].start_time = time;
        p[best].started = 1;
        g[gi++] = (GanttEntry){p[best].pid, time, time + p[best].burst_time};
        time += p[best].burst_time;
        p[best].completion_time = time;
        p[best].remaining_time = 0;
        done++;
    }
    *glen = merge_gantt(g, gi);
    calc_stats(p, n);
}

/* ================================================================
 *  Algorithm 6: Priority Preemptive
 * ================================================================ */
static void run_prio_p(SchedProcess *p, int n, GanttEntry *g, int *glen)
{
    copy_procs(p, g_procs, n);

    int min_arr = p[0].arrival_time;
    for (int i = 1; i < n; i++)
        if (p[i].arrival_time < min_arr) min_arr = p[i].arrival_time;
    int time = min_arr, gi = 0;

    while (!all_done(p, n)) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (p[i].remaining_time > 0 && p[i].arrival_time <= time) {
                if (best == -1 ||
                    p[i].priority < p[best].priority ||
                   (p[i].priority == p[best].priority && p[i].pid < p[best].pid))
                    best = i;
            }
        }
        if (best == -1) {
            int next_arr = 1 << 30;
            for (int i = 0; i < n; i++)
                if (p[i].remaining_time > 0 && p[i].arrival_time < next_arr)
                    next_arr = p[i].arrival_time;
            g[gi++] = (GanttEntry){0, time, next_arr};
            time = next_arr;
            continue;
        }
        if (!p[best].started) {
            p[best].start_time = time;
            p[best].started = 1;
        }
        g[gi++] = (GanttEntry){p[best].pid, time, time + 1};
        p[best].remaining_time--;
        time++;
        if (p[best].remaining_time == 0)
            p[best].completion_time = time;
    }
    *glen = merge_gantt(g, gi);
    calc_stats(p, n);
}

/* ================================================================
 *  Input: enter process data
 * ================================================================ */
static void input_processes(void)
{
    print_header("ENTER PROCESS DATA");

    g_count = get_int("Number of processes (1-20): ", 1, MAX_PROCS);

    printf("\n  For each process enter: Arrival Time, Burst Time, Priority\n");
    printf("  " DIM "(Lower priority number = higher priority)" RESET "\n\n");

    for (int i = 0; i < g_count; i++) {
        printf(BCYAN "  --- Process P%d ---\n" RESET, i + 1);
        g_procs[i].pid = i + 1;
        snprintf(g_procs[i].name, MAX_NAME, "P%d", i + 1);
        g_procs[i].arrival_time = get_nn_int("  Arrival Time : ");
        g_procs[i].burst_time   = get_pos_int("  Burst Time   : ");
        g_procs[i].priority     = get_nn_int("  Priority     : ");
        printf("\n");
    }

    g_data_loaded = 1;
    printf(BGREEN "  Process data saved successfully! (%d processes)\n" RESET,
           g_count);

    /* show summary */
    print_subheader("Process Summary");
    printf(BOLD "  %-5s %-10s %-10s %-10s\n" RESET,
           "PID", "Arrival", "Burst", "Priority");
    print_line();
    for (int i = 0; i < g_count; i++) {
        printf("  " BCYAN "%-5s" RESET " %-10d %-10d %-10d\n",
               g_procs[i].name, g_procs[i].arrival_time,
               g_procs[i].burst_time, g_procs[i].priority);
    }
    print_line();
}

/* ----------------------------------------------------------------
 *  Guard: ensure data is loaded before running any algorithm
 * ---------------------------------------------------------------- */
static int check_data(void)
{
    if (!g_data_loaded) {
        printf(BRED "\n  [!] No process data! "
               "Please select option 1 first.\n" RESET);
        return 0;
    }
    return 1;
}

/* ================================================================
 *  Compare All Algorithms
 * ================================================================ */
static void compare_all(void)
{
    if (!check_data()) return;

    int quantum = get_int("  Round Robin quantum (1-100): ", 1, 100);

    SchedProcess tmp[MAX_PROCS];
    GanttEntry   gantt[MAX_GANTT];
    int          glen;
    AlgoResult   results[6];

    const char *names[] = {
        "FCFS",
        "SJF (Non-Preemptive)",
        "SRTF (Preemptive)",
        "Round Robin",
        "Priority (Non-Preemptive)",
        "Priority (Preemptive)"
    };

    run_fcfs(tmp, g_count, gantt, &glen);
    results[0] = get_result(tmp, g_count);

    run_sjf_np(tmp, g_count, gantt, &glen);
    results[1] = get_result(tmp, g_count);

    run_srtf(tmp, g_count, gantt, &glen);
    results[2] = get_result(tmp, g_count);

    run_rr(tmp, g_count, gantt, &glen, quantum);
    results[3] = get_result(tmp, g_count);

    run_prio_np(tmp, g_count, gantt, &glen);
    results[4] = get_result(tmp, g_count);

    run_prio_p(tmp, g_count, gantt, &glen);
    results[5] = get_result(tmp, g_count);

    /* find best (lowest) in each column */
    double best_w = results[0].avg_wait;
    double best_t = results[0].avg_tat;
    double best_r = results[0].avg_resp;
    for (int i = 1; i < 6; i++) {
        if (results[i].avg_wait < best_w) best_w = results[i].avg_wait;
        if (results[i].avg_tat  < best_t) best_t = results[i].avg_tat;
        if (results[i].avg_resp < best_r) best_r = results[i].avg_resp;
    }

    /* display comparison table */
    print_header("ALGORITHM COMPARISON");

    /* build RR label with quantum shown */
    char rr_label[64];
    snprintf(rr_label, sizeof(rr_label), "Round Robin (Q=%d)", quantum);

    printf(BOLD "  %-28s %-12s %-12s %-12s\n" RESET,
           "Algorithm", "Avg Wait", "Avg TAT", "Avg Response");
    print_sep();

    for (int i = 0; i < 6; i++) {
        const char *lbl = (i == 3) ? rr_label : names[i];

        printf("  %-28s ", lbl);

        /* Avg Wait */
        if (results[i].avg_wait <= best_w + 0.001)
            printf(BGREEN "%-12.2f" RESET, results[i].avg_wait);
        else
            printf("%-12.2f", results[i].avg_wait);

        /* Avg TAT */
        if (results[i].avg_tat <= best_t + 0.001)
            printf(BGREEN "%-12.2f" RESET, results[i].avg_tat);
        else
            printf("%-12.2f", results[i].avg_tat);

        /* Avg Response */
        if (results[i].avg_resp <= best_r + 0.001)
            printf(BGREEN "%-12.2f" RESET, results[i].avg_resp);
        else
            printf("%-12.2f", results[i].avg_resp);

        printf("\n");
    }
    print_sep();
    printf("  " DIM "(Best values highlighted in green)" RESET "\n");
}

/* ================================================================
 *  Public entry point: scheduler_menu()
 * ================================================================ */
void scheduler_menu(void)
{
    SchedProcess procs[MAX_PROCS];
    GanttEntry   gantt[MAX_GANTT];
    int          glen;

    while (1) {
        print_header("CPU SCHEDULING ALGORITHMS");

        printf("  " BWHITE "1." RESET " Enter Process Data\n");
        printf("  " BWHITE "2." RESET " FCFS\n");
        printf("  " BWHITE "3." RESET " SJF (Non-Preemptive)\n");
        printf("  " BWHITE "4." RESET " SRTF (Preemptive)\n");
        printf("  " BWHITE "5." RESET " Round Robin\n");
        printf("  " BWHITE "6." RESET " Priority (Non-Preemptive)\n");
        printf("  " BWHITE "7." RESET " Priority (Preemptive)\n");
        printf("  " BWHITE "8." RESET " Compare All Algorithms\n");
        printf("  " BRED  "0." RESET " Back to Main Menu\n");
        print_line();

        int ch = get_int("Choose option: ", 0, 8);

        switch (ch) {
        case 0:
            return;

        case 1:
            input_processes();
            wait_enter();
            break;

        case 2:
            if (!check_data()) { wait_enter(); break; }
            run_fcfs(procs, g_count, gantt, &glen);
            show_results("FCFS (First Come First Serve)", procs, g_count,
                         gantt, glen);
            wait_enter();
            break;

        case 3:
            if (!check_data()) { wait_enter(); break; }
            run_sjf_np(procs, g_count, gantt, &glen);
            show_results("SJF (Non-Preemptive)", procs, g_count,
                         gantt, glen);
            wait_enter();
            break;

        case 4:
            if (!check_data()) { wait_enter(); break; }
            run_srtf(procs, g_count, gantt, &glen);
            show_results("SRTF (Shortest Remaining Time First)",
                         procs, g_count, gantt, glen);
            wait_enter();
            break;

        case 5:
            if (!check_data()) { wait_enter(); break; }
            {
                int q = get_int("  Time quantum (1-100): ", 1, 100);
                run_rr(procs, g_count, gantt, &glen, q);
                char title[64];
                snprintf(title, sizeof(title),
                         "Round Robin (Quantum = %d)", q);
                show_results(title, procs, g_count, gantt, glen);
            }
            wait_enter();
            break;

        case 6:
            if (!check_data()) { wait_enter(); break; }
            run_prio_np(procs, g_count, gantt, &glen);
            show_results("Priority Scheduling (Non-Preemptive)",
                         procs, g_count, gantt, glen);
            wait_enter();
            break;

        case 7:
            if (!check_data()) { wait_enter(); break; }
            run_prio_p(procs, g_count, gantt, &glen);
            show_results("Priority Scheduling (Preemptive)",
                         procs, g_count, gantt, glen);
            wait_enter();
            break;

        case 8:
            compare_all();
            wait_enter();
            break;
        }
    }
}
