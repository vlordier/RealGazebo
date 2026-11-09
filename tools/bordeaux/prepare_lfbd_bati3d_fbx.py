#!/usr/bin/env python3
"""Convert an explicitly extracted LFBD Bati3D subset from 3DS to FBX.

Input comes from BOD-CUAS' bounded extractor.  This script never downloads
or extracts the 1.7 GiB archive itself; it turns a reviewed subset into an
Unreal import manifest and carries the historical-context provenance forward.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--subtile", action="append", default=[])
    args = parser.parse_args()
    index = json.loads(args.index.read_text())
    records = index["subtiles"]
    wanted = set(args.subtile) if args.subtile else {path.name for path in args.source_dir.iterdir() if path.is_dir()}
    selected = [record for record in records if record["id"] in wanted]
    missing = wanted - {record["id"] for record in selected}
    if missing:
        raise SystemExit(f"subtiles missing from index: {', '.join(sorted(missing))}")
    if not selected:
        raise SystemExit("no Bati3D subtiles selected")
    assimp = shutil.which("assimp")
    if not assimp:
        raise SystemExit("assimp is required; install it before converting Bati3D")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    converted = []
    for record in selected:
        tile = record["id"]
        # The provider names meshes ``tile_xNNyNN.3ds`` but textures
        # ``xNNyNN.jpg``; retain both exact source conventions.
        source = args.source_dir / tile / Path(record["source_3ds"]).name
        texture = args.source_dir / tile / f"{tile}.jpg"
        target = args.output_dir / f"{tile}.fbx"
        if not source.is_file() or not texture.is_file():
            raise SystemExit(f"missing paired source for {tile}: run BOD-CUAS bati3d-core first")
        if not target.exists():
            subprocess.run([assimp, "export", str(source), str(target)], check=True)
        # Assimp may retain either the provider basename or a numeric texture
        # identifier from the 3DS material table. Preserve every referenced
        # JPEG alias beside the FBX so Unreal resolves the real texture.
        shutil.copy2(texture, args.output_dir / texture.name)
        aliases = set(re.findall(rb"([A-Za-z0-9_-]+\.jpg)", target.read_bytes()))
        for alias in aliases:
            shutil.copy2(texture, args.output_dir / alias.decode("ascii"))
        converted.append({"id": tile, "fbx": target.name, "texture": texture.name,
                          "epsg3945_bbox": record["epsg3945_bbox"]})
    manifest = {"schema": "furia.realgazebo.bati3d-import", "version": "1.0.0",
                "crs": index["crs"], "subtile_size_m": index["subtile_size_m"],
                "provenance": index["provenance"], "source_archive_sha256": index["archive_sha256"],
                "reference": {"icao": "LFBD", "epsg3945_easting_northing": [1407200, 4187700],
                              "unreal_mapping": "X=east, Y=north, centimetres; reference maps to world origin"},
                "tiles": converted}
    destination = args.output_dir / "manifest.json"
    destination.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"converted {len(converted)} historical Bati3D tiles -> {destination}")


if __name__ == "__main__":
    main()
