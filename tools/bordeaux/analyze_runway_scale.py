#!/usr/bin/env python3
"""Analyze Bordeaux OBJ runway geometry to determine scale."""
import argparse
import json
import math
import sys
from collections import Counter


def analyze_runway_scale(obj_path: str) -> dict:
    vertices = []
    runway_faces = []
    current = None
    
    with open(obj_path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if not parts:
                continue
            if parts[0] == 'v':
                vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif parts[0] == 'o':
                current = ' '.join(parts[1:])
            elif parts[0] == 'f' and current == 'aeroway=ru':
                runway_faces.append([int(p.split('/')[0]) - 1 for p in parts[1:]])
    
    # Get top surface faces (Y=0.5)
    top_faces = []
    for f in runway_faces:
        verts = [vertices[idx] for idx in f]
        if all(abs(v[1] - 0.5) < 0.01 for v in verts):
            top_faces.append(f)
    
    # For each top face, compute centroid and edge lengths
    face_data = []
    for _f in top_faces:
        verts = [vertices[idx] for idx in f]
        cx = sum(v[0] for v in verts) / 4
        cz = sum(v[2] for v in verts) / 4
        edges = []
        for i in range(4):
            j = (i + 1) % 4
            dx = verts[i][0] - verts[j][0]
            dz = verts[i][2] - verts[j][2]
            edges.append(math.hypot(dx, dz))
        edges.sort()
        face_data.append((cx, cz, edges[0], edges[-1]))
    
    # Separate into two runways by Z coordinate
    # Runway 05/23 is at Z ~21000, runway 11/29 is at Z ~-6000 to 3000
    rwy1_faces = [(cx, cz, w, ln) for cx, cz, w, ln in face_data if cz > 5000]
    rwy2_faces = [(cx, cz, w, ln) for cx, cz, w, ln in face_data if cz <= 5000]
    
    result = {
        "total_top_faces": len(top_faces),
        "runway_1_ne": {"faces": len(rwy1_faces), "centroid_z_range": "Z>5000"},
        "runway_2_sw": {"faces": len(rwy2_faces), "centroid_z_range": "Z<=5000"},
    }
    
    for rwy_faces_list, rwy_name in [(rwy1_faces, "runway_ne"), (rwy2_faces, "runway_sw")]:
        if not rwy_faces_list:
            continue
        
        # Get all unique vertex indices
        vert_indices = set()
        for _f in top_faces:
            # Find which top faces belong to this runway
            for tf in top_faces:
                verts = [vertices[idx] for idx in tf]
                tcz = sum(v[2] for v in verts) / 4
                if (rwy_name == "runway_ne" and tcz > 5000) or \
                   (rwy_name == "runway_sw" and tcz <= 5000):
                    vert_indices.update(tf)
        
        # Recompute from actual faces
        rwy_verts = [vertices[i] for i in vert_indices]
        xs = [v[0] for v in rwy_verts]
        zs = [v[2] for v in rwy_verts]
        n = len(rwy_verts)
        
        # PCA
        cx = sum(xs) / n
        cz = sum(zs) / n
        sxx = sum((x - cx)**2 for x in xs)
        szz = sum((z - cz)**2 for z in zs)
        sxz = sum((x - cx) * (z - cz) for x, z in zip(xs, zs, strict=False))
        
        trace = sxx + szz
        det = sxx * szz - sxz * sxz
        disc = max(0, trace * trace - 4 * det)
        sqrt_disc = math.sqrt(disc)
        lambda1 = (trace + sqrt_disc) / 2.0
        
        if abs(sxz) > 1e-10:
            vx = lambda1 - szz
            vz = sxz
        else:
            vx, vz = 1.0, 0.0
        norm = math.hypot(vx, vz)
        vx /= norm
        vz /= norm
        
        projs = [(x - cx) * vx + (z - cz) * vz for x, z in zip(xs, zs, strict=False)]
        perp = [(x - cx) * (-vz) + (z - cz) * vx for x, z in zip(xs, zs, strict=False)]
        
        length = max(projs) - min(projs)
        width = max(perp) - min(perp)
        angle = math.degrees(math.atan2(vz, vx))
        
        # Width analysis from individual face short edges
        widths = [w for _, _, w, _ in rwy_faces_list]
        width_counter = Counter(round(w) for w in widths)
        most_common_width = width_counter.most_common(1)[0][0]
        
        entry = {
            "vertices": n,
            "pca_length": round(length, 1),
            "pca_width": round(width, 1),
            "pca_aspect_ratio": round(length / width, 1) if width > 0 else 0,
            "pca_angle_deg": round(angle, 1),
            "centroid": [round(cx, 1), round(cz, 1)],
            "x_range": [round(min(xs), 1), round(max(xs), 1)],
            "z_range": [round(min(zs), 1), round(max(zs), 1)],
            "most_common_face_width": most_common_width,
            "scale_estimates": {}
        }
        
        # Try both runway assignments
        for rwy_label, known_len, known_wid in [("05/23", 3100, 45), ("11/29", 2415, 45)]:
            units_per_m_len = length / known_len
            units_per_m_wid = width / known_wid
            entry["scale_estimates"][rwy_label] = {
                "units_per_meter_len": round(units_per_m_len, 4),
                "units_per_meter_wid": round(units_per_m_wid, 4),
                "ue_cm_per_unit_len": round(units_per_m_len * 100, 2),
                "ue_cm_per_unit_wid": round(units_per_m_wid * 100, 2),
            }
        
        result[rwy_name] = entry
    
    # Determine recommended scale
    # Use the face width as the most reliable indicator
    all_widths = [w for _, _, w, _ in face_data]
    width_counter = Counter(round(w) for w in all_widths)
    # Filter to meaningful widths (between 20 and 60 units)
    meaningful_widths = {k: v for k, v in width_counter.items() if 20 <= k <= 60}
    if meaningful_widths:
        peak_width = max(meaningful_widths, key=meaningful_widths.get)
        # Runway is 45m wide, so units_per_m = peak_width / 45
        units_per_m = peak_width / 45.0
        result["recommended_scale"] = {
            "method": "face_width_analysis",
            "peak_face_width_units": peak_width,
            "assumed_runway_width_m": 45,
            "units_per_meter": round(units_per_m, 4),
            "unreal_cm_per_unit": round(units_per_m * 100, 2),
            "description": f"Most common top-face short edge is {peak_width} units, assuming 45m runway width"
        }
    
    return result

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("obj", help="Path to OBJ file")
    parser.add_argument("--output", "-o", help="Output JSON path")
    args = parser.parse_args()
    
    result = analyze_runway_scale(args.obj)
    
    output = json.dumps(result, indent=2)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Report written to {args.output}", file=sys.stderr)
    else:
        print(output)