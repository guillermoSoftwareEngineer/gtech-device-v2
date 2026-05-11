// ============================================================
// GTech-Device V2 — Google Apps Script Backend
// Autor: Guillermo Vásquez
// Versión: 2.0.0
// ============================================================

const SHEET_ID = SpreadsheetApp.getActiveSpreadsheet();
const HOJA_EVENTOS = SHEET_ID.getSheetByName("Eventos");
const HOJA_HEARTBEAT = SHEET_ID.getSheetByName("Heartbeat");
const HOJA_TARJETAS = SHEET_ID.getSheetByName("Tarjetas");

// ============================================================
// POST — Punto de entrada principal
// ============================================================
function doPost(e) {
    try {
        const data = JSON.parse(e.postData.contents);
        const tipo = data.tipo || "evento";

        if (tipo === "heartbeat") {
            return registrarHeartbeat(data);
        } else {
            return registrarEvento(data);
        }

    } catch (err) {
        return responder(500, "Error: " + err.message);
    }
}

// ============================================================
// GET — Consultas desde dashboard
// ============================================================
function doGet(e) {
    try {
        const accion = e.parameter.accion || "";

        if (accion === "eventos") {
            return getEventos();
        } else if (accion === "tarjetas") {
            return getTarjetas();
        } else if (accion === "heartbeat") {
            return getUltimoHeartbeat();
        } else {
            return responder(400, "Accion no reconocida");
        }

    } catch (err) {
        return responder(500, "Error: " + err.message);
    }
}

// ============================================================
// REGISTRAR EVENTO RFID
// ============================================================
function registrarEvento(data) {
    const timestamp = new Date().toISOString();
    const uid = data.uid || "DESCONOCIDO";
    const tipo = data.tipo || "DESCONOCIDO";
    const fuente = data.fuente || "online";
    const device = data.device || "DESCONOCIDO";

    HOJA_EVENTOS.appendRow([timestamp, uid, tipo, fuente, device]);

    return responder(200, "Evento registrado OK");
}

// ============================================================
// REGISTRAR HEARTBEAT
// ============================================================
function registrarHeartbeat(data) {
    const timestamp = new Date().toISOString();
    const device = data.device || "DESCONOCIDO";
    const estado = data.estado || 0;
    const reconex = data.reconexiones || 0;
    const cola = data.cola || 0;
    const enviados = data.enviados || 0;
    const tarjetas = data.tarjetas || 0;
    const uptime = data.uptime || 0;

    HOJA_HEARTBEAT.appendRow([
        timestamp, device, estado, reconex,
        cola, enviados, tarjetas, uptime
    ]);

    return responder(200, "Heartbeat registrado OK");
}

// ============================================================
// GET EVENTOS — Últimos 50
// ============================================================
function getEventos() {
    const datos = HOJA_EVENTOS.getDataRange().getValues();
    const header = datos[0];
    const filas = datos.slice(1).slice(-50).map(fila => {
        let obj = {};
        header.forEach((col, i) => obj[col] = fila[i]);
        return obj;
    });
    return ContentService
        .createTextOutput(JSON.stringify({ ok: true, data: filas }))
        .setMimeType(ContentService.MimeType.JSON);
}

// ============================================================
// GET TARJETAS
// ============================================================
function getTarjetas() {
    const datos = HOJA_TARJETAS.getDataRange().getValues();
    const header = datos[0];
    const filas = datos.slice(1).map(fila => {
        let obj = {};
        header.forEach((col, i) => obj[col] = fila[i]);
        return obj;
    });
    return ContentService
        .createTextOutput(JSON.stringify({ ok: true, data: filas }))
        .setMimeType(ContentService.MimeType.JSON);
}

// ============================================================
// GET ÚLTIMO HEARTBEAT
// ============================================================
function getUltimoHeartbeat() {
    const datos = HOJA_HEARTBEAT.getDataRange().getValues();
    if (datos.length < 2) {
        return responder(404, "Sin datos de heartbeat");
    }
    const header = datos[0];
    const ultima = datos[datos.length - 1];
    let obj = {};
    header.forEach((col, i) => obj[col] = ultima[i]);

    return ContentService
        .createTextOutput(JSON.stringify({ ok: true, data: obj }))
        .setMimeType(ContentService.MimeType.JSON);
}

// ============================================================
// HELPER — Respuesta JSON estándar
// ============================================================
function responder(code, mensaje) {
    return ContentService
        .createTextOutput(JSON.stringify({ code, mensaje }))
        .setMimeType(ContentService.MimeType.JSON);
}
