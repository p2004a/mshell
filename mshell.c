#define _POSIX_C_SOURCE 1
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"
#include "linereader.h"

int
redirect(const char *filename, int flags, int to_fd) {
	int fd, fd_dup;

	fd = open(filename, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd == -1) {
		fprintf(stderr, "%s: %s\n", filename, strerror(errno));
		goto error;
	}
	fd_dup = dup2(fd, to_fd);
	if (fd_dup != to_fd) {
		fprintf(stderr, "dup2 failed to copy %d to %d\n", fd, to_fd);
		goto error;
	}
	if (close(fd) == -1) {
		fprintf(stderr, "closing %d failed: %s\n", fd, strerror(errno));
		goto error;
	}
	return 0;
error:
	return -1;
}

int
exec_command(command * com) {
	int return_status, status;
	pid_t child_pid;

	char *input_filename, *output_filename;
	int output_additional_flags;
	redirection ** redir;

	child_pid = fork();
	if (child_pid == -1) {
		goto error;
	}
	if (child_pid) { /* parent */
		do {
			status = waitpid(child_pid, &return_status, 0);
		} while (status == -1 && errno == EINTR);
		if (status == -1) {
			goto error;
		}
		return return_status;
	} else { /* child */
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

		execvp(com->argv[0], com->argv);
		fprintf(stderr, "%s: %s\n", com->argv[0], strerror(errno));
		goto child_error;
	}

error:
	return -1;
child_error:
	exit(EXEC_FAILURE);
}

int exec_pipeline(pipeline pl) {
	command *com;
	int i, return_status, argc, pl_len;
	builtin_func builtin;
	pipeline tmp_pl;

	pl_len = 0;
	for (tmp_pl = pl; *tmp_pl != NULL; ++tmp_pl) {
		++pl_len;
	}

	for (i = 0; i < pl_len; ++i) {
		com = pl[i];

		if (com->argv[0] == NULL) {
			continue;
		}

		builtin = get_builtin(com->argv[0]);
		if (builtin && pl_len > 1) {
			for (argc = 0; com->argv[argc]; ++argc);
			if (builtin(argc, com->argv) == BUILTIN_ERROR) {
				fprintf(stderr, "Builtin %s error.\n", com->argv[0]);
				fflush(stderr);
			}
		} else {
			return_status = exec_command(com);
			if (return_status == -1) {
				goto error;
			}
			if (WIFEXITED(return_status)) {
				return_status = WEXITSTATUS(return_status);
				if (return_status != 0 && return_status != EXEC_FAILURE) {
					printf("Program returned status %d\n", return_status);
					fflush(stdout);
				}
			} else if (WIFSIGNALED(return_status)) {
				printf("Program killed by signal %d\n", WTERMSIG(return_status));
				fflush(stdout);
			}
		}
	}

	return 0;
error:
	return -1;
}

int
check_line(line * ln) {
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

int
exec_command_line(const char * buffor) {
	line * ln;
	pipeline * cl;

	ln = parseline(buffor);
	if (!check_line(ln)) {
		fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
		fflush(stderr);
		return 0;
	}

	for (cl = ln->pipelines; *cl != NULL; ++cl) {
		if (exec_pipeline(*cl) == -1) {
			goto error;
		}
	}

	return 0;
error:
	return -1;
}

int
main(int argc, char * argv[]) {
	const char * line;
	int result;
	struct linereader lr;

	result = lr_init(&lr);
	if (result == -1) {
		goto error;
	}

	do {
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

	lr_clean(&lr);
	return 0;

error:
	lr_clean(&lr);
	perror("main: ");
	exit(1);
}
