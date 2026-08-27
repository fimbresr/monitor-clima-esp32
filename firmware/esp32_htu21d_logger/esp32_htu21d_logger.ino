#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "Adafruit_HTU21DF.h"

// ==================== CONFIGURACIÓN POR MÓDULO ====================
// Crea un archivo config_local.h en esta carpeta con tus valores reales.
// Ese archivo NO se sube a GitHub (está en .gitignore).
#if __has_include("config_local.h")
  #include "config_local.h"
#else
  const char* DEVICE_ID  = "D01";
  const char* WIFI_SSID  = "TU_RED_WIFI";
  const char* WIFI_PASS  = "TU_CONTRASENA";
  const char* SCRIPT_URL = "https://script.google.com/macros/s/AKfycbXXXXXXXXXXXXXXXXXXXXXXX/exec";
  const char* API_TOKEN  = "TOKEN_GENERADO_EN_APPS_SCRIPT";
#endif
// ==================================================================

#define PIN_SDA 22   // pad D4 en el XIAO ESP32-C6
#define PIN_SCL 23   // pad D5 en el XIAO ESP32-C6

const unsigned long SENSOR_PERIOD_MS = 2000;
const unsigned long SEND_INTERVAL_MS = 60000;
const unsigned long WIFI_RETRY_MS   = 10000;
const unsigned long MAX_OFFLINE_MS  = 900000;
const int BUFFER_SIZE = 30;

Adafruit_HTU21DF htu = Adafruit_HTU21DF();

struct Par {
  int sda;
  int scl;
  const char* nombre;
};

Par pares[] = {
  {22, 23, "22/23 (C6: D4/D5)"},
  {4, 5,   "4/5   (S3: D4/D5)"},
  {8, 9,   "8/9   (S3: SDA/SCL)"},
  {6, 7,   "6/7   (C3: D4/D5)"},
  {21, 22, "21/22 (devboard clasico)"}
};

void escanearI2C() {
  Serial.println("Dispositivos en el bus I2C por cada par de pines:");
  for (int i = 0; i < 5; i++) {
    Wire.end();
    Wire.begin(pares[i].sda, pares[i].scl);
    pinMode(pares[i].sda, INPUT_PULLUP);
    pinMode(pares[i].scl, INPUT_PULLUP);
    Serial.print("  SDA/SCL ");
    Serial.print(pares[i].nombre);
    Serial.print(": ");
    int n = 0;
    for (byte a = 1; a < 127; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        Serial.print("0x");
        if (a < 16) Serial.print("0");
        Serial.print(a, HEX);
        if (a == 0x40) Serial.print("(HTU21D)");
        if (a == 0x38) Serial.print("(AHT20)");
        if (a == 0x44) Serial.print("(SHT30)");
        Serial.print(" ");
        n++;
      }
    }
    if (n == 0) Serial.print("nada");
    Serial.println();
    delay(200);
  }
  Wire.end();
  Wire.begin(PIN_SDA, PIN_SCL);
  Serial.println("Si aparece 0x40 en un par distinto a 22/23, usa esos numeros en PIN_SDA/PIN_SCL.");
  Serial.println("Si no aparece nada: revisa VCC->3V3, GND y pull-ups de 4.7k.");
}

struct Lectura {
  float temp;
  float hum;
};

Lectura buffer[BUFFER_SIZE];
int bufCount = 0;

unsigned long lastRead = 0;
unsigned long lastSend = 0;
unsigned long lastWifiTry = 0;
unsigned long offlineSince = 0;
unsigned long sendInterval = SEND_INTERVAL_MS;
unsigned long retryDelay = 10000;

void conectarWifi() {
  Serial.print("Conectando a WiFi ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 30000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Conectado. IP: ");
    Serial.println(WiFi.localIP());
    configTime(0, 0, "pool.ntp.org");
  } else {
    Serial.println("No se pudo conectar, se reintentará.");
  }
}

void anadirLectura(float temp, float hum) {
  if (isnan(temp) || isnan(hum)) return;
  if (bufCount < BUFFER_SIZE) {
    buffer[bufCount].temp = temp;
    buffer[bufCount].hum = hum;
    bufCount++;
  } else {
    for (int i = 1; i < BUFFER_SIZE; i++) buffer[i - 1] = buffer[i];
    buffer[BUFFER_SIZE - 1].temp = temp;
    buffer[BUFFER_SIZE - 1].hum = hum;
  }
}

void promedioLecturas(float* temp, float* hum) {
  float st = 0, sh = 0;
  for (int i = 0; i < bufCount; i++) {
    st += buffer[i].temp;
    sh += buffer[i].hum;
  }
  *temp = st / bufCount;
  *hum = sh / bufCount;
}

long extraerEntero(const String& texto, const char* clave) {
  String busqueda = "\"" + String(clave) + "\"";
  int pos = texto.indexOf(busqueda);
  if (pos < 0) return -1;
  pos = texto.indexOf(':', pos);
  if (pos < 0) return -1;
  while (pos + 1 < (int)texto.length() && !isdigit(texto[pos + 1]) && texto[pos + 1] != '-') pos++;
  return texto.substring(pos + 1).toInt();
}

bool enviarLecturas(float temp, float hum) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(SCRIPT_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);

  time_t ahora = time(nullptr);
  unsigned long epoch = (ahora > 1600000000) ? (unsigned long)ahora : 0;
  int rssi = WiFi.RSSI();

  char cuerpo[256];
  snprintf(cuerpo, sizeof(cuerpo),
           "{\"token\":\"%s\",\"device\":\"%s\",\"temp\":%.2f,\"hum\":%.2f,\"rssi\":%d,\"epoch\":%lu}",
           API_TOKEN, DEVICE_ID, temp, hum, rssi, epoch);

  int codigo = http.POST(cuerpo);
  String respuesta = http.getString();
  http.end();

  Serial.print("POST -> ");
  Serial.print(codigo);
  Serial.print(" | ");
  Serial.println(respuesta);

  if (codigo == 200 && respuesta.indexOf("\"ok\":true") >= 0) {
    long intervalo = extraerEntero(respuesta, "nextInterval");
    if (intervalo >= 30 && intervalo <= 600) {
      sendInterval = intervalo * 1000UL;
      Serial.print("Intervalo ajustado por servidor: ");
      Serial.print(intervalo);
      Serial.println(" s");
    }
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("XIAO ESP32-C6 + HTU21D -> Google Sheets");
  Serial.print("DeviceID: ");
  Serial.println(DEVICE_ID);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!htu.begin()) {
    Serial.println("ERROR: No se detecta el sensor HTU21D.");
    escanearI2C();
    while (true) delay(1000);
  }
  Serial.println("HTU21D detectado.");

  conectarWifi();
  lastSend = millis();
}

void loop() {
  unsigned long ahora = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (offlineSince == 0) offlineSince = ahora;
    if (ahora - lastWifiTry >= WIFI_RETRY_MS) {
      lastWifiTry = ahora;
      Serial.println("Reintentando conexión WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
    if (ahora - offlineSince > MAX_OFFLINE_MS) {
      Serial.println("Demasiado tiempo sin WiFi, reiniciando.");
      ESP.restart();
    }
  } else {
    offlineSince = 0;
  }

  if (ahora - lastRead >= SENSOR_PERIOD_MS) {
    lastRead = ahora;
    anadirLectura(htu.readTemperature(), htu.readHumidity());
  }

  if (bufCount > 0 && ahora - lastSend >= sendInterval) {
    float t, h;
    promedioLecturas(&t, &h);
    Serial.print("Enviando promedio de ");
    Serial.print(bufCount);
    Serial.print(" lecturas: ");
    Serial.print(t, 2);
    Serial.print(" °C / ");
    Serial.print(h, 2);
    Serial.println(" %");

    if (enviarLecturas(t, h)) {
      bufCount = 0;
      retryDelay = 10000;
      lastSend = ahora;
    } else {
      lastSend = ahora + retryDelay - sendInterval;
      retryDelay = retryDelay * 2;
      if (retryDelay > 600000) retryDelay = 600000;
      Serial.print("Envío fallido, reintento en ");
      Serial.print(retryDelay / 1000);
      Serial.println(" s (lecturas guardadas en buffer)");
    }
  }

  delay(50);
}
