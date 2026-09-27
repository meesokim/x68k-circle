#!/bin/bash
# ==============================================================================
# copy_to_sd.sh - Deploy PX68K Baremetal to SD Card
# Copies a file only when it is missing on the card or its contents differ.
# ==============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-}"
SRC_DIR="${ROOT_DIR}/dist_baremetal"

if ! command -v rsync >/dev/null 2>&1; then
    echo "rsync is required."
    exit 1
fi

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

if [ ! -d "$SRC_DIR" ]; then
    echo "Missing deploy directory: $SRC_DIR"
    exit 1
fi

echo "Deploying PX68K Baremetal to: ${TARGET_DIR} ..."
echo "Unchanged files are skipped."

mkdir -p "${TARGET_DIR}/keropi"

# --checksum compares file contents, so a newer timestamp alone does not copy.
# FAT timestamps are coarse; --modify-window keeps time updates from failing the run.
# Permissions and owners are omitted because the card is FAT.
rsync -r --checksum --modify-window=2 --omit-dir-times \
    --info=NAME,STATS \
    --include='/kernel*.img' \
    --include='/app*.bin' \
    --include='/config.txt' \
    --include='/cmdline.txt' \
    --include='/bootcode.bin' \
    --include='/start.elf' \
    --include='/start4.elf' \
    --include='/fixup.dat' \
    --include='/fixup4.dat' \
    --include='/armstub7-rpi4.bin' \
    --include='/LICENCE.broadcom' \
    --include='/COPYING.linux' \
    --include='/bcm*.dtb' \
    --include='/human302.xdf' \
    --include='/NanumGothic.ttf' \
    --include='/keropi/' \
    --include='/keropi/**' \
    --exclude='*' \
    "${SRC_DIR}/" "${TARGET_DIR}/"

echo "Syncing..."
sync
echo "Done!"
