#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bench: sanal IHA konumu — rakibe dogru hareket (lead pursuit testi).

Yazar: SAVASAN_BENCH_OWN_FILE (varsayilan /tmp/savasan_bench_own.env)
phase5: SAVASAN_BENCH_OWN_MOTION=1 ile okur.

Kullanım:
  export SAVASAN_BENCH_OWN_MOTION=1
  python3 scripts/fake_own_motion.py --duration 60 --speed 22
"""
from __future__ import annotations

import argparse
import math
import os
import time
from pathlib import Path

POOL_DEFAULT = "/tmp/savasan_rival_pool.env"
OUT_DEFAULT = "/tmp/savasan_bench_own.env"
EARTH_R = 6371000.0


def m_per_deg(lat_deg: float):
    return 111320.0, 111320.0 * max(0.2, math.cos(math.radians(lat_deg)))


def read_rival(path: str):
    data = {}
    p = Path(path)
    if not p.is_file():
        return None
    for line in p.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            data[k.strip()] = v.strip()
    if data.get("valid0", "0") != "1":
        return None
    return {
        "lat": float(data.get("lat0", "0")),
        "lon": float(data.get("lon0", "0")),
    }


def write_own(path: str, lat: float, lon: float, speed: float, yaw: float):
    body = (
        f"valid=1\n"
        f"lat={lat:.8f}\n"
        f"lon={lon:.8f}\n"
        f"speed_mps={speed:.2f}\n"
        f"yaw_deg={yaw:.1f}\n"
    )
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(body)
    os.replace(tmp, path)


def step_toward(own_lat, own_lon, tgt_lat, tgt_lon, speed_mps, dt_s):
    mlat, mlon = m_per_deg(own_lat)
    dn = (tgt_lat - own_lat) * mlat
    de = (tgt_lon - own_lon) * mlon
    dist = math.hypot(dn, de)
    if dist < 1.0:
        return own_lat, own_lon, 0.0
    step = min(dist, speed_mps * dt_s)
    yaw = math.degrees(math.atan2(de, dn)) % 360.0
    dn_s = step * math.cos(math.radians(yaw))
    de_s = step * math.sin(math.radians(yaw))
    return own_lat + dn_s / mlat, own_lon + de_s / mlon, yaw


def main():
    ap = argparse.ArgumentParser(description="Sanal IHA hareketi (bench lead test)")
    ap.add_argument("--duration", type=float, default=60.0)
    ap.add_argument("--hz", type=float, default=20.0)
    ap.add_argument("--speed", type=float, default=22.0, help="IHA cruise m/s")
    ap.add_argument("--start-lat", type=float, default=0.0)
    ap.add_argument("--start-lon", type=float, default=0.0)
    ap.add_argument("--rival-pool", default=POOL_DEFAULT)
    ap.add_argument("--out", default=OUT_DEFAULT)
    args = ap.parse_args()

    lat, lon = args.start_lat, args.start_lon
    period = 1.0 / max(1.0, args.hz)
    t0 = time.monotonic()
    print(f"[fake-own] out={args.out} speed={args.speed}m/s duration={args.duration}s")

    while True:
        t = time.monotonic() - t0
        if t >= args.duration:
            break
        rival = read_rival(args.rival_pool)
        if rival is not None:
            lat, lon, yaw = step_toward(lat, lon, rival["lat"], rival["lon"], args.speed, period)
        else:
            yaw = 0.0
        write_own(args.out, lat, lon, args.speed, yaw)
        time.sleep(period)

    print("[fake-own] bitti.")


if __name__ == "__main__":
    main()
