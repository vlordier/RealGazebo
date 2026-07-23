#!/usr/bin/env bash
set -euo pipefail
# Convert Bordeaux OBJ tiles to FBX for UE5.8 import

MANIFEST="${1:-/tmp/bordeaux_processed/manifest.json}"
TILES_DIR="${2:-/tmp/bordeaux_processed/tiles}"
OUTPUT_DIR="${3:-/tmp/bordeaux_processed/fbx}"

if [[ ! -f "$MANIFEST" ]]; then
  echo "Manifest not found: $MANIFEST" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"

# Read manifest and convert each tile
python3 -c "
import json, os, subprocess, sys

with open('$MANIFEST') as f:
    manifest = json.load(f)

tiles_dir = '$TILES_DIR'
output_dir = '$OUTPUT_DIR'

for tile in manifest['tiles']:
    obj_path = os.path.join(tiles_dir, tile['file'])
    fbx_name = tile['file'].replace('.obj', '.fbx')
    fbx_path = os.path.join(output_dir, fbx_name)

    if os.path.exists(fbx_path):
        print(f'  SKIP (exists): {fbx_name}')
        continue

    # Convert using assimp
    result = subprocess.run(
        ['assimp', 'export', obj_path, fbx_path],
        capture_output=True, text=True
    )

    if result.returncode == 0 and os.path.exists(fbx_path):
        print(f'  Converted: {fbx_name}')
    else:
        print(f'  FAILED: {fbx_name} - {result.stderr.strip()}')
        # Try with different export format
        alt_path = os.path.join(output_dir, tile['file'].replace('.obj', '.dae'))
        result = subprocess.run(
            ['assimp', 'export', obj_path, alt_path],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            print(f'  Converted (DAE): {os.path.basename(alt_path)}')

print(f'\\nDone! Output: {output_dir}')
" 2>&1