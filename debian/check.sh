#!/bin/sh

## Simple script for exercising the serial checks of Sundials
## Author: Rafael Laboissiere <rafael@debian.org>

RESULTS=$(tempfile)

cleanup(){
    rm -f $RESULTS
}
trap "cleanup" 1 2 3 13 15

CWD=$(pwd)

for dir in examples/*/serial ; do
    cd $dir
    for prog in $(ls .libs | fgrep -v .o | grep -v ^lt- | sed 's:.libs/::') ; do
        echo -n "Checking $dir/$prog... "
        ./$prog > $RESULTS
        if test -e $prog.out ; then
            DIFF=$(diff -u $prog.out $RESULTS)
            if test -z "$DIFF" ; then
                echo pass
            else
                echo fail
                echo "$DIFF"
            fi
        else
            echo pass
        fi
    done
    cd $CWD
done
