#!/usr/bin/env python3
"""
Streaming Bordeaux OBJ preprocessor for UE5.8 import.

Pipeline:
  1. Stream vertices, apply axis transform + scale, store in memory
  2. Stream faces, filter by semantic category, group by tile + category
  3. Export per-tile OBJ files with merged geometry
  4. Generate import manifest JSON

Scale: 1 OBJ unit = 14.3 UE centimeters (determined from runway PCA: 6.99 units/m)
Axis:  OBJ (X,Y,Z) → UE (X, -Z, Y)   [UE: X=East, Y=North, Z=Up]
Origin: recentered so airport is near UE (0,0,0)
"""

import argparse
import json
import math
import os
import sys
from collections import defaultdict

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SCALE_UE_CM = 14.3  # 1 OBJ unit = 14.3 UE cm (6.99 units/meter)

# Airport center in OBJ coordinates (from face centroid analysis)
ORIGIN_OBJ = (-12000.0, 0.0, 11000.0)

# Tile size in OBJ units (~1.43km per tile at 14.3 cm/unit)
TILE_SIZE = 10000.0

# Semantic categories to KEEP (everything else is discarded)
KEEP_CATEGORIES = {
    "aeroway", "building", "highway", "railway", "Water",
    "natural", "landuse", "area", "leisure", "barrier",
    "man", "power",
}

# Highway subtypes to keep (primary/secondary/tertiary/trunk/motorway/service near airport)
KEEP_HIGHWAY_SUBTYPES = {
    "primary", "secondary", "tertiary", "trunk", "motorway",
    "service", "residential", "unclassified",
}

# Material mapping: OBJ material name → UE material asset path
MATERIAL_MAP = {
    "aeroway":  "/RealGazebo/Bordeaux/Materials/M_Aeroway_Asphalt",
    "roads":    "/RealGazebo/Bordeaux/Materials/M_Road_Asphalt",
    "building": "/RealGazebo/Bordeaux/Materials/M_Building_Generic",
    "landuse":  "/RealGazebo/Bordeaux/Materials/M_Landuse",
    "landuse_1": "/RealGazebo/Bordeaux/Materials/M_Landuse",
    "landuse_2": "/RealGazebo/Bordeaux/Materials/M_Grass",
    "landuse_3": "/RealGazebo/Bordeaux/Materials/M_Landuse",
    "natural":  "/RealGazebo/Bordeaux/Materials/M_Natural",
    "natural_1": "/RealGazebo/Bordeaux/Materials/M_Natural",
    "Water":    "/RealGazebo/Bordeaux/Materials/M_Water",
    "rail":     "/RealGazebo/Bordeaux/Materials/M_Rail",
    "area":     "/RealGazebo/Bordeaux/Materials/M_Ground",
    "map":      "/RealGazebo/Bordeaux/Materials/M_Ground",
    "map_1":    "/RealGazebo/Bordeaux/Materials/M_Ground",
    "default":  "/RealGazebo/Bordeaux/Materials/M_Ground",
    "leisure":  "/RealGazebo/Bordeaux/Materials/M_Grass",
    "amenity":  "/RealGazebo/Bordeaux/Materials/M_Building_Generic",
    "barrier":  "/RealGazebo/Bordeaux/Materials/M_Road_Asphalt",
    "power":    "/RealGazebo/Bordeaux/Materials/M_Rail",
    "man":      "/RealGazebo/Bordeaux/Materials/M_Building_Generic",
    "tourism":  "/RealGazebo/Bordeaux/Materials/M_Building_Generic",
    "name":     "/RealGazebo/Bordeaux/Materials/M_Ground",
    "name_1":   "/RealGazebo/Bordeaux/Materials/M_Ground",
    "name_2":   "/RealGazebo/Bordeaux/Materials/M_Ground",
    "type":     "/RealGazebo/Bordeaux/Materials/M_Ground",
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def get_semantic_category(obj_name: str) -> str:
    """Extract semantic category from OBJ object name."""
    if "=" in obj_name:
        return obj_name.split("=")[0]
    elif "_" in obj_name:
        return obj_name.split("_")[0]
    return obj_name


def get_highway_subtype(obj_name: str) -> str:
    """Extract highway subtype (e.g., 'primary' from 'highway=primary_1')."""
    # Format: highway=primary_1 or highway_primary_1
    if "=" in obj_name:
        rest = obj_name.split("=", 1)[1]
    else:
        parts = obj_name.split("_", 1)
        rest = parts[1] if len(parts) > 1 else ""
    return rest.split("_")[0] if rest else ""


def should_keep(obj_name: str) -> bool:
    """Determine if an object should be kept based on semantic category."""
    cat = get_semantic_category(obj_name)
    if cat not in KEEP_CATEGORIES:
        return False
    # Filter highway subtypes
    if cat == "highway":
        subtype = get_highway_subtype(obj_name)
        if subtype and subtype not in KEEP_HIGHWAY_SUBTYPES:
            return False
    return True


def obj_to_ue(obj_x: float, obj_y: float, obj_z: float) -> tuple:
    """Transform OBJ coordinates to UE coordinates and apply scale."""
    # Axis transform: OBJ (X,Y,Z) → UE (X, -Z, Y)
    ue_x = (obj_x - ORIGIN_OBJ[0]) * SCALE_UE_CM
    ue_y = -(obj_z - ORIGIN_OBJ[2]) * SCALE_UE_CM
    ue_z = (obj_y - ORIGIN_OBJ[1]) * SCALE_UE_CM
    return (ue_x, ue_y, ue_z)


def get_tile_key(ue_x: float, ue_y: float) -> tuple:
    """Determine tile grid key from UE X,Y coordinates."""
    tx = math.floor(ue_x / (TILE_SIZE * SCALE_UE_CM))
    ty = math.floor(ue_y / (TILE_SIZE * SCALE_UE_CM))
    return (tx, ty)


def get_ue_material(obj_name: str, obj_material: str) -> str:
    """Map OBJ material to UE material asset path."""
    if obj_material and obj_material in MATERIAL_MAP:
        return MATERIAL_MAP[obj_material]
    cat = get_semantic_category(obj_name)
    return MATERIAL_MAP.get(cat, MATERIAL_MAP["default"])


# ---------------------------------------------------------------------------
# Main preprocessing
# ---------------------------------------------------------------------------

def preprocess(obj_path: str, output_dir: str):
    """Streaming preprocess: read, filter, transform, tile, export."""
    os.makedirs(output_dir, exist_ok=True)

    # ------------------------------------------------------------------
    # Pass 1: Read all vertices, transform, store
    # ------------------------------------------------------------------
    print("Pass 1: Reading and transforming vertices...", file=sys.stderr)
    ue_vertices = []  # list of (x, y, z) in UE cm
    vertex_count = 0

    with open(obj_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("mtllib"):
                continue
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "v":
                obj_x, obj_y, obj_z = float(parts[1]), float(parts[2]), float(parts[3])
                ue_vertices.append(obj_to_ue(obj_x, obj_y, obj_z))
                vertex_count += 1
                if vertex_count % 1000000 == 0:
                    print(f"  ... {vertex_count} vertices", file=sys.stderr)

    print(f"  Total: {vertex_count} vertices", file=sys.stderr)

    # ------------------------------------------------------------------
    # Pass 2: Read faces, filter, group by tile + category + material
    # ------------------------------------------------------------------
    print("Pass 2: Reading and filtering faces...", file=sys.stderr)

    # tile_data: {(tx, ty, category, material): [(obj_name, [v0, v1, v2, ...]), ...]}
    tile_data = defaultdict(list)
    current_object = None
    current_material = None
    face_count = 0
    kept_faces = 0
    discarded_faces = 0
    discarded_objects = set()

    with open(obj_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("mtllib"):
                continue
            parts = line.split()
            if not parts:
                continue
            keyword = parts[0]

            if keyword == "o":
                current_object = " ".join(parts[1:]) if len(parts) > 1 else None
                current_material = None

            elif keyword == "usemtl":
                current_material = parts[1]

            elif keyword == "f" and current_object:
                face_count += 1
                if face_count % 500000 == 0:
                    print(f"  ... {face_count} faces processed", file=sys.stderr)

                if not should_keep(current_object):
                    discarded_faces += 1
                    if face_count < 100:  # Track first few discarded
                        discarded_objects.add(get_semantic_category(current_object))
                    continue

                # Parse face vertex indices
                idxs = []
                for token in parts[1:]:
                    idx_str = token.split("/")[0]
                    if idx_str:
                        idx = int(idx_str) - 1  # OBJ is 1-indexed
                        if idx < len(ue_vertices):
                            idxs.append(idx)

                if len(idxs) < 3:
                    continue  # Skip degenerate faces

                kept_faces += 1

                # Determine tile from first vertex
                v0 = ue_vertices[idxs[0]]
                tile_key = get_tile_key(v0[0], v0[1])
                category = get_semantic_category(current_object)
                ue_material = get_ue_material(current_object, current_material)

                # Group by tile location, then by material
                tile_data[(tile_key[0], tile_key[1], ue_material)].append(
                    (current_object, idxs, category)
                )

    print(f"  Processed: {face_count} faces", file=sys.stderr)
    print(f"  Kept: {kept_faces} faces", file=sys.stderr)
    print(f"  Discarded: {discarded_faces} faces", file=sys.stderr)
    if discarded_objects:
        print(f"  Discarded categories: {sorted(discarded_objects)[:20]}", file=sys.stderr)

    # ------------------------------------------------------------------
    # Pass 3: Export per-tile OBJ files (merged by tile location)
    # ------------------------------------------------------------------
    print("Pass 3: Exporting tiles...", file=sys.stderr)

    tiles_dir = os.path.join(output_dir, "tiles")
    os.makedirs(tiles_dir, exist_ok=True)

    manifest = {
        "scale_ue_cm_per_unit": SCALE_UE_CM,
        "origin_obj": list(ORIGIN_OBJ),
        "axis_transform": "OBJ(X,Y,Z) -> UE(X, -Z, Y)",
        "tile_size_units": TILE_SIZE,
        "tile_size_m": TILE_SIZE * (SCALE_UE_CM / 100),
        "total_vertices": vertex_count,
        "total_faces_kept": kept_faces,
        "total_faces_discarded": discarded_faces,
        "tiles": [],
        "materials": {},
    }

    # Build unique material list
    for (_tx, _ty, mat) in tile_data:
        if mat not in manifest["materials"]:
            manifest["materials"][mat] = {
                "asset_path": mat,
            }

    # Group by tile location, then by material within each tile
    tile_locations = defaultdict(dict)  # (tx, ty) -> {material: [(obj_name, idxs, category), ...]}
    for (tx, ty, mat), faces_list in tile_data.items():
        if (tx, ty) not in tile_locations:
            tile_locations[(tx, ty)] = {}
        tile_locations[(tx, ty)][mat] = faces_list

    # Sort tile locations for deterministic output
    sorted_locations = sorted(tile_locations.keys())

    for (tx, ty) in sorted_locations:
        material_groups = tile_locations[(tx, ty)]

        # Build unique vertex list for this tile
        vert_map = {}  # old_idx -> new_idx
        tile_verts = []
        tile_material_sections = []  # [(material, [(category, [new_idx, ...]), ...]), ...]

        for mat, faces_list in sorted(material_groups.items()):
            section_faces = []
            for _obj_name, idxs, category in faces_list:
                face_new_idxs = []
                for old_idx in idxs:
                    if old_idx not in vert_map:
                        vert_map[old_idx] = len(tile_verts)
                        tile_verts.append(ue_vertices[old_idx])
                    face_new_idxs.append(vert_map[old_idx])
                section_faces.append((category, face_new_idxs))
            tile_material_sections.append((mat, section_faces))

        # Write OBJ file with material groups
        tile_name = f"Tile_{tx:03d}_{ty:03d}"
        obj_filename = f"{tile_name}.obj"
        obj_path_out = os.path.join(tiles_dir, obj_filename)

        # Collect categories in this tile
        tile_categories = set()
        total_faces = 0
        for _mat, section in tile_material_sections:
            for cat, _face in section:
                tile_categories.add(cat)
                total_faces += 1

        with open(obj_path_out, "w") as f:
            f.write(f"# RealGazebo Bordeaux tile: {tile_name}\n")
            f.write(f"# Categories: {', '.join(sorted(tile_categories))}\n")
            f.write(f"# Materials: {len(tile_material_sections)}\n")
            f.write(f"# Faces: {total_faces}, Vertices: {len(tile_verts)}\n")
            f.write(f"o {tile_name}\n")

            for v in tile_verts:
                f.write(f"v {v[0]:.1f} {v[1]:.1f} {v[2]:.1f}\n")

            for mat, section in tile_material_sections:
                f.write(f"usemtl {mat}\n")
                for _cat, face in section:
                    f.write("f " + " ".join(str(i + 1) for i in face) + "\n")

        manifest["tiles"].append({
            "name": tile_name,
            "file": obj_filename,
            "tile_x": tx,
            "tile_y": ty,
            "categories": sorted(tile_categories),
            "materials": [mat for mat, _ in tile_material_sections],
            "face_count": total_faces,
            "vertex_count": len(tile_verts),
        })

        if len(manifest["tiles"]) % 25 == 0:
            print(f"  ... {len(manifest['tiles'])} tiles exported", file=sys.stderr)

    # Write manifest
    manifest_path = os.path.join(output_dir, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    print("\nDone!", file=sys.stderr)
    print(f"  Tiles: {len(manifest['tiles'])}", file=sys.stderr)
    print(f"  Materials: {len(manifest['materials'])}", file=sys.stderr)
    print(f"  Manifest: {manifest_path}", file=sys.stderr)
    print(f"  Tiles dir: {tiles_dir}", file=sys.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Preprocess Bordeaux OBJ for UE5.8 import")
    parser.add_argument("obj", help="Path to Bordeaux.obj")
    parser.add_argument("--output", "-o", default="bordeaux_processed",
                        help="Output directory")
    args = parser.parse_args()

    preprocess(args.obj, args.output)