#!/bin/bash
set -xue

disks=""
for i in {0,1,2,3,4}; do
	dd if=/dev/zero of=$i.vdev bs=128M count=1
	disks="$disks $(pwd)/$i.vdev"
done
sudo zpool create test raidz2 $disks
