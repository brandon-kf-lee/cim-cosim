# Tools

## Required Versions
- QEMU: submodule (qemu-sc branch)
- SystemC: 3.0.2
- Buildroot: 2024.11.1

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