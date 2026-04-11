#!/bin/sh
# Automated script to load kernel modules, run CIM benchmark (setup), then poweroff immediately

set -eu

# Minimal mounts needed when using init=... (no normal init system runs)
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sys /sys 2>/dev/null || true
mount -t devtmpfs dev /dev 2>/dev/null || true

# Load IRQ module (best-effort)
insmod /lib/modules/*/extra/sc_dev_irq.ko 2>/dev/null || true

cd /usr/bin/cim

./build/mnist_CIM \
  --section setup \
  --warmup 0 \
  --iters 0 \
  --network binaries/mnist_network_q4.bin \
  || true

sync
poweroff -f