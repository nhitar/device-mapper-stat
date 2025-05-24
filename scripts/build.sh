#!/bin/bash

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: Start script with sudo"
    exit 1
fi

sudo insmod dmp.ko

sudo dmsetup create zero1 --table "0 5000 zero"

sudo dmsetup create dmp1 --table "0 5000 dmp /dev/mapper/zero1"

echo "Build finished"
