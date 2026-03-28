#ifndef PROCESS_MGMT_H
#define PROCESS_MGMT_H

#include "common.h"
#include <pthread.h>
#include <semaphore.h>

/*  Public entry point for the Process Management module.
 *  Displays the sub-menu and drives all demos.                       */
void process_mgmt_menu(void);

#endif /* PROCESS_MGMT_H */
