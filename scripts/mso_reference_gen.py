#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""MSO Python modullerinden altin (golden) referans cikti uretir.

C++ portlarinin dogrulanmasi icin kullanilir: ayni deterministik senaryo
Python'da kosulur, cikti CSV'ye yazilir, C++ testi ayni senaryoyu kosup
satir satir karsilastirir.

MSO kaynagi DEGISTIRILMEZ; buradan sadece import edilir.

Kullanim:
    python3 scripts/mso_reference_gen.py --mso-dir /home/nvidia/Desktop/MSO \\
        --out 02_Ana_Sistem_CPP/tests/fixtures

Uretilen dosyalar:
    mso_hedef_kestirici_ref.csv   - HedefKestirici.tahmin() cikti serisi
"""

import argparse
import math
import os
import sys

# Referans noktasi: MSO senaryolariyla ayni sahada kalmak icin sabit.
REF_LAT = 40.230000
REF_LON = 29.010000

# Kestirici varsayilanlari (HedefKestirici.__init__ ile ayni).
BORU_GECIKMESI_S = 0.6
ALPHA, BETA, GAMMA = 0.65, 0.55, 0.45


class Cerceve:
    """gorev_beyni.Cerceve ile birebir (import zinciri kurmamak icin kopya)."""

    def __init__(self, lat0, lon0):
        self.lat0 = lat0
        self.lon0 = lon0
        self.k_lat = 110540.0
        self.k_lon = 111320.0 * math.cos(math.radians(lat0))

    def yerel(self, lat, lon):
        return ((lon - self.lon0) * self.k_lon, (lat - self.lat0) * self.k_lat)

    def cografi(self, x, y):
        return (self.lat0 + y / self.k_lat, self.lon0 + x / self.k_lon)


def senaryo_olcumleri():
    """Deterministik rakip yorungesi -> olcum listesi.

    Uc asama: duz ucus, sabit donus, donusu ters cevirme. Boylece hem sabit
    donus orani modeli hem oynaklik guveni tetiklenir. Ayrica sunucu tekrari
    (ayni kaydin iki kez gelmesi) ve degisken zaman_farki taklit edilir.
    """
    cerceve = Cerceve(REF_LAT, REF_LON)
    olcumler = []

    x, y = 300.0, 200.0
    alt = 95.0
    hiz = 18.0
    yon = 90.0  # pusula: dogu

    t_olcum = 0.0
    adim = 0.5  # rakip 2 Hz basiyor
    for i in range(60):
        if i < 20:
            omega = 0.0
        elif i < 40:
            omega = 12.0  # saga sabit donus
        else:
            omega = -18.0  # sert ters donus

        yon = (yon + omega * adim) % 360.0
        yr = math.radians(yon)
        x += hiz * math.sin(yr) * adim
        y += hiz * math.cos(yr) * adim
        alt += 0.4 * math.sin(i * 0.2)

        t_olcum += adim
        lat, lon = cerceve.cografi(x, y)

        # Sunucu veri yasi 0.3-0.7 sn arasi salinir.
        zaman_farki_ms = 300 + (i % 5) * 100
        # Jetson'a varis ani: olcum ani + boru gecikmesi + sunucu yasi.
        t_alis = t_olcum + BORU_GECIKMESI_S + zaman_farki_ms / 1000.0

        olcumler.append({
            'lat': lat, 'lon': lon, 'alt': alt,
            'hiz': hiz, 'yon': yon,
            'zaman_farki_ms': zaman_farki_ms,
            't_alis': t_alis,
        })
        # Sunucu her 4. adimda ayni kaydi tekrar dondursun (tekrar tespiti).
        if i % 4 == 3:
            tekrar = dict(olcumler[-1])
            tekrar['t_alis'] += 0.25
            tekrar['zaman_farki_ms'] += 250
            olcumler.append(tekrar)

    return olcumler


def uret_hedef_kestirici(mso_dir, out_path):
    sys.path.insert(0, mso_dir)
    from hedef_kestirici import HedefKestirici  # noqa: E402

    cerceve = Cerceve(REF_LAT, REF_LON)
    kestirici = HedefKestirici(cerceve, boru_gecikmesi_s=BORU_GECIKMESI_S,
                               alpha=ALPHA, beta=BETA, gamma=GAMMA)

    satirlar = []
    for o in senaryo_olcumleri():
        kabul = kestirici.olcum_ekle(o['lat'], o['lon'], o['alt'], o['hiz'],
                                     o['yon'], o['zaman_farki_ms'], o['t_alis'],
                                     hedef_id=1)
        # Kendi komut gecikmemiz icin 0.3 sn ileri sarma ile tahmin.
        tahmin = kestirici.tahmin(o['t_alis'], ek_ileri_s=0.3)
        gecerli = kestirici.gecerli_mi(o['t_alis'])
        if tahmin is None:
            satirlar.append((o['lat'], o['lon'], o['alt'], o['hiz'], o['yon'],
                             o['zaman_farki_ms'], o['t_alis'],
                             int(kabul), 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0))
        else:
            satirlar.append((o['lat'], o['lon'], o['alt'], o['hiz'], o['yon'],
                             o['zaman_farki_ms'], o['t_alis'],
                             int(kabul), 1, int(gecerli),
                             tahmin['x'], tahmin['y'], tahmin['irtifa'],
                             tahmin['hiz'], tahmin['yon'], tahmin['donus_orani']))

    basliklar = ('lat,lon,alt,hiz,yon,zaman_farki_ms,t_alis,'
                 'kabul,tahmin_var,gecerli,x,y,irtifa,tahmin_hiz,tahmin_yon,donus_orani')

    def hucre(v):
        # %.17g double round-trip garantisi verir; daha az basamak C++ tarafinda
        # metre altinda sapma dogurur ve karsilastirma anlamsizlasir.
        return '%d' % v if isinstance(v, int) else '%.17g' % v

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('# MSO hedef_kestirici.py altin referansi — scripts/mso_reference_gen.py uretir\n')
        f.write('# ref_lat=%.6f ref_lon=%.6f boru_gecikmesi=%.3f alpha=%.2f beta=%.2f gamma=%.2f\n'
                % (REF_LAT, REF_LON, BORU_GECIKMESI_S, ALPHA, BETA, GAMMA))
        f.write(basliklar + '\n')
        for s in satirlar:
            f.write(','.join(hucre(v) for v in s) + '\n')
    return len(satirlar)


def uret_takip_gudum(mso_dir, out_path):
    """TakipGudum.tik() ve kilit_geometrisi() cikti serisi.

    Kestiriciden bagimsiz olsun diye hedef durumu dogrudan uretilir: boylece
    bu referans yalnizca gudumu sinar, kestirim hatasi karismaz.
    """
    sys.path.insert(0, mso_dir)
    from takip_gudum import TakipGudum, KilitSayaci  # noqa: E402

    cerceve = Cerceve(REF_LAT, REF_LON)
    gudum = TakipGudum(cerceve)
    sayac = KilitSayaci()

    # Biz: dogu yonunde ucuyoruz, hedefe yaklasiyoruz.
    bx, by, balt = 0.0, 0.0, 100.0
    bhiz = 17.0
    btrack = 0.0  # matematiksel aci: +x (dogu)

    # Hedef: onumuzde, once duz sonra donerek kaciyor.
    hx, hy, halt = 260.0, 40.0, 105.0
    hhiz = 15.0
    hyon = 80.0  # pusula

    dt = 0.25
    t = 0.0
    satirlar = []
    for i in range(160):
        if i < 40:
            omega = 0.0
        elif i < 90:
            omega = 10.0
        else:
            omega = -22.0

        hyon = (hyon + omega * dt) % 360.0
        hyr = math.radians(hyon)
        hvx = hhiz * math.sin(hyr)   # dogu
        hvy = hhiz * math.cos(hyr)   # kuzey
        hx += hvx * dt
        hy += hvy * dt
        halt += 0.3 * math.sin(i * 0.15)

        hedef = {
            'x': hx, 'y': hy, 'irtifa': halt,
            'vx': hvx, 'vy': hvy, 'vz': 0.0,
            'hiz': hhiz, 'yon': hyon, 'donus_orani': omega,
        }
        biz = {'x': bx, 'y': by, 'irtifa': balt, 'hiz': bhiz, 'track': btrack}

        geo = gudum.kilit_geometrisi(biz, hedef)
        niyet = gudum.tik(t, biz, hedef)
        vurus = bool(geo and geo.get('vurus_alaninda'))
        kilit_s = sayac.guncelle(t, vurus)

        if niyet is None:
            satirlar.append((t, bx, by, balt, bhiz, btrack,
                             hx, hy, halt, hvx, hvy, hhiz, hyon, omega,
                             0, 0, 0.0, 0.0, 0, 0, 0,
                             1 if geo else 0,
                             geo['menzil'] if geo else 0.0,
                             geo['oran'] if geo else 0.0,
                             int(vurus), kilit_s))
        else:
            nx, ny = cerceve.yerel(niyet['enlem'], niyet['boylam'])
            satirlar.append((t, bx, by, balt, bhiz, btrack,
                             hx, hy, halt, hvx, hvy, hhiz, hyon, omega,
                             1, {'YOK': 0, 'YAKLASMA': 1, 'TAKIP': 2, 'KOPMA': 3}[gudum.asama],
                             nx, ny, niyet['irtifa'], niyet['motor'], niyet['aci'],
                             1 if geo else 0,
                             geo['menzil'] if geo else 0.0,
                             geo['oran'] if geo else 0.0,
                             int(vurus), kilit_s))

            # Bizi nisan noktasina dogru sinirli donus hiziyla ilerlet ki
            # senaryo kapali dongu olsun ve KOPMA/TCA dallari tetiklensin.
            istek = math.atan2(ny - by, nx - bx)
            fark = (istek - btrack + math.pi) % (2 * math.pi) - math.pi
            w_max = 9.81 * math.tan(math.radians(22.0)) / max(5.0, bhiz)
            btrack += max(-w_max * dt, min(w_max * dt, fark))
            balt += max(-3.0 * dt, min(3.0 * dt, niyet['irtifa'] - balt))
        bx += bhiz * math.cos(btrack) * dt
        by += bhiz * math.sin(btrack) * dt
        t += dt

    basliklar = ('t,bx,by,balt,bhiz,btrack,hx,hy,halt,hvx,hvy,hhiz,hyon,donus_orani,'
                 'niyet_var,asama,nisan_x,nisan_y,irtifa,motor,aci,'
                 'geo_var,geo_menzil,geo_oran,vurus_alaninda,kilit_s')

    def hucre(v):
        return '%d' % v if isinstance(v, int) else '%.17g' % v

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('# MSO takip_gudum.py altin referansi — scripts/mso_reference_gen.py uretir\n')
        f.write('# ref_lat=%.6f ref_lon=%.6f\n' % (REF_LAT, REF_LON))
        f.write(basliklar + '\n')
        for s in satirlar:
            f.write(','.join(hucre(v) for v in s) + '\n')
    return len(satirlar)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mso-dir', default='/home/nvidia/Desktop/MSO')
    ap.add_argument('--out', default='02_Ana_Sistem_CPP/tests/fixtures')
    args = ap.parse_args()

    if not os.path.isdir(args.mso_dir):
        print('MSO dizini yok: %s' % args.mso_dir, file=sys.stderr)
        return 1
    os.makedirs(args.out, exist_ok=True)

    hedef = os.path.join(args.out, 'mso_hedef_kestirici_ref.csv')
    n = uret_hedef_kestirici(args.mso_dir, hedef)
    print('hedef_kestirici referansi: %d satir -> %s' % (n, hedef))

    gudum = os.path.join(args.out, 'mso_takip_gudum_ref.csv')
    n = uret_takip_gudum(args.mso_dir, gudum)
    print('takip_gudum referansi: %d satir -> %s' % (n, gudum))
    return 0


if __name__ == '__main__':
    sys.exit(main())
