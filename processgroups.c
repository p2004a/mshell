#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "utils.h"
#include "processgroups.h"

typedef struct process {
	pid_t pid;
	int pgn;
	int running;
	int return_status;
	void (*callback)(pid_t, int);
} process;

typedef struct group
{
	int pgn;
	pid_t pid;
	int num_processes;
	int running;
	void (*callback)(int);
} group;

static group * groups = NULL;
static int groups_size = 0;
static int groups_cap = 0;

static process * processes = NULL;
static int processes_size = 0;
static int processes_cap = 0;

static int pg_num = 0;                  /* for generating next number of group */
static int sigchld_blocked_counter = 0; /* counter for nested sigchld block/unblock */
static int old_sa_set = 0;              /* for pg_clean */
static struct sigaction old_sa;         /* old sigaction before pg_init */
static sigset_t old_sigset;             /* old set before pg_block_sigchld */
static volatile int got_sigchld;        /* for pg_wait_for_sigchld to distinct between signals */

group * _pg_get_group(int pgn) {
	int i;
	for (i = 0; i < groups_size; ++i) {
		if (groups[i].pgn == pgn) {
			return groups + i;
		}
	}
	return NULL;
}

void pg_wait_for_sigchld() {
	pg_block_sigchld();

	got_sigchld = 0;
	while (!got_sigchld) {
		sigsuspend(&old_sigset);
	}

	pg_unblock_sigchld();
}

void pg_block_sigchld() {
	sigset_t sigset;

	if (!sigchld_blocked_counter) {
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		sigprocmask(SIG_BLOCK, &sigset, &old_sigset);
	}
	++sigchld_blocked_counter;
}

void pg_unblock_sigchld() {
	sigset_t sigset;

	if (sigchld_blocked_counter == 0) {
		return;
	}

	--sigchld_blocked_counter;
	if (!sigchld_blocked_counter) {
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		if (!sigismember(&old_sigset, SIGCHLD)) {
			sigprocmask(SIG_UNBLOCK, &sigset, &old_sigset);
		}
	}
}

void sigchld_handler(int signo, siginfo_t * info, void * context) {
	pid_t child_pid;
	int i, return_status;
	group * g;

	got_sigchld = 1;

	do {
		EINTR_RETRY(child_pid, waitpid((pid_t)(-1), &return_status, WNOHANG));

		for (i = 0; child_pid > 0 && i < processes_size; ++i) {
			if (processes[i].pid == child_pid) {
				g = _pg_get_group(processes[i].pgn);
				assert(g != NULL);
				g->running -= 1;
				processes[i].running = 0;
				processes[i].return_status = return_status;
				if (processes[i].callback != NULL) {
					(processes[i].callback)(child_pid, return_status);
				}
				if (g->running == 0 && g->callback != NULL) {
					(g->callback)(processes[i].pgn);
				}
				break;
			}
		}
	} while (child_pid > 0);
}

int pg_init() {
	struct sigaction sa;

	pg_block_sigchld();

	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGTTOU, &sa, NULL) == -1) {
		goto error;
	}

	sa.sa_sigaction = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sa.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;

	old_sa_set = 0;
	if (sigaction(SIGCHLD, &sa, &old_sa) == -1) {
		goto error;
	}
	old_sa_set = 1;

	processes_cap = 30;
	processes_size = 0;
	pg_num = 1;
	processes = (process *) malloc(sizeof(process) * processes_cap);
	if (processes == NULL) {
		goto error;
	}

	groups_cap = 10;
	groups_size = 0;
	pg_num = 1;
	groups = (group *) malloc(sizeof(group) * groups_cap);
	if (groups == NULL) {
		goto error;
	}

	pg_unblock_sigchld();
	return 0;
error:
	pg_clean();
	pg_unblock_sigchld();
	return -1;
}

void pg_clean() {
	if (old_sa_set) {
		sigaction(SIGCHLD, &old_sa, NULL);
		old_sa_set = 0;
	}
	if (sigchld_blocked_counter > 0) {
		sigchld_blocked_counter = 1;
		pg_unblock_sigchld();
	}
	free(processes);
	free(groups);
	processes = NULL;
	groups = NULL;
}

int pg_new(void (*f)(int)) {
	pg_block_sigchld();

	if (groups_size == groups_cap) {
		groups_cap += groups_cap / 2;
		groups = realloc(groups, sizeof(group) * groups_cap);
		if (groups == NULL) {
			goto error;
		}
	}

	groups[groups_size].pgn = pg_num;
	groups[groups_size].num_processes = 0;
	groups[groups_size].running = 0;
	groups[groups_size].pid = 0;
	groups[groups_size].callback = f;
	++groups_size;

	pg_unblock_sigchld();
	return pg_num++;
error:
	pg_unblock_sigchld();
	return -1;
}

void pg_del(int pgn) {
	int i;
	group * g;

	pg_block_sigchld();

	g = _pg_get_group(pgn);
	if (g != NULL) {
		--groups_size;
		*g = groups[groups_size];
	}

	for (i = processes_size; i >= 0; --i) {
		if (processes[i].pgn == pgn) {
			--processes_size;
			processes[i] = processes[processes_size];
		}
	}
	pg_unblock_sigchld();
}

int pg_add_process(int pgn, pid_t pid, void (*f)(pid_t, int)) {
	group * g;

	pg_block_sigchld();

	g = _pg_get_group(pgn);
	if (g == NULL) {
		goto error;
	}
	if (g->pid == 0) {
		g->pid = pid;
	}
	g->num_processes += 1;
	g->running += 1;

#if _POSIX_VERSION >= 200809L
	setpgid(pid, g->pid); /* ignore errors */
#endif

	if (processes_size == processes_cap) {
		processes_cap += processes_cap / 2;
		processes = realloc(processes, sizeof(process) * processes_cap);
		if (processes == NULL) {
			goto error;
		}
	}

	processes[processes_size].pgn = pgn;
	processes[processes_size].pid = pid;
	processes[processes_size].running = 1;
	processes[processes_size].return_status = 0;
	processes[processes_size].callback = f;
	++processes_size;

	pg_unblock_sigchld();
	return 0;
error:
	pg_unblock_sigchld();
	return -1;
}

pid_t pg_pid(int pgn) {
	group *g = _pg_get_group(pgn);
	if (g != NULL) {
		return g->pid;
	}
	return -1;
}

int pg_running(int pgn) {
	group * g = _pg_get_group(pgn);
	return g != NULL && g->running;
}

void pg_wait(int pgn) {
	pg_block_sigchld();
	while (pg_running(pgn)) {
		pg_wait_for_sigchld();
	}
	pg_unblock_sigchld();
}

#if _POSIX_VERSION >= 200809L
int pg_foreground(int pgn) {
	group * g;

	if (pgn != 0) {
		g = _pg_get_group(pgn);
		if (g == NULL) {
			goto error;
		}

		tcsetpgrp(STDIN_FILENO, g->pid);
	} else {
		tcsetpgrp(STDIN_FILENO, getpgid(0));
	}

	return 0;
error:
	return -1;
}
#endif
