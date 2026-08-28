#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>

// ==================== CONFIGURACIÓN POR MÓDULO ====================
const char* DEVICE_ID  = "D02";
const char* WIFI_SSID  = "HOSPITAL SAN DIEGO";
const char* WIFI_PASS  = "SanDiego#23";
const char* SCRIPT_URL = "https://script.google.com/macros/s/AKfycbwCLYlBLa953tX1BhUVcqi49vX06riRtawxMGw2AXY7EkrWtT0VyW6oS7faQaPt6oLI/exec";
const char* API_TOKEN  = "dbff22909f32453d8259c738e079ad1901638e2c48da480892d1bdf449912406";
// ==================================================================

#define DHT_PIN 2        // pin de DATOS del DHT22 (silk D2 = GPIO2 en la C3 mini)
#define DHT_TYPE DHT22

const unsigned long SENSOR_PERIOD_MS = 3000;   // el DHT22 no admite lecturas más rápidas
const unsigned long SEND_INTERVAL_MS = 300000; // 5 minutos
const unsigned long WIFI_RETRY_MS   = 10000;
const unsigned long MAX_OFFLINE_MS  = 900000;
const int BUFFER_SIZE = 30;

DHT dht(DHT_PIN, DHT_TYPE);

struct Lectura { float temp; float hum; };
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

  bool exito = (codigo == 200 && respuesta.indexOf("\"ok\":true") >= 0) || (codigo >= 300 && codigo < 400);
  if (exito) {
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
  Serial.println("ESP32-C3 mini + DHT22 -> Google Sheets");
  Serial.print("DeviceID: ");
  Serial.println(DEVICE_ID);

  dht.begin();
  delay(2000);

  float t = dht.readTemperature();
  if (isnan(t)) {
    Serial.println("ADVERTENCIA: primera lectura del DHT22 falló (es normal al inicio).");
  } else {
    Serial.println("DHT22 respondiendo.");
  }

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
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
      Serial.println("Lectura DHT22 fallida, se ignora.");
    } else {
      anadirLectura(t, h);
    }
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
