#!/bin/bash

# Recompile shm library
echo "Recompiling shm library..."
cd ../../shm
make clean
make

# Recompile current files
echo "Recompiling current files..."
cd ../exp/local_cs
make clean
make

echo "Recompilation complete"

