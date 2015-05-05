#!/bin/bash
N=${1-4}
GA=${2-2}

# load and init
./uwfdtool "p $N main.bin; i $N uwfdtool.conf;m $N 0"
# program slave xilinxes
./uwfdtool "x $N 1=0x8000;x $N 0x2001=0x8000;x $N 0x4001=0x8000;x $N 0x6001=0x8000;x $N 0x28=0x40;x $N 0x2028=0x40;x $N 0x4028=0x40;x $N 0x6028=0x40;x $N 0x29=0x40;x $N 0x2029=0x40;x $N 0x4029=0x40;x $N 0x6029=0x40"
