/* ================================================================
 *  TaskForge Banking Application -- Header
 *  A full banking system built on top of the TaskForge OS kernel.
 *  Every operation goes through the OS via system calls.
 * ================================================================ */
#ifndef BANK_H
#define BANK_H

#include "../kernel/os_syscall.h"

#define MAX_ACCOUNTS    20
#define MAX_TRANSACTIONS 100

typedef struct {
    int     id;             /* account number (1-based) */
    char    holder[OS_MAX_NAME];
    double  balance;
    int     active;
    int     resource_id;    /* OS resource ID for deadlock management */
    int     file_id;        /* OS filesystem node ID */
} Account;

typedef struct {
    int     id;
    int     from_acc;       /* 0 if deposit/withdrawal */
    int     to_acc;
    double  amount;
    char    type[16];       /* "DEPOSIT", "WITHDRAW", "TRANSFER" */
    time_t  timestamp;
    int     pid;            /* OS process that handled this */
    int     mem_addr;       /* Memory allocated for this tx */
} Transaction;

typedef struct {
    Account     accounts[MAX_ACCOUNTS];
    int         acc_count;
    Transaction txlog[MAX_TRANSACTIONS];
    int         tx_count;
    int         accounts_dir;   /* filesystem dir ID for /accounts */
    int         logs_dir;       /* filesystem dir ID for /logs */
} BankState;

/* Initialize banking system (creates dirs in OS filesystem) */
void bank_init(BankState *bs);

/* Banking operations (all use syscalls internally) */
int  bank_create_account(BankState *bs, const char *holder, double initial);
int  bank_deposit(BankState *bs, int acc_id, double amount);
int  bank_withdraw(BankState *bs, int acc_id, double amount);
int  bank_transfer(BankState *bs, int from_id, int to_id, double amount);
double bank_get_balance(BankState *bs, int acc_id);
void bank_print_statement(BankState *bs, int acc_id);
void bank_print_all_accounts(BankState *bs);
void bank_print_tx_log(BankState *bs);

/* Interactive menu */
void bank_menu(BankState *bs);

#endif /* BANK_H */
