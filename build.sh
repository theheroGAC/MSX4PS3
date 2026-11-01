#!/bin/bash
set -e
if [ -z "$PS3DEV" ]; then
    if [ -d "/mnt/d/ps3dev-sdk" ]; then
        export PS3DEV=/mnt/d/ps3dev-sdk
    else
        export PS3DEV=/opt/ps3dev
    fi
fi
export PSL1GHT=$PS3DEV
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
make -j$(nproc 2>/dev/null || echo 4)
