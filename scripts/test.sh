#!/bin/bash

lsmod | grep -e dmp -e zero

ls -l /dev/mapper

echo -e "\n4 Kb blocks\n"

sudo dd if=/dev/random of=/dev/mapper/dmp1 bs=4k count=10

sudo dd if=/dev/mapper/dmp1 of=/dev/null bs=4k count=10

echo ""

cat /sys/module/dmp/stat/volumes

echo -e "\n1 Mb block\n"

sudo dd if=/dev/random of=/dev/mapper/dmp1 bs=1M count=1

sudo dd if=/dev/mapper/dmp1 of=/dev/null bs=1M count=1

echo ""

cat /sys/module/dmp/stat/volumes
