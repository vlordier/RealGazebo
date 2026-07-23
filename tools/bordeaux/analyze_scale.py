#!/usr/bin/env python3
"""Analyze Bordeaux OBJ runway geometry to determine scale."""
import argparse
import json
import math
import sys
from collections import defaultdict


def analyze_scale(obj_path: str) -> dict:
    vertices = []
    runway_faces = []
    current = None
    
    with open(obj_path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
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
    for f in top_faces:
        verts = [vertices[idx] for idx in f]
        n = len(verts)
        cx = sum(v[0] for v in verts) / n
        cz = sum(v[2] for v in verts) / n
        edges = []
        for i in range(n):
            j = (i + 1) % n
            dx = verts[i][0] - verts[j][0]
            dz = verts[i][2] - verts[j][2]
            edges.append(math.hypot(dx, dz))
        edges.sort()
        face_data.append((cx, cz, edges[0], edges[-1]))
    
    # Separate into two runways by Z coordinate
    rwy1 = [(cx, cz, w, ln) for cx, cz, w, ln in face_data if cz > 5000]
    rwy2 = [(cx, cz, w, ln) for cx, cz, w, ln in face_data if cz <= 5000]
    
    result = {
        "total_top_faces": len(top_faces),
        "runway_ne": {"faces": len(rwy1), "z_range": "Z>5000"},
        "runway_sw": {"faces": len(rwy2), "z_range": "Z<=5000"},
    }
    
    for rwy_faces_list, rwy_name in [(rwy1, "runway_ne"), (rwy2, "runway_sw")]:
        if not rwy_faces_list:
            continue
        
        # PCA on face centroids
        xs = [cx for cx, _, _, _ in rwy_faces_list]
        zs = [cz for _, cz, _, _ in rwy_faces_list]
        n = len(xs)
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
        
        entry = {
            "face_count": len(rwy_faces_list),
            "pca_length": round(length, 1),
            "pca_width": round(width, 1),
            "pca_aspect_ratio": round(length / width, 1) if width > 0 else 0,
            "pca_angle_deg": round(angle, 1),
            "centroid": [round(cx, 1), round(cz, 1)],
            "x_range": [round(min(xs), 1), round(max(xs), 1)],
            "z_range": [round(min(zs), 1), round(max(zs), 1)],
            "scale_estimates": {}
        }
        
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
    
    # Also analyze face widths to determine scale
    all_widths = [w for _, _, w, _ in face_data]
    width_counter = defaultdict(int)
    for w in all_widths:
        width_counter[round(w)] += 1
    
    # Find the most common face width (should be the runway quad width)
    meaningful_widths = {k: v for k, v in width_counter.items() if 20 <= k <= 60}
    if meaningful_widths:
        peak_width = max(meaningful_widths, key=meaningful_widths.get)
        result["face_width_analysis"] = {
            "most_common_width": peak_width,
            "count_at_peak": meaningful_widths[peak_width],
            "all_widths": dict(sorted(width_counter.items())),
        }
        
        # If peak_width is the runway quad width, and the runway is 45m wide,
        # we need to know how many quads span the width
        # Let's check by looking at faces at the same Z
        z_groups = defaultdict(list)
        for cx, cz, w, _l in face_data:
            z_key = round(cz / 5) * 5
            z_groups[z_key].append((cx, w))
        
        # Find Z groups with multiple faces (these span the runway width)
        multi_face_groups = {k: v for k, v in z_groups.items() if len(v) > 1}
        if multi_face_groups:
            # For each multi-face group, compute the X span
            x_spans = []
            for _z_key, group in multi_face_groups.items():
                xs_in_group = [cx for cx, _ in group]
                x_span = max(xs_in_group) - min(xs_in_group)
                x_spans.append(x_span)
            
            avg_x_span = sum(x_spans) / len(x_spans)
            result["face_width_analysis"]["multi_face_z_groups"] = len(multi_face_groups)
            result["face_width_analysis"]["avg_x_span_at_same_z"] = round(avg_x_span, 1)
            
            # The avg_x_span should be the runway width
            # units_per_m = avg_x_span / 45
            units_per_m = avg_x_span / 45.0
            result["face_width_analysis"]["scale_from_width"] = {
                "avg_x_span": round(avg_x_span, 1),
                "assumed_runway_width_m": 45,
                "units_per_meter": round(units_per_m, 4),
                "ue_cm_per_unit": round(units_per_m * 100, 2),
            }
    
    return result

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("obj", help="Path to OBJ file")
    parser.add_argument("--output", "-o", help="Output JSON path")
    args = parser.parse_args()
    
    result = analyze_scale(args.obj)
    
    output = json.dumps(result, indent=2)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Report written to {args.output}", file=sys.stderr)
    else:
        print(output)