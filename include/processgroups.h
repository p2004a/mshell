#ifndef _PROCESSGROUPS_H_
#define _PROCESSGROUPS_H_

#include <sys/types.h>
#include <stddef.h>

/* Initializes the procesgroups module, should be run once, return -1 on fail */
int pg_init();

/* Cleans state after end of procesgroups usage */
void pg_clean();

/* Creates new group and return it's id or -1 if error */
int pg_new(void (*f)(int));

/* Deletes a group, if group doesn't exists it doesn't to anything */
void pg_del(int pgn);

/* Adds a process to group returns -1 on error.
   If its first process added to the group it sets it as the leader of the
   group. Before that the pid of process group is 0.
   This function assumes that proces is running. */
int pg_add_process(int pgn, pid_t pid, void (*f)(pid_t, int));

/* Gets the proces group pid or (pid_t)-1 on error */
pid_t pg_pid(int pgn);

/* Checks if selected group is running, makes sense only after pg_block_sigchld */
int pg_running(int pgn);

/* Wait until a specific group stop */
void pg_wait(int pgn);

/* send signal to all processes in group */
void pg_kill(int pgn, int signal);

/* Set procesgroup to foreground. If pgn == 0 it means to set the current
process to foreground.  */
int pg_foreground(int pgn);

/* Blocks SIGCHLD, calls to this function can be nested, if you call it twice, to
   stop blocking SIGCHLD you need to call pg_unblock_sigchld twice. When SIGCHLD
   is not blocked pg_unblock_sigchld does nothig. */
void pg_block_sigchld();
void pg_unblock_sigchld();

/* BLocks execution until SIGCHLD is received. Doesn't blog other signals while
   waiting for SIGCHLD. */
void pg_wait_for_sigchld();

#endif /* !_PROCESSGROUPS_H_ */
