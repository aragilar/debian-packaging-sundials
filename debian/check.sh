#!/bin/sh

## Simple script for exercising the serial checks of Sundials
## Author: Rafael Laboissiere <rafael@debian.org>

RESULTS=$(tempfile)

cleanup(){
    rm -f $RESULTS
}
trap "cleanup" 1 2 3 13 15

CWD=$(pwd)
cd $1/examples

for dir in $(find . -type d -maxdepth 1 -mindepth 1) ; do
    #for mode in "serial" "fcmix_serial" "parallel" "fcmix_parallel"  ; do
    for mode in "serial" ; do
	cd $dir/$mode
	echo "tests in " $mode
	for prog in $(find . -executable -type f) ; do
            echo -n "Checking $prog... "
            if [ "$mode" != "parallel" ]; then
	       ./$prog > $RESULTS
	    else
	       OMPI_MCA_plm_rsh_agent=/bin/false ./$prog > $RESULTS
	    fi

            if test -e $2/examples/$dir/serial/$prog.out ; then
        	DIFF=$(diff -u $2/examples/$dir/serial/$prog.out $RESULTS)
        	if test -z "$DIFF" ; then
                    echo success
        	else
                    echo fail
                    echo "$DIFF"
        	fi
            else
        	echo pass
            fi
	done
	cd $1/examples
    done
done

cd $CWD
