MShell
======

This is a simple project implementing micro shell for POSIX compliant
operating system. It was tested under Linux and Minix operating systems.

Building
--------

To build project you need to have (b)yacc, flex, make and an ANSI C compiler.
To build project run make

    make

And you should have `mshell` binary file in root directory of project.
To clean directory out of generated file files run `make clean`.

TODO
----

The mshell tries to manage jobs even when the operating system (like Minix)
doesn't support POSIX job control. The module processgroups implements this
feature unfortunately there aren't implemented any builtins to control the
module.
