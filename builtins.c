#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>

#include "builtins.h"
#include "utils.h"

int builtin_echo(int, char * argv[]);
int builtin_undefined(int, char * argv[]);
int builtin_exit(int, char * argv[]);
int builtin_cd(int argc, char * argv[]);
int builtin_lcd(int argc, char * argv[]);
int builtin_lkill(int argc, char * argv[]);
int builtin_lls(int argc, char * argv[]);

builtin_pair builtins_table[]={
	{"exit",	&builtin_exit},
	{"lecho",	&builtin_echo},
	{"cd",		&builtin_cd},
	{"lcd",		&builtin_lcd},
	{"lkill",	&builtin_lkill},
	{"lls",		&builtin_lls},
	{NULL,NULL}
};

builtin_func get_builtin(const char * name) {
	builtin_pair * builtin;
	for (builtin = builtins_table; builtin->name != NULL; ++builtin) {
		if (strcmp(builtin->name, name) == 0) {
			return builtin->func;
		}
	}
	return NULL;
}

int builtin_undefined(int argc, char * argv[]) {
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	return BUILTIN_ERROR;
}

int builtin_exit(int argc, char * argv[]) {
	if (argc != 1) {
		return BUILTIN_ERROR;
	}
	exit(0);
}

int builtin_echo(int argc, char * argv[]) {
	int i = 1;
	if (argv[i]) printf("%s", argv[i++]);
	while  (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int builtin_cd(int argc, char * argv[]) {
	char *path;
	int res;

	if (argc == 1) {
		path = getenv("HOME");
	} else if (argc == 2) {
		path = argv[1];
	} else {
		fprintf(stderr, "Wrong number of arguments (%d) to cd\n", argc);
		return BUILTIN_ERROR;
	}

	if (path == NULL) {
		fprintf(stderr, "Couldn't find path to go to\n");
		return BUILTIN_ERROR;
	}

	res = chdir(path);
	if (res == -1) {
		fprintf(stderr, "cd to '%s' failed: %s\n", path, strerror(errno));
		return BUILTIN_ERROR;
	}
	return 0;
}


int builtin_lcd(int argc, char * argv[]) {
	char *path;
	int res;

	if (argc == 1) {
		path = getenv("HOME");
	} else if (argc == 2) {
		path = argv[1];
	} else {
		return BUILTIN_ERROR;
	}

	if (path == NULL) {
		return BUILTIN_ERROR;
	}

	res = chdir(path);
	if (res == -1) {
		return BUILTIN_ERROR;
	}

	return 0;
}

int get_int(char * str) {
	char * end;
	long int res;
	size_t len;

	len = strlen(str);
	errno = 0;
	res = strtol(str, &end, 10);
	if (errno || end != str + len || res < 0 || res > (1 << 30)) {
		return -1;
	}
	return res;
}

int builtin_lkill(int argc, char * argv[]) {
	int pid;
	int res;
	int signal_num = SIGTERM;

	if (argc == 2) {
		pid = get_int(argv[1]);
	} else if (argc == 3) {
		signal_num = get_int(argv[1] + 1);
		pid = get_int(argv[2]);
	} else {
		return BUILTIN_ERROR;
	}

	if (pid == -1 || signal_num == -1) {
		return BUILTIN_ERROR;
	}

	res = kill(pid, signal_num);
	if (res	== -1) {
		return BUILTIN_ERROR;
	}

	return 0;
}

int builtin_lls(int argc, char * argv[]) {
	DIR * dir;
	struct dirent * entry;
	int result;

	if (argc != 1) {
		return BUILTIN_ERROR;
	}

	dir = opendir(".");
	while (dir) {
		errno = 0;
		entry = readdir(dir);
		if (entry == NULL) {
			if (errno != 0) {
				EINTR_RETRY(result, closedir(dir));
				return BUILTIN_ERROR;
			}
			EINTR_RETRY(result, closedir(dir));
			return 0;
		}

		if (entry->d_name[0] != '.') {
			printf("%s\n", entry->d_name);
		}
	}

	return BUILTIN_ERROR;
}
