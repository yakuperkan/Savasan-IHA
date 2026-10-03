import struct
import serial
from serial.tools import list_ports
import time
import threading
import queue
from contextlib import contextmanager

class SeriIletisim:
    PLATFORM_MODLARI = {
        0: ["Manuel Mod", "Denge Modu", "Otonom Mod", "Otonom Eve Dönüş", "Eğitim Modu", "Aktif Takip Modu", "Serial Mod"],
        1: ["Manuel Mod", "Denge Modu", "Otonom Mod", "Otonom Eve Dönüş", "Eğitim Modu", "Aktif Takip Modu", "Serial Mod"],
        2: ["Manuel Mod", "Otonom Mod", "Otonom Eve Dönüş", "Serial Mod"],
        3: ["Manuel Mod", "Denge Modu", "Otonom Mod", "Otonom Eve Dönüş", "Serial Mod"]
    }
    
    def __init__(self, port, Baudrate):
        self.port = port
        self.Baudrate = Baudrate
        self.seri = None
        self.kilit = threading.Lock()
        self.baglanti_hatasi = None
        self._bagli = False
        self._yeniden_baglanma_suresi = 1.0
        self._son_baglanma_deneme = 0.0

        self._hiz_izleme_zamani = 0.0
        self._hiz_izleme_sayaci = 0
        self._aktif_veri_hizi = 0.0
        self._gorev_sifirlama_timeout_s = 30.0
        self._gorev_sifirlama_tekrar_hz = 5.0
        self._gorev_gonderim_periyot_s = 0.1
        self._gorev_gonderim_onay_timeout_s = 2.0
        self._gorev_gonderim_onay_tekrar_hz = 20.0
        self._gorev_gonderim_max_deneme = 3
        self.veri_yapisi = [
            ('kontrol_bayragi', 1),
            ('arac_modu', 1),
            ('sistem_durum', 1),
            ('arac_id', 1),
            ('enlem', 4),
            ('boylam', 4),
            ('uydu_sayisi', 1),
            ('gps_hiz', 1),
            ('pusula', 2),
            ('kordinat_sayisi', 1),
            ('otonom_gorev_sirasi', 1),
            ('secim_biti', 1),
            ('yukseklik', 2),
            ('ek_veri_1', 2),
            ('ek_veri_2', 2),
            ('ek_veri_3', 2),
            ('sabit_deger', 1)
        ]
        
        self.donulecek_alanlar = [
            'arac_modu', 'sistem_durum', 'arac_id', 'enlem', 'boylam',
            'uydu_sayisi', 'gps_hiz', 'pusula', '_kordinat_sayisi', 'gorev_sayisi', 'otonom_gorev_sirasi',
            'x_eksen', 'y_eksen', 'z_eksen',
            'secim_biti', 'yukseklik', 'voltaj', 'akim', 'harc_akim', 'motor_durumu',
            'gps_durum', 'arac_tipi', 'gps_saat', 'gps_dakika', 'gps_saniye',
            'gidilen_yol', 'temp', 'gps_gun', 'gps_ay', 'gps_yil'
        ]
        
        self.toplam_bayt = 32
        self.arac_verisi = {}
        self.son_ek_veriler = {
            'yukseklik': None, 'voltaj': None, 'akim': None, 'harc_akim': None,
            'motor_durumu': None, 'gps_durum': None, 'arac_tipi': None,
            'gidilen_yol': None, 'temp': None, 'gps_gun': None, 'gps_ay': None, 'gps_yil': None
        }
        self.veri_kuyrugu = queue.Queue(maxsize=5)
        self.okuma_thread = None
        self.sistem_aktif = False
        self.son_veri = None
        self.veri_sayaci = 0
        self.son_veri_zamani = time.time()
        self.baslangic_zamani = time.time()
        self._baglan()
        self._async_baslat()

    def _port_sistemde_var_mi(self):
        try:
            mevcut_portlar = {p.device for p in list_ports.comports()}
            return self.port in mevcut_portlar
        except Exception:
            return True

    def _baglan(self):
        try:
            if self.seri and self.seri.is_open:
                return True

            self.seri = serial.Serial(self.port, self.Baudrate, timeout=0.1)
            self.seri.reset_input_buffer()
            self.baglanti_hatasi = None
            self._bagli = True
            return True
        except Exception as e:
            self.baglanti_hatasi = str(e)
            self._bagli = False
            self.seri = None
            return False

    def _baglanti_koptu_isaretle(self, hata=None):
        self._bagli = False
        if hata:
            self.baglanti_hatasi = str(hata)
        if self.seri:
            try:
                if self.seri.is_open:
                    self.seri.close()
            except Exception:
                pass
            self.seri = None

    def is_available(self):
        if not self.seri or not self._bagli:
            return False
        if not self.seri.is_open:
            self._baglanti_koptu_isaretle("Seri port kapalı")
            return False
        if not self._port_sistemde_var_mi():
            self._baglanti_koptu_isaretle("USB port sistemde görünmüyor")
            return False
        return True

    def _yeniden_baglanmayi_dene(self):
        simdi = time.time()
        if simdi - self._son_baglanma_deneme < self._yeniden_baglanma_suresi:
            return False

        self._son_baglanma_deneme = simdi
        if not self._port_sistemde_var_mi():
            self.baglanti_hatasi = f"{self.port} portu sistemde bulunamadı"
            return False

        return self._baglan()

    def veri_oku(self):
        if not self.is_available():
            return None

        with self.kilit:
            try:
                deneme = 0
                while self.seri.in_waiting >= self.toplam_bayt and deneme < 50:
                    deneme += 1
                    bak = self.seri.read(1)
                    if bak[0] == 84:
                        kalan = self.toplam_bayt - 1
                        kalan_bytes = self.seri.read(kalan)
                        if len(kalan_bytes) == kalan:
                            paket = bak + kalan_bytes
                            if len(paket) == self.toplam_bayt:
                                if paket[31] == 42:
                                    cozulmus = self.uzun_paket_coz(paket)
                                    if cozulmus:
                                        self.arac_verisi = cozulmus
                                        return cozulmus
                                    else:
                                        self.arac_verisi = {}
                                        return None
                            else:
                                self.seri.reset_input_buffer()
                self.arac_verisi = {}
                return None
            except (serial.SerialException, OSError) as e:
                self._baglanti_koptu_isaretle(e)
                self.arac_verisi = {}
                return None

    def uzun_paket_coz(self, veri_bayt):
        if len(veri_bayt) != self.toplam_bayt:
            return None

        try:
            cozulmus_veri = {}
            cozulmus_veri['kontrol_bayragi'] = veri_bayt[0]
            arac_modu_kod = veri_bayt[1]
            cozulmus_veri['_arac_modu_kodu'] = arac_modu_kod
            cozulmus_veri['sistem_durum'] = veri_bayt[2]
            cozulmus_veri['arac_id'] = veri_bayt[3]
            enlem_int = struct.unpack('<I', veri_bayt[4:8])[0]
            cozulmus_veri['enlem'] = enlem_int / 1000000.0
            boylam_int = struct.unpack('<I', veri_bayt[8:12])[0]
            cozulmus_veri['boylam'] = boylam_int / 1000000.0
            cozulmus_veri['uydu_sayisi'] = veri_bayt[12]
            cozulmus_veri['gps_hiz'] = veri_bayt[13]
            pusula_int = struct.unpack('<H', veri_bayt[14:16])[0]
            cozulmus_veri['pusula'] = pusula_int
            kordinat_sayisi = veri_bayt[16]
            cozulmus_veri['_kordinat_sayisi'] = kordinat_sayisi
            if kordinat_sayisi == 0:
                cozulmus_veri['gorev_sayisi'] = 0
            else:
                cozulmus_veri['gorev_sayisi'] = (kordinat_sayisi + 1) // 2
            cozulmus_veri['otonom_gorev_sirasi'] = (veri_bayt[17] + 1) & 0xFF
            x_eksen_bayrak = veri_bayt[18]
            x_eksen_deger = veri_bayt[19]
            cozulmus_veri['x_eksen'] = -x_eksen_deger if x_eksen_bayrak == 1 else x_eksen_deger
            y_eksen_bayrak = veri_bayt[20]
            y_eksen_deger = veri_bayt[21]
            cozulmus_veri['y_eksen'] = -y_eksen_deger if y_eksen_bayrak == 1 else y_eksen_deger
            cozulmus_veri['secim_biti'] = veri_bayt[22]
            degisken_kuyruk_bytes = veri_bayt[23:31]
            secim_biti = cozulmus_veri['secim_biti']
            ek_veriler = self.ek_paket_coz(degisken_kuyruk_bytes, secim_biti)
            if ek_veriler:
                self.son_ek_veriler.update(ek_veriler)
            cozulmus_veri.update(self.son_ek_veriler)
            arac_tipi_kodu = cozulmus_veri.get('_arac_tipi_kodu')
            if arac_tipi_kodu is not None and arac_tipi_kodu in self.PLATFORM_MODLARI:
                modlari = self.PLATFORM_MODLARI[arac_tipi_kodu]
                if 0 <= arac_modu_kod < len(modlari):
                    cozulmus_veri['arac_modu'] = modlari[arac_modu_kod]
                else:
                    cozulmus_veri['arac_modu'] = "Bilinmiyor"
            else:
                cozulmus_veri['arac_modu'] = "Araç Tipi Bilinmiyor"
            cozulmus_veri['z_eksen'] = cozulmus_veri['pusula']
            filtrelenmis = {anahtar: cozulmus_veri[anahtar] for anahtar in self.donulecek_alanlar if anahtar in cozulmus_veri}
            return filtrelenmis
            
        except Exception as e:
            return None

    def _platforma_gore_arac_modu_coz(self, arac_modu_kod, arac_tipi_kodu):
        try:
            arac_modu_kod = int(arac_modu_kod)
        except (TypeError, ValueError):
            return None
        if arac_modu_kod == 0:
            mod_anahtari = 'manuel'
        elif arac_modu_kod == 1:
            mod_anahtari = 'denge'
        elif arac_modu_kod == 2:
            mod_anahtari = 'otonom'
        elif arac_modu_kod == 3:
            mod_anahtari = 'otonom_eve_donus'
        elif arac_modu_kod == 4:
            mod_anahtari = 'egitim'
        elif arac_modu_kod == 5:
            mod_anahtari = 'aktif_takip'
        elif 6 <= arac_modu_kod <= 12:
            mod_anahtari = 'serial_mod'
        else:
            return None
        if arac_tipi_kodu in (0, 1):
            platform_modlari = {
                'manuel': 'Manuel Mod',
                'denge': 'Denge Modu',
                'otonom': 'Otonom Mod',
                'otonom_eve_donus': 'Otonom Eve Donus',
                'egitim': 'Egitim Modu',
                'aktif_takip': 'Aktif Takip Modu',
                'serial_mod': 'Serial Mod'
            }
        elif arac_tipi_kodu == 2:
            platform_modlari = {
                'manuel': 'Manuel Mod',
                'otonom': 'Otonom Mod',
                'otonom_eve_donus': 'Otonom Eve Donus',
                'serial_mod': 'Serial Mod'
            }
        elif arac_tipi_kodu == 3:
            platform_modlari = {
                'manuel': 'Manuel Mod',
                'denge': 'Denge Modu',
                'otonom': 'Otonom Mod',
                'otonom_eve_donus': 'Otonom Eve Donus',
                'serial_mod': 'Serial Mod'
            }
        else:
            return None

        return platform_modlari.get(mod_anahtari)

    def _arac_tipi_adi_cevir(self, arac_tipi_num):
        arac_tipi_adlari = {
            0: "Sabit Kanat",
            1: "Kanat Uçak",
            2: "Kara Aracı",
            3: "Deniz Aracı"
        }
        return arac_tipi_adlari.get(arac_tipi_num, f"Bilinmeyen_{arac_tipi_num}")

    def ek_paket_coz(self, degisken_kuyruk_bytes, secim_biti):
        if len(degisken_kuyruk_bytes) != 8:
            return None
        try:
            ek_veriler = {}
            if secim_biti == 0 or secim_biti == 3:
                yukseklik_int = struct.unpack('<H', degisken_kuyruk_bytes[0:2])[0]
                ek_veriler['yukseklik'] = yukseklik_int / 10.0
                voltaj_int = struct.unpack('<H', degisken_kuyruk_bytes[2:4])[0]
                ek_veriler['voltaj'] = voltaj_int / 100.0
                akim_int = struct.unpack('<H', degisken_kuyruk_bytes[4:6])[0]
                ek_veriler['akim'] = akim_int / 10.0
                harc_akim_int = struct.unpack('<H', degisken_kuyruk_bytes[6:8])[0]
                ek_veriler['harc_akim'] = harc_akim_int
            elif secim_biti == 1:
                ek_veriler['motor_durumu'] = degisken_kuyruk_bytes[0]
                gps_arac_byte = degisken_kuyruk_bytes[1]
                gps_3bit = (gps_arac_byte >> 5) & 0b111
                arac_5bit = gps_arac_byte & 0b11111
                if gps_3bit == 1:
                    ek_veriler['gps_durum'] = "--"
                elif gps_3bit == 2:
                    ek_veriler['gps_durum'] = "2D"
                elif gps_3bit == 3:
                    ek_veriler['gps_durum'] = "3D"
                elif gps_3bit == 7:
                    ek_veriler['gps_durum'] = "GPS bagli degil"
                else:
                    ek_veriler['gps_durum'] = "--"
                if arac_5bit in [0, 1, 2, 3]:
                    arac_tipi_num = 0
                elif arac_5bit in [4, 5]:
                    arac_tipi_num = 1
                elif arac_5bit in [6, 7]:
                    arac_tipi_num = 2
                elif arac_5bit in [8, 9, 10]:
                    arac_tipi_num = 3
                else:
                    arac_tipi_num = 0
                ek_veriler['_arac_tipi_kodu'] = arac_tipi_num
                ek_veriler['arac_tipi'] = self._arac_tipi_adi_cevir(arac_tipi_num)
                ek_veriler['gps_saat'] = degisken_kuyruk_bytes[3]
                ek_veriler['gps_dakika'] = degisken_kuyruk_bytes[4]
                ek_veriler['gps_saniye'] = degisken_kuyruk_bytes[5]
                gidilen_yol_int = struct.unpack('<H', degisken_kuyruk_bytes[6:8])[0]
                ek_veriler['gidilen_yol'] = gidilen_yol_int
            elif secim_biti == 2:
                ek_veriler['temp'] = degisken_kuyruk_bytes[0]
                ek_veriler['gps_gun'] = degisken_kuyruk_bytes[1]
                ek_veriler['gps_ay'] = degisken_kuyruk_bytes[2]
                ek_veriler['gps_yil'] = degisken_kuyruk_bytes[3] + 2000
            
            return ek_veriler
        except Exception as e:
            return None

    def tampon_sifirla(self):
        if self.is_available():
            self.seri.flushInput()

    def koordinat_sifirlama_paketi_gonder(self, koordinat_sayisi=None, bayt4=None):
        if bayt4 is None and koordinat_sayisi is None:
            return False
        if bayt4 is None:
            bayt4 = (koordinat_sayisi + 1) & 0xFF
        else:
            bayt4 = int(bayt4) & 0xFF
        paket = bytes([84, 86, 7, 5, bayt4, 46])

        if not self.is_available():
            return False

        try:
            with self.kilit:
                if not self.seri or not self.seri.is_open:
                    return False
                self.seri.write(paket)
                self.seri.flush()
            return True
        except (serial.SerialException, OSError):
            self._baglanti_koptu_isaretle("Sifirlama paketi gonderilemedi")
            return False

    def _reset_bayt4_adaylari(self, koordinat_sayisi, gorev_sayisi):
        adaylar = []
        if koordinat_sayisi is not None:
            adaylar.append((koordinat_sayisi + 1) & 0xFF)
        if koordinat_sayisi is not None:
            adaylar.append(int(koordinat_sayisi) & 0xFF)
        if gorev_sayisi is not None:
            adaylar.append((int(gorev_sayisi) + 1) & 0xFF)
            adaylar.append(int(gorev_sayisi) & 0xFF)

        adaylar.extend([1, 0])
        uniq = []
        for deger in adaylar:
            if deger not in uniq:
                uniq.append(deger)
        return uniq

    def _gorev_sifirla_akisi(self, timeout_s, tekrar_hz):
        periyot = 1.0 / tekrar_hz if tekrar_hz > 0 else 0.1
        baslangic = time.time()
        deneme_sayisi = 0

        while True:
            if (time.time() - baslangic) > timeout_s:
                son_gorev = self.gorev_sayisi_al()
                return {
                    'basarili': False,
                    'mesaj': 'Sifirlama basarisiz',
                    'kalan_gorev': son_gorev
                }

            if not self.is_available():
                self._yeniden_baglanmayi_dene()
                time.sleep(periyot)
                continue

            koordinat_sayisi = self._kordinat_sayisi_al()
            gorev_sayisi = self.gorev_sayisi_al()

            if koordinat_sayisi is None:
                time.sleep(periyot)
                continue

            if koordinat_sayisi == 0:
                return {
                    'basarili': True,
                    'mesaj': 'Gorev sayisi sifir',
                    'kalan_gorev': 0
                }

            # Önce resmi paket, ardından sınırlı fallback paketleri ile dene
            bayt4_adaylari = self._reset_bayt4_adaylari(koordinat_sayisi, gorev_sayisi)
            gonderildi = False
            for bayt4 in bayt4_adaylari:
                if self.koordinat_sifirlama_paketi_gonder(bayt4=bayt4):
                    gonderildi = True
                    deneme_sayisi += 1
                time.sleep(0.01)

            if not gonderildi:
                time.sleep(periyot)
                continue

            # Paket sonrası yeni telemetriyi kısa pencerede bekle ve doğrula
            kontrol_bitis = time.time() + max(0.2, periyot)
            while time.time() < kontrol_bitis:
                yeni_koordinat = self._kordinat_sayisi_al()
                yeni_gorev = self.gorev_sayisi_al()
                if yeni_koordinat == 0 or yeni_gorev == 0:
                    return {
                        'basarili': True,
                        'mesaj': 'Gorevler sifirlandi',
                        'kalan_gorev': 0
                    }
                time.sleep(0.02)

            time.sleep(periyot)

    def gorev_sifirla(self, timeout_s=None, tekrar_hz=None):
        timeout = self._gorev_sifirlama_timeout_s if timeout_s is None else float(timeout_s)
        tekrar = self._gorev_sifirlama_tekrar_hz if tekrar_hz is None else float(tekrar_hz)
        return self._gorev_sifirla_akisi(timeout_s=timeout, tekrar_hz=tekrar)

    def koordinat_sifirla(self, timeout_s=None, tekrar_hz=None):
        return self.gorev_sifirla(timeout_s=timeout_s, tekrar_hz=tekrar_hz)

    def _koordinat_u32_cevir(self, koordinat):
        return int(float(koordinat) * 1000000) & 0xFFFFFFFF

    def _u16_baytlara_ayir(self, deger):
        deger = int(deger) & 0xFFFF
        return struct.pack('<H', deger)

    def gorev_paketi_olustur(
        self,
        gorev_turu,
        gorev_veri_sirasi,
        enlem,
        boylam,
        arac_hizi=0,
        yukseklik=0,
        hata_yaricapi=5,
        aci=0,
        MH_suresi=0,
        servo_atama_no=0,
        servo_deger=1500,
        deger_2=0,
    ):
        gorev_turu_haritasi = {'kalkis': 0, 'koordinat': 1, 'servo': 2, 'inis': 3}
        if gorev_turu not in gorev_turu_haritasi:
            raise ValueError("gorev_turu 'kalkis', 'koordinat', 'servo' veya 'inis' olmali")

        paket_turu = gorev_turu_haritasi[gorev_turu]
        enlem_b = struct.pack('<I', self._koordinat_u32_cevir(enlem))
        boylam_b = struct.pack('<I', self._koordinat_u32_cevir(boylam))
        hiz_b = self._u16_baytlara_ayir(arac_hizi)
        yukseklik_b = self._u16_baytlara_ayir(yukseklik)
        motor_sure_b = self._u16_baytlara_ayir(MH_suresi)
        servo_deger_b = self._u16_baytlara_ayir(servo_deger)

        if gorev_turu == 'kalkis':
            paket = bytearray(22)
            paket[0:5] = bytes([84, 86, 7, paket_turu, int(gorev_veri_sirasi) & 0xFF])
            paket[5:9] = enlem_b
            paket[9:13] = boylam_b
            paket[13:15] = hiz_b
            paket[15:17] = yukseklik_b
            paket[17] = int(hata_yaricapi) & 0xFF
            paket[18] = int(aci) & 0xFF
            paket[19:21] = motor_sure_b
            paket[21] = 46
            return bytes(paket)

        if gorev_turu == 'koordinat':
            paket = bytearray(20)
            paket[0:5] = bytes([84, 86, 7, paket_turu, int(gorev_veri_sirasi) & 0xFF])
            paket[5:9] = enlem_b
            paket[9:13] = boylam_b
            paket[13:15] = hiz_b
            paket[15:17] = yukseklik_b
            paket[17] = int(hata_yaricapi) & 0xFF
            paket[18] = int(aci) & 0xFF
            paket[19] = 46
            return bytes(paket)

        if gorev_turu == 'servo':
            paket = bytearray(22)
            paket[0:5] = bytes([84, 86, 7, paket_turu, int(gorev_veri_sirasi) & 0xFF])
            paket[5:9] = enlem_b
            paket[9:13] = boylam_b
            paket[13:15] = hiz_b
            paket[15:17] = yukseklik_b
            paket[17] = int(hata_yaricapi) & 0xFF
            paket[18] = int(servo_atama_no) & 0xFF
            paket[19:21] = servo_deger_b
            paket[21] = 46
            return bytes(paket)
        paket = bytearray(19)
        paket[0:5] = bytes([84, 86, 7, paket_turu, int(gorev_veri_sirasi) & 0xFF])
        paket[5:9] = enlem_b
        paket[9:13] = boylam_b
        paket[13:15] = yukseklik_b
        paket[15] = int(hata_yaricapi) & 0xFF
        paket[16] = int(aci) & 0xFF
        paket[17] = int(deger_2) & 0xFF
        paket[18] = 46
        return bytes(paket)

    def gorev_paketi_gonder(self, paket_bytes):
        if not self.is_available():
            return False

        try:
            with self.kilit:
                if not self.seri or not self.seri.is_open:
                    return False
                self.seri.write(paket_bytes)
                self.seri.flush()
            return True
        except (serial.SerialException, OSError):
            self._baglanti_koptu_isaretle("Gorev paketi gonderilemedi")
            return False

    def gorev_listesi_gonder(self, gorev_listesi):
        periyot = self._gorev_gonderim_periyot_s
        sonuc = []
        for beklenen_sira, gorev in enumerate(gorev_listesi, start=1):
            verilen_sira = gorev.get('gorev_veri_sirasi')
            if verilen_sira != beklenen_sira:
                mesaj = f'Gorev sirasi gecersiz. Beklenen: {beklenen_sira}, Gelen: {verilen_sira}. Gonderim durduruldu.'
                sonuc.append({'gorev_turu': gorev.get('gorev_turu'), 'gorev_veri_sirasi': verilen_sira,
                    'basarili': False, 'mesaj': mesaj, 'paket_uzunlugu': 0, 'paket_hex': ''})
                return sonuc
        sifirlama_sonucu = self.gorev_sifirla()
        sonuc.append({'gorev_turu': 'SIFIRLAMA', 'gorev_veri_sirasi': 0, 'basarili': sifirlama_sonucu.get('basarili'),
            'mesaj': sifirlama_sonucu.get('mesaj'), 'paket_uzunlugu': 0, 'paket_hex': ''})
        if not sifirlama_sonucu.get('basarili'):
            sonuc.append({'gorev_turu': 'GONDERIM_IPTAL', 'gorev_veri_sirasi': 0, 'basarili': False,
                'mesaj': 'Sifirlama basarisiz. Sirali gonderim guvenligi icin islem durduruldu.', 'paket_uzunlugu': 0, 'paket_hex': ''})
            return sonuc
        for gorev in gorev_listesi:
            hedef_sira = int(gorev.get('gorev_veri_sirasi'))
            paket = self.gorev_paketi_olustur(**gorev)
            basarili = False
            deneme_sayisi = 0
            kart_gorev_sayisi = self.gorev_sayisi_al()

            for deneme in range(1, self._gorev_gonderim_max_deneme + 1):
                deneme_sayisi = deneme
                yazma_basarili = self.gorev_paketi_gonder(paket)
                if not yazma_basarili:
                    time.sleep(periyot)
                    continue

                onay_basarili, kart_gorev_sayisi = self._gorev_sayisi_hedefe_bekle(hedef_sira)
                if onay_basarili:
                    basarili = True
                    break

                time.sleep(periyot)

            mesaj = (
                f'Gorev {hedef_sira} gonderildi'
                if basarili
                else f'Gorev {hedef_sira} onaylanamadi. Son kart gorev sayisi: {kart_gorev_sayisi}'
            )

            sonuc.append({
                'gorev_turu': gorev.get('gorev_turu'),
                'gorev_veri_sirasi': hedef_sira,
                'basarili': basarili,
                'mesaj': mesaj,
                'deneme_sayisi': deneme_sayisi,
                'kart_gorev_sayisi': kart_gorev_sayisi,
                'paket_uzunlugu': len(paket),
                'paket_hex': paket.hex(' ')
            })

            if not basarili:
                sonuc.append({
                    'gorev_turu': 'GONDERIM_IPTAL',
                    'gorev_veri_sirasi': hedef_sira,
                    'basarili': False,
                    'mesaj': f'Gorev {hedef_sira} onaylanamadigi icin kalan gorevler gonderilmedi.',
                    'paket_uzunlugu': 0,
                    'paket_hex': ''
                })
                break

            time.sleep(periyot)

        return sonuc

    def _gorev_sayisi_hedefe_bekle(self, hedef_gorev_sayisi):
        timeout_s = self._gorev_gonderim_onay_timeout_s
        tekrar_hz = self._gorev_gonderim_onay_tekrar_hz
        periyot = 1.0 / tekrar_hz if tekrar_hz > 0 else 0.05
        bitis = time.time() + max(0.0, float(timeout_s))
        son_gorev_sayisi = self.gorev_sayisi_al()

        while True:
            gorev_sayisi = self.gorev_sayisi_al()
            if gorev_sayisi is not None:
                son_gorev_sayisi = gorev_sayisi
                if gorev_sayisi >= int(hedef_gorev_sayisi):
                    return True, son_gorev_sayisi

            if time.time() >= bitis:
                return False, son_gorev_sayisi

            time.sleep(periyot)

    def mevcut_gorev_sayisi_al(self):
        timeout_s = 2.0
        tekrar_hz = 10.0
        periyot = 1.0 / tekrar_hz if tekrar_hz > 0 else 0.05
        bitis = time.time() + max(0.0, float(timeout_s))

        while True:
            gorev_sayisi = self.gorev_sayisi_al()
            if gorev_sayisi is not None:
                return gorev_sayisi

            if time.time() >= bitis:
                return None

            time.sleep(periyot)

    def gorev_ekle(
        self,
        gorev_turu,
        enlem,
        boylam,
        arac_hizi=0,
        yukseklik=0,
        hata_yaricapi=5,
        aci=0,
        MH_suresi=0,
        servo_atama_no=0,
        servo_deger=1500,
        deger_2=0,
        tekrar_hz=10.0,
    ):
        mevcut_sayisi = self.mevcut_gorev_sayisi_al()
        
        if mevcut_sayisi is None:
            return {
                'basarili': False,
                'mesaj': 'Mevcut gorev sayisi okunamadi',
                'yeni_gorev_no': None
            }
        
        yeni_gorev_no = mevcut_sayisi + 1
        
        # Maksimum görev sayısı kontrolü (1 byte = 255)
        if yeni_gorev_no > 255:
            return {
                'basarili': False,
                'mesaj': f'Maksimum gorev sayisi (255) asildi. Mevcut: {mevcut_sayisi}. Yeni gorev eklenemez.',
                'yeni_gorev_no': None
            }
        
        gorev = {
            'gorev_turu': gorev_turu,
            'gorev_veri_sirasi': yeni_gorev_no,
            'enlem': enlem,
            'boylam': boylam,
            'arac_hizi': arac_hizi,
            'yukseklik': yukseklik,
            'hata_yaricapi': hata_yaricapi,
            'aci': aci,
            'MH_suresi': MH_suresi,
            'servo_atama_no': servo_atama_no,
            'servo_deger': servo_deger,
            'deger_2': deger_2,
        }
        
        paket = self.gorev_paketi_olustur(**gorev)
        basarili = self.gorev_paketi_gonder(paket)

        if basarili:
            # Telemetri gecikse bile ardışık eklemelerde görev no tekrar etmesin.
            if not self.arac_verisi:
                self.arac_verisi = {}
            # Internal olarak Byte16 ve gorev_sayisi'ni güncelle
            self.arac_verisi['_kordinat_sayisi'] = ((2 * yeni_gorev_no) - 1) & 0xFF
            self.arac_verisi['gorev_sayisi'] = yeni_gorev_no
        
        return {
            'basarili': basarili,
            'mesaj': 'Gorev eklendi' if basarili else 'Gorev eklenemedi',
            'yeni_gorev_no': yeni_gorev_no if basarili else None,
            'paket_hex': paket.hex(' ')
        }

    def gorev_duzenle(
        self,
        duzenlenecek_gorev_no,
        gorev_turu,
        enlem,
        boylam,
        arac_hizi=0,
        yukseklik=0,
        hata_yaricapi=5,
        aci=0,
        MH_suresi=0,
        servo_atama_no=0,
        servo_deger=1500,
        deger_2=0,
    ):
        mevcut_sayisi = self.mevcut_gorev_sayisi_al()
        
        if mevcut_sayisi is None:
            return {
                'basarili': False,
                'mesaj': 'Mevcut gorev sayisi okunamadi'
            }
        
        if duzenlenecek_gorev_no < 1 or duzenlenecek_gorev_no > mevcut_sayisi:
            return {
                'basarili': False,
                'mesaj': f'Gorev no {duzenlenecek_gorev_no} gecersiz. Mevcut gorev sayisi: {mevcut_sayisi}. Duzenleme icin 1 ile {mevcut_sayisi} arasinda bir gorev no secilmeli.'
            }
        
        gorev_turu_haritasi = {
            'kalkis': 0,
            'koordinat': 1,
            'servo': 2,
            'inis': 3,
        }
        
        if gorev_turu not in gorev_turu_haritasi:
            return {
                'basarili': False,
                'mesaj': "gorev_turu 'kalkis', 'koordinat', 'servo' veya 'inis' olmali"
            }
        
        paket_turu = gorev_turu_haritasi[gorev_turu]
        enlem_b = struct.pack('<I', self._koordinat_u32_cevir(enlem))
        boylam_b = struct.pack('<I', self._koordinat_u32_cevir(boylam))
        hiz_b = self._u16_baytlara_ayir(arac_hizi)
        yukseklik_b = self._u16_baytlara_ayir(yukseklik)
        motor_sure_b = self._u16_baytlara_ayir(MH_suresi)
        servo_deger_b = self._u16_baytlara_ayir(servo_deger)
        
        if gorev_turu == 'kalkis':
            paket = bytearray(22)
            paket[0:5] = bytes([84, 86, 7, paket_turu, int(duzenlenecek_gorev_no) & 0xFF])
            paket[5:9] = enlem_b
            paket[9:13] = boylam_b
            paket[13:15] = hiz_b
            paket[15:17] = yukseklik_b
            paket[17] = int(hata_yaricapi) & 0xFF
            paket[18] = int(aci) & 0xFF
            paket[19:21] = motor_sure_b
            paket[21] = 46
            paket_bytes = bytes(paket)

        elif gorev_turu == 'koordinat':
            paket = bytearray(20)
            paket[0:5] = bytes([84, 86, 7, paket_turu, int(duzenlenecek_gorev_no) & 0xFF])
            paket[5:9] = enlem_b
            paket[9:13] = boylam_b
            paket[13:15] = hiz_b
            paket[15:17] = yukseklik_b
            paket[17] = int(hata_yaricapi) & 0xFF
            paket[18] = int(aci) & 0xFF
            paket[19] = 46
            paket_bytes = bytes(paket)

        elif gorev_turu == 'servo':
            paket = bytearray(22)
            paket[0:5] = bytes([84, 86, 7, paket_turu, int(duzenlenecek_gorev_no) & 0xFF])
            paket[5:9] = enlem_b
            paket[9:13] = boylam_b
            paket[13:15] = hiz_b
            paket[15:17] = yukseklik_b
            paket[17] = int(hata_yaricapi) & 0xFF
            paket[18] = int(servo_atama_no) & 0xFF
            paket[19:21] = servo_deger_b
            paket[21] = 46
            paket_bytes = bytes(paket)

        else:  # inis
            paket = bytearray(19)
            paket[0:5] = bytes([84, 86, 7, paket_turu, int(duzenlenecek_gorev_no) & 0xFF])
            paket[5:9] = enlem_b
            paket[9:13] = boylam_b
            paket[13:15] = yukseklik_b
            paket[15] = int(hata_yaricapi) & 0xFF
            paket[16] = int(aci) & 0xFF
            paket[17] = int(deger_2) & 0xFF
            paket[18] = 46
            paket_bytes = bytes(paket)
        
        basarili = self.gorev_paketi_gonder(paket_bytes)
        
        return {
            'basarili': basarili,
            'mesaj': f'Gorev {duzenlenecek_gorev_no} duzenlendi' if basarili else f'Gorev {duzenlenecek_gorev_no} duzenlenemedi',
            'paket_hex': paket_bytes.hex(' ')
        }

    def _async_baslat(self):
        if self.sistem_aktif:
            return
            
        self.sistem_aktif = True
        self.veri_sayaci = 0
        self.baslangic_zamani = time.time()
        self.okuma_thread = threading.Thread(target=self._surekli_veri_oku, daemon=True)
        self.okuma_thread.start()
    
    def _surekli_veri_oku(self):
        while self.sistem_aktif:
            try:
                if not self.is_available():
                    self._yeniden_baglanmayi_dene()
                    self._hiz_izleme_sayaci = 0
                    self._aktif_veri_hizi = 0.0
                    time.sleep(0.1)
                    continue

                veri = self.veri_oku()
                
                if veri:
                    self.veri_sayaci += 1
                    self.son_veri = veri.copy()  
                    self.son_veri_zamani = time.time()

                    # Gerçek zamanlı hız hesaplaması
                    simdi = time.time()
                    if self._hiz_izleme_zamani == 0:
                        self._hiz_izleme_zamani = simdi
                        self._hiz_izleme_sayaci = 0
                    
                    self._hiz_izleme_sayaci += 1
                    gecen = simdi - self._hiz_izleme_zamani
                    
                    # Her 1 saniyede bir hızı güncelle
                    if gecen >= 1.0:
                        self._aktif_veri_hizi = self._hiz_izleme_sayaci / gecen if gecen > 0 else 0.0
                        self._hiz_izleme_zamani = simdi
                        self._hiz_izleme_sayaci = 0
                    
                    try:
                        self.veri_kuyrugu.put_nowait(veri)
                    except queue.Full:
                        try:
                            self.veri_kuyrugu.get_nowait()
                            self.veri_kuyrugu.put_nowait(veri)
                        except queue.Empty:
                            pass
                else:
                    # Veri gelmedi, hız izlemesini sıfırla
                    simdi = time.time()
                    gecen = simdi - self.son_veri_zamani if self.son_veri_zamani else 0
                    if gecen > 0.5:
                        self._aktif_veri_hizi = 0.0
                        self._hiz_izleme_sayaci = 0
                        self._hiz_izleme_zamani = 0.0
                
                time.sleep(0.01)
                
            except Exception as e:
                time.sleep(0.1)
    
    def _async_durdur(self):
        if not self.sistem_aktif:
            return
            
        self.sistem_aktif = False
        if self.okuma_thread and self.okuma_thread.is_alive():
            self.okuma_thread.join(timeout=1.0)
    
    def _yeni_veri_var_mi(self):
        return not self.veri_kuyrugu.empty()
    
    def _yeni_veri_al(self):
        try:
            return self.veri_kuyrugu.get_nowait()
        except queue.Empty:
            return None
    
    def veri_hizi(self):
        gecen_sure = time.time() - self.baslangic_zamani
        if gecen_sure > 0 and self.veri_sayaci > 0:
            return self.veri_sayaci / gecen_sure
        return 0
    
    def durum(self):
        gecen_sure = time.time() - self.son_veri_zamani if self.son_veri_zamani else 0
        
        # Bağlantı yoksa hızı sıfırla
        if not self.is_available():
            aktif_hiz = 0.0
        else:
            aktif_hiz = round(self._aktif_veri_hizi, 1)
        
        return {
            'sistem_aktif': self.sistem_aktif,
            'baglanti_var': self.is_available(),
            'port': self.port,
            'baglanti_hatasi': self.baglanti_hatasi,
            'kuyruk_boyutu': self.veri_kuyrugu.qsize(),
            'toplam_veri': self.veri_sayaci,
            'son_veri_zamani': self.son_veri_zamani,
            'veri_hizi_hz': aktif_hiz,
            'son_veri_gecen_sure': round(gecen_sure, 2),
            'thread_calisiyor': self.okuma_thread.is_alive() if self.okuma_thread else False
        }

    def _veri_guncelle(self):
        guncel_veri = None
        while self._yeni_veri_var_mi():
            temp_veri = self._yeni_veri_al()
            if temp_veri:
                guncel_veri = temp_veri
        
        if guncel_veri:
            self.arac_verisi = guncel_veri
        elif self.son_veri and not self.arac_verisi:
            self.arac_verisi = self.son_veri

    def anlik_veri_al(self):
        self._veri_guncelle()
        veri = self.arac_verisi.copy() if self.arac_verisi else {}
        # Motor durumu dönüşümü: 0=Aktif, 1=Kapalı
        if 'motor_durumu' in veri and veri['motor_durumu'] is not None:
            veri['motor_durumu'] = "Aktif" if veri['motor_durumu'] == 0 else "Kapalı"
        return veri

    # ==================== GETTER FONKSİYONLARI ====================
    def arac_modu_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('arac_modu')
    
    def sistem_durum_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('sistem_durum')
    
    def arac_id_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('arac_id')
    
    def enlem_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('enlem')
    
    def boylam_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('boylam')
    
    def uydu_sayisi_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('uydu_sayisi')
    
    def gps_hiz_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_hiz')
    
    def yukseklik_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('yukseklik')
    
    def pusula_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('pusula')
    
    def _kordinat_sayisi_al(self):
        self._veri_guncelle()
        return self.arac_verisi.get('_kordinat_sayisi')
    
    def gorev_sayisi_al(self):
        self._veri_guncelle()
        return self.arac_verisi.get('gorev_sayisi')
    
    def otonom_gorev_sirasi_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('otonom_gorev_sirasi')
    
    def x_eksen_al(self):
        self._veri_guncelle()
        return self.arac_verisi.get('x_eksen')

    def y_eksen_al(self):
        self._veri_guncelle()
        return self.arac_verisi.get('y_eksen')

    def z_eksen_al(self):
        self._veri_guncelle()
        return self.arac_verisi.get('z_eksen')
    
    def secim_biti_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('secim_biti')

    def voltaj_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('voltaj')
    
    def akim_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('akim')
    
    def harc_akim_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('harc_akim')
    
    def motor_durumu_al(self): 
        self._veri_guncelle()
        durum = self.arac_verisi.get('motor_durumu')
        if durum is None:
            return None
        return "Aktif" if durum == 0 else "Kapalı"
    
    def gps_durum_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_durum')
    
    def arac_tipi_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('arac_tipi')
    

    def gps_saat_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_saat')
    
    def gps_dakika_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_dakika')
    
    def gps_saniye_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_saniye')
    
    def gidilen_yol_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gidilen_yol')
    
    def temp(self):
        self._veri_guncelle()
        return self.arac_verisi.get('temp')
    
    def gps_gun_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_gun')
    
    def gps_ay_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_ay')
    
    def gps_yil_al(self): 
        self._veri_guncelle()
        return self.arac_verisi.get('gps_yil')

    def gps_zaman_al(self):
        self._veri_guncelle()
        s = self.arac_verisi.get('gps_saat')
        d = self.arac_verisi.get('gps_dakika')
        sn = self.arac_verisi.get('gps_saniye')
        return f"{s:02}:{d:02}:{sn:02}" if s is not None and d is not None and sn is not None else None
    
    def gps_tarih_al(self):
        self._veri_guncelle()
        gun = self.arac_verisi.get('gps_gun')
        ay = self.arac_verisi.get('gps_ay')
        yil = self.arac_verisi.get('gps_yil')
        if all(x is not None for x in [gun, ay, yil]):
            yil_2digit = (yil - 2000) if yil > 2000 else yil
            return f"{gun:02}/{ay:02}/{yil_2digit:02}"
        return None

    @contextmanager
    def serial_connection(self):
        try:
            yield self
        finally:
            if self.seri and self.seri.is_open:
                self.seri.close()

    def kapat(self):
        if self.sistem_aktif:
            self._async_durdur()
        if self.seri and self.seri.is_open:
            self.seri.close()
