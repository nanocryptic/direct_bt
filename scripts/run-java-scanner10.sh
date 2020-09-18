#!/bin/sh

# export direct_bt_debug=hci.event,mgmt.event,gatt.data
#
# ../scripts/run-java-scanner10.sh -wait -mac C0:26:DA:01:DA:B1 2>&1 | tee ~/scanner-h01-java10.log
# ../scripts/run-java-scanner10.sh -wait -wl C0:26:DA:01:DA:B1 2>&1 | tee ~/scanner-h01-java10.log
# ../scripts/run-java-scanner10.sh -wait 2>&1 | tee ~/scanner-h01-java10.log
#

if [ ! -e lib/java/tinyb2.jar -o ! -e bin/java/ScannerTinyB10.jar -o ! -e lib/libdirect_bt.so ] ; then
    echo run from dist directory
    exit 1
fi
# hciconfig hci0 reset
ulimit -c unlimited

# run 'dpkg-reconfigure locales' enable 'en_US.UTF-8'
export LANG=en_US.UTF-8
export LC_MEASUREMENT=en_US.UTF-8

echo COMMANDLINE $0 $*
echo direct_bt_debug $direct_bt_debug
echo direct_bt_verbose $direct_bt_verbose

java -cp lib/java/tinyb2.jar:bin/java/ScannerTinyB10.jar -Djava.library.path=`pwd`/lib ScannerTinyB10 $*
