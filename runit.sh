#!/bin/bash

# this script assumes that absolutely nothing has been done

echo "========= shell script BEGIN ========="
echo

# create a disk image, if it doesn't exist, then set its size
echo "Creating disk image..."
echo "truncate -s 1M a1fs_test_image"
truncate -s 1M a1fs_test_image
echo "DONE creating disk image"
echo

echo "Running make..."
make
echo "DONE running Makefile"
echo

echo "Formatting image into a1fs file system..."
echo "./mkfs.a1fs -f -i 128 a1fs_test_image"
./mkfs.a1fs -f -i 128 a1fs_test_image
echo
echo "DONE formatting image into a1fs file system"
echo

echo "Making directory for mount point if it does not exist..."
echo "mkdir -p /tmp/chajeong"
mkdir -p /tmp/chajeong
echo "DONE Making directory"
echo

echo "Mounting a1fs..."
echo "./a1fs a1fs_test_image /tmp/chajeong"
./a1fs a1fs_test_image /tmp/chajeong
echo "DONE mounting a1fs"
echo

echo "Performing operations on the FS..."
echo
# create directories
echo "mkdir /tmp/chajeong/directoryA"
mkdir /tmp/chajeong/directoryA
echo "mkdir /tmp/chajeong/directoryB"
mkdir /tmp/chajeong/directoryB
echo "mkdir /tmp/chajeong/directoryC"
mkdir /tmp/chajeong/directoryC
echo "mkdir /tmp/chajeong/directoryD"
mkdir /tmp/chajeong/directoryD

# create nested directories, for fun
echo "mkdir /tmp/chajeong/directoryA/subdirA"
mkdir /tmp/chajeong/directoryA/subdirA
echo "mkdir /tmp/chajeong/directoryA/subdirA/subsubdirA"
mkdir /tmp/chajeong/directoryA/subdirA/subsubdirA
echo "mkdir /tmp/chajeong/directoryA/subdirA/subsubdirA/AAA"
mkdir /tmp/chajeong/directoryA/subdirA/subsubdirA/AAA
echo "mkdir /tmp/chajeong/directoryA/subdirA/subsubdirA/AAA/AAAA"
mkdir /tmp/chajeong/directoryA/subdirA/subsubdirA/AAA/AAAA

# display contents of several directories
echo "ls -la /tmp/chajeong/directoryB"
ls -la /tmp/chajeong/directoryB
echo "ls -la /tmp/chajeong/directoryA"
ls -la /tmp/chajeong/directoryA
echo "ls -la /tmp/chajeong/directoryA/subdirA/"
ls -la /tmp/chajeong/directoryA/subdirA/

# create a file
echo "touch /tmp/chajeong/directoryB/testfile.txt"
touch /tmp/chajeong/directoryB/testfile.txt

# add data to a file
echo "echo \"This is data for this file!\" >> /tmp/chajeong/directoryB/testfile.txt"
echo "This is data for this file!" >> /tmp/chajeong/directoryB/testfile.txt

# read data from file
echo "cat /tmp/chajeong/directoryB/testfile.txt"
cat /tmp/chajeong/directoryB/testfile.txt
echo "DONE performing operations on the FS"
echo

# duplicate file
echo "cp /tmp/chajeong/directoryB/testfile.txt /tmp/chajeong/directoryB/new.txt"
cp /tmp/chajeong/directoryB/testfile.txt /tmp/chajeong/directoryB/new.txt
echo

# state before file delete
echo "state before file delete: ls -la /tmp/chajeong/directoryB"
ls -la /tmp/chajeong/directoryB
echo

# delete file (rm)
echo "rm /tmp/chajeong/directoryB/testfile.txt"
rm /tmp/chajeong/directoryB/testfile.txt

# state after file delete
echo "state after file delete: ls -la /tmp/chajeong/directoryB"
ls -la /tmp/chajeong/directoryB
echo

# preparing for single dir and nested dir deletes
mkdir /tmp/chajeong/directoryE
mkdir /tmp/chajeong/directoryF

echo "Delete a directory without subdirectories:"
echo "rmdir /tmp/chajeong/directoryC"
rmdir /tmp/chajeong/directoryC
echo

echo "After directory deletes: ls -la /tmp/chajeong"
ls -la /tmp/chajeong
echo

# ====================================================================

# unmount the image
echo "Unmounting file system..."
echo "fusermount -u /tmp/chajeong"
fusermount -u /tmp/chajeong
echo "DONE unmounting file system"
echo

# mount the image again and display some contents of FS to show that
# relevant state was saved to the disk image

echo "REmounting file system..."
echo "./a1fs a1fs_test_image /tmp/chajeong"
./a1fs a1fs_test_image /tmp/chajeong
echo "DONE remounting file system"
echo

# ====================================================================

echo "Relevant state was saved to disk image:"
echo "ls /tmp/chajeong"
ls /tmp/chajeong
echo

# delete duplicate file
echo "Delete file again:"
echo "rm /tmp/chajeong/directoryB/new.txt"
rm /tmp/chajeong/directoryB/new.txt
echo

echo "status after file delete:"
echo "ls -la /tmp/chajeong/directoryB"
ls -la /tmp/chajeong/directoryB
echo

# delete some dirs
echo "rmdir /tmp/chajeong/directoryD"
rmdir /tmp/chajeong/directoryD
echo "rmdir /tmp/chajeong/directoryE"
rmdir /tmp/chajeong/directoryE
echo

echo "ls -la /tmp/chajeong"
ls -la /tmp/chajeong
echo
 
# uncomment lines below to cleanup
# fusermount -u /tmp/chajeong/
# rm -rf /tmp/chajeong/
# make clean
# rm a1fs_test_image

echo "========= END of shell script ========="
echo 

# NOTE: this script does not test corner cases
