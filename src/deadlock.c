/*  deadlock.c  --  Deadlock Management module for TaskForge
 *  Covers: Banker's Algorithm, Resource Allocation Graph,
 *          Deadlock Detection, Prevention Strategies, Recovery.
 *  Compile: gcc -Wall -Wextra -std=c11 -Iinclude -o deadlock src/deadlock.c
 */

#include "deadlock.h"

/* ================================================================
 *  Limits (bounded by MAX_PROCESSES / MAX_RESOURCES from common.h)
 * ================================================================ */
#define MP MAX_PROCESSES
#define MR MAX_RESOURCES

/* ================================================================
 *  Utility: print a matrix with column headers (R0 R1 ...)
 * ================================================================ */
static void print_matrix(const char *label, int mat[][MR], int n, int m)
{
    printf("\n  " BWHITE "%s:" RESET "\n", label);
    printf("       ");
    for (int j = 0; j < m; j++) printf(" R%-3d", j);
    printf("\n");
    for (int i = 0; i < n; i++) {
        printf("  P%-3d|", i);
        for (int j = 0; j < m; j++) printf(" %-4d", mat[i][j]);
        printf("\n");
    }
}

/* Print a single vector with resource labels */
static void print_vector(const char *label, int v[], int m)
{
    printf("  %-14s", label);
    for (int j = 0; j < m; j++) printf(" R%d=%-3d", j, v[j]);
    printf("\n");
}

/* ================================================================
 *  Input helpers for matrices / vectors
 * ================================================================ */
static void input_vector(const char *label, int v[], int m)
{
    printf("  Enter %s (%d values): ", label, m);
    for (int j = 0; j < m; j++) {
        while (scanf("%d", &v[j]) != 1 || v[j] < 0) {
            flush_input();
            printf(BRED "  Invalid! Re-enter value %d: " RESET, j);
        }
    }
    flush_input();
}

static void input_matrix(const char *label, int mat[][MR], int n, int m)
{
    printf("\n  " BWHITE "Enter %s (%dx%d):" RESET "\n", label, n, m);
    for (int i = 0; i < n; i++) {
        printf("  P%d: ", i);
        for (int j = 0; j < m; j++) {
            while (scanf("%d", &mat[i][j]) != 1 || mat[i][j] < 0) {
                flush_input();
                printf(BRED "  Invalid! Re-enter P%d R%d: " RESET, i, j);
            }
        }
        flush_input();
    }
}

/* ================================================================
 *  1. Banker's Algorithm  --  Safety Check
 * ================================================================ */
static int bankers_safety(int n, int m, int avail[], int alloc[][MR],
                          int need[][MR], int safe_seq[])
{
    int work[MR], finish[MP];
    for (int j = 0; j < m; j++) work[j] = avail[j];
    memset(finish, 0, sizeof(int) * (size_t)n);

    print_subheader("Safety Algorithm -- Step-by-step");

    int count = 0;
    while (count < n) {
        int found = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < m; j++) {
                if (need[i][j] > work[j]) { ok = 0; break; }
            }
            if (!ok) continue;

            /* Process i can finish */
            printf("  Step %d: P%d can proceed  |  Work: [", count + 1, i);
            for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", work[j]);
            printf("]  +  Alloc: [");
            for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", alloc[i][j]);

            for (int j = 0; j < m; j++) work[j] += alloc[i][j];

            printf("]  =>  Work: [");
            for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", work[j]);
            printf("]\n");

            finish[i] = 1;
            safe_seq[count++] = i;
            found = 1;
            break;                       /* restart scan */
        }
        if (!found) break;
    }

    print_line();
    if (count == n) {
        printf(BGREEN "\n  SAFE STATE -- Safe sequence: < ");
        for (int i = 0; i < n; i++) printf("P%d%s", safe_seq[i], i < n - 1 ? " -> " : "");
        printf(" >\n" RESET);
        return 1;
    }
    printf(BRED "\n  UNSAFE STATE -- No safe sequence exists!\n" RESET);
    printf("  Processes that cannot finish:");
    for (int i = 0; i < n; i++)
        if (!finish[i]) printf(" P%d", i);
    printf("\n");
    return 0;
}

static void banker_safety_demo(void)
{
    print_header("BANKER'S ALGORITHM -- Safety Check");

    int n = get_int("Number of processes (1-10): ", 1, 10);
    int m = get_int("Number of resource types (1-5): ", 1, 5);

    int avail[MR], alloc[MP][MR], max[MP][MR], need[MP][MR];

    input_vector("Available", avail, m);
    input_matrix("Allocation matrix", alloc, n, m);
    input_matrix("Max matrix", max, n, m);

    /* Compute Need = Max - Allocation */
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            need[i][j] = max[i][j] - alloc[i][j];
            if (need[i][j] < 0) {
                printf(BRED "  Error: Allocation exceeds Max for P%d R%d!\n" RESET, i, j);
                wait_enter();
                return;
            }
        }

    print_matrix("Allocation", alloc, n, m);
    print_matrix("Max", max, n, m);
    print_matrix("Need (Max - Alloc)", need, n, m);
    printf("\n");
    print_vector("Available:", avail, m);
    printf("\n");

    int safe_seq[MP];
    bankers_safety(n, m, avail, alloc, need, safe_seq);
    wait_enter();
}

/* ================================================================
 *  2. Banker's Algorithm  --  Resource Request
 * ================================================================ */
static void banker_request_demo(void)
{
    print_header("BANKER'S ALGORITHM -- Resource Request");

    int n = get_int("Number of processes (1-10): ", 1, 10);
    int m = get_int("Number of resource types (1-5): ", 1, 5);

    int avail[MR], alloc[MP][MR], max[MP][MR], need[MP][MR];

    input_vector("Available", avail, m);
    input_matrix("Allocation matrix", alloc, n, m);
    input_matrix("Max matrix", max, n, m);

    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            need[i][j] = max[i][j] - alloc[i][j];
            if (need[i][j] < 0) {
                printf(BRED "  Error: Allocation exceeds Max for P%d R%d!\n" RESET, i, j);
                wait_enter();
                return;
            }
        }

    print_matrix("Allocation", alloc, n, m);
    print_matrix("Max", max, n, m);
    print_matrix("Need", need, n, m);
    printf("\n");
    print_vector("Available:", avail, m);

    /* Get the request */
    int pid = get_int("\n  Requesting process id (0-n): ", 0, n - 1);
    int req[MR];
    printf("  Enter request vector for P%d (%d values): ", pid, m);
    for (int j = 0; j < m; j++) {
        while (scanf("%d", &req[j]) != 1 || req[j] < 0) {
            flush_input();
            printf(BRED "  Re-enter value %d: " RESET, j);
        }
    }
    flush_input();

    print_subheader("Processing Request");

    /* Step 1: Request <= Need? */
    printf("  Check 1: Request <= Need[P%d]?\n", pid);
    printf("    Request: [");
    for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", req[j]);
    printf("]  Need: [");
    for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", need[pid][j]);
    printf("]\n");

    for (int j = 0; j < m; j++) {
        if (req[j] > need[pid][j]) {
            printf(BRED "  DENIED -- Request exceeds maximum claim for R%d!\n" RESET, j);
            wait_enter();
            return;
        }
    }
    printf(BGREEN "    Passed.\n" RESET);

    /* Step 2: Request <= Available? */
    printf("  Check 2: Request <= Available?\n");
    printf("    Request: [");
    for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", req[j]);
    printf("]  Available: [");
    for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", avail[j]);
    printf("]\n");

    for (int j = 0; j < m; j++) {
        if (req[j] > avail[j]) {
            printf(BRED "  DENIED -- Not enough resources. P%d must wait.\n" RESET, pid);
            wait_enter();
            return;
        }
    }
    printf(BGREEN "    Passed.\n" RESET);

    /* Step 3: Pretend allocate */
    printf("\n  Tentatively allocating resources...\n");
    for (int j = 0; j < m; j++) {
        avail[j]       -= req[j];
        alloc[pid][j]  += req[j];
        need[pid][j]   -= req[j];
    }
    print_vector("New Available:", avail, m);
    print_matrix("New Allocation", alloc, n, m);
    print_matrix("New Need", need, n, m);

    /* Step 4: Safety check */
    int safe_seq[MP];
    if (bankers_safety(n, m, avail, alloc, need, safe_seq)) {
        printf(BGREEN "\n  REQUEST GRANTED for P%d.\n" RESET, pid);
    } else {
        /* Rollback */
        for (int j = 0; j < m; j++) {
            avail[j]      += req[j];
            alloc[pid][j] -= req[j];
            need[pid][j]  += req[j];
        }
        printf(BRED "\n  REQUEST DENIED -- would lead to unsafe state. Rolled back.\n" RESET);
    }
    wait_enter();
}

/* ================================================================
 *  3. Resource Allocation Graph (RAG)  +  Cycle Detection
 * ================================================================ */

/* Edge types stored in adjacency list style */
#define MAX_EDGES 100

typedef struct {
    int from, to;   /* process index or resource index (offset by n) */
    int is_request;  /* 1 = process->resource, 0 = resource->process */
} Edge;

static int rag_visited[MP + MR], rag_recstack[MP + MR];
static int rag_adj[MP + MR][MP + MR];
static int cycle_path[MP + MR], cycle_len;

static int rag_dfs(int v, int total)
{
    rag_visited[v] = 1;
    rag_recstack[v] = 1;
    cycle_path[cycle_len++] = v;

    for (int u = 0; u < total; u++) {
        if (!rag_adj[v][u]) continue;
        if (!rag_visited[u]) {
            if (rag_dfs(u, total)) return 1;
        } else if (rag_recstack[u]) {
            cycle_path[cycle_len++] = u;   /* close the cycle */
            return 1;
        }
    }
    cycle_path[--cycle_len] = 0;          /* backtrack */
    rag_recstack[v] = 0;
    return 0;
}

static void rag_demo(void)
{
    print_header("RESOURCE ALLOCATION GRAPH");

    int n = get_int("Number of processes (1-10): ", 1, 10);
    int r = get_int("Number of resources (1-5): ", 1, 5);

    int instances[MR];
    for (int j = 0; j < r; j++) {
        char buf[64];
        sprintf(buf, "Instances of R%d: ", j);
        instances[j] = get_pos_int(buf);
    }

    int total = n + r;                   /* nodes: P0..Pn-1, R0..Rr-1 */
    memset(rag_adj, 0, sizeof(rag_adj));

    Edge edges[MAX_EDGES];
    int ne = 0;

    int num_assign = get_nn_int("Number of assignment edges (Resource -> Process): ");
    for (int e = 0; e < num_assign && ne < MAX_EDGES; e++) {
        int ri = get_int("  Resource id: ", 0, r - 1);
        int pi = get_int("  -> Process id: ", 0, n - 1);
        rag_adj[n + ri][pi] = 1;         /* resource node -> process node */
        edges[ne++] = (Edge){n + ri, pi, 0};
    }

    int num_req = get_nn_int("Number of request edges (Process -> Resource): ");
    for (int e = 0; e < num_req && ne < MAX_EDGES; e++) {
        int pi = get_int("  Process id: ", 0, n - 1);
        int ri = get_int("  -> Resource id: ", 0, r - 1);
        rag_adj[pi][n + ri] = 1;         /* process node -> resource node */
        edges[ne++] = (Edge){pi, n + ri, 1};
    }

    /* Display the graph */
    print_subheader("Resource Allocation Graph");
    printf("  Processes: ");
    for (int i = 0; i < n; i++) printf("P%d%s", i, i < n - 1 ? ", " : "\n");
    printf("  Resources: ");
    for (int j = 0; j < r; j++) printf("R%d(%d)%s", j, instances[j], j < r - 1 ? ", " : "\n");

    printf("\n  " BWHITE "Assignments (Resource -> Process):" RESET "\n");
    for (int e = 0; e < ne; e++)
        if (!edges[e].is_request)
            printf("    R%d -> P%d\n", edges[e].from - n, edges[e].to);

    printf("\n  " BWHITE "Requests (Process -> Resource):" RESET "\n");
    for (int e = 0; e < ne; e++)
        if (edges[e].is_request)
            printf("    P%d -> R%d\n", edges[e].from, edges[e].to - n);

    /* Cycle detection (DFS) -- valid for single-instance resources */
    int all_single = 1;
    for (int j = 0; j < r; j++)
        if (instances[j] > 1) { all_single = 0; break; }

    print_subheader("Cycle Detection (DFS)");
    if (!all_single) {
        printf(BYELLOW "  Note: Multi-instance resources detected. Cycle does NOT\n"
               "  guarantee deadlock, but may indicate one.\n" RESET);
    }

    memset(rag_visited, 0, sizeof(rag_visited));
    memset(rag_recstack, 0, sizeof(rag_recstack));
    cycle_len = 0;

    int found_cycle = 0;
    for (int v = 0; v < total && !found_cycle; v++) {
        if (!rag_visited[v]) {
            cycle_len = 0;
            if (rag_dfs(v, total)) { found_cycle = 1; }
        }
    }

    if (found_cycle) {
        printf(BRED "  CYCLE DETECTED -- Deadlock may exist!\n  Cycle: ");
        /* Print from the start of the actual cycle to its close */
        int start = cycle_path[cycle_len - 1];  /* repeated node */
        int printing = 0;
        for (int k = 0; k < cycle_len; k++) {
            if (cycle_path[k] == start) printing = 1;
            if (printing) {
                int id = cycle_path[k];
                if (id < n) printf("P%d", id);
                else        printf("R%d", id - n);
                printf(" -> ");
            }
        }
        /* close with the start node again */
        if (start < n) printf("P%d", start);
        else           printf("R%d", start - n);
        printf(RESET "\n");
    } else {
        printf(BGREEN "  No cycle detected -- No deadlock.\n" RESET);
    }
    wait_enter();
}

/* ================================================================
 *  4. Deadlock Detection Algorithm
 * ================================================================ */
static int detection_run(int n, int m, int avail[], int alloc[][MR],
                         int request[][MR], int deadlocked[])
{
    int work[MR], finish[MP];
    for (int j = 0; j < m; j++) work[j] = avail[j];

    /* Initially, mark processes with zero allocation as finished */
    for (int i = 0; i < n; i++) {
        finish[i] = 1;
        for (int j = 0; j < m; j++) {
            if (alloc[i][j] > 0) { finish[i] = 0; break; }
        }
    }

    print_subheader("Detection Algorithm -- Step-by-step");
    int count = 0;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < m; j++) {
                if (request[i][j] > work[j]) { ok = 0; break; }
            }
            if (!ok) continue;

            printf("  Step %d: P%d can complete  |  Work: [", ++count, i);
            for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", work[j]);
            printf("]  +  Alloc: [");
            for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", alloc[i][j]);

            for (int j = 0; j < m; j++) work[j] += alloc[i][j];

            printf("]  =>  Work: [");
            for (int j = 0; j < m; j++) printf("%s%d", j ? ", " : "", work[j]);
            printf("]\n");

            finish[i] = 1;
            changed = 1;
        }
    }

    print_line();
    int dl_count = 0;
    for (int i = 0; i < n; i++) {
        deadlocked[i] = !finish[i];
        if (deadlocked[i]) dl_count++;
    }

    if (dl_count == 0) {
        printf(BGREEN "\n  No deadlock detected -- all processes can complete.\n" RESET);
    } else {
        printf(BRED "\n  DEADLOCK DETECTED! Deadlocked processes:");
        for (int i = 0; i < n; i++)
            if (deadlocked[i]) printf(" P%d", i);
        printf("\n" RESET);
    }
    return dl_count;
}

static void detection_demo(void)
{
    print_header("DEADLOCK DETECTION ALGORITHM");

    int n = get_int("Number of processes (1-10): ", 1, 10);
    int m = get_int("Number of resource types (1-5): ", 1, 5);

    int avail[MR], alloc[MP][MR], request[MP][MR], deadlocked[MP];

    input_vector("Available", avail, m);
    input_matrix("Allocation matrix", alloc, n, m);
    input_matrix("Request matrix", request, n, m);

    print_matrix("Allocation", alloc, n, m);
    print_matrix("Request", request, n, m);
    printf("\n");
    print_vector("Available:", avail, m);
    printf("\n");

    detection_run(n, m, avail, alloc, request, deadlocked);
    wait_enter();
}

/* ================================================================
 *  5. Deadlock Prevention Strategies (educational demo)
 * ================================================================ */
static void prevention_demo(void)
{
    print_header("DEADLOCK PREVENTION STRATEGIES");
    printf("\n  Deadlock requires ALL four Coffman conditions simultaneously.\n"
           "  Breaking any one prevents deadlock.\n");

    print_subheader("1. Mutual Exclusion");
    printf("  " BYELLOW "Condition:" RESET " Resources are held in a non-sharable mode.\n\n");
    printf("  " BWHITE "Before (deadlock possible):" RESET "\n");
    printf("    P0 holds Printer exclusively, P1 waits for Printer.\n\n");
    printf("  " BWHITE "After  (prevention):" RESET "\n");
    printf("    Use spooling: all jobs write to a spool queue.\n");
    printf("    Only the spooler daemon accesses the printer.\n");
    printf("    " BGREEN "=> No exclusive holding by user processes.\n" RESET);
    printf("\n  " DIM "Note: some resources (e.g., mutex locks) are inherently\n"
           "  non-sharable; this condition cannot always be broken.\n" RESET);
    print_line();

    print_subheader("2. Hold and Wait");
    printf("  " BYELLOW "Condition:" RESET " Process holds resources while waiting for others.\n\n");
    printf("  " BWHITE "Before (deadlock possible):" RESET "\n");
    printf("    P0 holds R0, requests R1.\n");
    printf("    P1 holds R1, requests R0.\n");
    printf("    Both hold and wait => potential deadlock.\n\n");
    printf("  " BWHITE "After  (prevention -- request all at once):" RESET "\n");
    printf("    P0 requests {R0, R1} atomically before execution.\n");
    printf("    If both are free => granted.  Otherwise => P0 waits\n");
    printf("    without holding anything.\n");
    printf("    " BGREEN "=> No process holds resources while waiting.\n" RESET);
    print_line();

    print_subheader("3. No Preemption");
    printf("  " BYELLOW "Condition:" RESET " Resources cannot be forcibly taken.\n\n");
    printf("  " BWHITE "Before (deadlock possible):" RESET "\n");
    printf("    P0 holds R0, needs R1 (held by P1). Neither yields.\n\n");
    printf("  " BWHITE "After  (prevention -- allow preemption):" RESET "\n");
    printf("    If P0 requests R1 and it is unavailable:\n");
    printf("      - P0's held resources (R0) are preempted (released).\n");
    printf("      - P0 restarts only when R0 AND R1 are available.\n");
    printf("    " BGREEN "=> Resources can be reclaimed; no indefinite hold.\n" RESET);
    print_line();

    print_subheader("4. Circular Wait");
    printf("  " BYELLOW "Condition:" RESET " Circular chain of processes each waiting for\n"
           "              a resource held by the next process.\n\n");
    printf("  " BWHITE "Before (deadlock possible):" RESET "\n");
    printf("    P0 holds R0, requests R1.\n");
    printf("    P1 holds R1, requests R0.\n");
    printf("    Cycle: P0 -> R1 -> P1 -> R0 -> P0\n\n");
    printf("  " BWHITE "After  (prevention -- impose resource ordering):" RESET "\n");
    printf("    Rule: every process must request resources in\n");
    printf("    increasing order of resource number.\n\n");
    printf("    R0 (order 0) < R1 (order 1)\n");
    printf("    P0 holds R0, may request R1 (0 < 1, ok).\n");
    printf("    P1 must request R0 before R1. P1 cannot hold R1\n");
    printf("    and then request R0 (1 > 0, violates ordering).\n");
    printf("    " BGREEN "=> Circular chain is impossible.\n" RESET);
    print_line();

    printf("\n  " BWHITE "Summary Table:" RESET "\n");
    printf("  %-22s %-38s\n", "Condition", "Prevention Approach");
    print_line();
    printf("  %-22s %-38s\n", "Mutual Exclusion",   "Spooling / sharable design");
    printf("  %-22s %-38s\n", "Hold and Wait",      "Request all resources at once");
    printf("  %-22s %-38s\n", "No Preemption",      "Allow forced resource reclaim");
    printf("  %-22s %-38s\n", "Circular Wait",      "Impose numeric resource ordering");

    wait_enter();
}

/* ================================================================
 *  6. Deadlock Recovery Demo
 * ================================================================ */
static void recovery_demo(void)
{
    print_header("DEADLOCK RECOVERY DEMO");

    int n = get_int("Number of processes (1-10): ", 1, 10);
    int m = get_int("Number of resource types (1-5): ", 1, 5);

    int avail[MR], alloc[MP][MR], request[MP][MR], deadlocked[MP];

    input_vector("Available", avail, m);
    input_matrix("Allocation matrix", alloc, n, m);
    input_matrix("Request matrix", request, n, m);

    print_matrix("Allocation", alloc, n, m);
    print_matrix("Request", request, n, m);
    printf("\n");
    print_vector("Available:", avail, m);
    printf("\n");

    int dl_count = detection_run(n, m, avail, alloc, request, deadlocked);
    if (dl_count == 0) {
        printf("\n  No deadlock to recover from.\n");
        wait_enter();
        return;
    }

    printf("\n");
    print_subheader("Recovery Options");
    printf("  1. Terminate ALL deadlocked processes\n");
    printf("  2. Terminate one process at a time\n");
    printf("  3. Resource preemption (select a victim)\n");
    int choice = get_int("  Choose recovery method (1-3): ", 1, 3);

    if (choice == 1) {
        /* ---- Terminate all deadlocked processes ---- */
        print_subheader("Terminating ALL deadlocked processes");
        for (int i = 0; i < n; i++) {
            if (!deadlocked[i]) continue;
            printf("  Terminating P%d -- releasing resources: [", i);
            for (int j = 0; j < m; j++) {
                printf("%s%d", j ? ", " : "", alloc[i][j]);
                avail[j] += alloc[i][j];
                alloc[i][j] = 0;
                request[i][j] = 0;
            }
            printf("]\n");
            deadlocked[i] = 0;
        }
        printf(BGREEN "\n  All deadlocked processes terminated.\n" RESET);
        print_vector("New Available:", avail, m);
        print_matrix("New Allocation", alloc, n, m);

    } else if (choice == 2) {
        /* ---- Terminate one at a time ---- */
        print_subheader("Terminate one process at a time");
        while (dl_count > 0) {
            /* Pick the first deadlocked process as victim */
            int victim = -1;
            for (int i = 0; i < n; i++) {
                if (deadlocked[i]) { victim = i; break; }
            }
            if (victim < 0) break;

            printf(BYELLOW "\n  Terminating P%d..." RESET "\n", victim);
            printf("  Resources released: [");
            for (int j = 0; j < m; j++) {
                printf("%s%d", j ? ", " : "", alloc[victim][j]);
                avail[j] += alloc[victim][j];
                alloc[victim][j] = 0;
                request[victim][j] = 0;
            }
            printf("]\n");
            deadlocked[victim] = 0;

            print_vector("Available now:", avail, m);

            /* Re-run detection on remaining */
            printf("\n  Re-running detection...\n");
            dl_count = detection_run(n, m, avail, alloc, request, deadlocked);
            if (dl_count == 0)
                printf(BGREEN "  Deadlock resolved!\n" RESET);
        }

    } else {
        /* ---- Resource preemption ---- */
        print_subheader("Resource Preemption");
        printf("  Select a victim from deadlocked processes:");
        for (int i = 0; i < n; i++)
            if (deadlocked[i]) printf(" P%d", i);
        printf("\n");

        int victim = get_int("  Victim process id: ", 0, n - 1);
        if (!deadlocked[victim]) {
            printf(BRED "  P%d is not deadlocked. Aborting.\n" RESET, victim);
            wait_enter();
            return;
        }

        printf(BYELLOW "\n  Preempting resources from P%d (rollback).\n" RESET, victim);
        printf("  Resources reclaimed: [");
        for (int j = 0; j < m; j++) {
            printf("%s%d", j ? ", " : "", alloc[victim][j]);
            avail[j] += alloc[victim][j];
            alloc[victim][j] = 0;
            request[victim][j] = 0;
        }
        printf("]\n");

        print_vector("Available now:", avail, m);
        printf("\n  Re-running detection...\n");
        detection_run(n, m, avail, alloc, request, deadlocked);
        print_matrix("Allocation after recovery", alloc, n, m);
    }

    wait_enter();
}

/* ================================================================
 *  Module menu
 * ================================================================ */
void deadlock_menu(void)
{
    while (1) {
        cls();
        print_header("DEADLOCK MANAGEMENT");
        printf("\n");
        printf("  " BWHITE "1." RESET " Banker's Algorithm (Safety Check)\n");
        printf("  " BWHITE "2." RESET " Banker's Algorithm (Resource Request)\n");
        printf("  " BWHITE "3." RESET " Resource Allocation Graph\n");
        printf("  " BWHITE "4." RESET " Deadlock Detection\n");
        printf("  " BWHITE "5." RESET " Deadlock Prevention Strategies\n");
        printf("  " BWHITE "6." RESET " Deadlock Recovery Demo\n");
        printf("  " BWHITE "0." RESET " Back to Main Menu\n");
        printf("\n");

        int ch = get_int("  Your choice: ", 0, 6);
        switch (ch) {
            case 1: banker_safety_demo();  break;
            case 2: banker_request_demo(); break;
            case 3: rag_demo();            break;
            case 4: detection_demo();      break;
            case 5: prevention_demo();     break;
            case 6: recovery_demo();       break;
            case 0: return;
        }
    }
}
