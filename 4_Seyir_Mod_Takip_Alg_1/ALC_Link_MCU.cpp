#include "ALC_Link_MCU.h"

ALC_Link_MCU_Class::ALC_Link_MCU_Class() {}

void ALC_Link_MCU_Class::ALC_Link_MCU_Begin(HardwareSerial& port_, unsigned long b_rate_)
{
  port_.begin(b_rate_);
}

byte Gln_byte[50];
byte byte_sayici_ = 0;
bool datas_gunc = false;
uint32_t ALC_Link_Conn_zmn = 0;

void ALC_Link_MCU_Class::ALC_Link_Datas(HardwareSerial& port_, float* arry_)
{
  //SERİ HABERLEŞMEDEN VERİ GELMESİ BEKLENİR:
  byte_sayici_ = 0;
  while (port_.available() > 0)
  {
    Gln_byte[byte_sayici_] = (byte)port_.read();
    datas_gunc = true;
    byte_sayici_++;
    delayMicroseconds(125); //verinin tam olarak gitmesi için kullanılır.
  }

  if (datas_gunc == true)
  {
    ALC_Link_Conn = true;
    if (Gln_byte[0] == 84 && Gln_byte[31] == 42)
    {
      //***Öncü Veriler:
      //Araç Çalışma Modu Ataması:
      arry_[0] = Gln_byte[1];//manuel, denge, oto vs...

      //ARAÇ ID NO YAZDIRILMASI:
      arry_[1] = Gln_byte[3];

      //Enlem Bilgisi:
      arry_[2] = (double)byte_birlestirici_4_byte(Gln_byte[4], Gln_byte[5], Gln_byte[6], Gln_byte[7]) / 1000000.0;

      //Boylam Bilgisi:
      arry_[3] = (double)byte_birlestirici_4_byte(Gln_byte[8], Gln_byte[9], Gln_byte[10], Gln_byte[11]) / 1000000.0;

      //Uydu Bilgisi:
      arry_[4] = Gln_byte[12];

      //GPS Hız Bilgisi:
      arry_[5] = Gln_byte[13];

      //Pusula Bilgisi:
      arry_[6] = byte_birlestirici_2_byte(Gln_byte[14], Gln_byte[15]);

      //Koordinat_Sayi Bilgisi:
      arry_[7] = (byte)(Gln_byte[16] + 1) / 2.0;

      //Otonom Uçuş Görev Sırası:
      arry_[8] = Gln_byte[17];

      //Yunuslama Bilgisi:
      if (Gln_byte[18] == 0) arry_[9] = Gln_byte[19];
      else if (Gln_byte[18] == 1)
      {
        arry_[9] = Gln_byte[19];
        arry_[9] *= -1;
      }

      //Yatis Bilgisi:
      if (Gln_byte[20] == 0) arry_[10] = Gln_byte[21];
      else if (Gln_byte[20] == 1)
      {
        arry_[10] = Gln_byte[21];
        arry_[10] *= -1;
      }

      switch (Gln_byte[22])
      {
        case 0: //pkt 1
          //Yukseklik Verisi:
          arry_[11] = byte_birlestirici_2_byte(Gln_byte[23], Gln_byte[24]) / 10.0;

          //Voltaj Bilgisi:
          arry_[12] = byte_birlestirici_2_byte(Gln_byte[25], Gln_byte[26]) / 100.0;

          //Akim Bilgisi:
          arry_[13] = byte_birlestirici_2_byte(Gln_byte[27], Gln_byte[28]) / 10.0;

          //Harcanan akım değeri:
          arry_[14] = byte_birlestirici_2_byte(Gln_byte[29], Gln_byte[30]);
          break;

        case 1:
          //Motor Aktiflik durumu:
          arry_[15] = Gln_byte[23];

          //GPS Düzeltme Durumu:
          //GPS Durum verisinin gönderilmesi:
          /*3-bit veri 0 - 3 arası:
             1: --
             2: 2D
             3: 3D

             5-bit veri 0 - 63 arası:
          */
          arry_[16] = (byte)((Gln_byte[24] >> 5) & 0x07); // İlk 2 biti al

          //Araç Türü
          switch ((Gln_byte[24] & 0x1F)) // Son 6 biti al
          {
            case 0:
            case 1:
            case 2:
            case 3:
              arry_[17] = 0;
              break;
            case 4:
            case 5:
              arry_[17] = 1;
              break;
            case 6:
            case 7:
              arry_[17] = 2;
              break;
            case 8:
            case 9:
            case 10:
              arry_[17] = 3;
              break;

            default:
              arry_[17] = 0;
              break;
          }

          //GPS UTC Saat, dakika, saniye Değişken Ataması:
          arry_[18] = Gln_byte[26]; //saat
          arry_[19] = Gln_byte[27]; //dk
          arry_[20] = Gln_byte[28]; //sn

          //Gidilen Toplam Yol Alınması:
          arry_[21] = byte_birlestirici_2_byte(Gln_byte[29], Gln_byte[30]);
          break;

        case 2:
          //Kart Sıcaklık Bilgisi:
          arry_[22] = (byte)Gln_byte[23];

          //GPS Gün, Ay, Yıl Verisi:
          arry_[23] = (byte)Gln_byte[24]; //gün
          arry_[24] = (byte)Gln_byte[25]; //ay
          arry_[25] = (byte)Gln_byte[26]; //yıl
          break;
      }
    }

    //Veri temizleme:
    for (int i = 0; i < 32; i++) Gln_byte[i] = 0;
    datas_gunc = false;
  }

  //bağlantı kontrol
  if (millis() - ALC_Link_Conn_zmn > 3000)
  {
    ALC_Link_Conn = false;
    ALC_Link_Conn_zmn = millis();
  }
}

int32_t ALC_Link_MCU_Class::byte_birlestirici_4_byte(byte veri_byte_1, byte veri_byte_2, byte veri_byte_3, byte veri_byte_4)
{
  int32_t bir_byte_verisi = 0;
  bir_byte_verisi = (int32_t)(veri_byte_1 | veri_byte_2 << 8 | veri_byte_3 << 16 | veri_byte_4 << 24);

  return bir_byte_verisi;
}

int16_t ALC_Link_MCU_Class::byte_birlestirici_2_byte(byte veri_byte_1, byte veri_byte_2)
{
  int16_t bir_byte_verisi = 0;
  bir_byte_verisi = (int16_t)(veri_byte_1 | veri_byte_2 << 8);

  return bir_byte_verisi;
}
