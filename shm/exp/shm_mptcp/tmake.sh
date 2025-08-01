#!/bin/bash

# Recompile shm library
echo "Recompiling shm library..."
cd ../../shm
make clean
make

# Recompile current files
echo "Recompiling current files..."
cd ../exp/shm_cs
make clean
make

echo "Recompilation complete"
if [ "$(ip addr show | grep -w inet | awk '{print $2}' | cut -d/ -f1)" != "192.168.80.145" ]; then
    scp -r ../../../shm_library 192.168.80.145:/home/emma/
fi
