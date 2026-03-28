#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* ============================================================
 *  ANSI Color Codes
 * ============================================================ */
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define UNDERLINE   "\033[4m"

#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"

#define BRED        "\033[1;31m"
#define BGREEN      "\033[1;32m"
#define BYELLOW     "\033[1;33m"
#define BBLUE       "\033[1;34m"
#define BMAGENTA    "\033[1;35m"
#define BCYAN       "\033[1;36m"
#define BWHITE      "\033[1;37m"

#define BG_RED      "\033[41m"
#define BG_GREEN    "\033[42m"
#define BG_BLUE     "\033[44m"
#define BG_CYAN     "\033[46m"
#define BG_MAGENTA  "\033[45m"
#define BG_YELLOW   "\033[43m"

/* ============================================================
 *  Constants
 * ============================================================ */
#define MAX_PROCESSES   30
#define MAX_RESOURCES   10
#define MAX_NAME        64
#define MAX_PATH_LEN    512
#define BUF_SIZE        1024

/* ============================================================
 *  Utility Functions (inline, header-only)
 * ============================================================ */

/* Flush leftover characters from stdin */
static inline void flush_input(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* Prompt and wait for Enter */
static inline void wait_enter(void) {
    printf("\n" BYELLOW "  [Press Enter to continue]" RESET);
    flush_input();
}

/* Print a thick separator line (65 chars) */
static inline void print_sep(void) {
    printf(BCYAN);
    for (int i = 0; i < 65; i++) putchar('=');
    printf(RESET "\n");
}

/* Print a thin separator line */
static inline void print_line(void) {
    printf(DIM);
    for (int i = 0; i < 65; i++) putchar('-');
    printf(RESET "\n");
}

/* Print a centered header inside separator lines */
static inline void print_header(const char *title) {
    printf("\n");
    print_sep();
    int len = (int)strlen(title);
    int pad = (65 - len) / 2;
    if (pad < 0) pad = 0;
    printf(BCYAN);
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s" RESET "\n", title);
    print_sep();
}

/* Print a sub-section header */
static inline void print_subheader(const char *title) {
    printf("\n  " BWHITE ">> %s" RESET "\n", title);
    print_line();
}

/* Get integer input with range [lo, hi] */
static inline int get_int(const char *prompt, int lo, int hi) {
    int val;
    while (1) {
        printf("  %s", prompt);
        if (scanf("%d", &val) == 1 && val >= lo && val <= hi) {
            flush_input();
            return val;
        }
        flush_input();
        printf(BRED "  Invalid! Enter %d to %d.\n" RESET, lo, hi);
    }
}

/* Get a positive integer (> 0, no upper bound) */
static inline int get_pos_int(const char *prompt) {
    int val;
    while (1) {
        printf("  %s", prompt);
        if (scanf("%d", &val) == 1 && val > 0) {
            flush_input();
            return val;
        }
        flush_input();
        printf(BRED "  Invalid! Enter a positive number.\n" RESET);
    }
}

/* Get a non-negative integer (>= 0) */
static inline int get_nn_int(const char *prompt) {
    int val;
    while (1) {
        printf("  %s", prompt);
        if (scanf("%d", &val) == 1 && val >= 0) {
            flush_input();
            return val;
        }
        flush_input();
        printf(BRED "  Invalid! Enter a non-negative number.\n" RESET);
    }
}

/* Safe string input (reads full line) */
static inline void get_str(const char *prompt, char *buf, int maxlen) {
    printf("  %s", prompt);
    if (fgets(buf, maxlen, stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    }
}

/* Clear terminal screen */
static inline void cls(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

/* min / max macros */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif /* COMMON_H */
