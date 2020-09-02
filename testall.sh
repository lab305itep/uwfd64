#!/bin/bash
N=${1-0}

./uwfdtool "p $N main.bin; w 1000; i $N; t $N 0 100000; t $N 1 100; t $N 2 100; t $N 3 10000; t $N 4 1000; t $N 5 10; m $N 0; k $N 1; t $N 6 100000; k $N 0; m $N 1; t $N 7 100000; i $N t8.conf; k $N -1; t $N 8 1; t $N 8 -1"
#                          ! neeed this to delay init, otherwise CDCUN does not read back

