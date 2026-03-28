/* ================================================================
 *  process_mgmt.c  --  Process Management Module for TaskForge
 *
 *  Covers:
 *    1. Process State Models  (2-State, 5-State, 7-State)
 *    2. Concurrency Problems  (Producer-Consumer, Readers-Writers,
 *                              Dining Philosophers)
 * ================================================================ */

#include "process_mgmt.h"

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

/* ============================================================
 *  Constants
 * ============================================================ */
#define MAX_PROCS       16
#define MAX_BUF         10
#define NUM_PHILOSOPHERS 5

/* ============================================================
 *  Thread-safe colored printing
 * ============================================================ */
static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

/* Colors cycled per-thread for easy visual distinction. */
static const char *THREAD_COLORS[] = {
    BGREEN, BCYAN, BYELLOW, BMAGENTA, BRED, BBLUE, BWHITE
};
#define NUM_COLORS 7

static const char *thread_color(int id) {
    return THREAD_COLORS[id % NUM_COLORS];
}

/* ============================================================
 *  Process Control Block (shared by all state models)
 * ============================================================ */
typedef struct {
    int    pid;
    char   name[MAX_NAME];
    int    state;           /* meaning depends on active model */
    int    priority;
    int    burst_time;
    int    arrival_time;
} PCB;

/* ============================================================
 *  2-STATE MODEL
 * ============================================================ */
enum state2 { S2_RUNNING = 0, S2_NOT_RUNNING = 1 };

static const char *state2_str(int s) {
    switch (s) {
        case S2_RUNNING:     return BGREEN  "RUNNING"     RESET;
        case S2_NOT_RUNNING: return BYELLOW "NOT RUNNING" RESET;
        default:             return "???";
    }
}

static void show_table_2(PCB *procs, int n) {
    print_subheader("Process Table (2-State)");
    printf("  " BOLD "%-5s %-12s %-16s %-6s %-6s\n" RESET,
           "PID", "Name", "State", "Prior", "Burst");
    print_line();
    for (int i = 0; i < n; i++) {
        printf("  %-5d %-12s %-16s %-6d %-6d\n",
               procs[i].pid, procs[i].name,
               state2_str(procs[i].state),
               procs[i].priority, procs[i].burst_time);
    }
}

static void demo_2state(void) {
    PCB procs[MAX_PROCS];
    int n = 0, next_pid = 1;

    while (1) {
        cls();
        print_header("2-STATE PROCESS MODEL");
        printf("\n  " DIM "States: " BGREEN "Running" RESET
               DIM " <-> " BYELLOW "Not Running" RESET "\n");

        if (n > 0) show_table_2(procs, n);

        printf("\n  " BWHITE "1." RESET " Create process\n");
        printf("  " BWHITE "2." RESET " Toggle state (Running <-> Not Running)\n");
        printf("  " BWHITE "0." RESET " Back\n");

        int ch = get_int("Choice: ", 0, 2);
        if (ch == 0) return;

        if (ch == 1) {
            if (n >= MAX_PROCS) {
                printf(BRED "  Process table full!\n" RESET);
                wait_enter(); continue;
            }
            PCB *p = &procs[n];
            p->pid = next_pid++;
            get_str("Process name: ", p->name, MAX_NAME);
            p->priority   = get_int("Priority (1-10): ", 1, 10);
            p->burst_time = get_pos_int("Burst time: ");
            p->arrival_time = n;
            p->state = S2_NOT_RUNNING;
            n++;
            printf(BGREEN "  Created PID %d [%s] -> NOT RUNNING\n" RESET,
                   p->pid, p->name);
            wait_enter();
        } else {
            int pid = get_int("Enter PID to toggle: ", 1, next_pid - 1);
            int found = 0;
            for (int i = 0; i < n; i++) {
                if (procs[i].pid == pid) {
                    procs[i].state = 1 - procs[i].state;
                    printf(BCYAN "  PID %d [%s] -> %s\n" RESET,
                           pid, procs[i].name,
                           state2_str(procs[i].state));
                    found = 1; break;
                }
            }
            if (!found) printf(BRED "  PID %d not found.\n" RESET, pid);
            wait_enter();
        }
    }
}

/* ============================================================
 *  5-STATE MODEL
 * ============================================================ */
enum state5 {
    S5_NEW = 0, S5_READY, S5_RUNNING, S5_WAITING, S5_TERMINATED
};

static const char *state5_str(int s) {
    switch (s) {
        case S5_NEW:        return BCYAN    "NEW"        RESET;
        case S5_READY:      return BYELLOW  "READY"      RESET;
        case S5_RUNNING:    return BGREEN   "RUNNING"    RESET;
        case S5_WAITING:    return BMAGENTA "WAITING"    RESET;
        case S5_TERMINATED: return BRED     "TERMINATED" RESET;
        default:            return "???";
    }
}

static void show_table_5(PCB *procs, int n) {
    print_subheader("Process Table (5-State)");
    printf("  " BOLD "%-5s %-12s %-18s %-6s %-6s\n" RESET,
           "PID", "Name", "State", "Prior", "Burst");
    print_line();
    for (int i = 0; i < n; i++) {
        printf("  %-5d %-12s %-18s %-6d %-6d\n",
               procs[i].pid, procs[i].name,
               state5_str(procs[i].state),
               procs[i].priority, procs[i].burst_time);
    }
}

static void show_5state_diagram(void) {
    printf("\n  " DIM "Transition Diagram:" RESET "\n");
    printf("  " BCYAN "NEW" RESET " --admit--> " BYELLOW "READY" RESET
           " --dispatch--> " BGREEN "RUNNING" RESET "\n");
    printf("                           ^               |\n");
    printf("                  timeout--+               +--exit--> "
           BRED "TERMINATED" RESET "\n");
    printf("                           |               |\n");
    printf("                  signal---+-- " BMAGENTA "WAITING" RESET
           " <--wait--+\n\n");
}

static int valid_5(int from, int to) {
    if (from == S5_NEW        && to == S5_READY)      return 1;
    if (from == S5_READY      && to == S5_RUNNING)    return 1;
    if (from == S5_RUNNING    && to == S5_READY)      return 1;
    if (from == S5_RUNNING    && to == S5_WAITING)    return 1;
    if (from == S5_RUNNING    && to == S5_TERMINATED) return 1;
    if (from == S5_WAITING    && to == S5_READY)      return 1;
    return 0;
}

static void demo_5state(void) {
    PCB procs[MAX_PROCS];
    int n = 0, next_pid = 1;

    while (1) {
        cls();
        print_header("5-STATE PROCESS MODEL");
        show_5state_diagram();
        if (n > 0) show_table_5(procs, n);

        printf("\n  " BWHITE "1." RESET " Create process (-> NEW)\n");
        printf("  " BWHITE "2." RESET " Admit        (NEW -> READY)\n");
        printf("  " BWHITE "3." RESET " Dispatch      (READY -> RUNNING)\n");
        printf("  " BWHITE "4." RESET " Timeout       (RUNNING -> READY)\n");
        printf("  " BWHITE "5." RESET " Event Wait    (RUNNING -> WAITING)\n");
        printf("  " BWHITE "6." RESET " Event Signal  (WAITING -> READY)\n");
        printf("  " BWHITE "7." RESET " Exit          (RUNNING -> TERMINATED)\n");
        printf("  " BWHITE "0." RESET " Back\n");

        int ch = get_int("Choice: ", 0, 7);
        if (ch == 0) return;

        if (ch == 1) {
            if (n >= MAX_PROCS) {
                printf(BRED "  Process table full!\n" RESET);
                wait_enter(); continue;
            }
            PCB *p = &procs[n];
            p->pid = next_pid++;
            get_str("Process name: ", p->name, MAX_NAME);
            p->priority   = get_int("Priority (1-10): ", 1, 10);
            p->burst_time = get_pos_int("Burst time: ");
            p->arrival_time = n;
            p->state = S5_NEW;
            n++;
            printf(BGREEN "  Created PID %d [%s] -> NEW\n" RESET,
                   p->pid, p->name);
            wait_enter();
            continue;
        }

        /* Map menu choice to desired target state */
        int target;
        switch (ch) {
            case 2: target = S5_READY;      break;
            case 3: target = S5_RUNNING;    break;
            case 4: target = S5_READY;      break;
            case 5: target = S5_WAITING;    break;
            case 6: target = S5_READY;      break;
            case 7: target = S5_TERMINATED; break;
            default: continue;
        }
        /* Expected source state for the chosen event */
        int expected_src;
        switch (ch) {
            case 2: expected_src = S5_NEW;     break;
            case 3: expected_src = S5_READY;   break;
            case 4: expected_src = S5_RUNNING; break;
            case 5: expected_src = S5_RUNNING; break;
            case 6: expected_src = S5_WAITING; break;
            case 7: expected_src = S5_RUNNING; break;
            default: continue;
        }

        int pid = get_int("Enter PID: ", 1, next_pid - 1);
        int found = 0;
        for (int i = 0; i < n; i++) {
            if (procs[i].pid == pid) {
                found = 1;
                if (procs[i].state != expected_src ||
                    !valid_5(procs[i].state, target)) {
                    printf(BRED "  Invalid transition! PID %d is %s, "
                           "cannot apply this event.\n" RESET,
                           pid, state5_str(procs[i].state));
                } else {
                    printf(BCYAN "  PID %d [%s]: %s -> %s\n" RESET,
                           pid, procs[i].name,
                           state5_str(procs[i].state),
                           state5_str(target));
                    procs[i].state = target;
                }
                break;
            }
        }
        if (!found) printf(BRED "  PID %d not found.\n" RESET, pid);
        wait_enter();
    }
}

/* ============================================================
 *  7-STATE MODEL
 * ============================================================ */
enum state7 {
    S7_NEW = 0, S7_READY, S7_RUNNING, S7_WAITING,
    S7_SUSP_READY, S7_SUSP_WAIT, S7_TERMINATED
};

static const char *state7_str(int s) {
    switch (s) {
        case S7_NEW:        return BCYAN    "NEW"            RESET;
        case S7_READY:      return BYELLOW  "READY"          RESET;
        case S7_RUNNING:    return BGREEN   "RUNNING"        RESET;
        case S7_WAITING:    return BMAGENTA "WAITING"        RESET;
        case S7_SUSP_READY: return BBLUE    "SUSPENDED-RDY"  RESET;
        case S7_SUSP_WAIT:  return RED      "SUSPENDED-WAIT" RESET;
        case S7_TERMINATED: return BRED     "TERMINATED"     RESET;
        default:            return "???";
    }
}

static void show_table_7(PCB *procs, int n) {
    print_subheader("Process Table (7-State)");
    printf("  " BOLD "%-5s %-12s %-22s %-6s %-6s\n" RESET,
           "PID", "Name", "State", "Prior", "Burst");
    print_line();
    for (int i = 0; i < n; i++) {
        printf("  %-5d %-12s %-22s %-6d %-6d\n",
               procs[i].pid, procs[i].name,
               state7_str(procs[i].state),
               procs[i].priority, procs[i].burst_time);
    }
}

static void show_7state_diagram(void) {
    printf("\n  " DIM "Transition Diagram:" RESET "\n");
    printf("  " BCYAN "NEW" RESET " --admit--> " BYELLOW "READY" RESET
           " --dispatch--> " BGREEN "RUNNING" RESET
           " --exit--> " BRED "TERMINATED" RESET "\n");
    printf("                      ^    |           |   |\n");
    printf("             timeout--+    |swap out   |   +--wait--> "
           BMAGENTA "WAITING" RESET "\n");
    printf("              signal--+    v           |              |"
           " swap out\n");
    printf("          " BBLUE "SUSP-READY" RESET
           " <--------+    swap out  |              v\n");
    printf("              ^  swap in        |        " RED
           "SUSP-WAIT" RESET "\n");
    printf("              +--signal---------+-----------^\n\n");
}

static int valid_7(int from, int to) {
    if (from == S7_NEW        && to == S7_READY)      return 1;
    if (from == S7_READY      && to == S7_RUNNING)    return 1;
    if (from == S7_RUNNING    && to == S7_READY)      return 1;
    if (from == S7_RUNNING    && to == S7_WAITING)    return 1;
    if (from == S7_RUNNING    && to == S7_TERMINATED) return 1;
    if (from == S7_WAITING    && to == S7_READY)      return 1;
    /* Suspend / resume transitions */
    if (from == S7_READY      && to == S7_SUSP_READY) return 1;
    if (from == S7_SUSP_READY && to == S7_READY)      return 1;
    if (from == S7_WAITING    && to == S7_SUSP_WAIT)  return 1;
    if (from == S7_SUSP_WAIT  && to == S7_SUSP_READY) return 1;
    if (from == S7_SUSP_WAIT  && to == S7_WAITING)    return 1;
    if (from == S7_RUNNING    && to == S7_SUSP_READY) return 1;
    return 0;
}

static void demo_7state(void) {
    PCB procs[MAX_PROCS];
    int n = 0, next_pid = 1;

    while (1) {
        cls();
        print_header("7-STATE PROCESS MODEL");
        show_7state_diagram();
        if (n > 0) show_table_7(procs, n);

        printf("\n  " BWHITE " 1." RESET " Create process (-> NEW)\n");
        printf("  " BWHITE " 2." RESET " Admit          (NEW -> READY)\n");
        printf("  " BWHITE " 3." RESET " Dispatch        (READY -> RUNNING)\n");
        printf("  " BWHITE " 4." RESET " Timeout         (RUNNING -> READY)\n");
        printf("  " BWHITE " 5." RESET " Event Wait      (RUNNING -> WAITING)\n");
        printf("  " BWHITE " 6." RESET " Event Signal    (WAITING -> READY)\n");
        printf("  " BWHITE " 7." RESET " Exit            (RUNNING -> TERMINATED)\n");
        printf("  " BWHITE " 8." RESET " Swap Out Ready  (READY -> SUSP-READY)\n");
        printf("  " BWHITE " 9." RESET " Swap In Ready   (SUSP-READY -> READY)\n");
        printf("  " BWHITE "10." RESET " Swap Out Wait   (WAITING -> SUSP-WAIT)\n");
        printf("  " BWHITE "11." RESET " Signal Susp     (SUSP-WAIT -> SUSP-READY)\n");
        printf("  " BWHITE "12." RESET " Swap In Wait    (SUSP-WAIT -> WAITING)\n");
        printf("  " BWHITE "13." RESET " Swap Out Run    (RUNNING -> SUSP-READY)\n");
        printf("  " BWHITE " 0." RESET " Back\n");

        int ch = get_int("Choice: ", 0, 13);
        if (ch == 0) return;

        if (ch == 1) {
            if (n >= MAX_PROCS) {
                printf(BRED "  Process table full!\n" RESET);
                wait_enter(); continue;
            }
            PCB *p = &procs[n];
            p->pid = next_pid++;
            get_str("Process name: ", p->name, MAX_NAME);
            p->priority   = get_int("Priority (1-10): ", 1, 10);
            p->burst_time = get_pos_int("Burst time: ");
            p->arrival_time = n;
            p->state = S7_NEW;
            n++;
            printf(BGREEN "  Created PID %d [%s] -> NEW\n" RESET,
                   p->pid, p->name);
            wait_enter();
            continue;
        }

        /* source -> target mapping for each event */
        static const int map[][2] = {
            /*  2 */ { S7_NEW,        S7_READY      },
            /*  3 */ { S7_READY,      S7_RUNNING    },
            /*  4 */ { S7_RUNNING,    S7_READY      },
            /*  5 */ { S7_RUNNING,    S7_WAITING    },
            /*  6 */ { S7_WAITING,    S7_READY      },
            /*  7 */ { S7_RUNNING,    S7_TERMINATED },
            /*  8 */ { S7_READY,      S7_SUSP_READY },
            /*  9 */ { S7_SUSP_READY, S7_READY      },
            /* 10 */ { S7_WAITING,    S7_SUSP_WAIT  },
            /* 11 */ { S7_SUSP_WAIT,  S7_SUSP_READY },
            /* 12 */ { S7_SUSP_WAIT,  S7_WAITING    },
            /* 13 */ { S7_RUNNING,    S7_SUSP_READY },
        };
        int idx = ch - 2;
        int expected_src = map[idx][0];
        int target       = map[idx][1];

        int pid = get_int("Enter PID: ", 1, next_pid - 1);
        int found = 0;
        for (int i = 0; i < n; i++) {
            if (procs[i].pid == pid) {
                found = 1;
                if (procs[i].state != expected_src ||
                    !valid_7(procs[i].state, target)) {
                    printf(BRED "  Invalid transition! PID %d is %s, "
                           "cannot apply this event.\n" RESET,
                           pid, state7_str(procs[i].state));
                } else {
                    printf(BCYAN "  PID %d [%s]: %s -> %s" RESET,
                           pid, procs[i].name,
                           state7_str(procs[i].state),
                           state7_str(target));
                    if (target == S7_SUSP_READY || target == S7_SUSP_WAIT)
                        printf(BYELLOW "  [swapped to disk]" RESET);
                    if (expected_src == S7_SUSP_READY ||
                        expected_src == S7_SUSP_WAIT)
                        printf(BGREEN "  [swapped to memory]" RESET);
                    printf("\n");
                    procs[i].state = target;
                }
                break;
            }
        }
        if (!found) printf(BRED "  PID %d not found.\n" RESET, pid);
        wait_enter();
    }
}

/* ============================================================
 *  Process State Models sub-menu
 * ============================================================ */
static void state_models_menu(void) {
    while (1) {
        cls();
        print_header("PROCESS STATE MODELS");
        printf("\n  " BWHITE "1." RESET " 2-State Model Demo\n");
        printf("  " BWHITE "2." RESET " 5-State Model Demo\n");
        printf("  " BWHITE "3." RESET " 7-State Model Demo\n");
        printf("  " BWHITE "0." RESET " Back\n");

        int ch = get_int("Choice: ", 0, 3);
        switch (ch) {
            case 1: demo_2state(); break;
            case 2: demo_5state(); break;
            case 3: demo_7state(); break;
            case 0: return;
        }
    }
}

/* ============================================================
 *  PRODUCER-CONSUMER (Bounded Buffer)
 * ============================================================ */
typedef struct {
    int    buffer[MAX_BUF];
    int    buf_size;
    int    in;
    int    out;
    int    count;
    int    total_to_produce;
    int    produced;
    int    consumed;
    int    done;               /* flag: all items produced */
    sem_t  empty;
    sem_t  full;
    pthread_mutex_t mtx;
} ProdConData;

static void print_buffer(ProdConData *d) {
    printf("  " DIM "Buffer [");
    for (int i = 0; i < d->buf_size; i++) {
        if (i) printf(",");
        /* Show items currently in buffer */
        int idx = (d->out + i) % d->buf_size;
        if (i < d->count)
            printf(BWHITE "%3d" RESET DIM, d->buffer[idx]);
        else
            printf("  _");
    }
    printf("]  (%d/%d)" RESET "\n", d->count, d->buf_size);
}

/* Wrapper passed to each thread */
typedef struct {
    int          id;
    ProdConData *data;
} ThreadArg;

static void *producer_thread(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    int id        = ta->id;
    ProdConData *d = ta->data;

    while (1) {
        sem_wait(&d->empty);
        pthread_mutex_lock(&d->mtx);

        if (d->produced >= d->total_to_produce) {
            pthread_mutex_unlock(&d->mtx);
            sem_post(&d->full);   /* unblock a waiting consumer */
            break;
        }

        int item = rand() % 100 + 1;
        d->buffer[d->in] = item;
        d->in = (d->in + 1) % d->buf_size;
        d->count++;
        d->produced++;
        int seq = d->produced;

        pthread_mutex_lock(&print_lock);
        printf("  %s[Producer %d]" RESET " produced item " BOLD "%3d"
               RESET " (%d/%d)\n", thread_color(id), id, item,
               seq, d->total_to_produce);
        print_buffer(d);
        pthread_mutex_unlock(&print_lock);

        pthread_mutex_unlock(&d->mtx);
        sem_post(&d->full);
        SLEEP_MS(200 + rand() % 300);
    }
    return NULL;
}

static void *consumer_thread(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    int id        = ta->id;
    ProdConData *d = ta->data;

    while (1) {
        sem_wait(&d->full);
        pthread_mutex_lock(&d->mtx);

        if (d->consumed >= d->total_to_produce && d->count == 0) {
            pthread_mutex_unlock(&d->mtx);
            sem_post(&d->empty);  /* unblock a waiting producer */
            break;
        }
        if (d->count == 0) {
            /* Spurious wake or done signal -- re-check */
            pthread_mutex_unlock(&d->mtx);
            if (d->done && d->consumed >= d->total_to_produce) break;
            sem_post(&d->full);
            SLEEP_MS(50);
            continue;
        }

        int item = d->buffer[d->out];
        d->out = (d->out + 1) % d->buf_size;
        d->count--;
        d->consumed++;
        int seq = d->consumed;

        pthread_mutex_lock(&print_lock);
        printf("  %s[Consumer %d]" RESET " consumed item " BOLD "%3d"
               RESET " (%d/%d)\n", thread_color(id + 3), id, item,
               seq, d->total_to_produce);
        print_buffer(d);
        pthread_mutex_unlock(&print_lock);

        pthread_mutex_unlock(&d->mtx);
        sem_post(&d->empty);
        SLEEP_MS(200 + rand() % 300);
    }
    return NULL;
}

static void demo_producer_consumer(void) {
    cls();
    print_header("PRODUCER-CONSUMER PROBLEM");
    printf("\n  " DIM "Bounded buffer with semaphores + mutex." RESET "\n\n");

    int buf_sz     = get_int("Buffer size (1-10): ", 1, MAX_BUF);
    int n_prod     = get_int("Number of producers (1-5): ", 1, 5);
    int n_cons     = get_int("Number of consumers (1-5): ", 1, 5);
    int total_items = get_int("Total items to produce (1-50): ", 1, 50);

    ProdConData d;
    memset(&d, 0, sizeof(d));
    d.buf_size        = buf_sz;
    d.total_to_produce = total_items;
    sem_init(&d.empty, 0, buf_sz);
    sem_init(&d.full,  0, 0);
    pthread_mutex_init(&d.mtx, NULL);
    srand((unsigned)time(NULL));

    printf("\n");
    print_subheader("Simulation Running");

    pthread_t prod_t[5], cons_t[5];
    ThreadArg pargs[5], cargs[5];

    for (int i = 0; i < n_prod; i++) {
        pargs[i].id   = i + 1;
        pargs[i].data  = &d;
        pthread_create(&prod_t[i], NULL, producer_thread, &pargs[i]);
    }
    for (int i = 0; i < n_cons; i++) {
        cargs[i].id   = i + 1;
        cargs[i].data  = &d;
        pthread_create(&cons_t[i], NULL, consumer_thread, &cargs[i]);
    }

    /* Wait for all producers */
    for (int i = 0; i < n_prod; i++)
        pthread_join(prod_t[i], NULL);

    d.done = 1;
    /* Wake up consumers that may be blocked */
    for (int i = 0; i < n_cons; i++)
        sem_post(&d.full);

    for (int i = 0; i < n_cons; i++)
        pthread_join(cons_t[i], NULL);

    sem_destroy(&d.empty);
    sem_destroy(&d.full);
    pthread_mutex_destroy(&d.mtx);

    printf("\n  " BGREEN "Simulation complete. Produced: %d  Consumed: %d"
           RESET "\n", d.produced, d.consumed);
    wait_enter();
}

/* ============================================================
 *  READERS-WRITERS (reader preference)
 * ============================================================ */
typedef struct {
    int             shared_data;
    int             reader_count;
    int             total_ops;
    int             ops_done;
    int             done;
    pthread_mutex_t rc_lock;     /* protects reader_count */
    pthread_mutex_t write_lock;  /* exclusive writer access */
} RWData;

typedef struct {
    int     id;
    int     ops;     /* operations this thread will perform */
    RWData *data;
} RWArg;

static void *reader_thread(void *arg) {
    RWArg *ra  = (RWArg *)arg;
    int id     = ra->id;
    RWData *rw = ra->data;

    for (int i = 0; i < ra->ops && !rw->done; i++) {
        pthread_mutex_lock(&rw->rc_lock);
        rw->reader_count++;
        if (rw->reader_count == 1)
            pthread_mutex_lock(&rw->write_lock);
        pthread_mutex_unlock(&rw->rc_lock);

        /* --- reading --- */
        pthread_mutex_lock(&print_lock);
        printf("  %s[Reader %d]" RESET " reads value = " BOLD "%d"
               RESET "  (active readers: %d)\n",
               thread_color(id), id, rw->shared_data, rw->reader_count);
        pthread_mutex_unlock(&print_lock);
        SLEEP_MS(200 + rand() % 200);

        pthread_mutex_lock(&rw->rc_lock);
        rw->reader_count--;
        if (rw->reader_count == 0)
            pthread_mutex_unlock(&rw->write_lock);
        rw->ops_done++;
        pthread_mutex_unlock(&rw->rc_lock);

        SLEEP_MS(100 + rand() % 200);
    }
    return NULL;
}

static void *writer_thread(void *arg) {
    RWArg *wa  = (RWArg *)arg;
    int id     = wa->id;
    RWData *rw = wa->data;

    for (int i = 0; i < wa->ops && !rw->done; i++) {
        pthread_mutex_lock(&rw->write_lock);

        int old_val = rw->shared_data;
        rw->shared_data += (rand() % 10 + 1);

        pthread_mutex_lock(&print_lock);
        printf("  %s[Writer %d]" RESET " writes value " BOLD "%d -> %d"
               RESET "  " BRED "(exclusive)" RESET "\n",
               thread_color(id + 4), id, old_val, rw->shared_data);
        pthread_mutex_unlock(&print_lock);
        SLEEP_MS(300 + rand() % 200);

        rw->ops_done++;
        pthread_mutex_unlock(&rw->write_lock);

        SLEEP_MS(200 + rand() % 300);
    }
    return NULL;
}

static void demo_readers_writers(void) {
    cls();
    print_header("READERS-WRITERS PROBLEM");
    printf("\n  " DIM "Reader-preference solution: multiple readers OR "
           "one exclusive writer." RESET "\n\n");

    int n_readers = get_int("Number of readers (1-5): ", 1, 5);
    int n_writers = get_int("Number of writers (1-3): ", 1, 3);
    int ops_each  = get_int("Operations per thread (1-10): ", 1, 10);

    RWData rw;
    memset(&rw, 0, sizeof(rw));
    rw.total_ops = (n_readers + n_writers) * ops_each;
    pthread_mutex_init(&rw.rc_lock, NULL);
    pthread_mutex_init(&rw.write_lock, NULL);
    srand((unsigned)time(NULL));

    printf("\n");
    print_subheader("Simulation Running");

    pthread_t rthr[5], wthr[3];
    RWArg rargs[5], wargs[3];

    for (int i = 0; i < n_readers; i++) {
        rargs[i].id   = i + 1;
        rargs[i].ops  = ops_each;
        rargs[i].data = &rw;
        pthread_create(&rthr[i], NULL, reader_thread, &rargs[i]);
    }
    for (int i = 0; i < n_writers; i++) {
        wargs[i].id   = i + 1;
        wargs[i].ops  = ops_each;
        wargs[i].data = &rw;
        pthread_create(&wthr[i], NULL, writer_thread, &wargs[i]);
    }

    for (int i = 0; i < n_readers; i++)
        pthread_join(rthr[i], NULL);
    for (int i = 0; i < n_writers; i++)
        pthread_join(wthr[i], NULL);

    rw.done = 1;
    pthread_mutex_destroy(&rw.rc_lock);
    pthread_mutex_destroy(&rw.write_lock);

    printf("\n  " BGREEN "Simulation complete. Final value: %d  "
           "Total ops: %d" RESET "\n", rw.shared_data, rw.ops_done);
    wait_enter();
}

/* ============================================================
 *  DINING PHILOSOPHERS
 * ============================================================ */
typedef struct {
    pthread_mutex_t forks[NUM_PHILOSOPHERS];
    int  state[NUM_PHILOSOPHERS];   /* 0=THINKING, 1=HUNGRY, 2=EATING */
    int  rounds;
    int  eat_count[NUM_PHILOSOPHERS];
} DiningData;

typedef struct {
    int         id;
    DiningData *data;
} PhilArg;

static const char *phil_state_str(int s) {
    switch (s) {
        case 0: return BCYAN   "THINKING" RESET;
        case 1: return BYELLOW "HUNGRY"   RESET;
        case 2: return BGREEN  "EATING"   RESET;
        default: return "???";
    }
}

static void show_table_phil(DiningData *d) {
    printf("  " DIM "+-----------+----------+-------+" RESET "\n");
    printf("  " DIM "| Philosopher|  State   | Meals |" RESET "\n");
    printf("  " DIM "+-----------+----------+-------+" RESET "\n");
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        printf("  " DIM "|" RESET "     %d     " DIM "|" RESET
               " %-8s " DIM "|" RESET "  %3d  " DIM "|" RESET "\n",
               i, phil_state_str(d->state[i]), d->eat_count[i]);
    }
    printf("  " DIM "+-----------+----------+-------+" RESET "\n");
}

static void *philosopher_thread(void *arg) {
    PhilArg *pa    = (PhilArg *)arg;
    int id         = pa->id;
    DiningData *d  = pa->data;

    /* Resource ordering: always pick lower-numbered fork first */
    int left  = id;
    int right = (id + 1) % NUM_PHILOSOPHERS;
    int first  = MIN(left, right);
    int second = MAX(left, right);

    for (int r = 0; r < d->rounds; r++) {
        /* THINKING */
        pthread_mutex_lock(&print_lock);
        d->state[id] = 0;
        printf("  %s[Philosopher %d]" RESET " is " BCYAN "THINKING"
               RESET "\n", thread_color(id), id);
        show_table_phil(d);
        pthread_mutex_unlock(&print_lock);
        SLEEP_MS(300 + rand() % 400);

        /* HUNGRY */
        pthread_mutex_lock(&print_lock);
        d->state[id] = 1;
        printf("  %s[Philosopher %d]" RESET " is " BYELLOW "HUNGRY"
               RESET " (wants fork %d & %d)\n",
               thread_color(id), id, first, second);
        pthread_mutex_unlock(&print_lock);

        /* Pick up forks in order */
        pthread_mutex_lock(&d->forks[first]);
        pthread_mutex_lock(&print_lock);
        printf("  %s[Philosopher %d]" RESET " picked up fork %d\n",
               thread_color(id), id, first);
        pthread_mutex_unlock(&print_lock);

        pthread_mutex_lock(&d->forks[second]);
        pthread_mutex_lock(&print_lock);
        printf("  %s[Philosopher %d]" RESET " picked up fork %d\n",
               thread_color(id), id, second);
        pthread_mutex_unlock(&print_lock);

        /* EATING */
        pthread_mutex_lock(&print_lock);
        d->state[id] = 2;
        d->eat_count[id]++;
        printf("  %s[Philosopher %d]" RESET " is " BGREEN "EATING"
               RESET " (meal #%d)\n", thread_color(id), id,
               d->eat_count[id]);
        show_table_phil(d);
        pthread_mutex_unlock(&print_lock);
        SLEEP_MS(400 + rand() % 300);

        /* Put down forks */
        pthread_mutex_unlock(&d->forks[second]);
        pthread_mutex_unlock(&d->forks[first]);

        pthread_mutex_lock(&print_lock);
        printf("  %s[Philosopher %d]" RESET " put down forks %d & %d\n",
               thread_color(id), id, first, second);
        pthread_mutex_unlock(&print_lock);
    }
    return NULL;
}

static void demo_dining_philosophers(void) {
    cls();
    print_header("DINING PHILOSOPHERS PROBLEM");
    printf("\n  " DIM "5 philosophers, 5 forks. Deadlock prevented via "
           "resource ordering\n  (always pick lower-numbered fork first)."
           RESET "\n\n");

    int rounds = get_int("Eating rounds per philosopher (1-10): ", 1, 10);

    DiningData d;
    memset(&d, 0, sizeof(d));
    d.rounds = rounds;
    for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        pthread_mutex_init(&d.forks[i], NULL);
    srand((unsigned)time(NULL));

    printf("\n");
    print_subheader("Simulation Running");

    pthread_t phil_t[NUM_PHILOSOPHERS];
    PhilArg   pargs[NUM_PHILOSOPHERS];

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pargs[i].id   = i;
        pargs[i].data = &d;
        pthread_create(&phil_t[i], NULL, philosopher_thread, &pargs[i]);
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        pthread_join(phil_t[i], NULL);

    for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        pthread_mutex_destroy(&d.forks[i]);

    printf("\n  " BGREEN "Simulation complete. No deadlock occurred!"
           RESET "\n");
    print_subheader("Final Results");
    show_table_phil(&d);
    wait_enter();
}

/* ============================================================
 *  Concurrency sub-menu
 * ============================================================ */
static void concurrency_menu(void) {
    while (1) {
        cls();
        print_header("CONCURRENCY SIMULATIONS");
        printf("\n  " BWHITE "1." RESET " Producer-Consumer Problem\n");
        printf("  " BWHITE "2." RESET " Readers-Writers Problem\n");
        printf("  " BWHITE "3." RESET " Dining Philosophers Problem\n");
        printf("  " BWHITE "0." RESET " Back\n");

        int ch = get_int("Choice: ", 0, 3);
        switch (ch) {
            case 1: demo_producer_consumer();    break;
            case 2: demo_readers_writers();      break;
            case 3: demo_dining_philosophers();  break;
            case 0: return;
        }
    }
}

/* ============================================================
 *  PUBLIC: top-level Process Management menu
 * ============================================================ */
void process_mgmt_menu(void) {
    while (1) {
        cls();
        print_header("PROCESS MANAGEMENT");
        printf("\n  " BWHITE "1." RESET " Process State Models\n");
        printf("     " DIM "1a. 2-State Model Demo" RESET "\n");
        printf("     " DIM "1b. 5-State Model Demo" RESET "\n");
        printf("     " DIM "1c. 7-State Model Demo" RESET "\n");
        printf("  " BWHITE "2." RESET " Concurrency Simulations\n");
        printf("     " DIM "2a. Producer-Consumer Problem" RESET "\n");
        printf("     " DIM "2b. Readers-Writers Problem" RESET "\n");
        printf("     " DIM "2c. Dining Philosophers Problem" RESET "\n");
        printf("  " BWHITE "0." RESET " Back to Main Menu\n");

        int ch = get_int("Choice: ", 0, 2);
        switch (ch) {
            case 1: state_models_menu(); break;
            case 2: concurrency_menu();  break;
            case 0: return;
        }
    }
}
