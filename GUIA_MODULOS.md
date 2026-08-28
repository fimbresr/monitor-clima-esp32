# Guía de operación: alta de módulos sensores

Esta guía documenta TODO el proceso para agregar cualquier módulo (cualquier placa,
cualquier sensor) al sistema: firmware, hoja de cálculo y dashboard.

**Regla de oro:** el `DEVICE_ID` es la llave maestra del sistema. Lo que pongas en el
firmware debe ser EXACTAMENTE igual a lo que registres en la pestaña `DISPOSITIVOS`.

---

## 1. Alta de un módulo nuevo — visión general

```
1. Programas la placa con un DEVICE_ID único      (tú, Arduino IDE)
2. La placa se conecta a WiFi y envía su lectura   (automático)
3. El servidor NO lo conoce → va a DISPOSITIVOS_PENDIENTES  (automático, cuarentena)
4. Lo registras en DISPOSITIVOS con su Área        (tú, hoja o dashboard)
5. Desde el siguiente envío sus datos van a HISTORIAL y ESTADO_ACTUAL  (automático)
6. Aparece en el dashboard filtrable por Área y Dispositivo  (automático)
```

---

## 2. Firmware (Arduino IDE) — qué cambiar en cada módulo

Bloque de configuración al inicio del `.ino`:

```cpp
const char* DEVICE_ID  = "D03";        // ÚNICO por placa. Nunca repitas.
const char* WIFI_SSID  = "TU_RED";
const char* WIFI_PASS  = "TU_PASSWORD";
const char* SCRIPT_URL = "https://script.google.com/macros/s/....../exec";  // igual para todos
const char* API_TOKEN  = "el-token-actual";  // igual para todos
```

| Campo | ¿Cambia por módulo? | Nota |
|---|---|---|
| `DEVICE_ID` | **SÍ, siempre** | Simple y estable: `QX1`, `D02`, `D03`... |
| `WIFI_SSID` / `WIFI_PASS` | Solo si cambia de red | |
| `SCRIPT_URL` | NO | Todos los módulos apuntan al mismo backend |
| `API_TOKEN` | NO | Si lo rotas, hay que reflashear TODOS los módulos |

### Combinaciones de hardware soportadas

| Placa | Sensor | Librerías a instalar | Pin de datos | Nota de placa en IDE |
|---|---|---|---|---|
| XIAO ESP32-C6 | HTU21D (I2C) | Adafruit HTU21DF | SDA=D4 (GPIO22), SCL=D5 (GPIO23) | XIAO_ESP32C6 |
| ESP32-C3 mini | DHT22 (1 cable) | DHT sensor library + Adafruit Unified Sensor | D2 (GPIO2) | ESP32C3 Dev Module + USB CDC On Boot: Enabled |

**Alimentación:** ambos sensores van a **3V3**, nunca a 5V.
**DHT22 pelado** (sin módulo): resistencia de 10k entre DATA y VCC.

---

## 3. La hoja de cálculo ("datos esp32")

⚠️ **El backend escribe por POSICIÓN de columna, no por nombre de encabezado.**
No reordenes, renombres ni agregues columnas. Si cambias los encabezados, los datos
quedan desalineados (ej. el área apareciendo bajo "Temperatura").

### Qué escribe cada quien

| Pestaña | ¿Quién escribe? | Orden fijo de columnas |
|---|---|---|
| `DISPOSITIVOS` | **TÚ** (registro manual o dashboard) | DeviceID, Area, Nombre, Activo, Tipo, Alta, Notas |
| `DISPOSITIVOS_PENDIENTES` | El servidor (automático) | DeviceID, PrimeraLectura, UltimaLectura, VecesVisto |
| `HISTORIAL` | El servidor (automático, 1 fila por envío) | Fecha, Dispositivo, Área, Temperatura, Humedad, RSSI |
| `ESTADO_ACTUAL` | El servidor (automático, 1 fila por dispositivo) | Dispositivo, Área, Temperatura, Humedad, Fecha, RSSI, Estado |

### Formato de celdas (tras mover encabezados, revisar esto)

- `ESTADO_ACTUAL`: Temperatura y Humedad → formato **Automático/Número**; Fecha → **Fecha y hora**
- `HISTORIAL`: igual (Temperatura/Humedad número, Fecha fecha)
- Si un número se ve como fecha "5/02/1900": Formato → Número → Automático

### Cuarentena (`DISPOSITIVOS_PENDIENTES`)

- Un DeviceID que envía datos y NO está en `DISPOSITIVOS` cae aquí automáticamente.
- Sus lecturas **no** se guardan en HISTORIAL ni ESTADO_ACTUAL hasta que lo registres.
- Cuando registras desde el dashboard, la fila pendiente se elimina sola.
  Si registras a mano en la hoja, borra la fila pendiente tú.

---

## 4. Registrar un dispositivo (dos formas)

### Opción A — Desde el dashboard (recomendada)

1. Abre el PWA → **Ajustes** → pega el **token** de administración (si aún no está) → Guardar
2. Sección **Administración de dispositivos** → en "Pendientes" pulsa **Registrar**
3. Llena Área (obligatorio), Nombre y Tipo → **Registrar**
4. El dispositivo queda activo y sale de pendientes automáticamente

### Opción B — A mano en la hoja

En `DISPOSITIVOS`, agrega una fila debajo de los encabezados:

| DeviceID | Area      | Nombre     | Activo | Tipo   |
|----------|-----------|------------|--------|--------|
| D03      | URGENCIAS | Sensor D03 | TRUE   | HTU21D |

- `Activo` debe ser `TRUE` (si es `FALSE`, el servidor rechaza sus datos)
- Borra tú la fila correspondiente de `DISPOSITIVOS_PENDIENTES`

**En el siguiente envío del módulo (~5 min):** sus datos entran a HISTORIAL y
ESTADO_ACTUAL, y aparece en el dashboard.

---

## 5. Dashboard (PWA)

- URL: https://fimbresr.github.io/monitor-clima-esp32/
- **Ajustes**: URL `/exec` del backend (ya viene por defecto) + token de administración
- **Filtros**: Área → Dispositivo → Rango (24 h / 7 d / 30 d)
- Refresco automático cada 60 s; sin internet muestra los últimos datos cacheados
- Las áreas del filtro salen de la columna `Area` de `DISPOSITIVOS` — registrar con
  un área nueva la agrega automáticamente al filtro

---

## 6. Intervalos y servidor

- El servidor dicta el intervalo de envío (`nextInterval` en cada respuesta)
- Para cambiarlo globalmente: editor de Apps Script → ejecutar
  `setIntervaloGlobal(300)` (300 = 5 minutos; rango permitido 30–600 s)
- No programar envíos más rápidos de 60 s: agota la cuota gratuita de Apps Script

---

## 7. Errores comunes (y su solución)

| Síntoma | Causa | Solución |
|---|---|---|
| El módulo aparece en PENDIENTES y no sube datos | DeviceID del firmware ≠ DeviceID en DISPOSITIVOS | Igualar exactamente ambos |
| Datos desalineados (área bajo "Temperatura") | Encabezados de la hoja movidos/renombrados | Restaurar encabezados al orden fijo de la sección 3 |
| Números que se ven como fecha 1900 | Formato de celda en fecha | Formato → Número → Automático |
| Dashboard "Sin conexión" | URL incorrecta en Ajustes o backend sin autorizar | Abrir la URL `/exec` en el navegador y autorizar; revisar Ajustes |
| Serial muestra `POST -> 302` y reintenta | Respuesta de Google al POST anónimo | El firmware actual ya lo acepta como éxito (no es error) |
| Serial muestra `TOKEN_INVALIDO` | Token rotado | Actualizar `API_TOKEN` en el firmware y reflashear |
| Ningún módulo envía tras cambiar el token | Token solo actualizado en un proyecto | El token vive por-proyecto en ScriptProperties; usa el del proyecto activo |
| Sensor no detectado (HTU21D) | Cableado/pull-ups | VCC→3V3, SDA→D4, SCL→D5, pull-ups 4.7k si el módulo no las trae |
| Lecturas DHT22 `nan` | Cable suelto o sensor recién energizado | Revisar DATA→D2; el código ignora lecturas fallidas |

---

## 8. Checklist rápido: alta de módulo nuevo

- [ ] Elegir `DEVICE_ID` único (ej. `D03`)
- [ ] Copiar el `.ino` del hardware correspondiente y cambiar solo el bloque de configuración
- [ ] Instalar librerías del sensor y seleccionar la placa correcta en el IDE
- [ ] Flashear y verificar en el Monitor Serial: sensor detectado + WiFi conectado
- [ ] Esperar el primer envío → verificar que aparece en `DISPOSITIVOS_PENDIENTES`
- [ ] Registrar en `DISPOSITIVOS` (dashboard u hoja) con DeviceID idéntico y su Área
- [ ] Verificar en `ESTADO_ACTUAL`/`HISTORIAL` que ya sube datos
- [ ] Verificar en el dashboard que aparece con su área y grafica
- [ ] Borrar su fila de `DISPOSITIVOS_PENDIENTES` (si registraste a mano)
