#! /bin/bash
set -e

BOOT_PARAM=`cat packetngin-boot.param`
echo $BOOT_PARAM

MSR_TOOLS_EXISTS=$(dpkg --get-selections| grep msr-tools | wc -l)
if (( $MSR_TOOLS_EXISTS < 1 )); then
	echo "msr-tools is not found. (perhaps you forgot to install this package?)"
	exit 1
fi

sudo insmod ./drivers/dispatcher.ko
sudo modprobe msr
sudo ./pnd $BOOT_PARAM
sudo rmmod msr
sudo rmmod dispatcher
