#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fake rakip telemetri — truth CSV + gecikmeli rival_pool.env.

Yollar:
  linear  — düz hat (eski)
  weave   — sinusoidal S-manivela (kontrol testi icin onerilen)
  circle  — dairesel orbit

Kullanım:
  python3 scripts/fake_rival_telemetry.py --path weave --duration 60 --stale-ms 1200
"""
from __future__ import annotations

import argparse
import math
import os
import time
from pathlib import Path

EARTH_R = 6371000.0
POOL_DEFAULT = "/tmp/savasan_rival_pool.env"
TRUTH_DEFAULT = "/tmp/savasan_rival_truth.csv"
HUB_DEFAULT = "/tmp/savasan_alc_hub.env"


def m_per_deg(lat_deg: float):
    mlat = 111320.0
    mlon = 111320.0 * max(0.2, math.cos(math.radians(lat_deg)))
    return mlat, mlon


def offset_m_to_ll(own_lat: float, own_lon: float, north_m: float, east_m: float):
    mlat, mlon = m_per_deg(own_lat)
    return own_lat + north_m / mlat, own_lon + east_m / mlon


def predict_latlon(lat_deg: float, lon_deg: float, speed_mps: float, hdg_deg: float, dt_s: float):
    if dt_s <= 0 or speed_mps <= 0:
        return lat_deg, lon_deg
    dt_s = min(dt_s, 2.0)
    h = math.radians(hdg_deg)
    dist = speed_mps * dt_s
    dlat = math.degrees(dist * math.cos(h) / EARTH_R)
    lat_rad = math.radians(lat_deg)
    dlon = math.degrees(dist * math.sin(h) / (EARTH_R * max(1e-6, math.cos(lat_rad))))
    return lat_deg + dlat, lon_deg + dlon


def read_hub_own():
    path = Path(os.environ.get("SAVASAN_ALC_HUB_FILE", HUB_DEFAULT))
    lat, lon = 0.0, 0.0
    if path.is_file():
        data = {}
        for line in path.read_text().splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                data[k.strip()] = v.strip()
        try:
            lat = float(data.get("enlem", "0"))
            lon = float(data.get("boylam", "0"))
        except ValueError:
            lat, lon = 0.0, 0.0
    if abs(lat) < 1e-6 and abs(lon) < 1e-6:
        return 0.0, 0.0
    return lat, lon


def write_pool(path: str, target_id: int, lat: float, lon: float, alt: float, spd: float,
               hdg: float, stale_ms: int):
    body = (
        f"count=1\n"
        f"valid0=1\n"
        f"id0={target_id}\n"
        f"lat0={lat:.8f}\n"
        f"lon0={lon:.8f}\n"
        f"alt0={alt:.1f}\n"
        f"spd0={spd:.2f}\n"
        f"pitch0=0\n"
        f"roll0=0\n"
        f"hdg0={hdg:.1f}\n"
        f"tdiff0={stale_ms}\n"
    )
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(body)
    os.replace(tmp, path)


def truth_state(path_mode: str, t: float, own_lat: float, own_lon: float, args):
    """t aninda truth konum + anlik hiz/heading."""
    br = math.radians(args.bearing_deg)
    base_n = args.start_range_m * math.cos(br)
    base_e = args.start_range_m * math.sin(br)

    if path_mode == "linear":
        lat, lon = offset_m_to_ll(own_lat, own_lon, base_n, base_e)
        lat, lon = predict_latlon(lat, lon, args.speed, args.heading, t)
        return lat, lon, args.speed, args.heading

    if path_mode == "weave":
        # Ileriye dogru + yanal sinus (S-manivela)
        fwd = args.speed * t
        amp = args.weave_amp_m
        period = max(4.0, args.weave_period_s)
        lat_off = amp * math.sin(2.0 * math.pi * t / period)
        # Yerel ENU: bearing yonunde ileri, sola-saga lat_off
        perp_n = math.cos(br + math.pi / 2.0)
        perp_e = math.sin(br + math.pi / 2.0)
        north = base_n + fwd * math.cos(br) + lat_off * perp_n
        east = base_e + fwd * math.sin(br) + lat_off * perp_e
        lat, lon = offset_m_to_ll(own_lat, own_lon, north, east)
        # Anlik hiz vektoru (türev)
        dt = 0.05
        lat2, lon2 = offset_m_to_ll(
            own_lat, own_lon,
            base_n + args.speed * (t + dt) * math.cos(br) + amp * math.sin(2 * math.pi * (t + dt) / period) * perp_n,
            base_e + args.speed * (t + dt) * math.sin(br) + amp * math.sin(2 * math.pi * (t + dt) / period) * perp_e,
        )
        mlat, mlon = m_per_deg(own_lat)
        vn = (lat2 - lat) / dt * mlat
        ve = (lon2 - lon) / dt * mlon
        spd = math.hypot(vn, ve)
        hdg = math.degrees(math.atan2(ve, vn)) % 360.0
        return lat, lon, max(1.0, spd), hdg

    if path_mode == "circle":
        omega = 2.0 * math.pi / max(8.0, args.circle_period_s)
        ang = omega * t
        r = args.circle_radius_m
        cn = base_n + r * math.cos(ang)
        ce = base_e + r * math.sin(ang)
        lat, lon = offset_m_to_ll(own_lat, own_lon, cn, ce)
        # Teget hiz
        vn = -r * omega * math.sin(ang)
        ve = r * omega * math.cos(ang)
        spd = math.hypot(vn, ve)
        hdg = math.degrees(math.atan2(ve, vn)) % 360.0
        return lat, lon, max(1.0, spd), hdg

    if path_mode == "headon":
        # Own'a dogru duz yaklasma (lead pursuit bench)
        hdg = (args.bearing_deg + 180.0) % 360.0
        lat, lon = offset_m_to_ll(own_lat, own_lon, base_n, base_e)
        lat, lon = predict_latlon(lat, lon, args.speed, hdg, t)
        return lat, lon, args.speed, hdg

    raise ValueError(f"bilinmeyen path: {path_mode}")


def main():
    ap = argparse.ArgumentParser(description="Fake rakip telemetri simülatörü")
    ap.add_argument("--path", choices=("linear", "weave", "circle", "headon"), default="weave")
    ap.add_argument("--duration", type=float, default=45.0)
    ap.add_argument("--hz", type=float, default=10.0)
    ap.add_argument("--stale-ms", type=int, default=1200)
    ap.add_argument("--speed", type=float, default=16.0)
    ap.add_argument("--heading", type=float, default=55.0)
    ap.add_argument("--start-range-m", type=float, default=420.0)
    ap.add_argument("--bearing-deg", type=float, default=65.0)
    ap.add_argument("--weave-amp-m", type=float, default=80.0, help="S-manivela genlik (m)")
    ap.add_argument("--weave-period-s", type=float, default=14.0, help="S-manivela periyot (s)")
    ap.add_argument("--circle-radius-m", type=float, default=120.0)
    ap.add_argument("--circle-period-s", type=float, default=24.0)
    ap.add_argument("--pool", default=POOL_DEFAULT)
    ap.add_argument("--truth-csv", default=TRUTH_DEFAULT)
    ap.add_argument("--own-lat", type=float, default=None)
    ap.add_argument("--own-lon", type=float, default=None)
    args = ap.parse_args()

    own_lat, own_lon = read_hub_own()
    if args.own_lat is not None:
        own_lat = args.own_lat
    if args.own_lon is not None:
        own_lon = args.own_lon

    stale_s = args.stale_ms / 1000.0
    truth_path = Path(args.truth_csv)
    with truth_path.open("w", encoding="utf-8") as truth_fp:
        truth_fp.write("t_s,truth_lat,truth_lon,rx_lat,rx_lon,stale_ms,speed_mps,heading_deg,path\n")

        t0 = time.monotonic()
        period = 1.0 / max(0.5, args.hz)
        print(f"[fake-rival] path={args.path} own=({own_lat:.6f},{own_lon:.6f}) "
              f"range={args.start_range_m}m stale={args.stale_ms}ms duration={args.duration}s")
        print(f"[fake-rival] pool={args.pool} truth={args.truth_csv}")

        while True:
            t = time.monotonic() - t0
            if t >= args.duration:
                break
            truth_lat, truth_lon, spd, hdg = truth_state(args.path, t, own_lat, own_lon, args)
            rx_lat, rx_lon, rx_spd, rx_hdg = truth_state(
                args.path, max(0.0, t - stale_s), own_lat, own_lon, args
            )
            write_pool(args.pool, 99, rx_lat, rx_lon, 80.0, rx_spd, rx_hdg, args.stale_ms)
            truth_fp.write(
                f"{t:.3f},{truth_lat:.8f},{truth_lon:.8f},{rx_lat:.8f},{rx_lon:.8f},"
                f"{args.stale_ms},{spd:.2f},{hdg:.1f},{args.path}\n"
            )
            truth_fp.flush()
            time.sleep(period)

    print("[fake-rival] bitti.")


if __name__ == "__main__":
    main()
