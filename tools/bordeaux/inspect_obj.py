#!/usr/bin/env python3
"""
Streaming OBJ inspector for Bordeaux-Mérignac airport model.

Reads the 1.1 GB OBJ file line-by-line without loading all geometry into memory.
Outputs a JSON summary: vertex/face counts, object hierarchy, material usage,
bounding boxes per semantic category, and statistics about tiny/large objects.
"""

import argparse
import json
import os
import sys
from collections import defaultdict


def inspect_obj(obj_path: str) -> dict:
    """Stream-inspect a Wavefront OBJ file, returning aggregate statistics."""
    if not os.path.isfile(obj_path):
        raise FileNotFoundError(f"OBJ file not found: {obj_path}")

    file_size = os.path.getsize(obj_path)

    result = {
        "file": obj_path,
        "file_size_bytes": file_size,
        "file_size_gb": file_size / (1024 ** 3),
        "vertices": 0,
        "faces": 0,
        "normals": 0,
        "texcoords": 0,
        "objects": 0,
        "materials": set(),
        "bbox": {
            "min": [float("inf"), float("inf"), float("inf")],
            "max": [float("-inf"), float("-inf"), float("-inf")],
        },
        "categories": defaultdict(lambda: {
            "objects": 0,
            "vertices": 0,
            "faces": 0,
            "bbox": {
                "min": [float("inf"), float("inf"), float("inf")],
                "max": [float("-inf"), float("-inf"), float("-inf")],
            },
        }),
        "object_stats": {
            "total": 0,
            "with_faces": 0,
            "empty": 0,
            "largest_objects": [],
        },
        "material_usage": defaultdict(int),
        "semantic_prefixes": defaultdict(int),
    }

    current_object = None
    current_material = None
    object_face_count = 0
    object_bbox_min = [float("inf"), float("inf"), float("inf")]
    object_bbox_max = [float("-inf"), float("-inf"), float("-inf")]

    # Track per-object stats for "largest" reporting
    per_object_faces = []

    with open(obj_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("mtllib"):
                continue

            parts = line.split()
            if not parts:
                continue

            keyword = parts[0]

            if keyword == "v":
                result["vertices"] += 1
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                bbox = result["bbox"]
                if x < bbox["min"][0]: bbox["min"][0] = x  # noqa: E701
                if y < bbox["min"][1]: bbox["min"][1] = y  # noqa: E701
                if z < bbox["min"][2]: bbox["min"][2] = z  # noqa: E701
                if x > bbox["max"][0]: bbox["max"][0] = x  # noqa: E701
                if y > bbox["max"][1]: bbox["max"][1] = y  # noqa: E701
                if z > bbox["max"][2]: bbox["max"][2] = z  # noqa: E701

                # Track per-object vertex bbox
                if current_object:
                    if x < object_bbox_min[0]: object_bbox_min[0] = x  # noqa: E701
                    if y < object_bbox_min[1]: object_bbox_min[1] = y  # noqa: E701
                    if z < object_bbox_min[2]: object_bbox_min[2] = z  # noqa: E701
                    if x > object_bbox_max[0]: object_bbox_max[0] = x  # noqa: E701
                    if y > object_bbox_max[1]: object_bbox_max[1] = y  # noqa: E701
                    if z > object_bbox_max[2]: object_bbox_max[2] = z  # noqa: E701

            elif keyword == "vn":
                result["normals"] += 1

            elif keyword == "vt":
                result["texcoords"] += 1

            elif keyword == "usemtl":
                current_material = parts[1]
                result["materials"].add(current_material)

            elif keyword == "o":
                # Finalize previous object
                if current_object is not None:
                    _finalize_object(result, current_object, object_face_count,
                                     object_bbox_min, object_bbox_max,
                                     per_object_faces)

                current_object = " ".join(parts[1:]) if len(parts) > 1 else "unnamed"
                result["objects"] += 1
                object_face_count = 0
                object_bbox_min = [float("inf"), float("inf"), float("inf")]
                object_bbox_max = [float("-inf"), float("-inf"), float("-inf")]

                # Extract semantic prefix (key before '=' in key=value format)
                if "=" in current_object:
                    semantic = current_object.split("=")[0]
                elif "_" in current_object:
                    semantic = current_object.split("_")[0]
                else:
                    semantic = current_object
                result["semantic_prefixes"][semantic] += 1

            elif keyword == "f":
                result["faces"] += 1
                object_face_count += 1
                if current_material:
                    result["material_usage"][current_material] += 1

    # Finalize last object
    if current_object is not None:
        _finalize_object(result, current_object, object_face_count,
                         object_bbox_min, object_bbox_max,
                         per_object_faces)

    # Sort largest objects
    per_object_faces.sort(key=lambda x: x[1], reverse=True)
    result["object_stats"]["largest_objects"] = [
        {"name": name, "faces": faces}
        for name, faces in per_object_faces[:20]
    ]

    # Convert sets to sorted lists for JSON
    result["materials"] = sorted(result["materials"])
    result["semantic_prefixes"] = dict(sorted(result["semantic_prefixes"].items(),
                                               key=lambda x: x[1], reverse=True))

    # Convert category bbox min/max from list to list
    for cat in result["categories"]:
        result["categories"][cat]["bbox"]["min"] = result["categories"][cat]["bbox"]["min"]
        result["categories"][cat]["bbox"]["max"] = result["categories"][cat]["bbox"]["max"]

    result["categories"] = dict(result["categories"])
    result["material_usage"] = dict(result["material_usage"])

    return result


def _finalize_object(result, obj_name, face_count, bbox_min, bbox_max, per_object_faces):
    """Record per-object stats and update category aggregates."""
    result["object_stats"]["total"] += 1
    if face_count > 0:
        result["object_stats"]["with_faces"] += 1
        per_object_faces.append((obj_name, face_count))
    else:
        result["object_stats"]["empty"] += 1

    # Update category
    if "=" in obj_name:
        semantic = obj_name.split("=")[0]
    elif "_" in obj_name:
        semantic = obj_name.split("_")[0]
    else:
        semantic = obj_name
    cat = result["categories"][semantic]
    cat["objects"] += 1
    cat["faces"] += face_count

    if bbox_min[0] != float("inf"):
        for i in range(3):
            if bbox_min[i] < cat["bbox"]["min"][i]:
                cat["bbox"]["min"][i] = bbox_min[i]
            if bbox_max[i] > cat["bbox"]["max"][i]:
                cat["bbox"]["max"][i] = bbox_max[i]


def main():
    parser = argparse.ArgumentParser(
        description="Stream-inspect a large Wavefront OBJ file")
    parser.add_argument("obj", help="Path to .obj file")
    parser.add_argument("--output", "-o", help="Output JSON path (default: stdout)")
    args = parser.parse_args()

    result = inspect_obj(args.obj)

    output = json.dumps(result, indent=2, default=str)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Report written to {args.output}", file=sys.stderr)
    else:
        print(output)


if __name__ == "__main__":
    main()
