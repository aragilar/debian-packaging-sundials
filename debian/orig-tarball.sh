#!/bin/sh -e

# called by uscan with '--upstream-version' <version> <file>
PKG=sundials
VER=$2
echo "plop :" $3
tar zxvf $3
tar -c -z -f $3 -X debian/orig-tar.exclude $PKG-$VER
rm -rf $PKG-$VER
# move to directory 'tarballs'
if [ -r .svn/deb-layout ]; then
    . .svn/deb-layout
    mv $TAR $origDir
    echo "moved $TAR to $origDir"
fi

exit 0
