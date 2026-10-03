#define R_km 6371
#define Pi 3.141592

void Komut_1_Dizilim(int8_t Brn_Aci, int16_t Kmt_Yukseklik)
{
  byte byte_veri[20];

  byte_veri[0] = 84; //Sabit kontrol byte
  byte_veri[1] = 86; //Sabit kontrol byte
  byte_veri[2] = 19; //Sabit seyir modu byte
  byte_veri[3] = 0; // Seyir modu yükselme alçalma komut sırası, sabit

  //Yükselme Alçalma Açısı:
  byte_veri[4] = Brn_Aci; //10 derece

  //Yükseklik verisinin dönüştürülmesi:
  byte_veri[5] = Kmt_Yukseklik;
  byte_veri[6] = Kmt_Yukseklik >> 8;


  byte_veri[7] = 42; //Sabit kontrol son byte

  //Komut Gönderme İşlemi:
  Serial1.write(byte_veri, 8);
}


byte byte_[6];
void Komut_2_Dizilim(int32_t En_veri, int32_t B_Veri)
{
  byte byte_veri[20];

  byte_veri[0] = 84; //Sabit kontrol byte
  byte_veri[1] = 86; //Sabit kontrol byte
  byte_veri[2] = 19; //Sabit seyir modu byte
  byte_veri[3] = 2; // Seyir modu kord yönelim komut sırası, sabit

  //Enlem Verisi:
  byte_default();
  Veri_Paket_Hazirlama_4_byte(En_veri);
  byte_veri[4] = byte_[0];
  byte_veri[5] = byte_[1];
  byte_veri[6] = byte_[2];
  byte_veri[7] = byte_[3];

  //Enlem Verisi:
  byte_default();
  Veri_Paket_Hazirlama_4_byte(B_Veri);
  byte_veri[8] = byte_[0];
  byte_veri[9] = byte_[1];
  byte_veri[10] = byte_[2];
  byte_veri[11] = byte_[3];

  byte_veri[12] = 42; //Sabit kontrol son byte

  //Komut Gönderme İşlemi:
  Serial1.write(byte_veri, 14);
}

void Veri_Paket_Hazirlama_4_byte(int32_t Veri_1) //4 byte veri paketi hazırlama
{
  byte_[0] = Veri_1;
  byte_[1] = Veri_1 >> 8;
  byte_[2] = Veri_1 >> 16;
  byte_[3] = Veri_1 >> 24;
}

void byte_default()
{
  byte_[0] = 0;
  byte_[1] = 0;
  byte_[2] = 0;
  byte_[3] = 0;
}

//Koordinat mesafe hesaplama fonksiyonu
float Dist(double E_1_, double B_1_, double E_2_, double B_2_)
{
  double lat_1_r, lat_2_r, lon_1_r, lon_2_r;
  double a, c, d;
  float d_;
  lat_1_r = E_1_ * Pi / 180;
  lat_2_r = E_2_ * Pi / 180;
  lon_1_r = B_1_ * Pi / 180;
  lon_2_r = B_2_ * Pi / 180;
  double d_lat_r = (E_2_ - E_1_) * Pi / 180;
  double d_lon_r = (B_2_ - B_1_) * Pi / 180;
  a = sin(d_lat_r / 2) * sin(d_lat_r / 2) + cos(lat_1_r) * cos(lat_2_r) * sin(d_lon_r / 2) * sin(d_lon_r / 2);
  c = 2 * atan2(sqrt(a), sqrt(1 - a));
  d = R_km * c;
  d = d * 1000;
  return d;
}

void Komut_4_Dizilim(int8_t Brn_Aci, int16_t Kmt_Yukseklik, int8_t Hiz_Oran)
{
  byte byte_veri[20];

  byte_veri[0] = 84; //Sabit kontrol byte
  byte_veri[1] = 86; //Sabit kontrol byte
  byte_veri[2] = 19; //Sabit seyir modu byte
  byte_veri[3] = 3; // Seyir modu yükselme alçalma komut sırası, sabit

  //Yükselme Alçalma Açısı:
  byte_veri[4] = Brn_Aci; //10 derece

  //Yükseklik verisinin dönüştürülmesi:
  byte_veri[5] = Kmt_Yukseklik;
  byte_veri[6] = Kmt_Yukseklik >> 8;

  //itki oran:
  byte_veri[7] = Hiz_Oran; //0 - 100 %


  byte_veri[8] = 42; //Sabit kontrol son byte

  //Komut Gönderme İşlemi:
  Serial1.write(byte_veri, 9);
}

void Komut_5_Dizilim(int32_t En_veri, int32_t B_Veri, int8_t Brn_Aci, int16_t Kmt_Yukseklik, int8_t Hiz_Oran )
{
  byte byte_veri[20];

  byte_veri[0] = 84; //Sabit kontrol byte
  byte_veri[1] = 86; //Sabit kontrol byte
  byte_veri[2] = 19; //Sabit seyir modu byte
  byte_veri[3] = 4; // Seyir modu kord yönelim, yukseklik, hız komut sırası, sabit

  //Enlem Verisi:
  byte_default();
  Veri_Paket_Hazirlama_4_byte(En_veri);
  byte_veri[4] = byte_[0];
  byte_veri[5] = byte_[1];
  byte_veri[6] = byte_[2];
  byte_veri[7] = byte_[3];

  //Enlem Verisi:
  byte_default();
  Veri_Paket_Hazirlama_4_byte(B_Veri);
  byte_veri[8] = byte_[0];
  byte_veri[9] = byte_[1];
  byte_veri[10] = byte_[2];
  byte_veri[11] = byte_[3];

  //Yükselme Alçalma Açısı:
  byte_veri[12] = Brn_Aci; //10 derece

  //Yükseklik verisinin dönüştürülmesi:
  byte_veri[13] = Kmt_Yukseklik;
  byte_veri[14] = Kmt_Yukseklik >> 8;

  //itki oran:
  byte_veri[15] = Hiz_Oran; //0 - 100 %

  byte_veri[16] = 42; //Sabit kontrol son byte

  //Komut Gönderme İşlemi:
  Serial1.write(byte_veri, 17);
}
