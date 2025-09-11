#!/bin/bash
qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m 1024 \
    -smp 1 \
    -kernel arch/arm64/boot/Image \
    -append "root=/dev/vda1 rw console=ttyAMA0 nokaslr" \
    -drive file="/data/bupt-rtos/newdisk.qcow2",format=qcow2,index=0,media=disk \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -nographic -s -S
