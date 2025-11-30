#!/bin/bash

if [ ! -d "bin" ]; then
    mkdir bin
else
    rm -f bin/*
fi


gcc -g -O0 -Wall -Wextra -std=c11 -I . -o bin/P2_2B P2_2B.c -pthread

if [ $? -ne 0 ]; then
    echo "Compilation failed."
    exit 1
fi

echo "Compilation successful."

NUM_TAS=3
echo "Running Part B with $NUM_TAS TAs..."
./bin/P2_2B "$NUM_TAS"
