#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "linereader.h"
#include "config.h"
#include "utils.h"

int
lr_init(struct linereader * lr) {
	int result;
	struct stat fd_stat;

	result = fstat(STDIN_FILENO, &fd_stat);
	if (result == -1) {
		goto error;
	}
	lr->print_prompt = S_ISCHR(fd_stat.st_mode);

	lr->buffor = malloc(MAX_LINE_LENGTH + 1);
	if (!lr->buffor) {
		goto error;
	}
	lr->offset = 0;
	lr->last_line_length = -1;

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
lr_readline(struct linereader * lr, char const** res) {
	int line_end, finished_line, ignore_command, read_bytes;

	finished_line = 1;
	ignore_command = 0;
	read_bytes = 1;

	if (lr->last_line_length != -1) {
		lr->offset -= lr->last_line_length + 1;
		memmove(lr->buffor, lr->buffor + lr->last_line_length + 1, lr->offset);
		lr->last_line_length = -1;
	}

	while (read_bytes > 0 || lr->offset > 0) {
		if (lr->print_prompt && finished_line) {
			finished_line = 0;
			printf(PROMPT_STR);
			fflush(stdout);
		}

		line_end = find_newline(lr->buffor, lr->offset);
		if (line_end == -1 && lr->offset == MAX_LINE_LENGTH + 1) { /* line too long */
			lr->offset = 0;
			ignore_command = 1;
		}

		if (line_end != -1 || read_bytes == 0) {
			if (line_end == -1) {
				line_end = lr->offset;
			}
			lr->buffor[line_end] = '\0';

			if (!ignore_command) {
				lr->last_line_length = line_end;

				*res = lr->buffor;
				return 0;
			}

			fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
			fflush(stderr);
			ignore_command = 0;
			finished_line = 1;
			lr->offset -= line_end + 1;
			memmove(lr->buffor, lr->buffor + line_end + 1, lr->offset);
		} else {
			EINTR_RETRY(read_bytes, read(STDIN_FILENO, lr->buffor + lr->offset, MAX_LINE_LENGTH + 1 - lr->offset));
			if (read_bytes == -1) {
				goto error;
			}

			lr->offset += read_bytes;
		}
	}

	*res = NULL;
	return 0;
error:
	return -1;
}

void
lr_clean(struct linereader * lr) {
	free(lr->buffor);
}
