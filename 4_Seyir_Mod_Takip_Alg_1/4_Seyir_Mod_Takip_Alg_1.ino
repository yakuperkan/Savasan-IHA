#include "ALC_Link_MCU.h"
ALC_Link_MCU_Class Veri_Alma;

//Serial Port Bağlantı Ayarları:
#define SERIAL_Port Serial2
#define SERIAL_BAUDRATE 57600

//Ayarlama Verileri:
float Dalma_Acisi = 32; //deg
float A_Yukseklik = 100; //metre
float Min_Yuksk = 35;

//Değişkenler:
float Veriler[30];
byte Asama_Sira = 1;
byte Koord_sirasi = 1;
uint32_t Alt_zmn = 0;
uint32_t iletim_zmn = 0;
// Derece -> Radyan
float radyan = PI / 180.0;
/*
   VERİ SIRALAMASI:
   0: UÇUŞ MOD
   1: ARAÇ ID
   2: ENLEM
   3: BOYLAM
   4: UYDU SAYISI
   5: GPS HIZ
   6: PUSULA
   7: KOORDİNAT GÖREV SAYISI
   8: OTONOM UÇUŞ GÖREV NO
   9: YUNUSLAMA VERİSİ
   10: YATIŞ VERİSİ
   11: YÜKSEKLİK VERİSİ
   12: VOLTAJ BİLGİSİ
   13: AKIM BİLGİSİ
   14: HARCANAN AKIM BİLGİSİ
   15: MOTOR ARM BİLGİSİ
   16: GPS DURUM
   17: ARAÇ TÜRÜ
   18: GPS SAAT
   19: GPS DAKİKA
   20: GPS SANİYE
   21: GİDİLEN TOPLAM YOL
   22: KART SICAKLIK BİLGİSİ
   23: GPS GÜN
   24: GPS AY
   25: GPS YIL
*/

struct Arac_Veriler {
  byte Ucus_Mod;
  byte Arac_ID;
  double Enlem;
  double Boylam;
  byte uydu_sayi;
  int GPS_hiz;
  float Pusula;
  byte Gorev_sayi;
  byte oto_gorev_no;
  int Yunuslama_Aci;
  int Yatis_Aci;
  float Yukseklik;
  float Voltaj;
  float Akim;
  float Harc_Akim;
  byte Motor_Arm_Drm;
  byte GPS_Drm;
  byte Arac_Tur;
  byte GPS_saat;
  byte GPS_dakika;
  byte GPS_saniye;
  float Gidilen_Yol;
  float Temp;
  byte GPS_Gun;
  byte GPS_Ay;
  byte GPS_Yil;
};
Arac_Veriler Alacakart_Veri;

void setup() {
  //LED setup
  LED_stp();

  //Başlangıç Ayarlama:
  Veri_Alma.ALC_Link_MCU_Begin(SERIAL_Port, SERIAL_BAUDRATE);

  //Başlama Renk:
  digitalWrite(PB13, 1);

  //Kart Haberleşme baudrate
  Serial1.begin(57600);
  delay(120000); //2.0dk sonra Takip, Başlangıç Bekleme süresi
}

void loop() {

  //LED kontrol:
  Dhl_Led_F1();

  Veri_Alma.ALC_Link_Datas(SERIAL_Port, Veriler); //byte dizisine atama yapılır
  Veri_Atama();

  //Minimum 6 uydu ve üzerinde takip komutu gönderilir:
  if (Alacakart_Veri.uydu_sayi >= 6 && Veri_Alma.ALC_Link_Conn == true)GPS_Takip_Alg();

  /*
    //GPS VERİLERİ:
    Serial.print(Alacakart_Veri.Enlem, 6); Serial.print(" | ");
    Serial.print(Alacakart_Veri.Boylam, 6); Serial.print(" | ");
    Serial.print(Alacakart_Veri.uydu_sayi); Serial.print(" | ");

    Serial.print(Alacakart_Veri.Yukseklik, 2); Serial.print(" | ");

    Serial.print(Alacakart_Veri.GPS_saat); Serial.print(":");
    Serial.print(Alacakart_Veri.GPS_dakika); Serial.print(":");
    Serial.print(Alacakart_Veri.GPS_saniye); Serial.print(" | ");
    Serial.print(Alacakart_Veri.GPS_Gun); Serial.print("/");
    Serial.print(Alacakart_Veri.GPS_Ay); Serial.print("/");
    Serial.print(Alacakart_Veri.GPS_Yil);

    Serial.println();
  */

  delay(20); //50Hz veri işleme
}

void GPS_Takip_Alg()
{
  if (millis() - iletim_zmn >= 1000) //1Hz
  {
    //Hedef: Enlem, Boylam, Yun Açısı, Yükseklik, Hız Oran(%)
    Komut_5_Dizilim(Alacakart_Veri.Enlem * pow(10, 6), Alacakart_Veri.Boylam * pow(10, 6), 8, Alacakart_Veri.Yukseklik, 45);

    iletim_zmn = millis();
  }
}
