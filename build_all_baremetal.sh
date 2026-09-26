#!/bin/bash
#
# Build Circle baremetal kernel images for PX68K on Raspberry Pi:
#   build/rpi1/kernel.img      -> dist_baremetal/kernel.img (and app.bin)
#   build/rpi2/kernel7.img     -> dist_baremetal/kernel7.img (and app7.bin)
#   build/rpi3/kernel8-32.img  -> dist_baremetal/kernel8-32.img (and app8-32.bin)
#   build/rpi4/kernel7l.img    -> dist_baremetal/kernel7l.img (and app7l.bin)
#
set -euo pipefail

APPDIR="$(cd "$(dirname "$0")" && pwd)"
CIRCLEHOME="${CIRCLEHOME:-$HOME/circle}"
PREFIX="${PREFIX:-arm-none-eabi-}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
TARGETS=(1 2 3 4)

echo "=========================================="
echo " PX68K Baremetal Build for Raspberry Pi"
echo " CIRCLEHOME: ${CIRCLEHOME}"
echo " Targets:    ${TARGETS[*]}"
echo " Jobs:       ${JOBS}"
echo "=========================================="

mkdir -p "${APPDIR}/dist_baremetal/keropi"

# Check if px68k-libretro exists, clone if missing
if [ ! -d "${APPDIR}/px68k-libretro" ]; then
    echo ">>> Cloning px68k-libretro..."
    git clone --depth 1 https://github.com/uraraworks/px68k-libretro "${APPDIR}/px68k-libretro"
fi

# Copy system assets to dist_baremetal
if [ -f "${APPDIR}/public/system/iplrom.dat" ]; then
    cp -f "${APPDIR}/public/system/iplrom.dat" "${APPDIR}/dist_baremetal/keropi/"
    cp -f "${APPDIR}/public/system/cgrom.dat" "${APPDIR}/dist_baremetal/keropi/"
    cp -f "${APPDIR}/public/system/human302.xdf" "${APPDIR}/dist_baremetal/"
fi

# Copy config.txt and cmdline.txt
cp -f "${APPDIR}/config.txt" "${APPDIR}/dist_baremetal/"
cp -f "${APPDIR}/cmdline.txt" "${APPDIR}/dist_baremetal/"

for TARGET in "${TARGETS[@]}"; do
    echo ""
    echo ">>> Building for Raspberry Pi target: ${TARGET} ..."
    
    # Ensure Circle libraries for this target exist in build/circle/rpi${TARGET}
    if [ ! -f "${APPDIR}/build/circle/rpi${TARGET}/lib/libcircle.a" ]; then
        echo ">>> Building Circle libraries for target ${TARGET}..."
        make -C "${APPDIR}" circle RASPPI="${TARGET}" CIRCLEHOME="${CIRCLEHOME}"
    fi

    # Build the kernel image
    make -C "${APPDIR}" RASPPI="${TARGET}" CIRCLEHOME="${CIRCLEHOME}" -j"${JOBS}"
done

echo ""
echo "=========================================="
echo " All baremetal targets built successfully!"
echo " Output files in ${APPDIR}/dist_baremetal:"
ls -lh "${APPDIR}/dist_baremetal"
echo "=========================================="
