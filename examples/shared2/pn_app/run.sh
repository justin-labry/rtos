#!/bin/bash

echo "PacketNgin IPsec ver2.0"
VMID=$(create -c 1 -m 0xc00000 -s 0x800000 -n dev=eth0,pool=0x400000| sed -n 2p)
upload $VMID main
start $VMID
