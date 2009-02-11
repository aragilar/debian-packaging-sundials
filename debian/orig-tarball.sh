#!/bin/sh -e

# called by uscan with '--upstream-version' <version> <file>
PKG=sundials
VER=$2

URL=https://computation.llnl.gov/casc/sundials/download/code/$PKG-$VER.tar.gz

wget $URL --output-document=../${PKG}_$VER.orig.tar.gz
