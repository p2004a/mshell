#!/bin/sh

V=`byacc -V 2> /dev/null 1> /dev/null; echo $?;`
if [ $V = "0" ]
then
    byacc $*
else
    yacc $*
fi
