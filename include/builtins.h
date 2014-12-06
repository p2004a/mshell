#ifndef _BUILTINS_H_
#define _BUILTINS_H_

#define BUILTIN_ERROR 2

typedef int (*builtin_func)(int, char **);

typedef struct {
	char* name;
	builtin_func func;
} builtin_pair;

extern builtin_pair builtins_table[];

builtin_func get_builtin(const char *name);

#endif /* !_BUILTINS_H_ */
