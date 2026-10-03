//******************DAHİLİ LED KONTROL**********************************
unsigned long Dhl_led_zmn = 0;
#define LED_R PB13
#define LED_G PB14
#define LED_B PB12

void LED_stp()
{
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  digitalWrite(LED_R, 0);
  digitalWrite(LED_G, 0);
  digitalWrite(LED_B, 0);
}

void Dhl_Led_F1()
{
  /*
    1 kısa led aydınlatma işlemi
  */
  int LED_As = 75; //LED açık kalma süresi
  int LED_Ks = 1500; //LED Kapalı kalma süresi

  if (millis() - Dhl_led_zmn <= LED_As)
  {
    digitalWrite(LED_R, 0);
    digitalWrite(LED_G, 1);
    digitalWrite(LED_B, 0);
  }
  else if (millis() - Dhl_led_zmn > LED_As && millis() - Dhl_led_zmn <= LED_Ks)
  {
    digitalWrite(LED_R, 0);
    digitalWrite(LED_G, 0);
    digitalWrite(LED_B, 0);
  }
  else Dhl_led_zmn = millis();
}
