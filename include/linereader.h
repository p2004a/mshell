#ifndef _LINEREADER_H_
#define _LINEREADER_H_

#include <stdio.h>

struct linereader
{
	char * buffor;
	int print_prompt;
	int offset;
	int last_line_length;
};

int lr_init(struct linereader *);
int lr_readline(struct linereader *, char const** result);
void lr_clean(struct linereader *);

#endif /* _LINEREADER_H_ */
