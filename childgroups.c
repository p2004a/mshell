#define _POSIX_C_SOURCE 200112L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "utils.h"
#include "childgroups.h"

typedef struct child {
	pid_t pid;
	int group;
	int running;
	int return_status;
} child;

static child * children = NULL;
static int children_size = 0;
static int children_cap = 0;
static int cg_num = 0;

static int sigchld_blocked = 0;
static sigset_t old_sigset;
static int got_sigchld;

void cg_wait_for_sigchld() {
	cg_block_sigchld();

	got_sigchld = 0;
	while (!got_sigchld) {
		sigsuspend(&old_sigset);
	}

	cg_unblock_sigchld();
}

void cg_block_sigchld() {
	sigset_t sigset;

	if (!sigchld_blocked) {
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		sigprocmask(SIG_BLOCK, &sigset, &old_sigset);
	}
	++sigchld_blocked;
}

void cg_unblock_sigchld() {
	sigset_t sigset;

	if (sigchld_blocked == 0) {
		return;
	}

	--sigchld_blocked;
	if (!sigchld_blocked) {
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		if (!sigismember(&old_sigset, SIGCHLD)) {
			sigprocmask(SIG_UNBLOCK, &sigset, &old_sigset);
		}
	}
}

void sigchld_handler(int signo, siginfo_t * info, void * context) {
	pid_t child;
	int i, return_status;

	got_sigchld = 1;

	do {
		EINTR_RETRY(child, waitpid((pid_t)(-1), &return_status, WNOHANG));
		for (i = 0; child > 0 && i < children_size; ++i) {
			if (children[i].pid == child) {
				children[i].running = 0;
				children[i].return_status = return_status;
				break;
			}
		}
	} while (child > 0);
}

int cg_init() {
	struct sigaction sa;

	cg_block_sigchld();

	sa.sa_sigaction = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sa.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;

	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		goto error;
	}

	children_cap = 20;
	children_size = 0;
	cg_num = 1;
	children = (child *) malloc(sizeof(child) * children_cap);
	if (children == NULL) {
		goto error;
	}

	cg_unblock_sigchld();
	return 0;
error:
	cg_unblock_sigchld();
	return -1;
}

void cg_clean() {
	free(children);
}

int cg_new() {
	return cg_num++;
}

void cg_del(int cgn) {
	int i;

	cg_block_sigchld();

	for (i = children_size; i >= 0; --i) {
		if (children[i].group == cgn) {
			--children_size;
			children[i] = children[children_size];
		}
	}
	cg_unblock_sigchld();
}

int cg_add_child(int cgn, pid_t pid) {
	cg_block_sigchld();
	if (children_size == children_cap) {
		children_cap += children_cap / 2;
		children = realloc(children, sizeof(child) * children_cap);
		if (children == NULL) {
			goto error;
		}
	}

	children[children_size].group = cgn;
	children[children_size].pid = pid;
	children[children_size].running = 1;
	children[children_size].return_status = 0;
	++children_size;

	cg_unblock_sigchld();
	return 0;
error:
	cg_unblock_sigchld();
	return -1;
}

int cg_running(int cgn) {
	int i;
	for (i = 0; i < children_size; ++i) {
		if (children[i].group == cgn && children[i].running) {
			return 1;
		}
	}
	return 0;
}

void cg_wait(int cgn) {
	cg_block_sigchld();
	while (cg_running(cgn)) {
		cg_wait_for_sigchld();
	}
	cg_unblock_sigchld();
}
