# Tools

## Required Versions
- QEMU: submodule (qemu-sc branch)
- SystemC: 3.0.2
- Buildroot: 2025.11.1

## Setup
```bash
./setup.sh
```

## Building QEMU
```bash
cd qemu
./configure --target-list=riscv64-softmmu --enable-slirp --enable-debug
make
```

## Building SystemC
```bash
cd systemc
./configure --prefix=$HOME/local/systemc
make && make install
export SYSTEMC_HOME=$HOME/local/systemc
```

## Building Buildroot

```bash
cd buildroot
make qemu_riscv64_virt_defconfig
make 
```

Note: If your username has an '@' symbol in it (e.g. name@university.edu), build buildroot in a directory you don't own, like /tmp.

```bash
make O=/tmp/buildroot_build qemu_riscv64_virt_defconfig
make O=/tmp/buildroot_build
```

Note: If make complains: "You seem to have the current working directory in your
LD_LIBRARY_PATH environment variable. This doesn't work.", temporarily unset the paths.

```bash
unset LIBRARY_PATH
unset LD_LIBRARY_PATH
```

### Buildroot settings to configure:
```bash
make menuconfig
```
1. Enable perf: Kernel -> Linux Kernel Tools -> perf
2. Set Linux kernel version to 6.18.7

