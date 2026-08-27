#include <Wire.h>

#define PIN_SDA 22
#define PIN_SCL 23

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("Escaneo I2C - XIAO ESP32-C6 (SDA=D4=GPIO22, SCL=D5=GPIO23)");
  Wire.begin(PIN_SDA, PIN_SCL);
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
}

void loop() {
  byte error;
  int nDevices = 0;

  Serial.println("Escaneando bus I2C...");
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Dispositivo encontrado en 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      switch (address) {
        case 0x40:
          Serial.println("  <- HTU21D / SI7021 (el esperado)");
          break;
        case 0x38:
          Serial.println("  <- AHT10/AHT20 (sensor distinto al esperado)");
          break;
        case 0x44:
          Serial.println("  <- SHT30/SHT31 (sensor distinto al esperado)");
          break;
        case 0x76:
        case 0x77:
          Serial.println("  <- BMP/BME280");
          break;
        default:
          Serial.println();
          break;
      }
      nDevices++;
    }
  }

  if (nDevices == 0) {
    Serial.println("Ningun dispositivo en el bus.");
    Serial.println("Revisa: VCC->3V3 (no 5V), GND->GND, SDA->D4, SCL->D5,");
    Serial.println("y resistencias pull-up de 4.7k de SDA/SCL hacia 3V3");
    Serial.println("si el modulo no las trae.");
  }

  Serial.println("-----------------------------");
  delay(3000);
}
