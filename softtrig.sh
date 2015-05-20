#!/bin/bash
N=${1-4}
GA=${2-2}

# load and init
./uwfdtool "p $N main.bin; i $N;m $N 0"
# program adc's for normal operation
./uwfdtool "r $N 0 0xD=0;r $N 1 0xD=0;r $N 2 0xD=0;r $N 3 0xD=0;r $N 4 0xD=0;r $N 5 0xD=0;r $N 6 0xD=0;r $N 7 0xD=0;r $N 8 0xD=0;r $N 9 0xD=0;r $N 10 0xD=0;r $N 11 0xD=0;r $N 12 0xD=0;r $N 13 0xD=0;r $N 14 0xD=0;r $N 15 0xD=0"
# program slave xilinxes
./uwfdtool "x $N 1=0x8000;x $N 0x2001=0x8000;x $N 0x4001=0x8000;x $N 0x6001=0x8000;x $N 0x28=0x40;x $N 0x2028=0x40;x $N 0x4028=0x40;x $N 0x6028=0x40;x $N 0x29=0x40;x $N 0x2029=0x40;x $N 0x4029=0x40;x $N 0x6029=0x40"

