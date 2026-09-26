#!/bin/bash
# ==============================================================================
# copy_to_sd.sh - Deploy PX68K Baremetal to SD Card
# ==============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-}"

if [ -z "$TARGET_DIR" ]; then
    # Try finding an auto-mounted removable FAT drive in /media/$USER
    for m in /media/"$USER"/* /mnt/f /mnt/e /mnt/d; do
        if [ -d "$m" ] && [ -w "$m" ]; then
            TARGET_DIR="$m"
            break
        fi
    done
fi

if [ -z "$TARGET_DIR" ] || [ ! -d "$TARGET_DIR" ]; then
    echo "Usage: $0 <path-to-sd-card-mount>"
    echo "Example: $0 /media/$USER/BOOT"
    exit 1
fi

echo "Deploying PX68K Baremetal to: ${TARGET_DIR} ..."

mkdir -p "${TARGET_DIR}/keropi"

# Copy kernels
cp -v "${ROOT_DIR}"/dist_baremetal/kernel*.img "${TARGET_DIR}/" || true
cp -v "${ROOT_DIR}"/dist_baremetal/app*.bin "${TARGET_DIR}/" || true

# Copy configs
cp -v "${ROOT_DIR}"/dist_baremetal/config.txt "${TARGET_DIR}/"
cp -v "${ROOT_DIR}"/dist_baremetal/cmdline.txt "${TARGET_DIR}/"

# Copy ROMs and system disks
cp -v "${ROOT_DIR}"/dist_baremetal/keropi/* "${TARGET_DIR}/keropi/"
cp -v "${ROOT_DIR}"/dist_baremetal/human302.xdf "${TARGET_DIR}/"

echo "Syncing..."
sync
echo "Done!"
