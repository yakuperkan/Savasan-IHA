#include "Arduino.h"


class ALC_Link_MCU_Class
{
  public:
    ALC_Link_MCU_Class();
    bool ALC_Link_Conn = false;
    void ALC_Link_MCU_Begin(HardwareSerial& port_, unsigned long b_rate_);
    void ALC_Link_Datas(HardwareSerial& port_, float* arry);
  private:
    int32_t byte_birlestirici_4_byte(byte veri_byte_1, byte veri_byte_2, byte veri_byte_3, byte veri_byte_4);
    int16_t byte_birlestirici_2_byte(byte veri_byte_1, byte veri_byte_2);
};
