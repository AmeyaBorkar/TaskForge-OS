/*  memory_mgmt.c  --  Memory Management module for TaskForge
 *  Covers: Fixed/Dynamic Partitioning, Buddy System, Paging,
 *          TLB, Segmentation, Page Replacement, Thrashing.          */

#include "memory_mgmt.h"

/* ================================================================
 *  Constants
 * ================================================================ */
#define MAX_PARTS       16
#define MAX_PROCS       16
#define MAX_HOLES       32
#define MAX_BUDDY       1024
#define MAX_BUDDY_NODES 64
#define MAX_PAGES       64
#define MAX_FRAMES      16
#define MAX_TLB         8
#define MAX_SEGMENTS    8
#define MAX_REF_STR     64
#define OS_SIZE         50      /* KB reserved for OS */

/* ================================================================
 *  Fixed Partitioning
 * ================================================================ */
static void fixed_partitioning(void)
{
    print_header("FIXED PARTITIONING");

    int np = get_int("Number of partitions (1-8): ", 1, 8);
    int sizes[MAX_PARTS];
    int total_mem = OS_SIZE;

    printf("\n");
    for (int i = 0; i < np; i++) {
        char prompt[64];
        sprintf(prompt, "Size of partition %d (KB): ", i + 1);
        sizes[i] = get_pos_int(prompt);
        total_mem += sizes[i];
    }

    int nproc = get_int("Number of processes (1-8): ", 1, 8);
    int preq[MAX_PROCS];
    printf("\n");
    for (int i = 0; i < nproc; i++) {
        char prompt[64];
        sprintf(prompt, "Memory needed by P%d (KB): ", i + 1);
        preq[i] = get_pos_int(prompt);
    }

    /* allocate: first fit into partitions */
    int alloc[MAX_PARTS];          /* process id in partition, -1 = free */
    for (int i = 0; i < np; i++) alloc[i] = -1;

    int placed[MAX_PROCS];
    memset(placed, 0, sizeof(placed));

    for (int p = 0; p < nproc; p++) {
        for (int i = 0; i < np; i++) {
            if (alloc[i] == -1 && preq[p] <= sizes[i]) {
                alloc[i] = p;
                placed[p] = 1;
                break;
            }
        }
    }

    /* display allocation table */
    print_subheader("Allocation Table");
    printf("  " BOLD "%-12s %-12s %-12s %-12s %-12s" RESET "\n",
           "Partition", "Size(KB)", "Process", "Used(KB)", "Int.Frag");
    print_line();

    int total_frag = 0;
    for (int i = 0; i < np; i++) {
        if (alloc[i] >= 0) {
            int frag = sizes[i] - preq[alloc[i]];
            total_frag += frag;
            printf("  %-12d %-12d " BGREEN "P%-11d" RESET " %-12d " BYELLOW "%-12d" RESET "\n",
                   i + 1, sizes[i], alloc[i] + 1, preq[alloc[i]], frag);
        } else {
            total_frag += sizes[i];
            printf("  %-12d %-12d " BRED "FREE        " RESET " %-12s " BYELLOW "%-12d" RESET "\n",
                   i + 1, sizes[i], "-", sizes[i]);
        }
    }

    for (int p = 0; p < nproc; p++) {
        if (!placed[p])
            printf(BRED "  P%d (%d KB) could not be allocated!\n" RESET, p + 1, preq[p]);
    }

    /* visual memory map */
    print_subheader("Memory Map");
    printf("  " BG_BLUE BWHITE " OS:%dKB " RESET, OS_SIZE);
    for (int i = 0; i < np; i++) {
        if (alloc[i] >= 0) {
            printf(BG_GREEN BWHITE " P%d:%d/%d " RESET,
                   alloc[i] + 1, preq[alloc[i]], sizes[i]);
        } else {
            printf(BG_RED BWHITE "  FREE:%d  " RESET, sizes[i]);
        }
    }
    printf("\n  0");
    int pos = OS_SIZE;
    for (int i = 0; i < np; i++) {
        printf("%*d", 10, pos);
        pos += sizes[i];
    }
    printf("  %d\n", pos);

    printf("\n  " BYELLOW "Total Internal Fragmentation: %d KB" RESET "\n", total_frag);
    wait_enter();
}

/* ================================================================
 *  Dynamic Partitioning  (First / Best / Worst / Next Fit)
 * ================================================================ */

typedef struct { int start, size, pid; /* pid -1 = hole */ } Block;

static Block dblocks[MAX_HOLES];
static int   dblock_cnt;
static int   next_fit_idx;

static void dyn_init(int mem)
{
    dblock_cnt = 1;
    dblocks[0].start = OS_SIZE;
    dblocks[0].size  = mem - OS_SIZE;
    dblocks[0].pid   = -1;
    next_fit_idx = 0;
}

static void dyn_show(int mem)
{
    print_subheader("Memory Map");
    printf("  " BG_BLUE BWHITE " OS " RESET);
    for (int i = 0; i < dblock_cnt; i++) {
        if (dblocks[i].pid >= 0)
            printf(BG_GREEN BWHITE " P%d:%dKB " RESET, dblocks[i].pid, dblocks[i].size);
        else
            printf(BG_RED BWHITE " H:%dKB " RESET, dblocks[i].size);
    }
    printf("\n  0");
    for (int i = 0; i < dblock_cnt; i++)
        printf("  %d", dblocks[i].start);
    printf("  %d\n", mem);

    /* external fragmentation */
    int ext = 0, holes = 0;
    for (int i = 0; i < dblock_cnt; i++)
        if (dblocks[i].pid == -1) { ext += dblocks[i].size; holes++; }
    if (holes > 1)
        printf("  " BYELLOW "External Fragmentation: %d KB across %d holes" RESET "\n", ext, holes);
}

static void merge_holes(void)
{
    for (int i = 0; i < dblock_cnt - 1; ) {
        if (dblocks[i].pid == -1 && dblocks[i + 1].pid == -1) {
            dblocks[i].size += dblocks[i + 1].size;
            for (int j = i + 1; j < dblock_cnt - 1; j++)
                dblocks[j] = dblocks[j + 1];
            dblock_cnt--;
        } else {
            i++;
        }
    }
}

static int dyn_alloc(int pid, int sz, int strategy)
{
    /* 0=first, 1=best, 2=worst, 3=next */
    int best = -1;
    int start = (strategy == 3) ? next_fit_idx : 0;
    int checked = 0;

    for (int raw = 0; raw < dblock_cnt; raw++) {
        int i = (start + raw) % dblock_cnt;
        if (dblocks[i].pid != -1 || dblocks[i].size < sz) {
            checked++;
            continue;
        }
        if (strategy == 0 || strategy == 3) { best = i; break; }
        if (strategy == 1) { /* best fit */
            if (best == -1 || dblocks[i].size < dblocks[best].size) best = i;
        }
        if (strategy == 2) { /* worst fit */
            if (best == -1 || dblocks[i].size > dblocks[best].size) best = i;
        }
        checked++;
    }
    (void)checked;

    if (best == -1) return 0;

    if (dblocks[best].size == sz) {
        dblocks[best].pid = pid;
    } else {
        /* split */
        if (dblock_cnt >= MAX_HOLES) return 0;
        for (int j = dblock_cnt; j > best + 1; j--)
            dblocks[j] = dblocks[j - 1];
        dblock_cnt++;
        dblocks[best + 1].start = dblocks[best].start + sz;
        dblocks[best + 1].size  = dblocks[best].size - sz;
        dblocks[best + 1].pid   = -1;
        dblocks[best].size = sz;
        dblocks[best].pid  = pid;
    }
    if (strategy == 3) next_fit_idx = (best + 1) % dblock_cnt;
    return 1;
}

static void dyn_dealloc(int pid)
{
    for (int i = 0; i < dblock_cnt; i++) {
        if (dblocks[i].pid == pid) {
            dblocks[i].pid = -1;
            merge_holes();
            return;
        }
    }
    printf(BRED "  Process P%d not found!\n" RESET, pid);
}

static void dyn_compact(void)
{
    /* find total memory extent before modifying anything */
    int mem_end = OS_SIZE;
    for (int i = 0; i < dblock_cnt; i++) {
        int end = dblocks[i].start + dblocks[i].size;
        if (end > mem_end) mem_end = end;
    }

    /* slide all allocated blocks to the front */
    int pos = OS_SIZE;
    int wi = 0;
    for (int i = 0; i < dblock_cnt; i++) {
        if (dblocks[i].pid >= 0) {
            dblocks[wi].pid   = dblocks[i].pid;
            dblocks[wi].size  = dblocks[i].size;
            dblocks[wi].start = pos;
            pos += dblocks[wi].size;
            wi++;
        }
    }
    /* create single free hole at the end */
    if (pos < mem_end) {
        dblocks[wi].start = pos;
        dblocks[wi].size  = mem_end - pos;
        dblocks[wi].pid   = -1;
        wi++;
    }
    dblock_cnt = wi;
    printf(BGREEN "  Compaction complete.\n" RESET);
}

static void dynamic_partitioning(void)
{
    print_header("DYNAMIC PARTITIONING");

    const char *names[] = {"First Fit", "Best Fit", "Worst Fit", "Next Fit"};
    printf("  Placement Strategy:\n");
    for (int i = 0; i < 4; i++)
        printf("    %d. %s\n", i + 1, names[i]);
    int strat = get_int("Choose (1-4): ", 1, 4) - 1;

    int mem = get_pos_int("Total memory size (KB, e.g. 1024): ");
    dyn_init(mem);

    printf(BGREEN "\n  Strategy: %s | Memory: %d KB (OS: %d KB)\n" RESET,
           names[strat], mem, OS_SIZE);
    printf("  Commands:  " BCYAN "A <pid> <size>" RESET " = Allocate  |  "
           BCYAN "D <pid>" RESET " = Deallocate  |  "
           BCYAN "C" RESET " = Compact  |  "
           BCYAN "Q" RESET " = Quit\n\n");

    char buf[BUF_SIZE];
    while (1) {
        dyn_show(mem);
        printf("\n  " BYELLOW "> " RESET);
        if (!fgets(buf, BUF_SIZE, stdin)) break;
        char cmd;
        int pid, sz;
        if (sscanf(buf, " %c", &cmd) != 1) continue;
        cmd = (char)toupper(cmd);

        if (cmd == 'Q') break;
        if (cmd == 'C') { dyn_compact(); continue; }
        if (cmd == 'A') {
            if (sscanf(buf, " %c %d %d", &cmd, &pid, &sz) != 3 || pid < 1 || sz < 1) {
                printf(BRED "  Usage: A <pid> <size>\n" RESET); continue;
            }
            if (!dyn_alloc(pid, sz, strat))
                printf(BRED "  Cannot allocate %d KB for P%d!\n" RESET, sz, pid);
            else
                printf(BGREEN "  Allocated %d KB for P%d.\n" RESET, sz, pid);
        } else if (cmd == 'D') {
            if (sscanf(buf, " %c %d", &cmd, &pid) != 2 || pid < 1) {
                printf(BRED "  Usage: D <pid>\n" RESET); continue;
            }
            dyn_dealloc(pid);
        } else {
            printf(BRED "  Unknown command.\n" RESET);
        }
    }
    wait_enter();
}

/* ================================================================
 *  Buddy System
 * ================================================================ */

typedef struct { int start, size, pid; /* -1=free, -2=split(unused) */ } BuddyBlk;
static BuddyBlk buddy[MAX_BUDDY_NODES];
static int buddy_cnt;
static int buddy_total;

static int next_pow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

static void buddy_init(int mem)
{
    buddy_total = next_pow2(mem);
    buddy_cnt = 1;
    buddy[0].start = 0;
    buddy[0].size  = buddy_total;
    buddy[0].pid   = -1;
}

static void buddy_show(void)
{
    print_subheader("Buddy Memory Map");
    for (int i = 0; i < buddy_cnt; i++) {
        if (buddy[i].pid >= 0)
            printf("  " BG_GREEN BWHITE " P%d:%dKB " RESET, buddy[i].pid, buddy[i].size);
        else
            printf("  " BG_RED BWHITE " Free:%dKB " RESET, buddy[i].size);
    }
    printf("\n  ");
    for (int i = 0; i < buddy_cnt; i++)
        printf("%-10d", buddy[i].start);
    printf("%d\n", buddy_total);

    /* tree view */
    print_subheader("Block Tree");
    printf("  Total: %d KB\n", buddy_total);
    for (int i = 0; i < buddy_cnt; i++) {
        int depth = 0;
        for (int s = buddy_total; s > buddy[i].size; s >>= 1) depth++;
        for (int d = 0; d < depth; d++) printf("    ");
        if (buddy[i].pid >= 0)
            printf(BGREEN "  [P%d | %dKB @ %d]" RESET "\n",
                   buddy[i].pid, buddy[i].size, buddy[i].start);
        else
            printf(BYELLOW "  [Free | %dKB @ %d]" RESET "\n",
                   buddy[i].size, buddy[i].start);
    }
}

static int buddy_alloc(int pid, int req)
{
    int need = next_pow2(req);

    /* find smallest free block >= need */
    int best = -1;
    for (int i = 0; i < buddy_cnt; i++) {
        if (buddy[i].pid == -1 && buddy[i].size >= need) {
            if (best == -1 || buddy[i].size < buddy[best].size)
                best = i;
        }
    }
    if (best == -1) return 0;

    /* split until block size == need */
    while (buddy[best].size > need) {
        if (buddy_cnt >= MAX_BUDDY_NODES) return 0;
        int half = buddy[best].size / 2;
        /* insert new buddy after best */
        for (int j = buddy_cnt; j > best + 1; j--)
            buddy[j] = buddy[j - 1];
        buddy_cnt++;
        buddy[best].size = half;
        buddy[best + 1].start = buddy[best].start + half;
        buddy[best + 1].size  = half;
        buddy[best + 1].pid   = -1;
    }
    buddy[best].pid = pid;
    return 1;
}

static void buddy_merge(void)
{
    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < buddy_cnt - 1; i++) {
            if (buddy[i].pid == -1 && buddy[i + 1].pid == -1 &&
                buddy[i].size == buddy[i + 1].size &&
                buddy[i].start % (buddy[i].size * 2) == 0) {
                buddy[i].size *= 2;
                for (int j = i + 1; j < buddy_cnt - 1; j++)
                    buddy[j] = buddy[j + 1];
                buddy_cnt--;
                merged = 1;
                break;
            }
        }
    }
}

static void buddy_free(int pid)
{
    for (int i = 0; i < buddy_cnt; i++) {
        if (buddy[i].pid == pid) {
            buddy[i].pid = -1;
            buddy_merge();
            printf(BGREEN "  Freed P%d and merged buddies.\n" RESET, pid);
            return;
        }
    }
    printf(BRED "  P%d not found!\n" RESET, pid);
}

static void buddy_system(void)
{
    print_header("BUDDY SYSTEM");
    int mem = get_pos_int("Total memory (power of 2, e.g. 1024): ");
    mem = next_pow2(mem);
    printf("  Rounded to nearest power of 2: %d KB\n", mem);
    buddy_init(mem);

    printf("  Commands:  " BCYAN "A <pid> <size>" RESET " = Allocate  |  "
           BCYAN "D <pid>" RESET " = Free  |  "
           BCYAN "Q" RESET " = Quit\n\n");

    char buf[BUF_SIZE];
    while (1) {
        buddy_show();
        printf("\n  " BYELLOW "> " RESET);
        if (!fgets(buf, BUF_SIZE, stdin)) break;
        char cmd; int pid, sz;
        if (sscanf(buf, " %c", &cmd) != 1) continue;
        cmd = (char)toupper(cmd);
        if (cmd == 'Q') break;
        if (cmd == 'A') {
            if (sscanf(buf, " %c %d %d", &cmd, &pid, &sz) != 3 || pid < 1 || sz < 1) {
                printf(BRED "  Usage: A <pid> <size>\n" RESET); continue;
            }
            if (!buddy_alloc(pid, sz))
                printf(BRED "  Cannot allocate %d KB for P%d!\n" RESET, sz, pid);
            else
                printf(BGREEN "  Allocated %d KB (rounded to %d) for P%d.\n" RESET,
                       sz, next_pow2(sz), pid);
        } else if (cmd == 'D') {
            if (sscanf(buf, " %c %d", &cmd, &pid) != 2 || pid < 1) {
                printf(BRED "  Usage: D <pid>\n" RESET); continue;
            }
            buddy_free(pid);
        } else {
            printf(BRED "  Unknown command.\n" RESET);
        }
    }
    wait_enter();
}

/* ================================================================
 *  Paging  (Address Translation)
 * ================================================================ */
static void paging_simulation(void)
{
    print_header("PAGING - ADDRESS TRANSLATION");

    int page_size = get_pos_int("Page size (bytes, e.g. 256): ");
    int num_pages = get_int("Number of pages (1-32): ", 1, 32);

    int page_table[MAX_PAGES];
    printf("\n  " BOLD "Auto-generating page table..." RESET "\n");
    srand((unsigned)time(NULL));
    for (int i = 0; i < num_pages; i++)
        page_table[i] = rand() % (num_pages * 2);

    /* display page table */
    print_subheader("Page Table");
    printf("  " BOLD "%-12s %-12s" RESET "\n", "Page#", "Frame#");
    print_line();
    for (int i = 0; i < num_pages; i++)
        printf("  %-12d " BCYAN "%-12d" RESET "\n", i, page_table[i]);

    /* address translation loop */
    printf("\n  Enter logical addresses to translate (0 to quit).\n");
    while (1) {
        int addr = get_nn_int("Logical address (0=quit): ");
        if (addr == 0) break;

        int page_num = addr / page_size;
        int offset   = addr % page_size;

        if (page_num >= num_pages) {
            printf(BRED "  ERROR: Page %d out of range (max %d)!\n" RESET,
                   page_num, num_pages - 1);
            continue;
        }

        int frame = page_table[page_num];
        int phys  = frame * page_size + offset;

        printf("  " BOLD "Logical Address : " RESET "%d\n", addr);
        printf("  " BOLD "Page Number     : " RESET "%d  (= %d / %d)\n", page_num, addr, page_size);
        printf("  " BOLD "Offset          : " RESET "%d  (= %d %% %d)\n", offset, addr, page_size);
        printf("  " BOLD "Frame Number    : " RESET BCYAN "%d" RESET "\n", frame);
        printf("  " BOLD "Physical Address: " RESET BGREEN "%d" RESET "  (= %d * %d + %d)\n\n",
               phys, frame, page_size, offset);
    }
    wait_enter();
}

/* ================================================================
 *  TLB Simulation
 * ================================================================ */
static void tlb_simulation(void)
{
    print_header("TLB SIMULATION");

    int page_size = get_pos_int("Page size (bytes): ");
    int num_pages = get_int("Number of pages (1-32): ", 1, 32);
    int tlb_size  = get_int("TLB size (1-8): ", 1, MAX_TLB);

    int page_table[MAX_PAGES];
    srand((unsigned)time(NULL));
    for (int i = 0; i < num_pages; i++)
        page_table[i] = rand() % (num_pages * 2);

    /* TLB: page, frame, lru_counter */
    int tlb_page[MAX_TLB], tlb_frame[MAX_TLB], tlb_lru[MAX_TLB];
    int tlb_used = 0;
    int lru_clock = 0;
    int hits = 0, accesses = 0;

    for (int i = 0; i < MAX_TLB; i++) tlb_page[i] = -1;

    int nrefs = get_int("Number of address references (1-32): ", 1, 32);
    int refs[MAX_PAGES];
    printf("  Enter %d logical addresses:\n", nrefs);
    for (int i = 0; i < nrefs; i++) {
        char prompt[32];
        sprintf(prompt, "  Addr[%d]: ", i + 1);
        refs[i] = get_nn_int(prompt);
    }

    print_subheader("TLB Access Trace");
    printf("  " BOLD "%-6s %-8s %-8s %-8s %-10s %-12s" RESET "\n",
           "Step", "Addr", "Page", "Frame", "TLB", "Phys.Addr");
    print_line();

    for (int r = 0; r < nrefs; r++) {
        int page_num = refs[r] / page_size;
        int offset   = refs[r] % page_size;
        accesses++;

        if (page_num >= num_pages) {
            printf("  %-6d %-8d " BRED "INVALID PAGE" RESET "\n", r + 1, refs[r]);
            continue;
        }

        /* search TLB */
        int found = -1;
        for (int t = 0; t < tlb_used; t++) {
            if (tlb_page[t] == page_num) { found = t; break; }
        }

        int frame;
        const char *status;
        if (found >= 0) {
            frame = tlb_frame[found];
            tlb_lru[found] = lru_clock++;
            hits++;
            status = BGREEN "HIT" RESET;
        } else {
            frame = page_table[page_num];
            status = BRED "MISS" RESET;
            /* insert into TLB */
            if (tlb_used < tlb_size) {
                tlb_page[tlb_used]  = page_num;
                tlb_frame[tlb_used] = frame;
                tlb_lru[tlb_used]   = lru_clock++;
                tlb_used++;
            } else {
                /* LRU replacement */
                int victim = 0;
                for (int t = 1; t < tlb_size; t++)
                    if (tlb_lru[t] < tlb_lru[victim]) victim = t;
                tlb_page[victim]  = page_num;
                tlb_frame[victim] = frame;
                tlb_lru[victim]   = lru_clock++;
            }
        }

        int phys = frame * page_size + offset;
        printf("  %-6d %-8d %-8d %-8d %-10s %-12d\n",
               r + 1, refs[r], page_num, frame, status, phys);
    }

    print_line();
    printf("  " BOLD "TLB Hits:    " BGREEN "%d / %d" RESET "\n", hits, accesses);
    printf("  " BOLD "Hit Ratio:   " BCYAN "%.2f%%" RESET "\n",
           accesses ? (hits * 100.0 / accesses) : 0.0);
    wait_enter();
}

/* ================================================================
 *  Segmentation
 * ================================================================ */
static void segmentation_sim(void)
{
    print_header("SEGMENTATION");

    const char *default_names[] = {"Code", "Data", "Stack", "Heap"};
    int nseg = get_int("Number of segments (1-8): ", 1, MAX_SEGMENTS);

    int base[MAX_SEGMENTS], limit[MAX_SEGMENTS];
    char sname[MAX_SEGMENTS][16];

    printf("\n");
    for (int i = 0; i < nseg; i++) {
        if (i < 4)
            strcpy(sname[i], default_names[i]);
        else
            sprintf(sname[i], "Seg%d", i);
        char prompt[64];
        sprintf(prompt, "Base address for %s (seg %d): ", sname[i], i);
        base[i] = get_nn_int(prompt);
        sprintf(prompt, "Limit (size) for %s (seg %d): ", sname[i], i);
        limit[i] = get_pos_int(prompt);
    }

    /* display segment table */
    print_subheader("Segment Table");
    printf("  " BOLD "%-6s %-10s %-12s %-12s" RESET "\n",
           "Seg#", "Name", "Base", "Limit");
    print_line();
    for (int i = 0; i < nseg; i++)
        printf("  %-6d %-10s " BCYAN "%-12d" RESET " %-12d\n",
               i, sname[i], base[i], limit[i]);

    printf("\n  Translate logical addresses (segment, offset). Negative segment to quit.\n");
    while (1) {
        int seg = get_int("Segment number: ", -1, nseg - 1);
        if (seg < 0) break;
        int offset = get_nn_int("Offset: ");

        if (offset >= limit[seg]) {
            printf(BRED "  SEGMENTATION FAULT! Offset %d >= limit %d for %s.\n" RESET,
                   offset, limit[seg], sname[seg]);
        } else {
            int phys = base[seg] + offset;
            printf(BGREEN "  Physical Address = %d + %d = %d\n" RESET,
                   base[seg], offset, phys);
        }
        printf("\n");
    }
    wait_enter();
}

/* ================================================================
 *  Page Replacement  (FIFO, LRU, Optimal, Clock)
 * ================================================================ */

static void pr_show_row(int step, int ref, int frames[], int nf, int fault, int replaced)
{
    printf("  %-6d %-6d", step, ref);
    for (int f = 0; f < nf; f++) {
        if (frames[f] == -1) printf(DIM " %-8s" RESET, "-");
        else                 printf(BCYAN " %-8d" RESET, frames[f]);
    }
    if (fault)
        printf(BRED "  Fault" RESET);
    else
        printf(BGREEN "  Hit" RESET);
    if (replaced >= 0)
        printf(DIM " [replaced %d]" RESET, replaced);
    printf("\n");
}

static int run_fifo(int refs[], int nr, int nf, int show)
{
    int frames[MAX_FRAMES], q_pos = 0, faults = 0;
    for (int i = 0; i < nf; i++) frames[i] = -1;

    if (show) {
        printf("  " BOLD "%-6s %-6s", "Step", "Ref");
        for (int f = 0; f < nf; f++) printf(" Frame%-3d", f + 1);
        printf("  Status" RESET "\n");
        print_line();
    }

    for (int r = 0; r < nr; r++) {
        int found = 0;
        for (int f = 0; f < nf; f++)
            if (frames[f] == refs[r]) { found = 1; break; }

        int replaced = -1;
        if (!found) {
            replaced = frames[q_pos];
            frames[q_pos] = refs[r];
            q_pos = (q_pos + 1) % nf;
            faults++;
        }
        if (show) pr_show_row(r + 1, refs[r], frames, nf, !found, !found ? replaced : -1);
    }
    return faults;
}

static int run_lru(int refs[], int nr, int nf, int show)
{
    int frames[MAX_FRAMES], last_used[MAX_FRAMES], faults = 0;
    for (int i = 0; i < nf; i++) { frames[i] = -1; last_used[i] = -1; }

    if (show) {
        printf("  " BOLD "%-6s %-6s", "Step", "Ref");
        for (int f = 0; f < nf; f++) printf(" Frame%-3d", f + 1);
        printf("  Status" RESET "\n");
        print_line();
    }

    for (int r = 0; r < nr; r++) {
        int found = -1;
        for (int f = 0; f < nf; f++)
            if (frames[f] == refs[r]) { found = f; break; }

        int replaced = -1;
        if (found >= 0) {
            last_used[found] = r;
        } else {
            /* find empty or LRU */
            int victim = 0;
            for (int f = 0; f < nf; f++) {
                if (frames[f] == -1) { victim = f; break; }
                if (last_used[f] < last_used[victim]) victim = f;
            }
            replaced = frames[victim];
            frames[victim] = refs[r];
            last_used[victim] = r;
            faults++;
        }
        if (show) pr_show_row(r + 1, refs[r], frames, nf, found < 0, found < 0 ? replaced : -1);
    }
    return faults;
}

static int run_optimal(int refs[], int nr, int nf, int show)
{
    int frames[MAX_FRAMES], faults = 0;
    for (int i = 0; i < nf; i++) frames[i] = -1;

    if (show) {
        printf("  " BOLD "%-6s %-6s", "Step", "Ref");
        for (int f = 0; f < nf; f++) printf(" Frame%-3d", f + 1);
        printf("  Status" RESET "\n");
        print_line();
    }

    for (int r = 0; r < nr; r++) {
        int found = 0;
        for (int f = 0; f < nf; f++)
            if (frames[f] == refs[r]) { found = 1; break; }

        int replaced = -1;
        if (!found) {
            /* find empty frame */
            int victim = -1;
            for (int f = 0; f < nf; f++) {
                if (frames[f] == -1) { victim = f; break; }
            }
            if (victim == -1) {
                /* find page not used for longest in future */
                int farthest = -1;
                for (int f = 0; f < nf; f++) {
                    int next_use = nr; /* never used again */
                    for (int k = r + 1; k < nr; k++) {
                        if (refs[k] == frames[f]) { next_use = k; break; }
                    }
                    if (next_use > farthest) { farthest = next_use; victim = f; }
                }
            }
            replaced = frames[victim];
            frames[victim] = refs[r];
            faults++;
        }
        if (show) pr_show_row(r + 1, refs[r], frames, nf, !found, !found ? replaced : -1);
    }
    return faults;
}

static int run_clock(int refs[], int nr, int nf, int show)
{
    int frames[MAX_FRAMES], refbit[MAX_FRAMES], hand = 0, faults = 0;
    for (int i = 0; i < nf; i++) { frames[i] = -1; refbit[i] = 0; }

    if (show) {
        printf("  " BOLD "%-6s %-6s", "Step", "Ref");
        for (int f = 0; f < nf; f++) printf(" Frame%-3d", f + 1);
        printf("  Status  Hand" RESET "\n");
        print_line();
    }

    for (int r = 0; r < nr; r++) {
        int found = 0;
        for (int f = 0; f < nf; f++) {
            if (frames[f] == refs[r]) { refbit[f] = 1; found = 1; break; }
        }

        int replaced = -1;
        if (!found) {
            while (refbit[hand]) {
                refbit[hand] = 0;
                hand = (hand + 1) % nf;
            }
            replaced = frames[hand];
            frames[hand] = refs[r];
            refbit[hand] = 1;
            hand = (hand + 1) % nf;
            faults++;
        }

        if (show) {
            printf("  %-6d %-6d", r + 1, refs[r]);
            for (int f = 0; f < nf; f++) {
                if (frames[f] == -1) printf(DIM " %-8s" RESET, "-");
                else {
                    char tmp[16];
                    sprintf(tmp, "%d(%d)", frames[f], refbit[f]);
                    printf(BCYAN " %-8s" RESET, tmp);
                }
            }
            if (!found) printf(BRED "  Fault" RESET);
            else        printf(BGREEN "  Hit" RESET);
            printf("  ->%d", hand);
            if (replaced >= 0) printf(DIM " [replaced %d]" RESET, replaced);
            printf("\n");
        }
    }
    return faults;
}

static void get_ref_string(int refs[], int *nr, int *nf)
{
    *nf = get_int("Number of frames (1-8): ", 1, MAX_FRAMES);
    *nr = get_int("Length of reference string (1-50): ", 1, MAX_REF_STR);
    printf("  Enter %d page numbers:\n  ", *nr);
    for (int i = 0; i < *nr; i++) {
        if (scanf("%d", &refs[i]) != 1) { refs[i] = 0; flush_input(); }
    }
    flush_input();
}

static void page_replacement(void)
{
    print_header("PAGE REPLACEMENT ALGORITHMS");
    printf("  1. FIFO\n  2. LRU\n  3. Optimal\n  4. Clock (Second Chance)\n");
    int choice = get_int("Choose algorithm (1-4): ", 1, 4);

    int refs[MAX_REF_STR], nr, nf;
    get_ref_string(refs, &nr, &nf);

    const char *names[] = {"FIFO", "LRU", "Optimal", "Clock"};
    char title[64];
    sprintf(title, "%s Page Replacement (%d frames)", names[choice - 1], nf);
    print_subheader(title);

    int faults;
    switch (choice) {
        case 1: faults = run_fifo(refs, nr, nf, 1);    break;
        case 2: faults = run_lru(refs, nr, nf, 1);     break;
        case 3: faults = run_optimal(refs, nr, nf, 1);  break;
        case 4: faults = run_clock(refs, nr, nf, 1);    break;
        default: faults = 0;
    }

    print_line();
    printf("  " BOLD "Total Page Faults: " BRED "%d / %d" RESET "\n", faults, nr);
    printf("  " BOLD "Hit Ratio:         " BGREEN "%.2f%%" RESET "\n",
           nr ? ((nr - faults) * 100.0 / nr) : 0.0);
    wait_enter();
}

/* ================================================================
 *  Compare All Page Replacement Algorithms
 * ================================================================ */
static void compare_page_replacement(void)
{
    print_header("COMPARE PAGE REPLACEMENT ALGORITHMS");

    int refs[MAX_REF_STR], nr, nf;
    get_ref_string(refs, &nr, &nf);

    int f_fifo = run_fifo(refs, nr, nf, 0);
    int f_lru  = run_lru(refs, nr, nf, 0);
    int f_opt  = run_optimal(refs, nr, nf, 0);
    int f_clk  = run_clock(refs, nr, nf, 0);

    int faults[] = {f_fifo, f_lru, f_opt, f_clk};
    const char *names[] = {"FIFO", "LRU", "Optimal", "Clock"};

    int best = 0;
    for (int i = 1; i < 4; i++)
        if (faults[i] < faults[best]) best = i;

    print_subheader("Comparison Table");
    printf("  " BOLD "%-12s %-14s %-14s %-10s" RESET "\n",
           "Algorithm", "Page Faults", "Hit Ratio", "");
    print_line();
    for (int i = 0; i < 4; i++) {
        double hr = nr ? ((nr - faults[i]) * 100.0 / nr) : 0.0;
        const char *tag = (i == best) ? BGREEN " << BEST" RESET : "";
        printf("  %-12s " BCYAN "%-3d / %-8d" RESET " %-13.2f%% %s\n",
               names[i], faults[i], nr, hr, tag);
    }

    /* bar chart */
    print_subheader("Fault Comparison (Bar Chart)");
    int max_f = 1;
    for (int i = 0; i < 4; i++)
        if (faults[i] > max_f) max_f = faults[i];

    for (int i = 0; i < 4; i++) {
        int bar_len = (faults[i] * 40) / max_f;
        if (bar_len < 1 && faults[i] > 0) bar_len = 1;
        printf("  %-8s |", names[i]);
        const char *color = (i == best) ? BGREEN : BYELLOW;
        printf("%s", color);
        for (int b = 0; b < bar_len; b++) printf("#");
        printf(RESET " %d\n", faults[i]);
    }

    wait_enter();
}

/* ================================================================
 *  Thrashing Demonstration
 * ================================================================ */
static void thrashing_demo(void)
{
    print_header("THRASHING DEMONSTRATION");

    int total_frames = get_int("Total physical frames (8-64): ", 8, 64);
    int ws_size      = get_int("Working-set size per process (2-8 pages): ", 2, 8);
    int max_procs    = get_int("Max processes to simulate (3-12): ", 3, 12);
    int refs_per     = 50;   /* reference string length per process */

    printf("\n  Simulating with %d total frames, working-set = %d pages...\n\n",
           total_frames, ws_size);

    int fault_pct[16];
    srand((unsigned)time(NULL));

    print_subheader("Simulation Results");
    printf("  " BOLD "%-12s %-16s %-16s %-14s" RESET "\n",
           "Processes", "Frames/Proc", "Page Faults", "Fault Rate");
    print_line();

    for (int np = 1; np <= max_procs; np++) {
        int fpproc = total_frames / np;
        int faults = 0, total_refs = 0;

        for (int p = 0; p < np; p++) {
            int eff_frames = fpproc > 0 ? fpproc : 1;
            if (eff_frames > MAX_FRAMES) eff_frames = MAX_FRAMES;

            int frames[MAX_FRAMES];
            for (int f = 0; f < eff_frames; f++) frames[f] = -1;
            int q = 0;

            for (int r = 0; r < refs_per; r++) {
                int page;
                /* generate reference with locality */
                if (rand() % 100 < 80)
                    page = rand() % ws_size;          /* within working set */
                else
                    page = ws_size + rand() % ws_size; /* outside */

                int hit = 0;
                for (int f = 0; f < eff_frames; f++)
                    if (frames[f] == page) { hit = 1; break; }
                if (!hit) {
                    frames[q] = page;
                    q = (q + 1) % eff_frames;
                    faults++;
                }
                total_refs++;
            }
        }

        int pct = total_refs ? (faults * 100 / total_refs) : 0;
        fault_pct[np] = pct;

        const char *color = (fpproc < ws_size) ? BRED : BGREEN;
        printf("  %-12d %-16d %s%-16d" RESET " %d%%",
               np, fpproc, color, faults, pct);
        if (fpproc < ws_size && (np == 1 || total_frames / (np - 1) >= ws_size))
            printf(BRED " <-- THRASHING BEGINS" RESET);
        printf("\n");
    }

    /* ASCII graph */
    print_subheader("Page Fault Rate vs Degree of Multiprogramming");
    int graph_h = 10;
    for (int row = graph_h; row >= 0; row--) {
        int pct_line = row * 100 / graph_h;
        printf("  %3d%%|", pct_line);
        for (int np = 1; np <= max_procs; np++) {
            int bar_row = fault_pct[np] * graph_h / 100;
            if (bar_row >= row && row > 0)
                printf(BRED "  * " RESET);
            else if (row == 0)
                printf("----");
            else
                printf("    ");
        }
        printf("\n");
    }
    printf("      ");
    for (int np = 1; np <= max_procs; np++) printf("  %d ", np);
    printf("  (processes)\n");

    /* find thrashing point */
    for (int np = 2; np <= max_procs; np++) {
        if (fault_pct[np] - fault_pct[np - 1] > 15) {
            printf("      ");
            for (int i = 1; i < np; i++) printf("    ");
            printf(BRED "  ^ Thrashing point" RESET "\n");
            break;
        }
    }

    printf("\n  " BOLD "Key insight:" RESET " When frames/process < working-set size (%d),\n"
           "  the page fault rate rises sharply -- this is " BRED "thrashing" RESET ".\n", ws_size);
    wait_enter();
}

/* ================================================================
 *  Main Menu
 * ================================================================ */
void memory_mgmt_menu(void)
{
    while (1) {
        print_header("MEMORY MANAGEMENT");
        printf("  " BWHITE "1." RESET " Fixed Partitioning\n");
        printf("  " BWHITE "2." RESET " Dynamic Partitioning (First/Best/Worst/Next Fit)\n");
        printf("  " BWHITE "3." RESET " Buddy System\n");
        printf("  " BWHITE "4." RESET " Paging (Address Translation)\n");
        printf("  " BWHITE "5." RESET " TLB Simulation\n");
        printf("  " BWHITE "6." RESET " Segmentation\n");
        printf("  " BWHITE "7." RESET " Page Replacement (FIFO/LRU/Optimal/Clock)\n");
        printf("  " BWHITE "8." RESET " Compare Page Replacement Algorithms\n");
        printf("  " BWHITE "9." RESET " Thrashing Demonstration\n");
        printf("  " BWHITE "0." RESET " Back to Main Menu\n");
        print_line();

        int ch = get_int("Enter choice (0-9): ", 0, 9);
        switch (ch) {
            case 1: fixed_partitioning();       break;
            case 2: dynamic_partitioning();     break;
            case 3: buddy_system();             break;
            case 4: paging_simulation();        break;
            case 5: tlb_simulation();           break;
            case 6: segmentation_sim();         break;
            case 7: page_replacement();         break;
            case 8: compare_page_replacement(); break;
            case 9: thrashing_demo();           break;
            case 0: return;
        }
    }
}
