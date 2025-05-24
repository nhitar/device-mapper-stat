#!/bin/bash

sudo dmsetup remove dmp1

sudo dmsetup remove zero1

sudo rmmod dmp

sudo rmmod dm_zero

echo "Clean finished"
