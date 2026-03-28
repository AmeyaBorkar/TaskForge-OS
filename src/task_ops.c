/*  task_ops.c  --  Task Operations: real file-processing utilities
 *  Part of TaskForge (OS concepts simulator)
 *  Compile: gcc -Wall -Wextra -std=c11 -Iinclude -o taskforge src/task_ops.c
 */

#include "task_ops.h"

#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>   /* GetTickCount for elapsed-time on Windows */
#endif

/* ============================================================
 *  Internal constants
 * ============================================================ */
#define COPY_BLOCK      4096
#define MAX_WORD_LEN    128
#define TOP_WORDS       10
#define PREVIEW_BYTES   100
#define MAX_LINE        4096
#define MAX_LINES       100000
#define MAX_MATCHES     50000

/* ============================================================
 *  Helpers
 * ============================================================ */

/* Portable millisecond clock */
static long ms_clock(void)
{
#ifdef _WIN32
    return (long)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

/* Return file size via stat; -1 on error */
static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

/* Format a byte count into a human-readable string */
static void fmt_bytes(long bytes, char *buf, int buflen)
{
    if (bytes >= 1048576)
        snprintf(buf, buflen, "%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024)
        snprintf(buf, buflen, "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, buflen, "%ld B", bytes);
}

/* Draw a progress bar: [##########------] pct% (cur / total) */
static void draw_progress(const char *label, long cur, long total)
{
    int bar_w = 30;
    int filled = (total > 0) ? (int)((cur * bar_w) / total) : 0;
    int pct    = (total > 0) ? (int)((cur * 100) / total) : 100;
    char s1[32], s2[32];
    fmt_bytes(cur, s1, sizeof s1);
    fmt_bytes(total, s2, sizeof s2);

    printf("\r  %s [" BGREEN, label);
    for (int i = 0; i < bar_w; i++)
        putchar(i < filled ? '#' : ' ');
    printf(RESET "] %3d%% (%s / %s)  ", pct, s1, s2);
    fflush(stdout);
}

/* ============================================================
 *  1. File Search  (Recursive Grep)
 * ============================================================ */

static int grep_total_matches;

static void grep_file(const char *filepath, const char *keyword)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp) return;

    char line[MAX_LINE];
    int lineno = 0;
    while (fgets(line, sizeof line, fp)) {
        lineno++;
        /* strip trailing newline for display */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        char *pos = strstr(line, keyword);
        if (!pos) continue;

        grep_total_matches++;
        /* Print filename:lineno: then line with keyword highlighted */
        printf("  " BCYAN "%s" RESET ":" BYELLOW "%d" RESET ": ", filepath, lineno);

        char *cur = line;
        size_t kwlen = strlen(keyword);
        while ((pos = strstr(cur, keyword)) != NULL) {
            /* text before match */
            printf("%.*s", (int)(pos - cur), cur);
            /* highlight match */
            printf(BRED "%.*s" RESET, (int)kwlen, pos);
            cur = pos + kwlen;
        }
        printf("%s\n", cur);
    }
    fclose(fp);
}

static void grep_dir(const char *dirpath, const char *keyword)
{
    DIR *d = opendir(dirpath);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof fullpath, "%s/%s", dirpath, ent->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            grep_dir(fullpath, keyword);
        } else if (S_ISREG(st.st_mode)) {
            grep_file(fullpath, keyword);
        }
    }
    closedir(d);
}

static void op_file_search(void)
{
    print_subheader("File Search (Recursive Grep)");
    char dirpath[MAX_PATH_LEN], keyword[MAX_WORD_LEN];
    get_str("Directory path: ", dirpath, sizeof dirpath);
    get_str("Search keyword: ", keyword, sizeof keyword);

    if (strlen(keyword) == 0) {
        printf(BRED "  Error: keyword cannot be empty.\n" RESET);
        return;
    }

    struct stat st;
    if (stat(dirpath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf(BRED "  Error: '%s' is not a valid directory.\n" RESET, dirpath);
        return;
    }

    printf("\n  Searching for " BMAGENTA "\"%s\"" RESET " in " BCYAN "%s" RESET " ...\n\n", keyword, dirpath);
    grep_total_matches = 0;
    grep_dir(dirpath, keyword);

    print_line();
    if (grep_total_matches == 0)
        printf(BYELLOW "  No matches found.\n" RESET);
    else
        printf(BGREEN "  Total matches: %d\n" RESET, grep_total_matches);
}

/* ============================================================
 *  2. Word Count & Frequency Analysis
 * ============================================================ */

typedef struct { char word[MAX_WORD_LEN]; int count; } WordFreq;

static void op_word_count(void)
{
    print_subheader("Word Count & Frequency Analysis");
    char filepath[MAX_PATH_LEN];
    get_str("File path: ", filepath, sizeof filepath);

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        printf(BRED "  Error: cannot open '%s' -- %s\n" RESET, filepath, strerror(errno));
        return;
    }

    long lines = 0, words = 0, chars = 0;
    int cap = 256;
    int wcount = 0;
    WordFreq *freq = malloc(cap * sizeof(WordFreq));
    if (!freq) { fclose(fp); printf(BRED "  Memory error.\n" RESET); return; }

    char line[MAX_LINE];
    while (fgets(line, sizeof line, fp)) {
        lines++;
        chars += (long)strlen(line);

        /* tokenize */
        char *tok = strtok(line, " \t\r\n");
        while (tok) {
            words++;
            /* lowercase copy */
            char lower[MAX_WORD_LEN];
            int i;
            for (i = 0; tok[i] && i < MAX_WORD_LEN - 1; i++)
                lower[i] = (char)tolower((unsigned char)tok[i]);
            lower[i] = '\0';

            /* update frequency table */
            int found = 0;
            for (int j = 0; j < wcount; j++) {
                if (strcmp(freq[j].word, lower) == 0) {
                    freq[j].count++;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (wcount >= cap) {
                    cap *= 2;
                    WordFreq *tmp = realloc(freq, cap * sizeof(WordFreq));
                    if (!tmp) { tok = strtok(NULL, " \t\r\n"); continue; }
                    freq = tmp;
                }
                strncpy(freq[wcount].word, lower, MAX_WORD_LEN - 1);
                freq[wcount].word[MAX_WORD_LEN - 1] = '\0';
                freq[wcount].count = 1;
                wcount++;
            }
            tok = strtok(NULL, " \t\r\n");
        }
    }
    fclose(fp);

    long fsize = file_size(filepath);
    char sbuf[32];
    fmt_bytes(fsize, sbuf, sizeof sbuf);

    printf("\n  " BWHITE "--- Statistics ---" RESET "\n");
    printf("  File size  : " BCYAN "%s" RESET "\n", sbuf);
    printf("  Lines      : " BCYAN "%ld" RESET "\n", lines);
    printf("  Words      : " BCYAN "%ld" RESET "\n", words);
    printf("  Characters : " BCYAN "%ld" RESET "\n", chars);

    /* sort by frequency descending -- simple selection sort for top N */
    int show = (wcount < TOP_WORDS) ? wcount : TOP_WORDS;
    for (int i = 0; i < show; i++) {
        int best = i;
        for (int j = i + 1; j < wcount; j++)
            if (freq[j].count > freq[best].count) best = j;
        if (best != i) {
            WordFreq tmp = freq[i];
            freq[i] = freq[best];
            freq[best] = tmp;
        }
    }

    printf("\n  " BWHITE "--- Top %d Words ---" RESET "\n", show);
    for (int i = 0; i < show; i++)
        printf("  %2d. " BYELLOW "%-20s" RESET " %d\n", i + 1, freq[i].word, freq[i].count);

    free(freq);
}

/* ============================================================
 *  3. File Encryption / Decryption  (XOR cipher)
 * ============================================================ */

static void show_preview(const char *label, const unsigned char *buf, int n)
{
    printf("  %s (%d bytes): ", label, n);
    for (int i = 0; i < n; i++) {
        if (buf[i] >= 32 && buf[i] < 127)
            putchar(buf[i]);
        else
            printf(DIM "." RESET);
    }
    printf("\n");
}

static void op_xor_cipher(void)
{
    print_subheader("File Encryption / Decryption (XOR Cipher)");
    char inpath[MAX_PATH_LEN], outpath[MAX_PATH_LEN], key[MAX_WORD_LEN];
    get_str("Input file path : ", inpath, sizeof inpath);
    get_str("Output file path: ", outpath, sizeof outpath);
    get_str("Key string      : ", key, sizeof key);

    size_t keylen = strlen(key);
    if (keylen == 0) {
        printf(BRED "  Error: key cannot be empty.\n" RESET);
        return;
    }

    FILE *fin = fopen(inpath, "rb");
    if (!fin) {
        printf(BRED "  Error: cannot open '%s' -- %s\n" RESET, inpath, strerror(errno));
        return;
    }
    FILE *fout = fopen(outpath, "wb");
    if (!fout) {
        fclose(fin);
        printf(BRED "  Error: cannot create '%s' -- %s\n" RESET, outpath, strerror(errno));
        return;
    }

    long total = file_size(inpath);
    long done = 0;
    size_t ki = 0;

    unsigned char before[PREVIEW_BYTES], after[PREVIEW_BYTES];
    int pre_n = 0;

    unsigned char buf[COPY_BLOCK];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, fin)) > 0) {
        /* save 'before' preview from first block */
        if (done == 0) {
            pre_n = (int)((n < PREVIEW_BYTES) ? n : PREVIEW_BYTES);
            memcpy(before, buf, pre_n);
        }

        for (size_t i = 0; i < n; i++) {
            buf[i] ^= (unsigned char)key[ki % keylen];
            ki++;
        }

        /* save 'after' preview from first block */
        if (done == 0)
            memcpy(after, buf, pre_n);

        fwrite(buf, 1, n, fout);
        done += (long)n;
        draw_progress("Processing", done, total);
    }
    printf("\n");
    fclose(fin);
    fclose(fout);

    printf("\n");
    show_preview(BCYAN "Before" RESET, before, pre_n);
    show_preview(BYELLOW "After " RESET, after, pre_n);

    char sb[32];
    fmt_bytes(done, sb, sizeof sb);
    printf("\n" BGREEN "  Done! %s processed.\n" RESET, sb);
}

/* ============================================================
 *  4. File Compression / Decompression  (RLE)
 * ============================================================ */

static void rle_compress(const char *inpath, const char *outpath)
{
    FILE *fin = fopen(inpath, "rb");
    if (!fin) {
        printf(BRED "  Error: cannot open '%s' -- %s\n" RESET, inpath, strerror(errno));
        return;
    }
    FILE *fout = fopen(outpath, "wb");
    if (!fout) {
        fclose(fin);
        printf(BRED "  Error: cannot create '%s' -- %s\n" RESET, outpath, strerror(errno));
        return;
    }

    long orig_size = file_size(inpath);
    long comp_size = 0;
    int prev = fgetc(fin);
    if (prev == EOF) {
        fclose(fin); fclose(fout);
        printf(BYELLOW "  File is empty, nothing to compress.\n" RESET);
        return;
    }

    unsigned char count = 1;
    int cur;
    while ((cur = fgetc(fin)) != EOF) {
        if (cur == prev && count < 255) {
            count++;
        } else {
            fputc(count, fout);
            fputc(prev, fout);
            comp_size += 2;
            prev = cur;
            count = 1;
        }
    }
    /* flush last run */
    fputc(count, fout);
    fputc(prev, fout);
    comp_size += 2;

    fclose(fin);
    fclose(fout);

    char s1[32], s2[32];
    fmt_bytes(orig_size, s1, sizeof s1);
    fmt_bytes(comp_size, s2, sizeof s2);
    double ratio = (orig_size > 0) ? (1.0 - (double)comp_size / orig_size) * 100.0 : 0;

    printf("  Original size   : " BCYAN "%s" RESET "\n", s1);
    printf("  Compressed size : " BCYAN "%s" RESET "\n", s2);
    if (ratio > 0)
        printf("  Compression     : " BGREEN "%.1f%% reduction" RESET "\n", ratio);
    else
        printf("  Compression     : " BYELLOW "%.1f%% (expanded)" RESET "\n", -ratio);
    printf(BGREEN "  Compressed file written to '%s'\n" RESET, outpath);
}

static void rle_decompress(const char *inpath, const char *outpath)
{
    FILE *fin = fopen(inpath, "rb");
    if (!fin) {
        printf(BRED "  Error: cannot open '%s' -- %s\n" RESET, inpath, strerror(errno));
        return;
    }
    FILE *fout = fopen(outpath, "wb");
    if (!fout) {
        fclose(fin);
        printf(BRED "  Error: cannot create '%s' -- %s\n" RESET, outpath, strerror(errno));
        return;
    }

    long out_size = 0;
    int cnt, val;
    while ((cnt = fgetc(fin)) != EOF) {
        val = fgetc(fin);
        if (val == EOF) break;
        for (int i = 0; i < cnt; i++) {
            fputc(val, fout);
            out_size++;
        }
    }
    fclose(fin);
    fclose(fout);

    char sb[32];
    fmt_bytes(out_size, sb, sizeof sb);
    printf("  Decompressed size : " BCYAN "%s" RESET "\n", sb);
    printf(BGREEN "  Decompressed file written to '%s'\n" RESET, outpath);
}

static void op_compression(void)
{
    print_subheader("File Compression (Run-Length Encoding)");
    int mode = get_int("1) Compress  2) Decompress : ", 1, 2);

    char inpath[MAX_PATH_LEN], outpath[MAX_PATH_LEN];
    get_str("Input file path : ", inpath, sizeof inpath);
    get_str("Output file path: ", outpath, sizeof outpath);

    printf("\n");
    if (mode == 1)
        rle_compress(inpath, outpath);
    else
        rle_decompress(inpath, outpath);
}

/* ============================================================
 *  5. File Sort
 * ============================================================ */

static int cmp_alpha(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

static int cmp_numeric(const void *a, const void *b)
{
    double da = atof(*(const char **)a);
    double db = atof(*(const char **)b);
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

static void op_file_sort(void)
{
    print_subheader("File Sort");
    char inpath[MAX_PATH_LEN], outpath[MAX_PATH_LEN];
    get_str("Input file path : ", inpath, sizeof inpath);
    get_str("Output file path: ", outpath, sizeof outpath);
    int mode = get_int("Sort mode  1) Alphabetical  2) Numeric : ", 1, 2);

    FILE *fp = fopen(inpath, "r");
    if (!fp) {
        printf(BRED "  Error: cannot open '%s' -- %s\n" RESET, inpath, strerror(errno));
        return;
    }

    int cap = 1024, count = 0;
    char **lines = malloc(cap * sizeof(char *));
    if (!lines) { fclose(fp); printf(BRED "  Memory error.\n" RESET); return; }

    char buf[MAX_LINE];
    while (fgets(buf, sizeof buf, fp)) {
        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(lines, cap * sizeof(char *));
            if (!tmp) break;
            lines = tmp;
        }
        /* strip trailing newline */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
        lines[count] = malloc(len + 1);
        if (lines[count]) {
            memcpy(lines[count], buf, len + 1);
            count++;
        }
    }
    fclose(fp);

    if (count == 0) {
        printf(BYELLOW "  File is empty, nothing to sort.\n" RESET);
        free(lines);
        return;
    }

    qsort(lines, count, sizeof(char *), (mode == 1) ? cmp_alpha : cmp_numeric);

    FILE *fout = fopen(outpath, "w");
    if (!fout) {
        printf(BRED "  Error: cannot create '%s' -- %s\n" RESET, outpath, strerror(errno));
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return;
    }
    for (int i = 0; i < count; i++)
        fprintf(fout, "%s\n", lines[i]);
    fclose(fout);

    printf(BGREEN "  Sorted %d lines -> '%s'\n" RESET, count, outpath);

    int show = (count < 20) ? count : 20;
    printf("\n  " BWHITE "--- First %d lines of sorted output ---" RESET "\n", show);
    for (int i = 0; i < show; i++)
        printf("  %4d | %s\n", i + 1, lines[i]);
    if (count > 20)
        printf("  " DIM "... (%d more lines)" RESET "\n", count - 20);

    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

/* ============================================================
 *  6. Checksum Calculator
 * ============================================================ */

static void op_checksum(void)
{
    print_subheader("Checksum Calculator");
    char filepath[MAX_PATH_LEN];
    get_str("File path: ", filepath, sizeof filepath);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        printf(BRED "  Error: cannot open '%s' -- %s\n" RESET, filepath, strerror(errno));
        return;
    }

    unsigned int sum_ck = 0;
    unsigned int xor_ck = 0;
    long total = 0;

    unsigned char buf[COPY_BLOCK];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) {
        for (size_t i = 0; i < n; i++) {
            sum_ck += buf[i];
            xor_ck ^= buf[i];
        }
        total += (long)n;
    }
    fclose(fp);

    char sb[32];
    fmt_bytes(total, sb, sizeof sb);

    printf("\n  " BWHITE "--- Checksum Results ---" RESET "\n");
    printf("  File        : " BCYAN "%s" RESET "\n", filepath);
    printf("  File size   : " BCYAN "%s" RESET " (%ld bytes)\n", sb, total);
    print_line();
    printf("  Additive    : " BYELLOW "0x%08X" RESET "  (decimal: %u)\n", sum_ck, sum_ck);
    printf("  XOR         : " BYELLOW "0x%08X" RESET "  (decimal: %u)\n", xor_ck, xor_ck);
}

/* ============================================================
 *  7. File Copy with Progress
 * ============================================================ */

static void op_file_copy(void)
{
    print_subheader("File Copy with Progress");
    char srcpath[MAX_PATH_LEN], dstpath[MAX_PATH_LEN];
    get_str("Source file path     : ", srcpath, sizeof srcpath);
    get_str("Destination file path: ", dstpath, sizeof dstpath);

    FILE *fin = fopen(srcpath, "rb");
    if (!fin) {
        printf(BRED "  Error: cannot open '%s' -- %s\n" RESET, srcpath, strerror(errno));
        return;
    }
    FILE *fout = fopen(dstpath, "wb");
    if (!fout) {
        fclose(fin);
        printf(BRED "  Error: cannot create '%s' -- %s\n" RESET, dstpath, strerror(errno));
        return;
    }

    long total = file_size(srcpath);
    if (total <= 0) {
        /* still try to copy, but progress won't be meaningful */
        total = 0;
    }

    printf("\n  Copying: " BCYAN "%s" RESET " -> " BCYAN "%s" RESET "\n", srcpath, dstpath);

    long done = 0;
    long t_start = ms_clock();
    unsigned char buf[COPY_BLOCK];
    size_t n;

    while ((n = fread(buf, 1, sizeof buf, fin)) > 0) {
        size_t w = fwrite(buf, 1, n, fout);
        if (w < n) {
            printf(BRED "\n  Error: write failed -- %s\n" RESET, strerror(errno));
            break;
        }
        done += (long)n;
        draw_progress("Copying", done, (total > 0) ? total : done);
    }
    long elapsed = ms_clock() - t_start;
    printf("\n");

    fclose(fin);
    fclose(fout);

    char sb[32];
    fmt_bytes(done, sb, sizeof sb);
    double secs = elapsed / 1000.0;
    double throughput = (secs > 0) ? (done / 1048576.0) / secs : 0;

    printf("\n" BGREEN "  Copy complete!" RESET "\n");
    printf("  Bytes copied : " BCYAN "%s" RESET "\n", sb);
    printf("  Time         : " BCYAN "%.2f s" RESET "\n", secs);
    printf("  Throughput   : " BCYAN "%.2f MB/s" RESET "\n", throughput);
}

/* ============================================================
 *  8. Batch File Rename
 * ============================================================ */

static void op_batch_rename(void)
{
    print_subheader("Batch File Rename");
    char dirpath[MAX_PATH_LEN], pattern[MAX_WORD_LEN], replacement[MAX_WORD_LEN];
    get_str("Directory path     : ", dirpath, sizeof dirpath);
    get_str("Search pattern     : ", pattern, sizeof pattern);
    get_str("Replacement string : ", replacement, sizeof replacement);

    if (strlen(pattern) == 0) {
        printf(BRED "  Error: search pattern cannot be empty.\n" RESET);
        return;
    }

    DIR *d = opendir(dirpath);
    if (!d) {
        printf(BRED "  Error: cannot open directory '%s' -- %s\n" RESET, dirpath, strerror(errno));
        return;
    }

    /* First pass -- collect matching entries */
    typedef struct { char oldname[MAX_PATH_LEN]; char newname[MAX_PATH_LEN]; } RenameEntry;
    int cap = 64, count = 0;
    RenameEntry *entries = malloc(cap * sizeof(RenameEntry));
    if (!entries) { closedir(d); printf(BRED "  Memory error.\n" RESET); return; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (!strstr(ent->d_name, pattern))
            continue;

        if (count >= cap) {
            cap *= 2;
            RenameEntry *tmp = realloc(entries, cap * sizeof(RenameEntry));
            if (!tmp) break;
            entries = tmp;
        }

        /* build new name by replacing first occurrence of pattern */
        char newname[MAX_PATH_LEN];
        char *pos = strstr(ent->d_name, pattern);
        size_t prefix_len = (size_t)(pos - ent->d_name);
        snprintf(newname, sizeof newname, "%.*s%s%s",
                 (int)prefix_len, ent->d_name,
                 replacement,
                 pos + strlen(pattern));

        snprintf(entries[count].oldname, sizeof(entries[count].oldname), "%.255s/%.255s", dirpath, ent->d_name);
        snprintf(entries[count].newname, sizeof(entries[count].newname), "%.255s/%.255s", dirpath, newname);
        count++;
    }
    closedir(d);

    if (count == 0) {
        printf(BYELLOW "  No files matching pattern '%s' found.\n" RESET, pattern);
        free(entries);
        return;
    }

    printf("\n  " BWHITE "Preview of renames (%d files):" RESET "\n", count);
    print_line();
    for (int i = 0; i < count; i++) {
        /* show just filenames, not full paths */
        const char *oldbase = strrchr(entries[i].oldname, '/');
        const char *newbase = strrchr(entries[i].newname, '/');
        oldbase = oldbase ? oldbase + 1 : entries[i].oldname;
        newbase = newbase ? newbase + 1 : entries[i].newname;
        printf("  " BCYAN "%s" RESET " -> " BGREEN "%s" RESET "\n", oldbase, newbase);
    }
    print_line();

    char confirm[8];
    get_str("Proceed? (y/n): ", confirm, sizeof confirm);
    if (confirm[0] != 'y' && confirm[0] != 'Y') {
        printf(BYELLOW "  Rename cancelled.\n" RESET);
        free(entries);
        return;
    }

    int success = 0, fail = 0;
    for (int i = 0; i < count; i++) {
        if (rename(entries[i].oldname, entries[i].newname) == 0) {
            success++;
        } else {
            printf(BRED "  Failed: %s -- %s\n" RESET, entries[i].oldname, strerror(errno));
            fail++;
        }
    }
    printf(BGREEN "\n  Renamed %d file(s) successfully." RESET, success);
    if (fail > 0)
        printf(BRED "  %d file(s) failed." RESET, fail);
    printf("\n");

    free(entries);
}

/* ============================================================
 *  Task Operations Menu
 * ============================================================ */

void task_ops_menu(void)
{
    int choice;
    do {
        print_header("TASK OPERATIONS (Real File Processing)");
        printf("  " BGREEN "1." RESET " File Search (Recursive Grep)\n");
        printf("  " BGREEN "2." RESET " Word Count & Frequency Analysis\n");
        printf("  " BGREEN "3." RESET " File Encryption/Decryption (XOR)\n");
        printf("  " BGREEN "4." RESET " File Compression (RLE)\n");
        printf("  " BGREEN "5." RESET " File Sort\n");
        printf("  " BGREEN "6." RESET " Checksum Calculator\n");
        printf("  " BGREEN "7." RESET " File Copy with Progress\n");
        printf("  " BGREEN "8." RESET " Batch File Rename\n");
        printf("  " BRED  "0." RESET " Back to Main Menu\n");
        print_line();

        choice = get_int("Select option [0-8]: ", 0, 8);

        switch (choice) {
        case 1: op_file_search();   break;
        case 2: op_word_count();    break;
        case 3: op_xor_cipher();    break;
        case 4: op_compression();   break;
        case 5: op_file_sort();     break;
        case 6: op_checksum();      break;
        case 7: op_file_copy();     break;
        case 8: op_batch_rename();  break;
        case 0: break;
        }

        if (choice != 0) wait_enter();
    } while (choice != 0);
}
