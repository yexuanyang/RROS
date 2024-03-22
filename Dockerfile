FROM ubuntu:22.04

LABEL maintainer="shanmu"
LABEL e-mail="syx@bupt.edu.cn"

WORKDIR /root

## install basic tools or libs: git, gdb, ssh, curl, xz-utils, bzip2, libtinfo5, cmake, cpio, vim
RUN apt-get update && apt-get upgrade -y \
    && apt install git -y \
    && apt-get install gdb-multiarch -y\
    && apt-get install openssh-server -y \
    && apt-get install curl -y \
    && apt-get install xz-utils -y \
    && apt-get install bzip2 -y \
    && apt-get install libtinfo5 -y \
    && apt-get install cmake -y \
    && apt-get install cpio -y \
    && apt-get install vim -y

## install qemu
RUN apt-get install qemu qemu-system qemu-user -y

## add env config
ENV ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu-

## get RROS source and libevl source
RUN git clone https://github.com/BUPT-OS/RROS.git \
    && git clone https://github.com/BUPT-OS/libevl.git

## install compile toolchain
## 1. rustup
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs > rust-init.sh \
    && chmod +x rust-init.sh \
    && sh rust-init.sh -y
ENV PATH /root/.cargo/bin:$PATH
## 2. LLVM
RUN wget https://github.com/llvm/llvm-project/releases/download/llvmorg-13.0.1/clang+llvm-13.0.1-x86_64-linux-gnu-ubuntu-18.04.tar.xz \
    && tar xvf clang+llvm-13.0.1-x86_64-linux-gnu-ubuntu-18.04.tar.xz \
    && mv clang+llvm-13.0.1-x86_64-linux-gnu-ubuntu-18.04 llvm
ENV PATH /root/llvm/bin:$PATH
## 3. bindgen and specific rust toolchain
WORKDIR /root/RROS
    RUN rustup toolchain install beta-2021-06-23-x86_64-unknown-linux-gnu \
    && rustup override set beta-2021-06-23-x86_64-unknown-linux-gnu \
    && rustup component add rust-src \
    && cargo install --locked --version 0.56.0 bindgen \
## 4. aarch64 cross-compilation tool
    && apt-get install gcc-12-aarch64-linux-gnu -y \
    && mv /usr/bin/aarch64-linux-gnu-gcc-12 /usr/bin/aarch64-linux-gnu-gcc \
## 5. some missing libraries for 'make menuconfig'
    && apt-get install flex -y \
    && apt-get install bison -y \
    && apt-get install libncurses-dev -y \
## 6. some missing libs or headers for compiling the kernel
    && apt-get install libssl-dev -y \
    && apt-get install bc -y \
## 7. generate rros_defconfig to .config
    && make LLVM=1 rros_defconfig

## build a rootfs using busybox
WORKDIR /root
RUN wget https://www.busybox.net/downloads/busybox-1.36.1.tar.bz2 \
    && tar xvf busybox-1.36.1.tar.bz2 \
    && mkdir -p rootfs \
    && cd /root/busybox-1.36.1 \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- install CONFIG_PREFIX=/root/rootfs \
## change path to /root/rootfs, to construct a toy rootfs
    && cd /root/rootfs \
    && mkdir -p lib && cp /usr/aarch64-linux-gnu/lib/* lib/ && ln -s lib lib64 \
    && mkdir dev proc mnt sys tmp root \
    && echo '/bin/mount -t devtmpfs devtmpfs /dev' >> init \
    && echo 'exec 0</dev/console' >> init \
    && echo 'exec 1>/dev/console' >> init \
    echo 'exec 2>/dev/console' >> init \
    echo 'exec /sbin/init "$@"' >> init \
    && mkdir etc && mkdir etc/init.d \
    && echo 'PATH=/sbin:/bin:/usr/sbin:/usr/bin' >> etc/init.d/rcS\
    && echo 'LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/lib:/usr/lib' >> etc/init.d/rcS \
    && echo 'export PATH LD_LIBRARY_PATH runlevel' >> etc/init.d/rcS \
    && echo '/bin/hostname root' >> etc/init.d/rcS \
    && echo 'mount -a' >> etc/init.d/rcS \
    && echo 'mkdir /dev/pts' >> etc/init.d/rcS \
    && echo 'mount -t devpts devpts /dev/pts' >> etc/init.d/rcS \
    && echo '/sbin/mdev > /proc/sys/kernel/hotplug' >> etc/init.d/rcS \
    && echo 'mdev -s' >> etc/init.d/rcS \
    && chmod +x etc/init.d/rcS \
    && echo 'proc    /proc   proc    defaults 0 0' >> etc/fstab \
    && echo 'tmpfs   /tmp    tmpfs   defaults 0 0' >> etc/fstab \
    && echo 'sysfs   /sys    sysfs   defaults 0 0' >> etc/fstab \
    && echo 'tmpfs   /dev    tmpfs   defaults 0 0' >> etc/fstab \
    && echo '::sysinit:/etc/init.d/rcS' >> etc/inittab\
    && echo 'console::askfirst:-/bin/sh' >> etc/inittab\
    && echo '::restart:/sbin/init' >> etc/inittab\
    && echo '::ctrlaltdel:/sbin/reboot' >> etc/inittab\
    && echo '::shutdown:/bin/umount -a -r' >> etc/inittab\
    && echo '::shutdown:/sbin/swapoff -a' >> etc/inittab\
    && echo 'USER="`root`"' >> etc/profile\                                                      
    && echo 'LOGNAME=$USER' >> etc/profile\
    && echo 'HOSTNAME=`/bin/hostname`' >> etc/profile\
    && echo 'HOME=/root' >> etc/profile\
    && echo 'PS1="[$USER@$HOSTNAME \W]\# "' >> etc/profile\
    && echo 'PATH=$PATH' >> etc/profile\
    && echo 'export USER LOGNAME HOSTNAME HOME PS1 PATH PATH LD_LIBRARY_PATH' >> etc/profile\
    && find ./* | cpio -H newc -o > rootfs.cpio \
    && gzip rootfs.cpio \
    && cp rootfs.cpio.gz /root/RROS/

## dirty clean
WORKDIR /root
RUN rm -rf busybox-1.36.1 busybox-1.36.1.tar.bz2 clang+llvm-13.0.1-x86_64-linux-gnu-ubuntu-18.04.tar.xz rust-init.sh
