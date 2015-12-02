#!/bin/bash
#	unit number
UNIT=${1-0}
SIZE=50
DATADIR=/mnt/data1/tests
D=`date +%F`
DD=$DATADIR/$D

if [ $UNIT == "0" ] ; then
    echo "Usage: ./sipmtest2.sh unit_number"
    exit 99
fi

if [[ ! -d $DD ]] ; then
    mkdir $DD
fi

F=$DD/sipmcosm-m$UNIT
echo "Starting at `date`"
uwfdtool "p ${UNIT} main.bin;w 2000;i ${UNIT} cosmtrig-sipm.conf;m ${UNIT} 0;n ${UNIT} ${SIZE} ${F}.dat"
echo "Data taken at `date`"
dat2root2 ${F}.dat ${F}.root
root -l -b -q "sipmtest2.C(\"$F\")"
evince ${F}.pdf 2>> /dev/null &
echo "Finished at `date`"
