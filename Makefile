INC=-Iinclude
CFLAGS=$(INC) -Wall -Wextra -ansi -pedantic -Wno-unused-parameter

PARSERDIR=input_parse

SRCS=utils.c mshell.c builtins.c linereader.c processgroups.c
OBJS:=$(SRCS:.c=.o)

all: mshell 

mshell: $(OBJS) siparse.a
	cc $(CFLAGS) $(OBJS) siparse.a -o $@ 

%.o: %.c
	cc $(CFLAGS) -c $<

siparse.a:
	$(MAKE) -C $(PARSERDIR)

clean:
	make -C $(PARSERDIR) clean
	rm -f mshell *.o *.a
