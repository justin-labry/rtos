#! /bin/bash
set -e

BOOT_PARAM=`cat packetngin-boot.param`
echo $BOOT_PARAM

MSR_TOOLS_EXISTS=$(dpkg --get-selections| grep msr-tools | wc -l)
if (( $MSR_TOOLS_EXISTS < 1 )); then
	echo "msr-tools is not found. (perhaps you forgot to install this package?)"
	exit 1
fi

DISPATCHER_EXISTS=$(lsmod | grep dispatcher | wc -l)
if (( $DISPATCHER_EXISTS < 1 )); then
	sudo insmod ./drivers/dispatcher.ko
fi

sudo modprobe msr

sudo ./pnd $BOOT_PARAM

sudo rmmod msr
sudo rmmod dispatcher
