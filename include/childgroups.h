#ifndef _CHILDGROUPS_H_
#define _CHILDGROUPS_H_

#include <sys/types.h>

int cg_init();
void cg_clean();
int cg_new();
void cg_del(int cgn);
int cg_add_child(int cgn, pid_t pid);
int cg_running(int cgn);
void cg_wait(int cgn);

void cg_block_sigchld();
void cg_unblock_sigchld();
void cg_wait_for_sigchld();

#endif /* !_CHILDGROUPS_H_ */
