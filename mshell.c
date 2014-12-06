#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"

int
exec_command(char * file, char *const argv[]) {
	int return_status, status;
	pid_t child_pid;

	child_pid = fork();
	if (child_pid == -1) {
		goto error;
	}
	if (child_pid) { // parent
		int return_status;
		do {
			status = waitpid(child_pid, &return_status, 0);
		} while (status == -1 && errno == EINTR);
		if (status == -1) {
			goto error;
		}
		return return_status;
	} else { // child
		execvp(file, argv);
		fprintf(stderr, "%s: %s\n", file, strerror(errno));
		exit(EXEC_FAILURE);
	}

error:
	return -1;
}

int
parse_command_line(char * buffor) {
	line * ln;
	command * com;
	int return_status;
	builtin_pair * builtin;
	int called_builtin;
	int argc;

	ln = parseline(buffor);
	if (ln == NULL) {
		fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
		fflush(stderr);
		return 0;
	}

	com = pickfirstcommand(ln);
	if (com->argv[0] == NULL) {
		return 0;
	}

	called_builtin = 0;
	for (builtin = builtins_table; !called_builtin && builtin->name != NULL; ++builtin) {
		if (strcmp(builtin->name, com->argv[0]) == 0) {
			called_builtin = 1;
			for (argc = 0; com->argv[argc]; ++argc);
			if (builtin->fun(argc, com->argv) == BUILTIN_ERROR) {
				fprintf(stderr, "Builtin %s error.\n", com->argv[0]);
				fflush(stderr);
			}
		}
	}

	if (!called_builtin) {
		return_status = exec_command(com->argv[0], com->argv);
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

	return 0;
error:
	return -1;
}

int
find_newline(char * buf, ssize_t size) {
	int i;
	for (i = 0; i < size; ++i) {
		if (buf[i] == '\n') {
			return i;
		}
	}
	return -1;
}

int
main(int argc, char * argv[]) {
	char * buffor;
	ssize_t read_bytes;
	struct stat stdin_stat;
	int print_prompt = 1;
	int result;
	int offset;
	int line_end;
	int execed_command;
	int ignore_command;

	result = fstat(STDIN_FILENO, &stdin_stat);
	if (result == -1) {
		goto error0;
	}
	print_prompt = S_ISCHR(stdin_stat.st_mode);

	buffor = malloc(MAX_LINE_LENGTH + 1);
	if (!buffor) {
		goto error0;
	}
	offset = 0;

	read_bytes = 1;
	execed_command = 1;
	ignore_command = 0;
	while (read_bytes > 0 || offset > 0) {
		if (print_prompt && execed_command) {
			execed_command = 0;
			printf(PROMPT_STR);
			fflush(stdout);
		}

		line_end = find_newline(buffor, offset);

		if (line_end == -1 && offset == MAX_LINE_LENGTH + 1) { // line too long
			offset = 0;
			ignore_command = 1;
		}

		if (line_end != -1 || read_bytes == 0) {
			if (line_end == -1) {
				line_end = offset;
			}
			buffor[line_end] = '\0';

			if (ignore_command) {
				fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
				fflush(stderr);
				ignore_command = 0;
			} else {
				result = parse_command_line(buffor);
				if (result == -1) {
					goto error1;
				}
			}
			execed_command = 1;

			offset -= line_end + 1;
			memmove(buffor, buffor + line_end + 1, offset);
		} else {
			do {
				read_bytes = read(STDIN_FILENO, buffor + offset, MAX_LINE_LENGTH + 1 - offset);
			} while (read_bytes == -1 && errno == EINTR);
			if (read_bytes == -1) {
				goto error1;
			}

			offset += read_bytes;
		}
	}

	free(buffor);
	exit(0);
error1:
	free(buffor);
error0:
	perror("main: ");
	exit(1);
}
