/** CONFIGURACIÓN GENERAL */
const CONFIG = {
  SHEET_ID: '1GAVrxohwDdz-0GAcrz_kIC5J14_81H7K3RZirZQLSsE',
  TOKEN_PROP: 'API_TOKEN',
  INTERVAL_PROP: 'API_INTERVAL',
  TZ: 'America/Hermosillo',
  TAB_ESTADO: 'ESTADO_ACTUAL',
  TAB_HISTORIAL: 'HISTORIAL',
  TAB_DISPOSITIVOS: 'DISPOSITIVOS',
  TAB_PENDIENTES: 'DISPOSITIVOS_PENDIENTES',
  INTERVAL_DEFAULT: 60,
  INTERVAL_MIN: 30,
  INTERVAL_MAX: 600,
  MAX_HISTORY_ROWS: 5000,
  MAX_CHART_POINTS: 1500
};

/** Función de configuración inicial: ejecutar UNA VEZ desde el editor.
 *  Genera un token aleatorio, lo guarda y lo imprime en el log. */
function configurar() {
  const props = PropertiesService.getScriptProperties();
  const token = Utilities.getUuid().replace(/-/g, '') + Utilities.getUuid().replace(/-/g, '');
  props.setProperty(CONFIG.TOKEN_PROP, token);
  props.setProperty(CONFIG.INTERVAL_PROP, String(CONFIG.INTERVAL_DEFAULT));
  Logger.log('TOKEN generado (cópialo en cada ESP32): ' + token);
  return token;
}

/** Reemplaza el token actual por uno nuevo (invalida los anteriores). */
function rotarToken() {
  return configurar();
}

/** Cambia el intervalo de envío (segundos) que el servidor ordena a los módulos. */
function setIntervaloGlobal(segundos) {
  const n = Math.min(CONFIG.INTERVAL_MAX, Math.max(CONFIG.INTERVAL_MIN, parseInt(segundos, 10)));
  PropertiesService.getScriptProperties().setProperty(CONFIG.INTERVAL_PROP, String(n));
  return n;
}

function getSpreadsheet_() {
  return SpreadsheetApp.openById(CONFIG.SHEET_ID);
}

function jsonResponse_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function intervaloConfigurado_() {
  const props = PropertiesService.getScriptProperties();
  const raw = props.getProperty(CONFIG.INTERVAL_PROP);
  const n = parseInt(raw || String(CONFIG.INTERVAL_DEFAULT), 10);
  if (isNaN(n)) return CONFIG.INTERVAL_DEFAULT;
  return Math.min(CONFIG.INTERVAL_MAX, Math.max(CONFIG.INTERVAL_MIN, n));
}

function contarEjecucion_() {
  const props = PropertiesService.getScriptProperties();
  const key = 'EXECS_' + Utilities.formatDate(new Date(), CONFIG.TZ, 'yyyyMMdd');
  const total = parseInt(props.getProperty(key) || '0', 10) + 1;
  props.setProperty(key, String(total));
}

/** API de ingesta de lecturas (la llaman los ESP32 por POST). */
function doPost(e) {
  let payload;
  try {
    payload = JSON.parse(e.postData.contents);
  } catch (err) {
    return jsonResponse_({ ok: false, error: 'JSON_INVALIDO' });
  }

  const props = PropertiesService.getScriptProperties();
  const tokenGuardado = props.getProperty(CONFIG.TOKEN_PROP);
  if (!tokenGuardado || String(payload.token) !== tokenGuardado) {
    return jsonResponse_({ ok: false, error: 'TOKEN_INVALIDO' });
  }

  const accion = String(payload.accion || '');
  if (accion === 'registrar') {
    try {
      registrarDispositivo(payload.device, payload.area, payload.nombre, payload.tipo);
      return jsonResponse_({ ok: true });
    } catch (err) {
      return jsonResponse_({ ok: false, error: 'REGISTRO_FALLIDO' });
    }
  }
  if (accion === 'activar') {
    try {
      setDispositivoActivo(payload.device, payload.activo === true || payload.activo === 'true');
      return jsonResponse_({ ok: true });
    } catch (err) {
      return jsonResponse_({ ok: false, error: 'ACTIVAR_FALLIDO' });
    }
  }

  const deviceId = String(payload.device || '').trim().toUpperCase();
  const temp = Number(payload.temp);
  const hum = Number(payload.hum);
  const rssi = Number(payload.rssi);
  if (!deviceId || isNaN(temp) || isNaN(hum)) {
    return jsonResponse_({ ok: false, error: 'DATOS_INVALIDOS' });
  }

  contarEjecucion_();
  const now = new Date();
  const ss = getSpreadsheet_();
  const dispositivo = buscarDispositivo_(ss, deviceId);

  if (!dispositivo) {
    registrarPendiente_(ss, deviceId, now);
    return jsonResponse_({ ok: false, error: 'DISPOSITIVO_NO_REGISTRADO', nextInterval: intervaloConfigurado_() });
  }
  if (!dispositivo.activo) {
    return jsonResponse_({ ok: false, error: 'DISPOSITIVO_INACTIVO', nextInterval: intervaloConfigurado_() });
  }

  const sheetHist = ss.getSheetByName(CONFIG.TAB_HISTORIAL);
  sheetHist.appendRow([now, deviceId, dispositivo.area, temp, hum, rssi]);

  let upsertOk = false;
  const sheetEstado = ss.getSheetByName(CONFIG.TAB_ESTADO);
  const lock = LockService.getScriptLock();
  if (lock.tryLock(10000)) {
    try {
      upsertEstado_(sheetEstado, deviceId, dispositivo.area, temp, hum, now, rssi);
      upsertOk = true;
    } finally {
      lock.releaseLock();
    }
  }

  return jsonResponse_({ ok: true, nextInterval: intervaloConfigurado_(), upsert: upsertOk });
}

function buscarDispositivo_(ss, deviceId) {
  const sheet = ss.getSheetByName(CONFIG.TAB_DISPOSITIVOS);
  const data = sheet.getDataRange().getValues();
  for (let i = 1; i < data.length; i++) {
    const fila = data[i];
    const id = String(fila[0] || '').trim().toUpperCase();
    if (id === deviceId) {
      return {
        deviceId: id,
        area: String(fila[1] || ''),
        activo: esActivo_(fila[3])
      };
    }
  }
  return null;
}

function esActivo_(valor) {
  if (typeof valor === 'boolean') return valor;
  const v = String(valor || '').trim().toUpperCase();
  return v === 'TRUE' || v === 'SI' || v === 'SÍ' || v === '1' || v === 'ACTIVO';
}

function upsertEstado_(sheet, deviceId, area, temp, hum, fecha, rssi) {
  const data = sheet.getDataRange().getValues();
  const estado = 'ONLINE';
  let filaEncontrada = -1;
  for (let i = 1; i < data.length; i++) {
    if (String(data[i][0] || '').trim().toUpperCase() === deviceId) {
      filaEncontrada = i + 1;
      break;
    }
  }
  const valores = [deviceId, area, temp, hum, fecha, rssi, estado];
  if (filaEncontrada === -1) {
    sheet.appendRow(valores);
  } else {
    sheet.getRange(filaEncontrada, 1, 1, 7).setValues([valores]);
  }
}

function registrarPendiente_(ss, deviceId, ahora) {
  const sheet = ss.getSheetByName(CONFIG.TAB_PENDIENTES);
  const data = sheet.getDataRange().getValues();
  let fila = -1;
  let veces = 0;
  for (let i = 1; i < data.length; i++) {
    if (String(data[i][0] || '').trim().toUpperCase() === deviceId) {
      fila = i + 1;
      veces = Number(data[i][3] || 0);
      break;
    }
  }
  if (fila === -1) {
    sheet.appendRow([deviceId, ahora, ahora, 1]);
  } else {
    sheet.getRange(fila, 3, 1, 2).setValues([[ahora, veces + 1]]);
  }
}

/** Punto de entrada del navegador: dashboard, API JSON de lectura (CORS-free vía JSONP)
 *  o dashboard HtmlService.
 *  Parámetros: route=api, accion=areas|dispositivos|estado|historial|pendientes|config,
 *  callback=<nombre> para JSONP, device/deviceId y horas para historial. */
function doGet(e) {
  const route = String((e && e.parameter && e.parameter.route) || '');
  if (route === 'api') {
    const accion = String((e && e.parameter && e.parameter.accion) || '');
    const callback = (e && e.parameter && e.parameter.callback) || '';
    let data;
    if (accion === 'areas') {
      data = { areas: getAreas() };
    } else if (accion === 'dispositivos') {
      data = { dispositivos: getDispositivos() };
    } else if (accion === 'estado') {
      data = { estado: getUltimoEstado() };
    } else if (accion === 'historial') {
      const device = String((e && e.parameter.device) || '');
      const horas = Math.min(720, Math.max(1, parseInt(e.parameter.horas, 10) || 24));
      data = { puntos: getHistorial(device, horas) };
    } else if (accion === 'pendientes') {
      data = { pendientes: getPendientes() };
    } else if (accion === 'config') {
      data = { config: getConfiguracion() };
    } else {
      data = { ultimoEstado: getUltimoEstado() };
    }
    if (callback && /^[A-Za-z_$][\w$]*$/.test(callback)) {
      return ContentService.createTextOutput(callback + '(' + JSON.stringify(data) + ');');
    }
    return jsonResponse_(data);
  }
  return HtmlService.createHtmlOutputFromFile('dashboard')
    .setTitle('Monitor de Clima')
    .addMetaTag('viewport', 'width=device-width, initial-scale=1')
    .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL);
}

/** --- Funciones para el dashboard (google.script.run) --- */

function getAreas() {
  const ss = getSpreadsheet_();
  const data = ss.getSheetByName(CONFIG.TAB_DISPOSITIVOS).getDataRange().getValues();
  const areas = [];
  for (let i = 1; i < data.length; i++) {
    const area = String(data[i][1] || '').trim();
    if (area && areas.indexOf(area) === -1) areas.push(area);
  }
  return areas.sort();
}

function getDispositivos() {
  const ss = getSpreadsheet_();
  const data = ss.getSheetByName(CONFIG.TAB_DISPOSITIVOS).getDataRange().getValues();
  const lista = [];
  for (let i = 1; i < data.length; i++) {
    lista.push({
      deviceId: String(data[i][0] || '').trim().toUpperCase(),
      area: String(data[i][1] || '').trim(),
      nombre: String(data[i][2] || '').trim(),
      activo: esActivo_(data[i][3]),
      tipo: String(data[i][4] || '').trim(),
      alta: data[i][5] instanceof Date ? data[i][5].toISOString() : ''
    });
  }
  return lista;
}

function getUltimoEstado() {
  const ss = getSpreadsheet_();
  const data = ss.getSheetByName(CONFIG.TAB_ESTADO).getDataRange().getValues();
  const lista = [];
  for (let i = 1; i < data.length; i++) {
    lista.push({
      deviceId: String(data[i][0] || '').trim().toUpperCase(),
      area: String(data[i][1] || '').trim(),
      temp: Number(data[i][2]),
      hum: Number(data[i][3]),
      fecha: data[i][4] instanceof Date ? data[i][4].toISOString() : '',
      rssi: Number(data[i][5]),
      estado: String(data[i][6] || '')
    });
  }
  return lista;
}

function getHistorial(deviceId, horas) {
  const ss = getSpreadsheet_();
  const sheet = ss.getSheetByName(CONFIG.TAB_HISTORIAL);
  const lastRow = sheet.getLastRow();
  if (lastRow < 2) return [];
  const startRow = Math.max(2, lastRow - CONFIG.MAX_HISTORY_ROWS + 1);
  const data = sheet.getRange(startRow, 1, lastRow - startRow + 1, 6).getValues();
  const limite = new Date(Date.now() - horas * 3600000);
  const id = String(deviceId || '').trim().toUpperCase();
  const puntos = [];
  for (let i = data.length - 1; i >= 0; i--) {
    const fila = data[i];
    const fecha = fila[0];
    if (!(fecha instanceof Date)) continue;
    if (id && String(fila[1] || '').trim().toUpperCase() !== id) continue;
    if (fecha < limite) continue;
    puntos.push([fecha.toISOString(), Number(fila[3]), Number(fila[4]), Number(fila[5])]);
    if (puntos.length >= CONFIG.MAX_CHART_POINTS) break;
  }
  return puntos.reverse();
}

function getPendientes() {
  const ss = getSpreadsheet_();
  const data = ss.getSheetByName(CONFIG.TAB_PENDIENTES).getDataRange().getValues();
  const lista = [];
  for (let i = 1; i < data.length; i++) {
    lista.push({
      deviceId: String(data[i][0] || '').trim().toUpperCase(),
      primera: data[i][1] instanceof Date ? data[i][1].toISOString() : '',
      ultima: data[i][2] instanceof Date ? data[i][2].toISOString() : '',
      veces: Number(data[i][3] || 0)
    });
  }
  return lista;
}

/** Registra o actualiza un dispositivo en el catálogo (quita de pendientes si estaba). */
function registrarDispositivo(deviceId, area, nombre, tipo) {
  const id = String(deviceId || '').trim().toUpperCase();
  const areaOk = String(area || '').trim();
  if (!id || !areaOk) {
    throw new Error('DeviceID y Área son obligatorios');
  }
  const ss = getSpreadsheet_();
  const sheet = ss.getSheetByName(CONFIG.TAB_DISPOSITIVOS);
  const data = sheet.getDataRange().getValues();
  let fila = -1;
  for (let i = 1; i < data.length; i++) {
    if (String(data[i][0] || '').trim().toUpperCase() === id) {
      fila = i + 1;
      break;
    }
  }
  const ahora = new Date();
  const valores = [id, areaOk, String(nombre || '').trim(), 'TRUE', String(tipo || '').trim(), ahora, ''];
  if (fila === -1) {
    sheet.appendRow(valores);
  } else {
    sheet.getRange(fila, 1, 1, 6).setValues([valores.slice(0, 6)]);
  }
  quitarPendiente_(ss, id);
  return { ok: true, deviceId: id };
}

function setDispositivoActivo(deviceId, activo) {
  const ss = getSpreadsheet_();
  const sheet = ss.getSheetByName(CONFIG.TAB_DISPOSITIVOS);
  const data = sheet.getDataRange().getValues();
  const id = String(deviceId || '').trim().toUpperCase();
  for (let i = 1; i < data.length; i++) {
    if (String(data[i][0] || '').trim().toUpperCase() === id) {
      sheet.getRange(i + 1, 4).setValue(activo ? 'TRUE' : 'FALSE');
      return { ok: true };
    }
  }
  throw new Error('Dispositivo no encontrado');
}

function quitarPendiente_(ss, deviceId) {
  const sheet = ss.getSheetByName(CONFIG.TAB_PENDIENTES);
  const data = sheet.getDataRange().getValues();
  for (let i = data.length - 1; i >= 1; i--) {
    if (String(data[i][0] || '').trim().toUpperCase() === deviceId) {
      sheet.deleteRow(i + 1);
    }
  }
}

function getConfiguracion() {
  const props = PropertiesService.getScriptProperties();
  const key = 'EXECS_' + Utilities.formatDate(new Date(), CONFIG.TZ, 'yyyyMMdd');
  return {
    intervalo: intervaloConfigurado_(),
    ejecucionesHoy: parseInt(props.getProperty(key) || '0', 10),
    hayToken: Boolean(props.getProperty(CONFIG.TOKEN_PROP))
  };
}
