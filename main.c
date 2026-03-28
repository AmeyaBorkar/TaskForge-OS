/* ================================================================
 *  TaskForge -- Multi-Threaded Parallel Task Processing Engine
 *  Main entry point: interactive menu integrating all OS modules
 * ================================================================ */

#include "common.h"
#include "process_mgmt.h"
#include "scheduler.h"
#include "deadlock.h"
#include "memory_mgmt.h"
#include "io_file_mgmt.h"
#include "task_ops.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* Enable ANSI escape codes and UTF-8 on Windows console */
static void init_console(void) {
#ifdef _WIN32
    /* Enable ANSI/VT100 processing for colors */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
    }
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

/* -- Banner ---------------------------------------------------- */
static void print_banner(void) {
    printf(BCYAN "\n");
    printf("  ============================================================\n");
    printf("  |                                                          |\n");
    printf("  |" BWHITE "      TASKFORGE -- OS Concepts Task Engine v1.0          " BCYAN "|\n");
    printf("  |" BYELLOW "      Parallel Task Processing + OS Simulator           " BCYAN "|\n");
    printf("  |                                                          |\n");
    printf("  ============================================================\n");
    printf(RESET "\n");
}

/* -- Main Menu ------------------------------------------------- */
static void print_main_menu(void) {
    printf(BCYAN "  +----------------------------------------------------------+\n");
    printf("  |              " BWHITE "M A I N    M E N U" BCYAN "                      |\n");
    printf("  +----------------------------------------------------------+\n" RESET);

    printf(BYELLOW "\n  [A] APPLICATION -- Real File Operations\n" RESET);
    printf("      " BGREEN "1." RESET "  Task Operations (Search, Compress, Encrypt...)\n");

    printf(BYELLOW "\n  [B] OS CONCEPTS LAB -- Interactive Simulations\n" RESET);
    printf("      " BMAGENTA "2." RESET "  Process Management & Concurrency\n");
    printf("      " BMAGENTA "3." RESET "  CPU Scheduling Algorithms\n");
    printf("      " BMAGENTA "4." RESET "  Deadlock Management\n");
    printf("      " BMAGENTA "5." RESET "  Memory Management\n");
    printf("      " BMAGENTA "6." RESET "  I/O & File System Management\n");

    printf(BYELLOW "\n  [C] INFO\n" RESET);
    printf("      " BCYAN "7." RESET "  About TaskForge\n");

    printf("\n      " BRED "0." RESET "  Exit\n");

    printf(BCYAN "\n  +----------------------------------------------------------+\n" RESET);
}

/* -- About / Credits ------------------------------------------- */
static void print_about(void) {
    print_header("ABOUT TASKFORGE");
    printf("\n");
    printf("  " BWHITE "TaskForge" RESET " is a multi-threaded parallel task processing engine\n");
    printf("  that demonstrates Operating System concepts through a real,\n");
    printf("  interactive application.\n\n");
    printf("  " BYELLOW "OS Concepts Covered:" RESET "\n");
    printf("    " BGREEN "Unit I  " RESET " - System Calls, OS Types\n");
    printf("    " BGREEN "Unit II " RESET " - Process Management, Threads, Concurrency\n");
    printf("    " BGREEN "Unit III" RESET " - CPU Scheduling (FCFS, SJF, RR, Priority)\n");
    printf("    " BGREEN "Unit IV " RESET " - Deadlocks (Banker's, RAG, Detection)\n");
    printf("    " BGREEN "Unit V  " RESET " - Memory Management (Paging, Replacement, etc.)\n");
    printf("    " BGREEN "Unit VI " RESET " - I/O & File Management (Disk Scheduling, VFS)\n");
    printf("\n");
    printf("  " BYELLOW "Application Features:" RESET "\n");
    printf("    File Search, Compression, Encryption, Sorting,\n");
    printf("    Word Count, Checksum, File Copy, Batch Rename\n");
    printf("\n");
    printf("  " DIM "Built with C (pthreads) | Course Project - Operating Systems" RESET "\n");
    wait_enter();
}

/* -- Entry Point ----------------------------------------------- */
int main(void) {
    int choice;

    init_console();

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    cls();
    print_banner();
    printf(BGREEN "  Welcome to TaskForge!\n" RESET);
    printf("  An interactive Operating System concepts engine.\n");
    wait_enter();

    while (1) {
        cls();
        print_banner();
        print_main_menu();

        printf("\n");
        choice = get_int("Select option [0-7]: ", 0, 7);

        switch (choice) {
            /* -- Application: Real File Operations -- */
            case 1:
                task_ops_menu();
                break;

            /* -- OS Concepts Lab -- */
            case 2:
                process_mgmt_menu();
                break;
            case 3:
                scheduler_menu();
                break;
            case 4:
                deadlock_menu();
                break;
            case 5:
                memory_mgmt_menu();
                break;
            case 6:
                io_file_mgmt_menu();
                break;

            /* -- About -- */
            case 7:
                print_about();
                break;

            /* -- Exit -- */
            case 0:
                cls();
                printf("\n");
                print_sep();
                printf(BGREEN "  Thank you for using TaskForge!\n" RESET);
                printf(DIM "  Operating Systems Course Project\n" RESET);
                print_sep();
                printf("\n");
                return 0;
        }
    }
}
