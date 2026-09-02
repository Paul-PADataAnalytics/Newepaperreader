#!/bin/bash
set -e

if [ "$EUID" -ne 0 ]
  then echo "Please run as root (sudo)"
  exit
fi

echo "Unmounting any existing partitions on /dev/mmcblk0..."
umount /dev/mmcblk0* 2>/dev/null || true

echo "Wiping and partitioning /dev/mmcblk0..."
parted -s /dev/mmcblk0 mklabel msdos mkpart primary fat32 1MiB 100%
partprobe /dev/mmcblk0 || true
sleep 2

echo "Formatting /dev/mmcblk0p1 as FAT32..."
mkfs.fat -F 32 /dev/mmcblk0p1

echo "Mounting to /tmp/sdcard..."
mkdir -p /tmp/sdcard
mount /dev/mmcblk0p1 /tmp/sdcard

echo "Creating directory structure..."
mkdir -p /tmp/sdcard/books
mkdir -p /tmp/sdcard/Downloads
mkdir -p /tmp/sdcard/data
mkdir -p /tmp/sdcard/games/hero_adventure/ui

echo "Copying fonts..."
cp data/Roboto-Regular.ttf /tmp/sdcard/data/

echo "Copying Hero Adventure game files..."
if [ -d "games/hero_adventure" ]; then
    cp -r games/hero_adventure/ui/* /tmp/sdcard/games/hero_adventure/ui/
    echo "Hero Adventure UI files copied successfully"
fi

echo "Syncing and unmounting..."
sync
umount /tmp/sdcard
echo "SD Card successfully prepared!"
