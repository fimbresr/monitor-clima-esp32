# Monitor de Clima — ESP32 + Google Sheets + Dashboard

Sistema de monitoreo de temperatura y humedad: módulos ESP32 con sensores (HTU21D/DHT22)
envían lecturas por WiFi a un backend de Google Apps Script, que las guarda en una hoja de
Google Sheets ("datos esp32") y sirve un dashboard web con gráficas por área y dispositivo.

**📚 Guía de operación:** para dar de alta cualquier módulo nuevo (firmware, registro en
hoja, dashboard y solución de errores comunes) lee [GUIA_MODULOS.md](GUIA_MODULOS.md).

## Estructura

```
apps-script/
  Code.gs           Backend: API de ingesta (doPost) + dashboard (doGet) + registro de dispositivos
  appsscript.json   Manifiesto (Web App pública, ejecuta como tú, tz America/Hermosillo)
  dashboard.html    Dashboard legacy servido por Apps Script (HtmlService)
docs/               PWA publicada en GitHub Pages (dashboard principal)
  index.html        PWA: Chart.js + JSONP contra el backend de Apps Script
  manifest.webmanifest
  sw.js             Service worker (offline: muestra los últimos datos cacheados)
  icons/            Iconos 192/512
firmware/esp32_htu21d_logger/
  esp32_htu21d_logger.ino   Código Arduino para cada módulo ESP32-C6
```

## Flujo de datos

- El ESP32 lee el HTU21D cada 2 s, acumula en un buffer y envía el promedio cada 60 s (el
  servidor puede cambiar este intervalo en cada respuesta).
- `doPost` valida el token → busca el DeviceID en `DISPOSITIVOS` → agrega fila en `HISTORIAL`
  y actualiza la fila del dispositivo en `ESTADO_ACTUAL`.
- Un DeviceID desconocido **no** entra al panel: se registra en `DISPOSITIVOS_PENDIENTES`
  hasta que lo actives en el dashboard o directamente en la hoja.
- El dashboard (`doGet`) muestra gráficas con filtro por Área, Dispositivo y rango de tiempo,
  tabla de estado actual y un panel de administración (registro y activación de dispositivos).

## 1. Crear el proyecto en Apps Script

### Opción A — con clasp (recomendada)

```bash
cd "apps-script"
clasp login                 # solo la primera vez (ya debería tener sesión)
clasp create --type standalone --title "datos esp32 backend"
clasp push
```

### Opción B — manual

1. Abre https://script.google.com y crea un proyecto nuevo.
2. Pega el contenido de `Code.gs` en `Code.gs`.
3. Crea un archivo HTML llamado `dashboard` y pega el contenido de `dashboard.html`.
4. En Configuración del proyecto: marca "Mostrar archivo appsscript.json" y pega el contenido
   de `appsscript.json`.

## 2. Configurar el backend

1. En el editor, ejecuta una vez la función `configurar` (menú desplegable de funciones).
   Esto genera un token y lo guarda. Copia el token del log (Vista > Registros) o del valor
   devuelto; va en cada ESP32.
   - Para invalidar todos los módulos: ejecuta `rotarToken`.
2. Verifica que `CONFIG.SHEET_ID` en `Code.gs` sea tu hoja:
   `1GAVrxohwDdz-0GAcrz_kIC5J14_81H7K3RZirZQLSsE`.
3. Desplegar como Web App:
   - **Implementar > Nueva implementación > Web app**
   - Ejecutar como: **Yo** · Acceso: **Cualquier persona**
   - Copia la URL `/exec` (la usarás en el firmware y para abrir el dashboard).

## 3. Programar cada módulo ESP32

1. Abre `firmware/esp32_htu21d_logger/esp32_htu21d_logger.ino` en Arduino IDE.
2. Instala la librería **Adafruit HTU21DF** (Gestor de librerías).
3. Selecciona la placa **XIAO ESP32-C6** (requiere el core ESP32 v3.x de Espressif).
4. Edita el bloque de configuración de CADA módulo:
   - `DEVICE_ID`: identificador único, p. ej. `D01`, `D02`...
   - `WIFI_SSID` / `WIFI_PASS`: tu red.
   - `SCRIPT_URL`: la URL `/exec` del paso anterior.
   - `API_TOKEN`: el token generado con `configurar`.
5. Compila y sube.

Comportamiento ante fallas: reconexión WiFi automática, reintentos con espera creciente,
buffer de hasta 30 lecturas en RAM mientras no hay red, y reinicio si lleva más de 15 min
desconectado.

## 4. Registrar los dispositivos

Opción A — desde el dashboard: abre la URL `/exec` en el navegador, sección
"Administración de dispositivos" → los módulos que ya enviaron lecturas aparecen en
"pendientes"; usa el botón Registrar y define Área y Nombre.

Opción B — directamente en la hoja `DISPOSITIVOS`:

| DeviceID | Area    | Nombre        | Activo | Tipo   |
|----------|---------|---------------|--------|--------|
| D01      | COCINA  | Sensor cocina | TRUE   | HTU21D |

## 5. Verificar

- Dashboard Apps Script (legacy): abre la URL `/exec` → verás el dashboard HtmlService.
- PWA en GitHub Pages: `https://<usuario>.github.io/monitor-clima-esp32/` → en **Ajustes**
  pega la URL `/exec` de tu Apps Script (queda guardada solo en tu navegador) y, si vas a
  administrar dispositivos, el token.
- Añade `?route=api` a la URL `/exec` para ver el JSON crudo del último estado (diagnóstico).
- En la hoja: `HISTORIAL` debe crecer con cada envío; `ESTADO_ACTUAL` mantiene una fila por
  dispositivo con su última lectura.

## 6. PWA en GitHub Pages

El PWA vive en `docs/` y se sirve como sitio estático desde GitHub Pages (Settings > Pages >
Source: Deploy from a branch > `main` / `/docs`). Es instalable (agrega a pantalla de inicio)
y funciona sin conexión mostrando los últimos datos cacheados por el service worker.

Cómo se comunica con Apps Script sin CORS:
- **Lecturas (JSONP)**: el PWA pide datos vía `GET route=api&accion=...&callback=...`
  (etiquetas `<script>`, no requieren CORS).
- **Escrituras admin (POST no-cors)**: registrar/activar dispositivos envía un POST
  `text/plain` (sin preflight) y confirma releyendo con JSONP.

**Seguridad del token:** el token JAMÁS va en este repositorio público. El PWA lo pide en
Ajustes y lo guarda en `localStorage` del navegador. La ingesta de datos de los ESP32 sigue
exigiendo el token en el POST; la lectura de datos es pública por diseño. Para invalidar
accesos: ejecuta `rotarToken` en el editor y actualiza el token en cada ESP32 y en el PWA.

## Notas y límites

- Cuota gratuita de Apps Script: ~90 min de ejecución/día y 30 ejecuciones simultáneas.
  Con el intervalo por defecto de 60 s, cada módulo genera ~1440 requests/día. Para muchos
  módulos, sube el intervalo con la propiedad `API_INTERVAL` (en segundos):
  ejecuta en el editor `setIntervaloGlobal(n)` o cambia el valor en Propiedades del proyecto.
- El token viaja dentro del JSON del POST (los Web Apps de Apps Script no exponen headers
  personalizados).
- La fecha/hora la asigna el servidor (America/Hermosillo); el reloj del ESP32 solo se usa
  como diagnóstico (`epoch`).
- Historial grande: el dashboard lee como máximo las últimas 5000 filas; si la hoja crece
  mucho, archiva `HISTORIAL` por año manualmente.


 token: 90484f2b35624865bfecb58ac7e7f01051d2078bd0d145a58df40f3fbc9e70ec
