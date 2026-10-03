# -*- coding: utf-8 -*-
"""
============================================================
 ALPAGULINK — Jetson RF Köprüsü (AIR_LOCK)
============================================================
Bu dosya SADECE telsiz (RFD) köprüsüdür. Karar vermez, uçağa
komut basmaz, Alacakart seri portunu (ACM0) AÇMAZ.

ACM0'ın tek sahibi C++ tarafındaki AlcLinkBridge'dir. İki süreç
aynı portu açarsa kartın çerçeve akışı bozulur; bu yüzden veri
alışverişi /tmp altındaki IPC dosyaları üzerinden yapılır:

  OKUNAN (C++ yazar)
    savasan_alc_hub.env    telemetri + kilit + hedef pikseli
    savasan_lock.state     "0"/"1" — kilit bitişini yakalamak için

  YAZILAN (C++ okur)
    savasan_rival_pool.env rakip telemetrisi havuzu (0x20)
    savasan_hss_rf.env     HSS listesi (0x24)
    savasan_boundary_rf.env uçuş sınırı köşeleri (0x25)
    savasan_goto.env       operatör varış noktası (0x2A / 0x2D)
    savasan_alc_uplink.bin  karta iletilecek ham bayt (varsa)

------------------------------------------------------------
 VARIŞ NOKTASI (0x2A)
------------------------------------------------------------
Operatör haritada bir noktaya tıklar; nokta karta YAZILMAZ,
Jetson güdümü kendi uçurur. Şartname §6.1.7 bu komutu otonomiyi
bozmayan komutlar arasında sayar. Nokta önce güdümün geofence
süzgecinden geçer; kabul/red sonucu 0x33 ile geri bildirilir.

------------------------------------------------------------
 KAPSAM DIŞI
------------------------------------------------------------
Kamikaze/QR bu workspace'te yok (ayrı Kamikaze workspace'i).
Kart görev listesi yükleme (0x10/0x12) de yok: ALC kütüphane
protokolü ister, o da ACM0 demektir. Tekil varış noktası için
0x2A kullanılır ve ACM0'a hiç ihtiyaç duymaz.

Çalıştırma: config/systemd/savasan-alpagulink.sh
============================================================
"""

import os
import struct
import threading
import time

import serial

# ============================================================
# AYARLAR
# ============================================================
RFD_PORT = os.environ.get('SAVASAN_RFD_PORT', '/dev/ttyUSB0')
RFD_BAUD = int(os.environ.get('SAVASAN_RFD_BAUD', '57600'))
MAX_PAYLOAD = 150

HUB_FILE = os.environ.get('SAVASAN_ALC_HUB_FILE', '/tmp/savasan_alc_hub.env')
LOCK_FILE = os.environ.get('SAVASAN_LOCK_STATE_FILE', '/tmp/savasan_lock.state')
RIVAL_POOL_FILE = os.environ.get('SAVASAN_RIVAL_POOL_FILE', '/tmp/savasan_rival_pool.env')
HSS_RF_FILE = os.environ.get('SAVASAN_HSS_RF_FILE', '/tmp/savasan_hss_rf.env')
BOUNDARY_RF_FILE = os.environ.get('SAVASAN_BOUNDARY_RF_FILE', '/tmp/savasan_boundary_rf.env')
GOTO_FILE = os.environ.get('SAVASAN_GOTO_FILE', '/tmp/savasan_goto.env')

DOWNLINK_HZ = float(os.environ.get('SAVASAN_ALPAGULINK_HZ', '10'))
# Bu süre boyunca tazelenmeyen rakip havuzdan düşer: C++ tarafı dosyayı
# okuduğu anı "alınma zamanı" sayar, yani bayatlığı ancak burada eleyebiliriz.
RIVAL_TTL_S = float(os.environ.get('SAVASAN_RIVAL_TTL_S', '3.0'))

rfd = serial.Serial(RFD_PORT, RFD_BAUD, timeout=0.1)
tx_lock = threading.Lock()

rakipler = {}          # target_id -> (alan sözlüğü, alınma zamanı)
rakip_kilidi = threading.Lock()

goto_sira = 0          # her yeni noktada artar; 0x33 ACK'i buna göre eşleşir
goto_kilidi = threading.Lock()


# ============================================================
# TELSİZ YARDIMCILARI
# ============================================================
def calc_checksum(data):
    crc = 0
    for b in data:
        crc ^= b
    return crc


def send_to_gcs(msg_id, payload):
    header = struct.pack('<BBBB', 0xAA, 0x55, msg_id, len(payload))
    checksum = calc_checksum(header[2:] + payload)
    packet = header + payload + struct.pack('<B', checksum)
    with tx_lock:
        rfd.write(packet)
        rfd.flush()


def durum_bildir(metin):
    """0x02: karargahta log satırı."""
    try:
        send_to_gcs(0x02, metin.encode('utf-8')[:120])
    except Exception as e:
        print(f"[HATA - DURUM RAPORU] {e}")


def _u8(x):
    return max(0, min(255, int(x)))


# ============================================================
# IPC DOSYALARI
# ============================================================
def env_dosya_oku(yol):
    try:
        with open(yol, 'r') as f:
            satirlar = f.read().splitlines()
    except OSError:
        return None
    veri = {}
    for satir in satirlar:
        if not satir or satir.startswith('#') or '=' not in satir:
            continue
        anahtar, _, deger = satir.partition('=')
        veri[anahtar] = deger
    return veri


def env_dosya_yaz(yol, govde):
    """Atomik yazım: C++ tarafı yarım dosya okumasın."""
    gecici = yol + '.tmp'
    try:
        with open(gecici, 'w') as f:
            f.write(govde)
            f.flush()
            os.fsync(f.fileno())
        os.replace(gecici, yol)
        return True
    except OSError as e:
        print(f"[HATA - IPC YAZIM] {yol}: {e}")
        return False


def _f(veri, anahtar, varsayilan=0.0):
    try:
        return float(veri.get(anahtar, varsayilan))
    except (TypeError, ValueError):
        return varsayilan


def _i(veri, anahtar, varsayilan=0):
    try:
        return int(float(veri.get(anahtar, varsayilan)))
    except (TypeError, ValueError):
        return varsayilan


# ============================================================
# NOKTA HEDEFİ GERİ BİLDİRİMİ (0x33)
# ============================================================
GOTO_DURUM_ADI = {0: 'BOSTA', 1: 'AKTIF', 2: 'VARILDI', 3: 'REDDEDILDI'}
GOTO_RED_ADI = {
    0: '-',
    1: 'saha disi',
    2: 'HSS icinde',
    3: 'rota kesiyor',
    4: 'sinir verisi yok',
    5: 'GPS yok',
}

_goto_son_bildirim = None  # (durum, sira) — aynı sonucu tekrar basmamak için


def _goto_durum_bildir(hub):
    """Güdümün nokta kararı değiştiyse operatöre tek paket bas.

    Durum kalıcıdır (varıldı/reddedildi yeni nokta gelene kadar durur), bu
    yüzden telsiz paketi kaçsa bile bir sonraki değişimde yakalanır; yine de
    aynı sonucu 10 Hz tekrarlamamak için son bildirim hatırlanır.
    """
    global _goto_son_bildirim
    durum = _i(hub, 'goto_state')
    sira = _i(hub, 'goto_seq')
    sebep = _i(hub, 'goto_reason')
    if (durum, sira) == _goto_son_bildirim:
        return
    _goto_son_bildirim = (durum, sira)
    if durum == 0:
        return
    try:
        send_to_gcs(0x33, struct.pack('<BBB', durum, sira, sebep))
    except Exception as e:
        print(f"[HATA - NOKTA ACK] {e}")
        _goto_son_bildirim = None
        return
    ad = GOTO_DURUM_ADI.get(durum, str(durum))
    if durum == 3:
        print(f"[NOKTA] sira={sira} {ad}: {GOTO_RED_ADI.get(sebep, sebep)}")
    else:
        print(f"[NOKTA] sira={sira} {ad}")


# ============================================================
# DOWNLINK — hub dosyasını oku, karargaha bas
# ============================================================
def downlink_task():
    print(f"[SİSTEM] Downlink açıldı ({DOWNLINK_HZ:.0f} Hz, kaynak: {HUB_FILE})")
    periyot = 1.0 / max(DOWNLINK_HZ, 1.0)
    sayac = 0
    hub_uyarisi_verildi = False

    while True:
        try:
            hub = env_dosya_oku(HUB_FILE)
            if hub is None:
                if not hub_uyarisi_verildi:
                    print(f"[UYARI] Hub dosyası yok: {HUB_FILE} — C++ tarafı çalışıyor mu?")
                    hub_uyarisi_verildi = True
                time.sleep(periyot)
                continue
            hub_uyarisi_verildi = False

            lat = _f(hub, 'enlem')
            lon = _f(hub, 'boylam')
            alt = _f(hub, 'irtifa_m')
            roll = _f(hub, 'roll_deg')
            pitch = _f(hub, 'pitch_deg')
            hdg = int(_f(hub, 'yaw_deg')) % 360
            spd = _u8(_f(hub, 'gps_hiz_mps'))
            bat = _u8(_f(hub, 'voltaj_v') * 10)
            # Ham kart mod kodu: 0 manuel, 1 denge, 2 otonom, ... 7 seyir.
            mod_id = _u8(_i(hub, 'arac_modu'))
            sats = _u8(_i(hub, 'uydu_sayisi'))
            kilit = 1 if _i(hub, 'lock') else 0
            _goto_durum_bildir(hub)
            hx = _i(hub, 'hedef_x')
            hy = _i(hub, 'hedef_y')
            hw = _i(hub, 'hedef_w')
            hh = _i(hub, 'hedef_h')

            payload = struct.pack('<fffffHBBBBBHHHH',
                                  lat, lon, alt, roll, pitch, hdg, spd, bat,
                                  mod_id, sats, kilit, hx, hy, hw, hh)
            send_to_gcs(0x01, payload)

            sayac += 1
            if sayac % 20 == 0:
                print(f"[TELEMETRİ] İrtifa: {alt:.1f} m | Uydu: {sats} | "
                      f"Mod: {mod_id} | Kilit: {kilit}")

            time.sleep(periyot)
        except Exception as e:
            print(f"[HATA - DOWNLINK] {e}")
            time.sleep(1)


# ============================================================
# KİLİT İZLEYİCİ — 1->0 geçişinde karargaha 0x31
# ============================================================
def kilit_izleyici_task():
    print(f"[SİSTEM] Kilit izleyici açıldı ({LOCK_FILE})")
    onceki = 0
    while True:
        try:
            try:
                with open(LOCK_FILE, 'r') as f:
                    simdiki = 1 if f.read().strip() == '1' else 0
            except OSError:
                simdiki = 0

            if simdiki != onceki:
                if simdiki == 1:
                    print("[KİLİT] Başladı.")
                else:
                    send_to_gcs(0x31, b'')
                    print("[KİLİT] Bitti, karargaha 0x31 basıldı.")
                onceki = simdiki

            time.sleep(0.05)
        except Exception as e:
            print(f"[HATA - KİLİT İZLEYİCİ] {e}")
            time.sleep(1)


# ============================================================
# İSTİHBARAT — karargahtan gelir, IPC dosyasına yazılır
# ============================================================
def rakip_havuzunu_yaz():
    """Bayatlayanları eleyip havuzu diske basar."""
    simdi = time.time()
    with rakip_kilidi:
        for tid in [k for k, (_, t) in rakipler.items() if simdi - t > RIVAL_TTL_S]:
            del rakipler[tid]
        kayitlar = [r for r, _ in rakipler.values()]

    satirlar = [f"count={len(kayitlar)}"]
    for i, r in enumerate(kayitlar):
        satirlar += [
            f"valid{i}=1",
            f"id{i}={r['id']}",
            f"lat{i}={r['lat']:.9f}",
            f"lon{i}={r['lon']:.9f}",
            f"alt{i}={r['alt']:.3f}",
            f"spd{i}={r['spd']:.3f}",
            f"pitch{i}={r['pitch']:.3f}",
            f"roll{i}={r['roll']:.3f}",
            f"hdg{i}={r['hdg']:.3f}",
            f"tdiff{i}={r['tdiff']}",
        ]
    env_dosya_yaz(RIVAL_POOL_FILE, '\n'.join(satirlar) + '\n')


def parse_target_data(payload):
    """0x20: rakip telemetrisi (33 bayt)."""
    try:
        lat, lon, alt, spd, pitch, roll, hdg, time_diff, target_id = \
            struct.unpack('<fffffffiB', payload)
    except struct.error as e:
        print(f"[HATA - HEDEF ÇÖZÜMLEME] {e}")
        return

    with rakip_kilidi:
        rakipler[int(target_id)] = ({
            'id': int(target_id),
            'lat': float(lat),
            'lon': float(lon),
            'alt': float(alt),
            'spd': float(spd),
            'pitch': float(pitch),
            'roll': float(roll),
            'hdg': float(hdg),
            'tdiff': int(time_diff),
        }, time.time())
    rakip_havuzunu_yaz()


def parse_intel_hss(payload):
    """0x24: HSS listesi. Boş liste önbelleği temizler."""
    if len(payload) < 1:
        return
    adet = payload[0]
    liste = []
    offset = 1
    for _ in range(adet):
        if offset + 13 <= len(payload):
            h_id, lat, lon, rad = struct.unpack_from('<Bfff', payload, offset)
            liste.append((int(h_id), float(lat), float(lon), float(rad)))
            offset += 13

    satirlar = [f"count={len(liste)}"]
    for i, (h_id, lat, lon, rad) in enumerate(liste):
        satirlar += [f"id{i}={h_id}", f"lat{i}={lat:.9f}",
                     f"lon{i}={lon:.9f}", f"rad{i}={rad:.3f}"]
    env_dosya_yaz(HSS_RF_FILE, '\n'.join(satirlar) + '\n')

    print(f"[İSTİHBARAT] {len(liste)} HSS yazıldı.")
    for h in liste:
        print(f"   -> HSS {h[0]}: {h[1]:.6f}, {h[2]:.6f} | r={h[3]:.0f} m")
    durum_bildir(f"HSS alindi ({len(liste)} adet)")


def parse_goto(payload):
    """0x2A: operatörün haritadan gönderdiği varış noktası.

    8 bayt  = enlem + boylam (float32)
    10 bayt = enlem + boylam + irtifa (uint16, metre)

    Güvenlik süzgeci burada DEĞİL güdümde çalışır: sınır ve HSS verisinin
    yerel düzlemdeki karşılığı orada tutuluyor. Sonuç 0x33 ile geri döner.
    """
    global goto_sira
    if len(payload) == 8:
        lat, lon = struct.unpack('<ff', payload)
        irtifa = None
    elif len(payload) == 10:
        lat, lon, irtifa = struct.unpack('<ffH', payload)
    else:
        print(f"[HATA - NOKTA] beklenen 8 veya 10 bayt, gelen {len(payload)}")
        return

    with goto_kilidi:
        goto_sira = (goto_sira + 1) % 256
        sira = goto_sira

    satirlar = [
        'valid=1',
        f'seq={sira}',
        f'lat={float(lat):.9f}',
        f'lon={float(lon):.9f}',
        f'has_alt={1 if irtifa is not None else 0}',
        f'alt_m={float(irtifa) if irtifa is not None else 0.0:.1f}',
    ]
    env_dosya_yaz(GOTO_FILE, '\n'.join(satirlar) + '\n')

    ek = f" irtifa={irtifa} m" if irtifa is not None else ""
    print(f"[NOKTA] sira={sira} hedef={lat:.6f},{lon:.6f}{ek}")


def parse_goto_cancel():
    """0x2D: aktif varış noktasını iptal et, uçak normal göreve dönsün."""
    env_dosya_yaz(GOTO_FILE, 'valid=0\n')
    print("[NOKTA] iptal edildi")
    durum_bildir("NOKTA iptal")


def parse_intel_boundary(payload):
    """0x25: uçuş sınırı köşeleri."""
    if len(payload) < 1:
        return
    adet = payload[0]
    koseler = []
    offset = 1
    for _ in range(adet):
        if offset + 8 <= len(payload):
            lat, lon = struct.unpack_from('<ff', payload, offset)
            koseler.append((float(lat), float(lon)))
            offset += 8

    if len(koseler) < 3:
        print("[UYARI] Sınır en az 3 köşe olmalı, yok sayıldı.")
        return

    satirlar = [f"count={len(koseler)}"]
    for i, (lat, lon) in enumerate(koseler):
        satirlar += [f"lat{i}={lat:.9f}", f"lon{i}={lon:.9f}"]
    env_dosya_yaz(BOUNDARY_RF_FILE, '\n'.join(satirlar) + '\n')

    print(f"[İSTİHBARAT] {len(koseler)} köşeli uçuş sınırı yazıldı.")
    durum_bildir(f"SINIR alindi ({len(koseler)} kose)")


# ============================================================
# UPLINK — emir çözücü
# ============================================================
def uplink_task():
    print("[SİSTEM] Uplink kanalı açıldı.")
    state = 0
    msg_id = 0
    length = 0
    payload = bytearray()

    while True:
        data = rfd.read(rfd.in_waiting or 1)
        if not data:
            continue
        for b in data:
            if state == 0:
                if b == 0xAA:
                    state = 1
            elif state == 1:
                if b == 0x55:
                    state = 2
                elif b == 0xAA:
                    state = 1
                else:
                    state = 0
            elif state == 2:
                msg_id = b
                state = 3
            elif state == 3:
                length = b
                if length > MAX_PAYLOAD:
                    state = 0
                else:
                    payload = bytearray()
                    state = 4 if length > 0 else 5
            elif state == 4:
                payload.append(b)
                if len(payload) == length:
                    state = 5
            elif state == 5:
                if calc_checksum(bytes([msg_id, length]) + payload) == b:
                    if msg_id == 0x20 and length == 33:
                        parse_target_data(payload)
                    elif msg_id == 0x24:
                        parse_intel_hss(payload)
                    elif msg_id == 0x25:
                        parse_intel_boundary(payload)
                    elif msg_id == 0x2A:
                        parse_goto(payload)
                    elif msg_id == 0x2D and length == 0:
                        parse_goto_cancel()
                state = 0


# ============================================================
# BAKIM — rakip havuzunu tazele (yeni paket gelmese de bayatlar düşsün)
# ============================================================
def bakim_task():
    while True:
        time.sleep(1.0)
        try:
            with rakip_kilidi:
                bos = not rakipler
            if not bos:
                rakip_havuzunu_yaz()
        except Exception as e:
            print(f"[HATA - BAKIM] {e}")


# ============================================================
if __name__ == '__main__':
    print("=" * 62)
    print(" ALPAGULINK RF KÖPRÜSÜ")
    print(f" Telsiz     : {RFD_PORT} @ {RFD_BAUD}")
    print(f" Hub        : {HUB_FILE}")
    print(" Alacakart  : ACILMIYOR (tek sahip C++ AlcLinkBridge)")
    print(" Bekleniyor : karargahtan SINIR (0x25), HSS (0x24), HEDEF (0x20)")
    print("=" * 62)

    threading.Thread(target=downlink_task, daemon=True).start()
    threading.Thread(target=uplink_task, daemon=True).start()
    threading.Thread(target=kilit_izleyici_task, daemon=True).start()
    threading.Thread(target=bakim_task, daemon=True).start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[SİSTEM] Kapatılıyor...")
        rfd.close()
