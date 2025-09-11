#!/bin/sh
gdb-multiarch vmlinux -ex 'target remote :1234' -ex 'set architecture aarch64'

