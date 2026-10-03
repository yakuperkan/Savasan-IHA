#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Telemetri tahmin CSV → SVG (ortak ölçek + hata grafiği)."""
from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path

PRED_DEFAULT = "/tmp/telem_pred.csv"
TRUTH_DEFAULT = "/tmp/savasan_rival_truth.csv"


def read_csv(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def fval(row, key, default=0.0):
    try:
        return float(row[key])
    except (KeyError, ValueError, TypeError):
        return default


def ll_to_xy(lat, lon, ref_lat, ref_lon):
    m_per_deg_lat = 111320.0
    m_per_deg_lon = 111320.0 * max(0.2, math.cos(math.radians(ref_lat)))
    return (lon - ref_lon) * m_per_deg_lon, (lat - ref_lat) * m_per_deg_lat


def project_all(points, w, h, pad=50):
    if not points:
        return []
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    span_x = max(max_x - min_x, 20.0)
    span_y = max(max_y - min_y, 20.0)
    scale = min((w - 2 * pad) / span_x, (h - 2 * pad) / span_y)
    cx = (min_x + max_x) * 0.5
    cy = (min_y + max_y) * 0.5
    return [((w * 0.5 + (x - cx) * scale), (h * 0.5 - (y - cy) * scale)) for x, y in points]


def polyline_svg(pts, color, width=2, dash=None):
    if len(pts) < 2:
        return ""
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    coords = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    return (
        f'<polyline fill="none" stroke="{color}" stroke-width="{width}"{dash_attr} '
        f'points="{coords}"/>\n'
    )


def scatter_svg(pts, color, r=2, step=1):
    return "".join(
        f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{color}"/>\n'
        for i, (x, y) in enumerate(pts) if i % step == 0
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pred", default=PRED_DEFAULT)
    ap.add_argument("--truth", default=TRUTH_DEFAULT)
    ap.add_argument("--save", default="/tmp/telem_pred.svg")
    args = ap.parse_args()

    pred_path = Path(args.pred)
    truth_path = Path(args.truth)
    if not pred_path.is_file() or not truth_path.is_file():
        print("HATA: pred veya truth CSV eksik", file=sys.stderr)
        sys.exit(1)

    pred = read_csv(pred_path)
    truth = read_csv(truth_path)
    if not pred:
        print("HATA: pred CSV bos", file=sys.stderr)
        sys.exit(1)

    ref_lat = fval(pred[0], "own_lat")
    ref_lon = fval(pred[0], "own_lon")

    truth_xy = [ll_to_xy(fval(r, "truth_lat"), fval(r, "truth_lon"), ref_lat, ref_lon) for r in truth]
    rx_truth_xy = [ll_to_xy(fval(r, "rx_lat"), fval(r, "rx_lon"), ref_lat, ref_lon) for r in truth]
    dr_xy = [ll_to_xy(fval(r, "rival_dr_lat"), fval(r, "rival_dr_lon"), ref_lat, ref_lon) for r in pred]
    aim_xy = [ll_to_xy(fval(r, "aim_lat"), fval(r, "aim_lon"), ref_lat, ref_lon) for r in pred]
    own_xy = ll_to_xy(fval(pred[0], "own_lat"), fval(pred[0], "own_lon"), ref_lat, ref_lon)

    # Ortak ölçek — tum seriler ayni cercevede
    all_pts = truth_xy + rx_truth_xy + dr_xy + aim_xy + [own_xy]
    w, h = 920, 380
    proj = project_all(all_pts, w, h)
    n_t, n_rx = len(truth_xy), len(rx_truth_xy)
    truth_s = proj[:n_t]
    rx_s = proj[n_t : n_t + n_rx]
    dr_s = proj[n_t + n_rx : n_t + n_rx + len(dr_xy)]
    aim_s = proj[n_t + n_rx + len(dr_xy) : n_t + n_rx + len(dr_xy) + len(aim_xy)]
    own_s = proj[-1]

    uniq_rx = len({(fval(r, "rival_rx_lat"), fval(r, "rival_rx_lon")) for r in pred})
    lead_n = sum(1 for r in pred if fval(r, "used_lead") >= 0.5)
    lead_pct = 100.0 * lead_n / len(pred)
    yak_n = sum(1 for r in pred if r.get("phase") == "YAKLASMA")

    # DR hata (m) — truth ile RX paketi eslestirerek (index hizasi YANLISTI)
    err_m = []
    truth_by_rx = {}
    for r in truth:
        key = (fval(r, "rx_lat"), fval(r, "rx_lon"))
        truth_by_rx[key] = r
    for r in pred:
        key = (fval(r, "rival_rx_lat"), fval(r, "rival_rx_lon"))
        trow = truth_by_rx.get(key)
        if trow is None:
            continue
        tlat, tlon = fval(trow, "truth_lat"), fval(trow, "truth_lon")
        dlat, dlon = fval(r, "rival_dr_lat"), fval(r, "rival_dr_lon")
        tx, ty = ll_to_xy(tlat, tlon, ref_lat, ref_lon)
        dx, dy = ll_to_xy(dlat, dlon, ref_lat, ref_lon)
        err_m.append(math.hypot(tx - dx, ty - dy))

    avg_err = sum(err_m) / len(err_m) if err_m else 0.0
    max_err = max(err_m) if err_m else 0.0
    lag_truth_rx = []
    for t in truth:
        tx, ty = ll_to_xy(fval(t, "truth_lat"), fval(t, "truth_lon"), ref_lat, ref_lon)
        rx, ry = ll_to_xy(fval(t, "rx_lat"), fval(t, "rx_lon"), ref_lat, ref_lon)
        lag_truth_rx.append(math.hypot(tx - rx, ty - ry))
    rx_lag = sum(lag_truth_rx) / len(lag_truth_rx) if lag_truth_rx else 0.0

    err_w, err_h = 920, 120
    err_pts = []
    if err_m:
        emax = max(max(err_m), 1.0)
        for i, e in enumerate(err_m):
            x = 40 + (err_w - 80) * i / max(1, len(err_m) - 1)
            y = err_h - 20 - (err_h - 40) * (e / emax)
            err_pts.append((x, y))

    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h + err_h + 90}" viewBox="0 0 {w} {h + err_h + 90}">
  <rect width="100%" height="100%" fill="#1a1a1e"/>
  <text x="16" y="22" fill="#e8e8e8" font-family="monospace" font-size="13">
    YAKLASMA={yak_n} | lead=%{lead_pct:.0f} | pred_rx_cesit={uniq_rx} | DR hata ort={avg_err:.1f}m max={max_err:.1f}m
  </text>
  <text x="16" y="38" fill="#888" font-family="monospace" font-size="11">
    cyan RX ~{rx_lag:.0f}m geride (stale) | DR truth'a ~{avg_err:.0f}m (duzeltme calisiyor) | lead=%{lead_pct:.0f}
  </text>
  <rect x="10" y="48" width="{w-20}" height="{h-10}" fill="#252528" stroke="#444"/>
  {polyline_svg(truth_s, "#4ade80", 2)}
  {polyline_svg(rx_s, "#22d3ee", 2, dash="8,5")}
  {scatter_svg(dr_s, "#60a5fa", 2, step=max(1, len(dr_s)//120))}
  {scatter_svg(aim_s, "#f87171", 2, step=max(1, len(aim_s)//120))}
  <polygon points="{own_s[0]:.1f},{own_s[1]:.1f} {own_s[0]-8:.1f},{own_s[1]+12:.1f} {own_s[0]+8:.1f},{own_s[1]+12:.1f}" fill="#fbbf24"/>
  <text x="16" y="{h + 52}" fill="#aaa" font-family="monospace" font-size="11">
    yesil=truth | cyan=RX gecikmeli | mavi=DR | kirmizi=lead aim | sari=own
  </text>
  <text x="16" y="{h + 68}" fill="#ccc" font-family="monospace" font-size="12">DR konum hatasi (m) — truth'a gore</text>
  <rect x="10" y="{h + 74}" width="{err_w-20}" height="{err_h-10}" fill="#252528" stroke="#444"/>
  {polyline_svg(err_pts, "#f472b6", 2)}
</svg>
'''
    Path(args.save).write_text(svg, encoding="utf-8")
    print(f"Kaydedildi: {args.save} | pred={len(pred)} truth={len(truth)} uniq_rx={uniq_rx} lead%={lead_pct:.0f}")


if __name__ == "__main__":
    main()
