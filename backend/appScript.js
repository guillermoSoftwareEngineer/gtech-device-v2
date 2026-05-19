// ============================================================
// GTech-Device V2 — Google Apps Script Backend
// Autor: Guillermo Vásquez
// Versión: 2.1.0 — Hora Bogotá + Gestión tarjetas
// ============================================================

const SS           = SpreadsheetApp.getActiveSpreadsheet();
const HOJA_EVENTOS    = SS.getSheetByName("Eventos");
const HOJA_HEARTBEAT  = SS.getSheetByName("Heartbeat");
const HOJA_TARJETAS   = SS.getSheetByName("Tarjetas");

function timestamp() {
  return Utilities.formatDate(new Date(), "America/Bogota", "yyyy-MM-dd HH:mm:ss");
}

function doPost(e) {
  try {
    const data = JSON.parse(e.postData.contents);
    const tipo = data.tipo || "evento";
    if (tipo === "heartbeat")   return registrarHeartbeat(data);
    if (tipo === "add-card")    return agregarTarjeta(data);
    if (tipo === "delete-card") return eliminarTarjeta(data);
    return registrarEvento(data);
  } catch (err) {
    return responder(500, "Error: " + err.message);
  }
}

function doGet(e) {
  try {
    const accion = e.parameter.accion || "";
    if (accion === "eventos")   return getEventos();
    if (accion === "tarjetas")  return getTarjetas();
    if (accion === "heartbeat") return getUltimoHeartbeat();
    return responder(400, "Accion no reconocida");
  } catch (err) {
    return responder(500, "Error: " + err.message);
  }
}

function registrarEvento(data) {
  HOJA_EVENTOS.appendRow([
    timestamp(),
    data.uid    || "DESCONOCIDO",
    data.tipo   || "DESCONOCIDO",
    data.fuente || "online",
    data.device || "DESCONOCIDO"
  ]);
  return responder(200, "Evento registrado OK");
}

function registrarHeartbeat(data) {
  HOJA_HEARTBEAT.appendRow([
    timestamp(),
    data.device       || "DESCONOCIDO",
    data.estado       || 0,
    data.reconexiones || 0,
    data.cola         || 0,
    data.enviados     || 0,
    data.tarjetas     || 0,
    data.uptime       || 0
  ]);
  return responder(200, "Heartbeat registrado OK");
}

function agregarTarjeta(data) {
  const uid = (data.uid || "").toUpperCase().trim();
  if (!uid) return responder(400, "UID requerido");
  const datos = HOJA_TARJETAS.getDataRange().getValues();
  for (let i = 1; i < datos.length; i++) {
    if (datos[i][0] === uid) return responder(409, "Tarjeta ya existe");
  }
  HOJA_TARJETAS.appendRow([uid, timestamp(), "SI"]);
  return responder(200, "Tarjeta agregada: " + uid);
}

function eliminarTarjeta(data) {
  const uid = (data.uid || "").toUpperCase().trim();
  if (!uid) return responder(400, "UID requerido");
  const datos = HOJA_TARJETAS.getDataRange().getValues();
  for (let i = 1; i < datos.length; i++) {
    if (datos[i][0] === uid) {
      HOJA_TARJETAS.deleteRow(i + 1);
      return responder(200, "Tarjeta eliminada: " + uid);
    }
  }
  return responder(404, "Tarjeta no encontrada");
}

function getEventos() {
  const datos  = HOJA_EVENTOS.getDataRange().getValues();
  const header = datos[0];
  const filas  = datos.slice(1).slice(-50).map(fila => {
    let obj = {};
    header.forEach((col, i) => obj[col] = fila[i]);
    return obj;
  });
  return jsonResponse({ ok: true, data: filas });
}

function getTarjetas() {
  const datos = HOJA_TARJETAS.getDataRange().getValues();
  if (datos.length < 2) return jsonResponse({ ok: true, data: [] });
  const header = datos[0];
  const filas  = datos.slice(1).map(fila => {
    let obj = {};
    header.forEach((col, i) => obj[col] = fila[i]);
    return obj;
  });
  return jsonResponse({ ok: true, data: filas });
}

function getUltimoHeartbeat() {
  const datos = HOJA_HEARTBEAT.getDataRange().getValues();
  if (datos.length < 2) return responder(404, "Sin datos");
  const header = datos[0];
  const ultima = datos[datos.length - 1];
  let obj = {};
  header.forEach((col, i) => obj[col] = ultima[i]);
  return jsonResponse({ ok: true, data: obj });
}

function responder(code, mensaje) {
  return ContentService
    .createTextOutput(JSON.stringify({ code, mensaje }))
    .setMimeType(ContentService.MimeType.JSON);
}

function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}