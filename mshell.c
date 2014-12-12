#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"
#include "linereader.h"
#include "processgroups.h"

typedef struct child {
	pid_t pid;
	int return_status;
} child;

child *dead_children = NULL;
int dead_children_size = 0;
int dead_children_capacity = 0;

void dead_child(pid_t pid, int return_status) {
	pg_block_sigchld();

	if (dead_children_size == dead_children_capacity) {
		dead_children_capacity += dead_children_capacity / 2;
		dead_children = (child *) realloc(dead_children, sizeof(child) * dead_children_capacity);
		assert(dead_children != NULL);
	}
	dead_children[dead_children_size].pid = pid;
	dead_children[dead_children_size].return_status = return_status;
	++dead_children_size;

	pg_unblock_sigchld();
}

int redirect(const char * filename, int flags, int to_fd) {
	int fd, fd_dup, result;

	EINTR_RETRY(fd, open(filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
	if (fd == -1) {
		fprintf(stderr, "%s: %s\n", filename, strerror(errno));
		goto error;
	}
	EINTR_RETRY(fd_dup, dup2(fd, to_fd));
	if (fd_dup != to_fd) {
		fprintf(stderr, "dup2 failed to copy %d to %d\n", fd, to_fd);
		goto error;
	}
	EINTR_RETRY(result, close(fd));
	if (result == -1) {
		fprintf(stderr, "closing %d failed: %s\n", fd, strerror(errno));
		goto error;
	}
	return 0;
error:
	return -1;
}

int exec_command(command * com, int in_fd, int out_fd, int pg_pid) {
	pid_t child_pid;
	char *input_filename, *output_filename;
	int output_additional_flags;
	redirection ** redir;
	int ret_fd;

	child_pid = fork();
	if (child_pid == -1) {
		goto error;
	}
	if (child_pid) { /* parent */
		return child_pid;
	} else { /* child */
#if _POSIX_VERSION >= 200809L
		if (setpgid(0, pg_pid) == -1) {
			fprintf(stderr, "cannot set pgid to %d: %s\n", pg_pid, strerror(errno));
			goto error;
		}
#else
		if (setsid() == -1) {
			fprintf(stderr, "setsid failed: %s\n", strerror(errno));
			goto error;
		}
#endif

		if (in_fd != STDIN_FILENO) {
			EINTR_RETRY(ret_fd, dup2(in_fd, STDIN_FILENO));
			if (ret_fd != STDIN_FILENO) {
				fprintf(stderr, "dup2 failed to copy %d to %d\n", in_fd, STDIN_FILENO);
				goto error;
			}
		}

		if (out_fd != STDOUT_FILENO) {
			EINTR_RETRY(ret_fd, dup2(out_fd, STDOUT_FILENO));
			if (ret_fd != STDOUT_FILENO) {
				fprintf(stderr, "dup2 failed to copy %d to %d\n", out_fd, STDOUT_FILENO);
				goto error;
			}
		}

		input_filename = NULL;
		output_filename = NULL;

		for (redir = com->redirs; *redir != NULL; ++redir) {
			if (IS_RIN((*redir)->flags)) {
				input_filename = (*redir)->filename;
			} else if (IS_ROUT((*redir)->flags)) {
				output_filename = (*redir)->filename;
				output_additional_flags = O_TRUNC;
			} else if (IS_RAPPEND((*redir)->flags)) {
				output_filename = (*redir)->filename;
				output_additional_flags = O_APPEND;
			}
		}

		if (input_filename && redirect(input_filename, O_RDONLY, STDIN_FILENO) == -1) {
			goto child_error;
		}

		if (output_filename && redirect(output_filename, O_WRONLY | O_CREAT | output_additional_flags, STDOUT_FILENO) == -1) {
			goto child_error;
		}

		if (fcntl(STDIN_FILENO, F_SETFD, 0) == -1
		  || fcntl(STDOUT_FILENO, F_SETFD, 0) == -1) {
			fprintf(stderr, "cannot remove FD_CLOEXEC flag from file descriptors\n");
			goto child_error;
		}

		execvp(com->argv[0], com->argv);
		fprintf(stderr, "%s: %s\n", com->argv[0], strerror(errno));
		goto child_error;
	}

error:
	return -1;
child_error:
	exit(EXEC_FAILURE);
}

void swap_ptr(void ** a, void ** b) {
	void *tmp_ptr = *a;
	*a = *b;
	*b = tmp_ptr;
}

int close_pipe(int p[2]) {
	int result;

	if (p[0] != STDIN_FILENO) {
		EINTR_RETRY(result, close(p[0]));
		if (result == -1) {
			goto error;
		}
		p[0] = STDIN_FILENO;
		EINTR_RETRY(result, close(p[1]));
		if (result == -1) {
			goto error;
		}
		p[1] = STDOUT_FILENO;
	}
	return 0;
error:
	return -1;
}

int exec_pipeline(pipeline pl, int background) {
	command *com;
	int i, argc, pl_len, pgn;
	builtin_func builtin;
	pipeline tmp_pl;
	pid_t child_pid;
	int p_tab[4];
	int * p1, * p2;

	p1 = p_tab;
	p2 = p_tab + 2;
	p1[0] = p2[0] = STDIN_FILENO;
	p1[1] = p2[1] = STDOUT_FILENO;

	pl_len = 0;
	for (tmp_pl = pl; *tmp_pl != NULL; ++tmp_pl) {
		++pl_len;
	}

	pgn = pg_new(NULL);
	if (pgn == -1) {
		goto error;
	}

	pg_block_sigchld();
	for (i = 0; i < pl_len; ++i) {
		swap_ptr((void **) &p1, (void **) &p2);
		if (close_pipe(p2) == -1) {
			goto error;
		}
		if (i < pl_len - 1) {
			if (pipe(p2) == -1
			  || fcntl(p2[0], F_SETFD, FD_CLOEXEC) == -1
			  || fcntl(p2[1], F_SETFD, FD_CLOEXEC) == -1) {
				goto error;
			}
		}

		com = pl[i];

		if (com->argv[0] == NULL) {
			continue;
		}

		builtin = get_builtin(com->argv[0]);
		if (builtin && pl_len < 2) {
			for (argc = 0; com->argv[argc]; ++argc);
			if (builtin(argc, com->argv) == BUILTIN_ERROR) {
				fprintf(stderr, "Builtin %s error.\n", com->argv[0]);
				fflush(stderr);
			}
		} else {
			child_pid = exec_command(com, p1[0], p2[1], pg_pid(pgn));
			if (child_pid == -1) {
				goto error;
			}
			if (pg_add_process(pgn, child_pid, background ? dead_child : NULL) == -1) {
				goto error;
			}
		}
	}
	pg_unblock_sigchld();

	if (close_pipe(p2) == -1
	  || close_pipe(p1) == -1) {
		goto error;
	}

	if (!background) {
#if _POSIX_VERSION >= 200809L
		pg_foreground(pgn);
#endif
		pg_wait(pgn);
		pg_del(pgn);
#if _POSIX_VERSION >= 200809L
		pg_foreground(0);
#endif
	}

	return 0;
error:
	pg_unblock_sigchld();
	return -1;
}

int check_line(line * ln) {
	pipeline * cl;
	pipeline pl;
	command * com;
	int pl_len, was_null;

	if (ln == NULL) {
		return 0;
	}

	for (cl = ln->pipelines; *cl != NULL; ++cl) {
		pl_len = 0;
		was_null = 0;
		for (pl = *cl; *pl != NULL; ++pl) {
			++pl_len;
			com = *pl;
			if (com->argv[0] == NULL) {
				was_null = 1;
			}
		}
		if (pl_len > 1 && was_null) {
			return 0;
		}
	}
	return 1;
}

int exec_command_line(const char * buffor) {
	line * ln;
	pipeline * cl;

	ln = parseline(buffor);
	if (!check_line(ln)) {
		fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
		fflush(stderr);
		return 0;
	}

	for (cl = ln->pipelines; *cl != NULL; ++cl) {
		if (exec_pipeline(*cl, (ln->flags & LINBACKGROUND) && *(cl + 1) == NULL) == -1) {
			goto error;
		}
	}

	return 0;
error:
	return -1;
}

void print_dead_childred() {
	int i, rs;

	pg_block_sigchld();

	for (i = 0; i < dead_children_size; ++i) {
		rs = dead_children[i].return_status;

		printf("Background process %d terminated. ", dead_children[i].pid);

		if (WIFEXITED(rs)) {
			printf("(exited with status %d)\n", WEXITSTATUS(rs));
		} else if (WIFSIGNALED(rs)) {
			printf("(killed by signal %d)\n", WTERMSIG(rs));
		}
	}

	dead_children_size = 0;

	pg_unblock_sigchld();
}

int main(int argc, char * argv[]) {
	const char * line;
	int result;
	struct linereader lr;

	dead_children_capacity = 50;
	dead_children = (child *) malloc(sizeof(child) * dead_children_capacity);
	if (dead_children == NULL) {
		goto error;
	}

	result = lr_init(&lr);
	if (result == -1) {
		goto error;
	}

	result = pg_init();
	if (result == -1) {
		goto error;
	}

	do {
		if (lr.print_prompt) {
			print_dead_childred();
		}
		result = lr_readline(&lr, &line);
		if (result == -1) {
			goto error;
		}
		if (line != NULL) {
			result = exec_command_line(line);
			if (result == -1) {
				goto error;
			}
		}
	} while (line != NULL);

	pg_clean();
	lr_clean(&lr);
	return 0;

error:
	free(dead_children);
	pg_clean();
	lr_clean(&lr);
	perror("main: ");
	exit(1);
}
