#!/bin/bash

CC=gcc
CFLAGS="-Wall -Werror -Wpedantic -Wextra -Wunused-variable -Wuninitialized -Wshadow -Wformat -Wconversion -Wfloat-equal -Wcast-qual -Wcast-align -Wstrict-aliasing -Wswitch-default -Werror=return-type -Werror=uninitialized -Werror=sign-compare -Wunused-function -Werror=aggressive-loop-optimizations -Werror=array-bounds "
FAST=" -Ofast -funroll-loops -finline-functions -march=native -fpeel-loops"
LIBS="-lm -lX11 -fopenmp"
SRC="main.c"
OUT="main"

echo "Compiling $SRC..."
$CC $CFLAGS $FAST -o $OUT $SRC $LIBS

if [ $? -eq 0 ]; then
    echo "Build successful."
    ./$OUT
else
    echo "Build failed."
fi

