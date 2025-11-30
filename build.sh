#!/bin/bash

# Build script for SYSC4001 A3 Part 2

if [ ! -d "bin" ]; then
    mkdir bin
else
    rm -f bin/*
fi

gcc -g -O0 -Wall -Wextra -std=c11 -I . -o bin/P2_2A P2_2A.c

if [ $? -ne 0 ]; then
    echo "Compilation failed."
    exit 1
fi

echo "Compilation successful."

NUM_TAS=3

echo "Running with $NUM_TAS TAs..."
./bin/P2_2A "$NUM_TAS"

echo "Program finished."


