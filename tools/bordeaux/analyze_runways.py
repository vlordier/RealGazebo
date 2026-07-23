#!/usr/bin/env python3
"""
Connected-component runway analyzer for Bordeaux-Mérignac OBJ.

Streams the OBJ, isolates aeroway faces, builds connected components,
computes PCA / oriented bounding rectangles, and estimates source units
per meter using known runway dimensions.

Known Bordeaux-Mérignac (LFBD) runways:
  05/23 : 3100 m x 45 m
  11/29 : 2415 m x 45 m
"""

import argparse
import json
import math
import sys
from collections import defaultdict, deque


def parse_obj_vertices_and_faces(obj_path: str, target_prefix: str = "aeroway"):
    """
    Streaming parse: read all vertices, then collect faces belonging to
    objects whose name starts with target_prefix.
    Returns (vertices, face_groups) where face_groups maps object_name -> list of face vertex indices.
    """
    vertices = []  # list of (x, y, z)
    face_groups = defaultdict(list)
    current_object = None

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
                vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))

            elif keyword == "o":
                current_object = " ".join(parts[1:]) if len(parts) > 1 else None

            elif keyword == "f" and current_object and current_object.startswith(target_prefix):
                # Parse face vertex indices (handle v/vt/vn and v//vn formats)
                face_verts = []
                for token in parts[1:]:
                    idx = token.split("/")[0]
                    if idx:
                        face_verts.append(int(idx) - 1)  # OBJ is 1-indexed
                if face_verts:
                    face_groups[current_object].append(face_verts)

    return vertices, dict(face_groups)


def build_connected_components(vertices, face_groups):
    """
    Build connected components from face groups.
    Returns list of components, each with: vertices, faces, bbox, centroid.
    """
    # Build vertex adjacency from faces
    all_faces = []
    for _obj_name, obj_faces in face_groups.items():
        all_faces.extend(obj_faces)

    if not all_faces:
        return []

    # Build vertex-to-vertex adjacency
    adj = defaultdict(set)
    for face in all_faces:
        for i in range(len(face)):
            v0 = face[i]
            v1 = face[(i + 1) % len(face)]
            adj[v0].add(v1)
            adj[v1].add(v0)

    # BFS to find connected components
    visited = set()
    components = []

    # Collect all unique vertex indices used in faces
    all_verts = set()
    for face in all_faces:
        all_verts.update(face)

    for start_v in all_verts:
        if start_v in visited:
            continue

        # BFS
        comp_verts = set()
        comp_faces = []
        queue = deque([start_v])
        visited.add(start_v)

        while queue:
            v = queue.popleft()
            comp_verts.add(v)
            for neighbor in adj.get(v, set()):
                if neighbor not in visited:
                    visited.add(neighbor)
                    queue.append(neighbor)

        # Collect faces that use only vertices in this component
        for face in all_faces:
            if all(v in comp_verts for v in face):
                comp_faces.append(face)

        if comp_faces:
            components.append({
                "vertex_indices": sorted(comp_verts),
                "faces": comp_faces,
                "vertex_count": len(comp_verts),
                "face_count": len(comp_faces),
            })

    return components


def compute_bbox(vertices, vertex_indices):
    """Axis-aligned bounding box for a set of vertex indices."""
    xs = [vertices[i][0] for i in vertex_indices]
    ys = [vertices[i][1] for i in vertex_indices]
    zs = [vertices[i][2] for i in vertex_indices]
    return {
        "min": [min(xs), min(ys), min(zs)],
        "max": [max(xs), max(ys), max(zs)],
        "extent": [max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)],
    }


def compute_pca_oriented_bbox(vertices, vertex_indices):
    """
    Compute PCA-based oriented bounding rectangle in the XZ plane (runway
    is roughly horizontal). Returns length, width, angle, and the two
    principal axes.
    """
    n = len(vertex_indices)
    if n < 3:
        return None

    # Compute centroid
    cx = sum(vertices[i][0] for i in vertex_indices) / n
    cz = sum(vertices[i][2] for i in vertex_indices) / n

    # Compute covariance in XZ
    sxx = sum((vertices[i][0] - cx) ** 2 for i in vertex_indices)
    szz = sum((vertices[i][2] - cz) ** 2 for i in vertex_indices)
    sxz = sum((vertices[i][0] - cx) * (vertices[i][2] - cz) for i in vertex_indices)

    # 2x2 covariance matrix [[sxx, sxz], [sxz, szz]]
    # Compute eigenvalues
    trace = sxx + szz
    det = sxx * szz - sxz * sxz
    disc = trace * trace - 4 * det
    if disc < 0:
        disc = 0
    sqrt_disc = math.sqrt(disc)

    lambda1 = (trace + sqrt_disc) / 2.0
    (trace - sqrt_disc) / 2.0

    # Principal eigenvector (largest eigenvalue)
    if abs(sxz) > 1e-10:
        vx = lambda1 - szz
        vz = sxz
    else:
        vx = 1.0
        vz = 0.0

    norm = math.hypot(vx, vz)
    if norm > 0:
        vx /= norm
        vz /= norm

    # Project all points onto principal axis
    projections = []
    for i in vertex_indices:
        dx = vertices[i][0] - cx
        dz = vertices[i][2] - cz
        proj = dx * vx + dz * vz
        projections.append(proj)

    min_proj = min(projections)
    max_proj = max(projections)
    length = max_proj - min_proj

    # Project onto perpendicular axis
    perp_x = -vz
    perp_z = vx
    perp_projections = []
    for i in vertex_indices:
        dx = vertices[i][0] - cx
        dz = vertices[i][2] - cz
        proj = dx * perp_x + dz * perp_z
        perp_projections.append(proj)

    min_perp = min(perp_projections)
    max_perp = max(perp_projections)
    width = max_perp - min_perp

    # Angle in degrees from X axis
    angle_deg = math.degrees(math.atan2(vz, vx))

    return {
        "length": length,
        "width": width,
        "aspect_ratio": length / width if width > 0 else float("inf"),
        "angle_deg": angle_deg,
        "centroid": [cx, vertices[vertex_indices[0]][1], cz],  # Y from first vertex
        "principal_axis": [vx, vz],
    }


def estimate_scale(components, known_runways):
    """
    Match runway-like components (high aspect ratio) to known runway
    dimensions and estimate source units per meter.
    """
    # Filter to runway-like: high aspect ratio, reasonable size
    runway_candidates = []
    for comp in components:
        pca = comp.get("pca")
        if not pca:
            continue
        if pca["aspect_ratio"] > 5 and pca["length"] > 100:
            runway_candidates.append(comp)

    # Sort by aspect ratio descending
    runway_candidates.sort(key=lambda c: c["pca"]["aspect_ratio"], reverse=True)

    results = []
    for comp in runway_candidates[:6]:  # Top 6 candidates
        pca = comp["pca"]
        results.append({
            "face_count": comp["face_count"],
            "vertex_count": comp["vertex_count"],
            "source_length": pca["length"],
            "source_width": pca["width"],
            "aspect_ratio": pca["aspect_ratio"],
            "angle_deg": pca["angle_deg"],
            "centroid": pca["centroid"],
        })

    # Estimate scale from known runways
    scales = []
    for r in results:
        for name, known_len, known_width in known_runways:
            if abs(r["aspect_ratio"] - (known_len / known_width)) < 20:
                units_per_meter = r["source_length"] / known_len
                scales.append({
                    "candidate_runway": name,
                    "source_length": r["source_length"],
                    "known_length_m": known_len,
                    "source_width": r["source_width"],
                    "known_width_m": known_width,
                    "units_per_meter": units_per_meter,
                    "units_per_meter_from_width": r["source_width"] / known_width,
                    "angle_deg": r["angle_deg"],
                })

    return results, scales


def main():
    parser = argparse.ArgumentParser(
        description="Analyze runways in Bordeaux OBJ using connected components and PCA")
    parser.add_argument("obj", help="Path to .obj file")
    parser.add_argument("--output", "-o", help="Output JSON path")
    parser.add_argument("--prefix", default="aeroway=ru",
                        help="Object name prefix for runway geometry (default: aeroway=ru)")
    args = parser.parse_args()

    print(f"Parsing vertices and {args.prefix} faces from {args.obj}...", file=sys.stderr)
    vertices, face_groups = parse_obj_vertices_and_faces(args.obj, args.prefix)
    print(f"  Vertices: {len(vertices)}", file=sys.stderr)
    print(f"  Face groups ({args.prefix}): {len(face_groups)}", file=sys.stderr)

    total_faces = sum(len(faces) for faces in face_groups.values())
    print(f"  Total {args.prefix} faces: {total_faces}", file=sys.stderr)

    print("Building connected components...", file=sys.stderr)
    components = build_connected_components(vertices, face_groups)
    print(f"  Connected components: {len(components)}", file=sys.stderr)

    # Compute bbox and PCA for each component
    for comp in components:
        comp["bbox"] = compute_bbox(vertices, comp["vertex_indices"])
        comp["pca"] = compute_pca_oriented_bbox(vertices, comp["vertex_indices"])

    # Sort by face count descending
    components.sort(key=lambda c: c["face_count"], reverse=True)

    # Known LFBD runways: (name, length_m, width_m)
    known_runways = [
        ("05/23", 3100, 45),
        ("11/29", 2415, 45),
    ]

    runway_results, scale_estimates = estimate_scale(components, known_runways)

    result = {
        "total_vertices": len(vertices),
        "total_aeroway_faces": total_faces,
        "aeroway_face_groups": len(face_groups),
        "connected_components": len(components),
        "top_components": [
            {
                "rank": i + 1,
                "face_count": comp["face_count"],
                "vertex_count": comp["vertex_count"],
                "bbox": comp["bbox"],
                "pca": comp["pca"],
            }
            for i, comp in enumerate(components[:20])
        ],
        "runway_candidates": runway_results,
        "scale_estimates": scale_estimates,
    }

    if scale_estimates:
        # Average the scale estimates
        avg_units_per_m = sum(s["units_per_meter"] for s in scale_estimates) / len(scale_estimates)
        avg_units_per_m_w = sum(s["units_per_meter_from_width"] for s in scale_estimates) / len(scale_estimates)
        result["recommended_scale"] = {
            "units_per_meter_from_length": avg_units_per_m,
            "units_per_meter_from_width": avg_units_per_m_w,
            "units_per_meter_combined": (avg_units_per_m + avg_units_per_m_w) / 2,
            "unreal_cm_per_unit": (avg_units_per_m + avg_units_per_m_w) / 2 * 100,
            "note": "Multiply OBJ coordinates by unreal_cm_per_unit to get UE centimeters",
        }
    else:
        result["recommended_scale"] = None
        result["note"] = "Could not match runway candidates to known dimensions. Check aspect ratios."

    output = json.dumps(result, indent=2, default=str)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Report written to {args.output}", file=sys.stderr)
    else:
        print(output)


if __name__ == "__main__":
    main()