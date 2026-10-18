#!/bin/bash
set -euo pipefail

# ---------- Configuration ----------
BR_OUT="/tmp/buildroot_build"
ROOTFS="${BR_OUT}/images/rootfs.ext2"

USER_SRC_DIR="/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/mem_ctrl/test"
KMOD_DIR="/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/mem_ctrl/linux/sc_dev_kmod"

# Kernel build directory (modules)
KDIR="${BR_OUT}/build/linux-6.18.7"
ARCH="riscv"

# Ensure rootfs is ext2
file "${ROOTFS}" | grep -qi 'ext2 filesystem' || die "ROOTFS is not a valid ext2 image: ${ROOTFS}"

# Dynamically locate the cross-compiler in the Buildroot host bin directory
HOST_BIN="${BR_OUT}/host/bin"
TOOLCHAIN_BIN=""

# Try common known prefixes first
for candidate in \
    "$HOST_BIN/riscv64-buildroot-linux-gnu-gcc" \
    "$HOST_BIN/riscv64-linux-gnu-gcc"; do
    if [ -x "$candidate" ]; then
        TOOLCHAIN_BIN="$candidate"
        break
    fi
done

# Fallback: auto-discover any RISC-V gcc wrapper, avoiding .br_real files
if [ -z "$TOOLCHAIN_BIN" ]; then
    TOOLCHAIN_BIN=$(find "$HOST_BIN" -maxdepth 1 -name "*-linux*-gcc" ! -name "*.br_real" | head -n 1)
fi

# Validate that we actually found it
if [ -z "$TOOLCHAIN_BIN" ] || [ ! -x "$TOOLCHAIN_BIN" ]; then
    echo "Error: Could not find a valid RISC-V GCC compiler in $HOST_BIN" >&2
    exit 1
fi

# Extract the proper compiler prefix by stripping out everything from '-gcc' onward
COMPILER_NAME="$(basename "$TOOLCHAIN_BIN")"
CROSS_PREFIX="${COMPILER_NAME%%-gcc*}"
CROSS_COMPILE="${BR_OUT}/host/bin/${CROSS_PREFIX}-"

# ---------- Color Helpers ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

die() { echo -e "${RED}Error: $*${NC}" >&2; exit 1; }

# ---------- debugfs Helper Functions ----------
dbgfs_rm()    { debugfs -w "${ROOTFS}" -R "rm $1" 2>/dev/null || true; }
dbgfs_mkdir() { debugfs -w "${ROOTFS}" -R "mkdir $1" 2>/dev/null || true; }
dbgfs_write() { debugfs -w "${ROOTFS}" -R "write $1 $2" || die "debugfs write failed: $1 -> $2"; }
dbgfs_chmod() { debugfs -w "${ROOTFS}" -R "set_inode_field $1 mode $2" 2>/dev/null || true; }

need_file() { [[ -f "$1" ]] || die "missing file: $1"; }

# resolve a host path (relative -> absolute) without requiring realpath(1)
abs_host_path() {
  local p="$1"
  if [[ "$p" == /* ]]; then
    printf '%s\n' "$p"
  else
    printf '%s\n' "$(pwd)/$p"
  fi
}

# mkdir -p equivalent for debugfs (creates each path component)
dbgfs_mkdir_p() {
  local path="$1"
  [[ "$path" == /* ]] || die "dest path must be absolute inside rootfs: $path"

  local cur=""
  IFS='/' read -r -a parts <<< "${path#/}"
  for p in "${parts[@]}"; do
    [[ -z "$p" ]] && continue
    cur="${cur}/${p}"
    dbgfs_mkdir "${cur}"
  done
}

# Copy one host file into rootfs at exact absolute destination path
dbgfs_copy_file_to() {
  local host_src="$1"
  local rootfs_dest="$2"

  need_file "${host_src}"
  dbgfs_mkdir_p "$(dirname "${rootfs_dest}")"
  dbgfs_rm "${rootfs_dest}"
  dbgfs_write "${host_src}" "${rootfs_dest}"
}

# Recursively copy a host directory into rootfs under a dest directory.
# Example: cp -r DEST /path/to/dir  -> copies into DEST/dir/<contents...>
dbgfs_copy_tree_into() {
  local host_src_dir="$1"
  local rootfs_dest_dir="$2"

  [[ -d "${host_src_dir}" ]] || die "not a directory: ${host_src_dir}"
  [[ "${rootfs_dest_dir}" == /* ]] || die "dest must be absolute: ${rootfs_dest_dir}"

  local base
  base="$(basename "${host_src_dir}")"
  local rootfs_root="${rootfs_dest_dir%/}/${base}"

  dbgfs_mkdir_p "${rootfs_root}"

  # Create directories (do not follow symlinks, do not cross filesystems)
  while IFS= read -r -d '' d; do
    local rel="${d#${host_src_dir}/}"
    [[ -z "${rel}" ]] && continue
    dbgfs_mkdir_p "${rootfs_root}/${rel}"
  done < <(find "${host_src_dir}" -xdev -type d -print0)

  # Copy regular files only (skip symlinks, devices, etc.)
  while IFS= read -r -d '' f; do
    local rel="${f#${host_src_dir}/}"
    dbgfs_copy_file_to "${f}" "${rootfs_root}/${rel}"
  done < <(find "${host_src_dir}" -xdev -type f -print0)

  # Optional: warn if symlinks exist
  if find "${host_src_dir}" -xdev -type l -print -quit 2>/dev/null | grep -q .; then
    echo -e "${YELLOW}warning: symlinks exist under ${host_src_dir}; they were not copied${NC}" >&2
    find "${host_src_dir}" -xdev -maxdepth 5 -type l -print >&2 || true
  fi
}

# ---------- User Functions ----------
user() {
    [[ $# -ge 1 ]] || usage
    SRC_FILE="$1"; shift
    EXTRA_FLAGS=("$@")
    SRC_DIR="${USER_SRC_DIR}"

    [[ "${SRC_FILE}" == *.c ]] || die "source file must be .c"
    need_file "${TOOLCHAIN_BIN}"
    need_file "${ROOTFS}"
    need_file "${SRC_DIR}/${SRC_FILE}"

    OUT_FILE="${SRC_FILE%.c}"
    DEST_PATH="/usr/bin/${OUT_FILE}"

    echo "=== Deploy (user) ==="
    echo -e "Source:      ${YELLOW}${SRC_DIR}/${SRC_FILE}${NC}"
    echo -e "Output:      ${YELLOW}${OUT_FILE}${NC}"
    echo -e "Destination: ${YELLOW}${DEST_PATH}${NC}"
    if [[ ${#EXTRA_FLAGS[@]} -gt 0 ]]; then
      echo -e "Extra flags: ${YELLOW}${EXTRA_FLAGS[*]}${NC}"
    fi

    echo "[1/3] Cross-compiling user binary..."
    (
      cd "${SRC_DIR}"
      "${TOOLCHAIN_BIN}" -static -O2 -o "${OUT_FILE}" "${SRC_FILE}" "${EXTRA_FLAGS[@]}"
    )
    need_file "${SRC_DIR}/${OUT_FILE}"

    echo "[2/3] Copying into rootfs..."
    dbgfs_copy_file_to "${SRC_DIR}/${OUT_FILE}" "${DEST_PATH}"

    echo "[3/3] Setting permissions..."
    dbgfs_chmod "${DEST_PATH}" 0100755

    echo -e "${GREEN}Done.${NC}"
    echo "Run in guest: ${DEST_PATH}"
}

kmod() {
# defaults
    MODDIR="${KMOD_DIR}"
    MODNAME="sc_dev_irq"
    KVER="6.18.7"
    DESTDIR="" # auto
    while [[ $# -gt 0 ]]; do
      case "$1" in
        --kmoddir) MODDIR="$2"; shift 2;;
        --name)    MODNAME="$2"; shift 2;;
        --kver)    KVER="$2"; shift 2;;
        --destdir) DESTDIR="$2"; shift 2;;
        *) die "unknown option: $1";;
      esac
    done

    [[ -n "${DESTDIR}" ]] || DESTDIR="/lib/modules/${KVER}/extra"
    KO_SRC="${MODDIR}/${MODNAME}.ko"
    KO_DEST="${DESTDIR}/${MODNAME}.ko"

    need_file "${ROOTFS}"
    [[ -d "${KDIR}" ]] || die "KDIR not found: ${KDIR}"

    # check kernel build markers:
    need_file "${KDIR}/Module.symvers"
    need_file "${KDIR}/include/config/auto.conf"

    [[ -d "${MODDIR}" ]] || die "module dir not found: ${MODDIR}"
    need_file "${MODDIR}/Makefile"

    echo "=== Deploy (kmod) ==="
    echo -e "Module dir:  ${YELLOW}${MODDIR}${NC}"
    echo -e "KDIR:        ${YELLOW}${KDIR}${NC}"
    echo -e "KVER:        ${YELLOW}${KVER}${NC}"
    echo -e "KO dest:     ${YELLOW}${KO_DEST}${NC}"

    echo "[1/3] Building kernel module..."
    export PATH="${BR_OUT}/host/bin:${PATH}"

    make -C "${KDIR}" M="${MODDIR}" ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" clean
    make -C "${KDIR}" M="${MODDIR}" ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" modules

    need_file "${KO_SRC}"

    echo "[2/3] Copying .ko into rootfs..."
    dbgfs_mkdir_p "${DESTDIR}"
    dbgfs_rm "${KO_DEST}"
    dbgfs_write "${KO_SRC}" "${KO_DEST}"
    dbgfs_chmod "${KO_DEST}" 0100644

    echo "[3/3] Done."
    echo -e "${GREEN}Done.${NC}"
    echo "Load in guest:"
    echo "  insmod ${KO_DEST}"
    echo "Then:"
    echo "  dmesg | tail"
    echo "  cat /proc/interrupts"
}

cp() {
    RECURSE=false
    if [[ $# -ge 1 && "$1" == "-r" ]]; then
      RECURSE=true
      shift
    fi

    [[ $# -ge 2 ]] || usage
    
    # Extract the LAST argument as the destination
    DESTDIR="${@: -1}"
    
    # Put all arguments EXCEPT the last one into an array of sources
    # This safely handles filenames with spaces
    SOURCES=("${@:1:$#-1}")

    [[ "${DESTDIR}" == /* ]] || die "dest_in_rootfs must be absolute (start with /)"
    need_file "${ROOTFS}"

    echo "=== Deploy (cp) ==="
    echo -e "Dest dir: ${YELLOW}${DESTDIR}${NC}"
    echo -e "Rootfs:   ${YELLOW}${ROOTFS}${NC}"
    echo -e "Recurse:  ${YELLOW}${RECURSE}${NC}"

    echo "[1/2] Ensuring destination directory exists in rootfs..."
    dbgfs_mkdir_p "${DESTDIR}"

    echo "[2/2] Copying..."
    # Iterate over the SOURCES array instead of $@
    for src_in in "${SOURCES[@]}"; do
      src="$(abs_host_path "${src_in}")"

      if [[ -d "${src}" ]]; then
        if ${RECURSE}; then
          echo -e "  ${YELLOW}${src_in}${NC} -> ${YELLOW}${DESTDIR%/}/$(basename "${src}")/${NC} (recursive)"
          dbgfs_copy_tree_into "${src}" "${DESTDIR}"
        else
          echo -e "${YELLOW}  skipping directory (use -r): ${src_in}${NC}" >&2
        fi
        continue
      fi

      [[ -f "${src}" ]] || die "missing file: ${src}"
      base="$(basename "${src}")"
      dest="${DESTDIR%/}/${base}"
      echo -e "  ${YELLOW}${src_in}${NC} -> ${YELLOW}${dest}${NC}"
      dbgfs_copy_file_to "${src}" "${dest}"
    done

    echo -e "${GREEN}Done.${NC}"
}

# mnist_perf() {
#     [[ $# -ge 1 ]] || usage
#     CIM_DIR="$(abs_host_path "$1")"
#     shift

#     DEST_ROOT="/usr/bin/cim"

#     # Ensure files exist
#     need_file "${ROOTFS}"
#     need_file "${TOOLCHAIN_BIN}"
#     [[ -d "${CIM_DIR}" ]] || die "not a directory: ${CIM_DIR}"
#     # need_file "${CIM_DIR}/Makefile"
    
#     # Source/implementation files
#     need_file "${CIM_DIR}/src/cim.c"
#     need_file "${CIM_DIR}/src/mnist.c"
#     need_file "${CIM_DIR}/harness/src/mnist_bench.c"
#     need_file "${CIM_DIR}/harness/src/perf_gate.c"

#     # Main programs
#     need_file "${CIM_DIR}/examples/mnist_CIM.c"
#     need_file "${CIM_DIR}/examples/mnist_CPU.c"
#     need_file "${CIM_DIR}/examples/mnist_CIM_inst.c"
#     need_file "${CIM_DIR}/examples/mnist_CPU_inst.c"
#     need_file "${CIM_DIR}/examples/mnist_CIM_time.c"
#     need_file "${CIM_DIR}/examples/mnist_CPU_time.c"
#     need_file "${CIM_DIR}/examples/mnist_accurate.c"
#     need_file "${CIM_DIR}/examples/run_automation.sh"

#     # Include & binaries directories
#     [[ -d "${CIM_DIR}/include" ]] || die "missing include/: ${CIM_DIR}/include"
#     [[ -d "${CIM_DIR}/harness/include" ]] || die "missing harness/include/: ${CIM_DIR}/harness/include"
#     [[ -d "${CIM_DIR}/binaries" ]] || die "missing binaries/: ${CIM_DIR}/binaries"

#     echo "=== Deploy (mnist_perf) ==="
#     echo -e "CIM dir:     ${YELLOW}${CIM_DIR}${NC}"
#     echo -e "Toolchain:   ${YELLOW}${TOOLCHAIN_BIN}${NC}"
#     echo -e "Rootfs:      ${YELLOW}${ROOTFS}${NC}"
#     echo -e "Guest dest:  ${YELLOW}${DEST_ROOT}${NC}"

#     echo "[1/3] Building mnist_CIM_inst & mnist_CPU_inst with cross-compile toolchain..."
#     (
#       cd "${CIM_DIR}"
#       mkdir -p build
    
#       CFLAGS="-std=c11 -Wall -Wextra -O2"
#       CFLAGS_SYNTH="${CFLAGS} -DUSE_SYNTHETIC_NETWORK=1"
#       INCS="-Iinclude -Iharness/include"
    
#       # --- Build libcim.a (userspace CIM library) ---
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim.c          -o build/cim.o
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim_dma.c      -o build/cim_dma.o
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim_internal.c -o build/cim_internal.o
    
#       rm -f build/libcim.a
#       ar rcs build/libcim.a build/cim.o build/cim_dma.o build/cim_internal.o
#       "${TOOLCHAIN_BIN}" ranlib build/libcim.a 2>/dev/null || true
    
#       # --- Other objects (app/helpers) ---
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/mnist.c               -o build/mnist.o
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c harness/src/mnist_bench.c -o build/mnist_bench.o
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c harness/src/perf_gate.c   -o build/perf_gate.o
    
#       # --- Programs (compile+link) ---
    
#       # CIM
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
#         examples/mnist_CIM.c build/mnist.o build/mnist_bench.o \
#         build/libcim.a -lm \
#         -o build/mnist_CIM

#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
#         examples/mnist_CIM_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
#         build/libcim.a -lm \
#         -o build/mnist_CIM_inst

#       "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
#         examples/mnist_CIM.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
#         build/libcim.a -lm \
#         -o build/synthetic_CIM

#       "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
#         examples/mnist_CIM_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
#         build/libcim.a -lm \
#         -o build/synthetic_CIM_inst
    
#       # "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
#       #   examples/mnist_CIM_time.c build/mnist.o build/mnist_bench.o \
#       #   build/libcim.a -lm \
#       #   -o build/mnist_CIM_time
    
#       # CPU
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
#         examples/mnist_CPU.c build/mnist.o build/mnist_bench.o \
#         -o build/mnist_CPU -lm

#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
#         examples/mnist_CPU_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
#         -o build/mnist_CPU_inst -lm

#       "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
#         examples/mnist_CPU.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
#         -o build/synthetic_CPU -lm

#       "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
#         examples/mnist_CPU_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
#         -o build/synthetic_CPU_inst -lm
    
#       # "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
#       #   examples/mnist_CPU_time.c build/mnist.o build/mnist_bench.o \
#       #   -o build/mnist_CPU_time -lm
    
#       # CPU (accurate)
#       "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
#         examples/mnist_accurate.c build/mnist.o build/mnist_bench.o \
#         -o build/mnist_accurate -lm
#     )

#     # MNIST
#     need_file "${CIM_DIR}/build/mnist_CIM"
#     need_file "${CIM_DIR}/build/mnist_CPU"
#     need_file "${CIM_DIR}/build/mnist_CIM_inst"
#     need_file "${CIM_DIR}/build/mnist_CPU_inst"

#     # Synthetic
#     need_file "${CIM_DIR}/build/synthetic_CIM"
#     need_file "${CIM_DIR}/build/synthetic_CPU"
#     need_file "${CIM_DIR}/build/synthetic_CIM_inst"
#     need_file "${CIM_DIR}/build/synthetic_CPU_inst"
    
#     need_file "${CIM_DIR}/build/mnist_accurate"
#     # need_file "${CIM_DIR}/build/mnist_CIM_time"
#     # need_file "${CIM_DIR}/build/mnist_CPU_time"
    

#     echo "[2/3] Copying executables into ${DEST_ROOT}/build/ ..."
#     dbgfs_mkdir_p "${DEST_ROOT}/build"

#     # MNIST
#     dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CIM" "${DEST_ROOT}/build/mnist_CIM"
#     dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CPU" "${DEST_ROOT}/build/mnist_CPU"    
#     dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CIM_inst" "${DEST_ROOT}/build/mnist_CIM_inst"
#     dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CPU_inst" "${DEST_ROOT}/build/mnist_CPU_inst"
    
#     dbgfs_chmod "${DEST_ROOT}/build/mnist_CIM" 0100755 || true
#     dbgfs_chmod "${DEST_ROOT}/build/mnist_CPU" 0100755 || true
#     dbgfs_chmod "${DEST_ROOT}/build/mnist_CIM_inst" 0100755 || true
#     dbgfs_chmod "${DEST_ROOT}/build/mnist_CPU_inst" 0100755 || true

#     # Synthetic
#     dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CIM" "${DEST_ROOT}/build/synthetic_CIM"
#     dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CPU" "${DEST_ROOT}/build/synthetic_CPU"
#     dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CIM_inst" "${DEST_ROOT}/build/synthetic_CIM_inst"
#     dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CPU_inst" "${DEST_ROOT}/build/synthetic_CPU_inst"
    
#     dbgfs_chmod "${DEST_ROOT}/build/synthetic_CIM" 0100755 || true
#     dbgfs_chmod "${DEST_ROOT}/build/synthetic_CPU" 0100755 || true
#     dbgfs_chmod "${DEST_ROOT}/build/synthetic_CIM_inst" 0100755 || true
#     dbgfs_chmod "${DEST_ROOT}/build/synthetic_CPU_inst" 0100755 || true


#     dbgfs_copy_file_to "${CIM_DIR}/build/mnist_accurate" "${DEST_ROOT}/build/mnist_accurate"
#     dbgfs_copy_file_to "${CIM_DIR}/examples/run_automation.sh" "${DEST_ROOT}/run_automation.sh"
    
#     dbgfs_chmod "${DEST_ROOT}/build/mnist_accurate" 0100755 || true
#     dbgfs_chmod "${DEST_ROOT}/run_automation.sh" 0100755 || true

#     # dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CIM_time" "${DEST_ROOT}/build/mnist_CIM_time"
#     # dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CPU_time" "${DEST_ROOT}/build/mnist_CPU_time"    
#     # dbgfs_chmod "${DEST_ROOT}/build/mnist_CIM_time" 0100755 || true
#     # dbgfs_chmod "${DEST_ROOT}/build/mnist_CPU_time" 0100755 || true
    

#     echo "[3/3] Copying required binaries/scripts into ${DEST_ROOT}/binaries/ ..."
#     dbgfs_mkdir_p "${DEST_ROOT}/binaries"
#     dbgfs_copy_file_to "${CIM_DIR}/binaries/MNIST_0.bin"           "${DEST_ROOT}/binaries/MNIST_0.bin"
#     dbgfs_copy_file_to "${CIM_DIR}/binaries/mnist_network.bin"     "${DEST_ROOT}/binaries/mnist_network.bin"
#     dbgfs_copy_file_to "${CIM_DIR}/binaries/mnist_network_q4.bin"  "${DEST_ROOT}/binaries/mnist_network_q4.bin"
#     dbgfs_copy_file_to "${CIM_DIR}/binaries/t10k-images-idx3-ubyte" "${DEST_ROOT}/binaries/t10k-images-idx3-ubyte"
#     dbgfs_copy_file_to "${CIM_DIR}/binaries/t10k-labels-idx1-ubyte" "${DEST_ROOT}/binaries/t10k-labels-idx1-ubyte"
    
#     # Copy synthetic network file if it exists
#     if [[ -f "${CIM_DIR}/binaries/synthetic_network_q4.bin" ]]; then
#         dbgfs_copy_file_to "${CIM_DIR}/binaries/synthetic_network_q4.bin" "${DEST_ROOT}/binaries/synthetic_network_q4.bin"
#     fi

#     echo -e "${GREEN}Done.${NC}"
#     echo "In guest:"
#     echo "  cd ${DEST_ROOT}"
#     echo "  ./build/mnist_CIM"
#     echo "  ./build/mnist_CPU"
#     echo "  ./build/synthetic_CIM"
#     echo "  ./build/synthetic_CPU"
#     echo "  ./build/mnist_CIM_inst"
#     echo "  ./build/mnist_CPU_inst"
#     echo "  ./build/synthetic_CIM_inst"
#     echo "  ./build/synthetic_CPU_inst"
#     # echo "  ./build/mnist_CIM_time"
#     # echo "  ./build/mnist_CPU_time"
#     echo "  ./build/mnist_accurate"
#     echo "Binaries under:"
#     echo "  ${DEST_ROOT}/binaries/"
# }

inst_bench() {
    [[ $# -ge 1 ]] || usage
    CIM_DIR="$(abs_host_path "$1")"
    shift

    DEST_ROOT="/usr/bin/cim"

    # Ensure files exist
    need_file "${ROOTFS}"
    need_file "${TOOLCHAIN_BIN}"
    [[ -d "${CIM_DIR}" ]] || die "not a directory: ${CIM_DIR}"
    
    # Source/implementation files
    need_file "${CIM_DIR}/src/cim.c"
    need_file "${CIM_DIR}/src/mnist.c"
    need_file "${CIM_DIR}/harness/src/mnist_bench.c"
    need_file "${CIM_DIR}/harness/src/perf_gate.c"

    # Main programs
    need_file "${CIM_DIR}/examples/CIM_inst.c"
    need_file "${CIM_DIR}/examples/CPU_inst.c"

    # Include & binaries directories
    [[ -d "${CIM_DIR}/include" ]] || die "missing include/: ${CIM_DIR}/include"
    [[ -d "${CIM_DIR}/harness/include" ]] || die "missing harness/include/: ${CIM_DIR}/harness/include"
    [[ -d "${CIM_DIR}/binaries" ]] || die "missing binaries/: ${CIM_DIR}/binaries"

    echo "=== Deploy (inst_bench) ==="
    echo -e "CIM dir:     ${YELLOW}${CIM_DIR}${NC}"
    echo -e "Toolchain:   ${YELLOW}${TOOLCHAIN_BIN}${NC}"
    echo -e "Rootfs:      ${YELLOW}${ROOTFS}${NC}"
    echo -e "Guest dest:  ${YELLOW}${DEST_ROOT}${NC}"

    echo "[1/3] Building CIM_inst & CPU_inst with cross-compile toolchain..."
    (
      cd "${CIM_DIR}"
      mkdir -p build
    
      CFLAGS="-std=c11 -Wall -Wextra -O2"
      CFLAGS_SYNTH="${CFLAGS} -DUSE_SYNTHETIC_NETWORK=1"
      INCS="-Iinclude -Iharness/include"
    
      # --- Build libcim.a (userspace CIM library) ---
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim.c          -o build/cim.o
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim_dma.c      -o build/cim_dma.o
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim_internal.c -o build/cim_internal.o
      rm -f build/libcim.a
      ar rcs build/libcim.a build/cim.o build/cim_dma.o build/cim_internal.o
      "${TOOLCHAIN_BIN}" ranlib build/libcim.a 2>/dev/null || true
    
      # --- Other objects (app/helpers) ---
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/mnist.c               -o build/mnist.o
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c harness/src/mnist_bench.c -o build/mnist_bench.o
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c harness/src/perf_gate.c   -o build/perf_gate.o
    
      # --- Programs (compile+link) ---
      # MNIST
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
        examples/CIM_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
        build/libcim.a -lm \
        -o build/mnist_CIM_inst

      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
        examples/CPU_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
        -o build/mnist_CPU_inst -lm
      
      # Synthetic
      "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
        examples/CIM_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
        build/libcim.a -lm \
        -o build/synthetic_CIM_inst

      "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
        examples/CPU_inst.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
        -o build/synthetic_CPU_inst -lm
    )

    # Ensure successful compile
    need_file "${CIM_DIR}/build/mnist_CIM_inst"
    need_file "${CIM_DIR}/build/mnist_CPU_inst"
    need_file "${CIM_DIR}/build/synthetic_CIM_inst"
    need_file "${CIM_DIR}/build/synthetic_CPU_inst"


    echo "[2/3] Copying executables into ${DEST_ROOT}/build/ ..."
    dbgfs_mkdir_p "${DEST_ROOT}/build"

    # MNIST   
    dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CIM_inst" "${DEST_ROOT}/build/mnist_CIM_inst"
    dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CPU_inst" "${DEST_ROOT}/build/mnist_CPU_inst"
    dbgfs_chmod "${DEST_ROOT}/build/mnist_CIM_inst" 0100755 || true
    dbgfs_chmod "${DEST_ROOT}/build/mnist_CPU_inst" 0100755 || true

    # Synthetic
    dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CIM_inst" "${DEST_ROOT}/build/synthetic_CIM_inst"
    dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CPU_inst" "${DEST_ROOT}/build/synthetic_CPU_inst"
    dbgfs_chmod "${DEST_ROOT}/build/synthetic_CIM_inst" 0100755 || true
    dbgfs_chmod "${DEST_ROOT}/build/synthetic_CPU_inst" 0100755 || true
    

    echo "[3/3] Copying required binaries/scripts into ${DEST_ROOT}/binaries/ ..."
    dbgfs_mkdir_p "${DEST_ROOT}/binaries"
    dbgfs_copy_file_to "${CIM_DIR}/binaries/mnist_network_q4.bin"  "${DEST_ROOT}/binaries/mnist_network_q4.bin"
    dbgfs_copy_file_to "${CIM_DIR}/binaries/t10k-images-idx3-ubyte" "${DEST_ROOT}/binaries/t10k-images-idx3-ubyte"
    dbgfs_copy_file_to "${CIM_DIR}/binaries/t10k-labels-idx1-ubyte" "${DEST_ROOT}/binaries/t10k-labels-idx1-ubyte"
    
    # Copy synthetic network file if it exists
    if [[ -f "${CIM_DIR}/binaries/synthetic_network_q4.bin" ]]; then
        dbgfs_copy_file_to "${CIM_DIR}/binaries/synthetic_network_q4.bin" "${DEST_ROOT}/binaries/synthetic_network_q4.bin"
    fi

    echo -e "${GREEN}Done.${NC}"
    echo "In guest:"
    echo "  cd ${DEST_ROOT}"
    echo "  ./build/mnist_CIM_inst"
    echo "  ./build/mnist_CPU_inst"
    echo "  ./build/synthetic_CIM_inst"
    echo "  ./build/synthetic_CPU_inst"
    echo "Binaries under:"
    echo "  ${DEST_ROOT}/binaries/"
}

cache_bench() {
    [[ $# -ge 1 ]] || usage
    CIM_DIR="$(abs_host_path "$1")"
    shift

    DEST_ROOT="/usr/bin/cim"

    # Ensure files exist
    need_file "${ROOTFS}"
    need_file "${TOOLCHAIN_BIN}"
    [[ -d "${CIM_DIR}" ]] || die "not a directory: ${CIM_DIR}"

    # Source/implementation files
    need_file "${CIM_DIR}/src/cim.c"
    need_file "${CIM_DIR}/src/mnist.c"
    need_file "${CIM_DIR}/harness/src/mnist_bench.c"

    # Main programs
    need_file "${CIM_DIR}/examples/CIM_cache.c"
    need_file "${CIM_DIR}/examples/CPU_cache.c"

    # Include & binaries directories
    [[ -d "${CIM_DIR}/include" ]] || die "missing include/: ${CIM_DIR}/include"
    [[ -d "${CIM_DIR}/harness/include" ]] || die "missing harness/include/: ${CIM_DIR}/harness/include"
    [[ -d "${CIM_DIR}/binaries" ]] || die "missing binaries/: ${CIM_DIR}/binaries"

    echo "=== Deploy (cache_bench) ==="
    echo -e "CIM dir:     ${YELLOW}${CIM_DIR}${NC}"
    echo -e "Toolchain:   ${YELLOW}${TOOLCHAIN_BIN}${NC}"
    echo -e "Rootfs:      ${YELLOW}${ROOTFS}${NC}"
    echo -e "Guest dest:  ${YELLOW}${DEST_ROOT}${NC}"

    echo "[1/3] Building CIM_cache & CPU_cache with cross-compile toolchain..."
    (
      cd "${CIM_DIR}"
      mkdir -p build
    
      CFLAGS="-std=c11 -Wall -Wextra -O2"
      CFLAGS_SYNTH="${CFLAGS} -DUSE_SYNTHETIC_NETWORK=1"
      INCS="-Iinclude -Iharness/include"
    
      # --- Build libcim.a (userspace CIM library) ---
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim.c          -o build/cim.o
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim_dma.c      -o build/cim_dma.o
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/cim_internal.c -o build/cim_internal.o
      rm -f build/libcim.a
      ar rcs build/libcim.a build/cim.o build/cim_dma.o build/cim_internal.o
      "${TOOLCHAIN_BIN}" ranlib build/libcim.a 2>/dev/null || true
    
      # --- Other objects (app/helpers) ---
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c src/mnist.c               -o build/mnist.o
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} -c harness/src/mnist_bench.c -o build/mnist_bench.o
    
      # --- Programs (compile+link) ---
      # MNIST
      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
        examples/CIM_cache.c build/mnist.o build/mnist_bench.o \
        build/libcim.a -lm \
        -o build/mnist_CIM_cache

      "${TOOLCHAIN_BIN}" ${CFLAGS} ${INCS} \
        examples/CPU_cache.c build/mnist.o build/mnist_bench.o \
        -o build/mnist_CPU_cache -lm  

      # Synthetic
      "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
        examples/CIM_cache.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
        build/libcim.a -lm \
        -o build/synthetic_CIM_cache
  
      "${TOOLCHAIN_BIN}" ${CFLAGS_SYNTH} ${INCS} \
        examples/CPU_cache.c build/mnist.o build/mnist_bench.o build/perf_gate.o \
        -o build/synthetic_CPU_cache -lm
    )

    # Ensure successful compile
    need_file "${CIM_DIR}/build/mnist_CIM_cache"
    need_file "${CIM_DIR}/build/mnist_CPU_cache"
    need_file "${CIM_DIR}/build/synthetic_CIM_cache"
    need_file "${CIM_DIR}/build/synthetic_CPU_cache"
    

    echo "[2/3] Copying executables into ${DEST_ROOT}/build/ ..."
    dbgfs_mkdir_p "${DEST_ROOT}/build"

    # MNIST
    dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CIM" "${DEST_ROOT}/build/mnist_CIM_cache"
    dbgfs_copy_file_to "${CIM_DIR}/build/mnist_CPU" "${DEST_ROOT}/build/mnist_CPU_cache"    
    dbgfs_chmod "${DEST_ROOT}/build/mnist_CIM" 0100755 || true
    dbgfs_chmod "${DEST_ROOT}/build/mnist_CPU" 0100755 || true

    # Synthetic
    dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CIM" "${DEST_ROOT}/build/synthetic_CIM_cache"
    dbgfs_copy_file_to "${CIM_DIR}/build/synthetic_CPU" "${DEST_ROOT}/build/synthetic_CPU_cache"
    dbgfs_chmod "${DEST_ROOT}/build/synthetic_CIM" 0100755 || true
    dbgfs_chmod "${DEST_ROOT}/build/synthetic_CPU" 0100755 || true


    echo "[3/3] Copying required binaries/scripts into ${DEST_ROOT}/binaries/ ..."
    dbgfs_mkdir_p "${DEST_ROOT}/binaries"
    # dbgfs_copy_file_to "${CIM_DIR}/binaries/MNIST_0.bin"           "${DEST_ROOT}/binaries/MNIST_0.bin"
    # dbgfs_copy_file_to "${CIM_DIR}/binaries/mnist_network.bin"     "${DEST_ROOT}/binaries/mnist_network.bin"
    dbgfs_copy_file_to "${CIM_DIR}/binaries/mnist_network_q4.bin"  "${DEST_ROOT}/binaries/mnist_network_q4.bin"
    dbgfs_copy_file_to "${CIM_DIR}/binaries/t10k-images-idx3-ubyte" "${DEST_ROOT}/binaries/t10k-images-idx3-ubyte"
    dbgfs_copy_file_to "${CIM_DIR}/binaries/t10k-labels-idx1-ubyte" "${DEST_ROOT}/binaries/t10k-labels-idx1-ubyte"
    
    # Copy synthetic network file if it exists
    if [[ -f "${CIM_DIR}/binaries/synthetic_network_q4.bin" ]]; then
        dbgfs_copy_file_to "${CIM_DIR}/binaries/synthetic_network_q4.bin" "${DEST_ROOT}/binaries/synthetic_network_q4.bin"
    fi

    echo -e "${GREEN}Done.${NC}"
    echo "In guest:"
    echo "  cd ${DEST_ROOT}"
    echo "  ./build/mnist_CIM_cache"
    echo "  ./build/mnist_CPU_cache"
    echo "  ./build/synthetic_CIM_cache"
    echo "  ./build/synthetic_CPU_cache"
    echo "Binaries under:"
    echo "  ${DEST_ROOT}/binaries/"
}

usage() {
  cat <<EOF
Usage:
  $0 user <source_file.c> [extra gcc/link flags...]
     - builds a static RISC-V binary and installs to /usr/bin/<name>
     - example: $0 user cim_test.c -lm

  $0 kmod [--kmoddir <dir>] [--name sc_dev_irq] [--kver 6.18.7] [--destdir /lib/modules/<kver>/extra]

  $0 cp [-r] <src1> [src2 src3 ...] <dest_in_rootfs>

  $0 inst_bench <cim_tree_dir>
     - builds the neural network instruction counting benchmark (CIM & CPU) using cross-compile toolchain
     - copies into the guest all testbenches and binaries

  $0 cache_bench <cim_tree_dir>
     - builds the neural network cache benchmark (CIM & CPU) using cross-compile toolchain
     - copies into the guest all testbenches and binaries

Notes:
  - Host-side src args may be relative or absolute (resolved against current working directory).
  - Without -r: only regular files are copied; directories are skipped with a warning.
  - With -r: directories are copied recursively into <dest_in_rootfs> keeping basenames.

Examples:
  $0 user cim_test.c -lm
  $0 kmod --kmoddir ${KMOD_DIR} --name sc_dev_irq
  $0 cp ./a.txt ../b.bin /root
  $0 cp -r ~/CIM/cim-mem-ctrl/cim /usr/bin 
  $0 mnist_perf ~/CIM/cim-mem-ctrl/cim
EOF
  exit 1
}

# ---------- Parse ----------
[[ $# -ge 1 ]] || usage
MODE="$1"; shift

case "${MODE}" in
  user)        user "$@" ;;
  kmod)        kmod "$@" ;;
  cp)          cp "$@" ;;
  # mnist_perf)  mnist_perf "$@" ;;
  inst_bench)  inst_bench "$@" ;;
  cache_bench) cache_bench "$@" ;;
  *)           usage ;;
esac

