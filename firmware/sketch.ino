// ============================================================
// GTech-Device V2 — Firmware Completo
// Autor: Guillermo Vásquez
// Versión: 2.0.0
// Nota: Watchdog pendiente — implementar en hardware físico
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// ── Credenciales WiFi ────────────────────────────────────────
#define WIFI_SSID       "Wokwi-GUEST"
#define WIFI_PASSWORD   ""

// ── Backend ──────────────────────────────────────────────────
#define GAS_URL "https://script.google.com/macros/s/AKfycbxepUEakmoDbwkDN9AtLgMF5V3IwDR6VdP3nxrCG94Pd4qY4ZGiaVK2uFxAE7PcPxCc/exec"

// ── Pines ────────────────────────────────────────────────────
#define PIN_LED_VERDE    26
#define PIN_LED_ROJO      4
#define PIN_LED_AMARILLO 15
#define PIN_BUZZER       25
#define PIN_RFID_SS       5
#define PIN_RFID_RST     27

// ── EEPROM ───────────────────────────────────────────────────
#define EEPROM_SIZE       512
#define EEPROM_ADDR_COUNT   0
#define EEPROM_ADDR_CARDS   2
#define MAX_TARJETAS       20
#define UID_LEN             9

// ── Objetos ──────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

// ── Máquina de estados ───────────────────────────────────────
enum SystemState { BOOT, LISTO, ERROR_WIFI, DIAGNOSTICO };
SystemState estadoActual = BOOT;

// ── Cola offline ─────────────────────────────────────────────
#define MAX_COLA 20
struct EventoRFID {
  char uid[12];
  char tipo[10];
};
EventoRFID colaOffline[MAX_COLA];
int colaCabeza   = 0;
int colaCola     = 0;
int colaConteo   = 0;

// ── Tarjetas en RAM ──────────────────────────────────────────
char tarjetasRAM[MAX_TARJETAS][UID_LEN];
int  totalTarjetas = 0;

// ── Métricas ─────────────────────────────────────────────────
int reconexiones       = 0;
int eventosEnviados    = 0;
int eventosDescartados = 0;

// ── Variables de control ─────────────────────────────────────
unsigned long ultimoParpadeo  = 0;
bool          ledAmarilloOn   = false;
unsigned long tiempoEnEstado  = 0;
unsigned long ultimoHeartbeat = 0;
#define INTERVALO_HEARTBEAT 30000

// ============================================================
// EEPROM — Gestión de tarjetas
// ============================================================
void cargarTarjetas() {
  totalTarjetas = EEPROM.read(EEPROM_ADDR_COUNT);
  if (totalTarjetas > MAX_TARJETAS) totalTarjetas = 0;

  for (int i = 0; i < totalTarjetas; i++) {
    int addr = EEPROM_ADDR_CARDS + (i * UID_LEN);
    for (int j = 0; j < UID_LEN; j++) {
      tarjetasRAM[i][j] = EEPROM.read(addr + j);
    }
  }
  Serial.print("[EEPROM] Tarjetas cargadas: ");
  Serial.println(totalTarjetas);
}

bool guardarTarjeta(String uid) {
  if (totalTarjetas >= MAX_TARJETAS) {
    Serial.println("[EEPROM] Máximo de tarjetas alcanzado");
    return false;
  }
  for (int i = 0; i < totalTarjetas; i++) {
    if (String(tarjetasRAM[i]) == uid) {
      Serial.println("[EEPROM] Tarjeta ya existe");
      return false;
    }
  }
  int addr = EEPROM_ADDR_CARDS + (totalTarjetas * UID_LEN);
  uid.toCharArray(tarjetasRAM[totalTarjetas], UID_LEN);
  for (int j = 0; j < UID_LEN; j++) {
    EEPROM.write(addr + j, tarjetasRAM[totalTarjetas][j]);
  }
  totalTarjetas++;
  EEPROM.write(EEPROM_ADDR_COUNT, totalTarjetas);
  EEPROM.commit();
  Serial.print("[EEPROM] Tarjeta guardada: ");
  Serial.println(uid);
  return true;
}

bool eliminarTarjeta(String uid) {
  for (int i = 0; i < totalTarjetas; i++) {
    if (String(tarjetasRAM[i]) == uid) {
      for (int j = i; j < totalTarjetas - 1; j++) {
        memcpy(tarjetasRAM[j], tarjetasRAM[j + 1], UID_LEN);
        int addr = EEPROM_ADDR_CARDS + (j * UID_LEN);
        for (int k = 0; k < UID_LEN; k++) {
          EEPROM.write(addr + k, tarjetasRAM[j][k]);
        }
      }
      totalTarjetas--;
      EEPROM.write(EEPROM_ADDR_COUNT, totalTarjetas);
      EEPROM.commit();
      Serial.print("[EEPROM] Tarjeta eliminada: ");
      Serial.println(uid);
      return true;
    }
  }
  return false;
}

bool esTarjetaAutorizada(String uid) {
  for (int i = 0; i < totalTarjetas; i++) {
    if (String(tarjetasRAM[i]) == uid) return true;
  }
  return false;
}

void listarTarjetas() {
  Serial.print("[EEPROM] Total tarjetas: ");
  Serial.println(totalTarjetas);
  for (int i = 0; i < totalTarjetas; i++) {
    Serial.print("  ["); Serial.print(i); Serial.print("] ");
    Serial.println(tarjetasRAM[i]);
  }
}

// ============================================================
// UTILIDADES
// ============================================================
void parpadeoAmarillo(int intervalo) {
  if (millis() - ultimoParpadeo >= intervalo) {
    ultimoParpadeo = millis();
    ledAmarilloOn  = !ledAmarilloOn;
    digitalWrite(PIN_LED_AMARILLO, ledAmarilloOn ? HIGH : LOW);
  }
}

void beep(int duracionMs) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(duracionMs);
  digitalWrite(PIN_BUZZER, LOW);
}

// ============================================================
// MÁQUINA DE ESTADOS
// ============================================================
void setEstado(SystemState nuevo) {
  estadoActual   = nuevo;
  tiempoEnEstado = millis();

  digitalWrite(PIN_LED_VERDE,    LOW);
  digitalWrite(PIN_LED_ROJO,     LOW);
  digitalWrite(PIN_LED_AMARILLO, LOW);

  switch (nuevo) {
    case BOOT:
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("GTech-Device V2");
      lcd.setCursor(0, 1); lcd.print("Iniciando...");
      Serial.println("[ESTADO] BOOT");
      break;

    case LISTO:
      digitalWrite(PIN_LED_VERDE, HIGH);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Sistema LISTO");
      lcd.setCursor(0, 1); lcd.print("Acerque tarjeta");
      Serial.println("[ESTADO] LISTO");
      break;

    case ERROR_WIFI:
      digitalWrite(PIN_LED_ROJO, HIGH);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("ERROR: WiFi");
      lcd.setCursor(0, 1);
      if (colaConteo > 0) {
        lcd.print("Cola: "); lcd.print(colaConteo); lcd.print(" eventos");
      } else {
        lcd.print("Reconectando...");
      }
      Serial.println("[ESTADO] ERROR_WIFI");
      break;

    case DIAGNOSTICO:
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("DIAGNOSTICO");
      lcd.setCursor(0, 1); lcd.print("Analizando...");
      Serial.println("[ESTADO] DIAGNOSTICO");
      break;
  }
}

// ============================================================
// WIFI — Backoff exponencial
// ============================================================
void connectWiFi() {
  Serial.println("[WiFi] Conectando...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int  intentos = 0;
  long delayMs  = 1000;

  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(delayMs);
    intentos++;
    delayMs = min(delayMs * 2, 60000L);
    Serial.print("[WiFi] Intento "); Serial.println(intentos);
    lcd.setCursor(0, 1);
    lcd.print("Intento: "); lcd.print(intentos); lcd.print("   ");
    parpadeoAmarillo(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Conectado!");
    reconexiones++;
    setEstado(LISTO);
    vaciarCola();
  } else {
    Serial.println("[WiFi] Fallo — modo ERROR");
    setEstado(ERROR_WIFI);
  }
}

// ============================================================
// COLA OFFLINE
// ============================================================
void agregarEventoCola(String uid, String tipo) {
  if (colaConteo >= MAX_COLA) {
    eventosDescartados++;
    Serial.println("[COLA] Llena — evento descartado");
    return;
  }
  uid.toCharArray(colaOffline[colaCola].uid,   12);
  tipo.toCharArray(colaOffline[colaCola].tipo,  10);
  colaCola = (colaCola + 1) % MAX_COLA;
  colaConteo++;
  Serial.print("[COLA] Evento agregado — en cola: ");
  Serial.println(colaConteo);
  lcd.setCursor(0, 1);
  lcd.print("Cola: "); lcd.print(colaConteo); lcd.print(" eventos  ");
}

bool enviarEvento(String uid, String tipo, String fuente) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(GAS_URL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["uid"]    = uid;
  doc["tipo"]   = tipo;
  doc["fuente"] = fuente;
  doc["device"] = "GTech-ESP32-V2";

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  http.end();

  if (code > 0) {
    eventosEnviados++;
    Serial.print("[HTTP] Enviado OK — code: "); Serial.println(code);
    return true;
  }
  Serial.print("[HTTP] Error: "); Serial.println(code);
  return false;
}

void vaciarCola() {
  if (colaConteo == 0) return;
  Serial.print("[COLA] Vaciando "); Serial.print(colaConteo); Serial.println(" eventos...");
  while (colaConteo > 0) {
    String uid  = String(colaOffline[colaCabeza].uid);
    String tipo = String(colaOffline[colaCabeza].tipo);
    colaCabeza  = (colaCabeza + 1) % MAX_COLA;
    colaConteo--;
    enviarEvento(uid, tipo, "offline-queue");
  }
  Serial.println("[COLA] Vaciada completamente");
}

// ============================================================
// RFID
// ============================================================
String leerUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void procesarLectura() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid  = leerUID();
  bool   auth = esTarjetaAutorizada(uid);
  String tipo = auth ? "ACCESO" : "DENEGADO";

  Serial.print("[RFID] UID: "); Serial.print(uid);
  Serial.print(" — "); Serial.println(tipo);

  if (auth) {
    digitalWrite(PIN_LED_VERDE, LOW);
    beep(100); delay(100); beep(100);
    digitalWrite(PIN_LED_VERDE, HIGH);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("ACCESO OK");
    lcd.setCursor(0, 1); lcd.print(uid);
  } else {
    digitalWrite(PIN_LED_ROJO, HIGH);
    beep(500);
    digitalWrite(PIN_LED_ROJO, LOW);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("DENEGADO");
    lcd.setCursor(0, 1); lcd.print(uid);
  }

  delay(2000);

  if (WiFi.status() == WL_CONNECTED) {
    if (!enviarEvento(uid, tipo, "online")) {
      agregarEventoCola(uid, tipo);
      setEstado(ERROR_WIFI);
    } else {
      setEstado(LISTO);
    }
  } else {
    agregarEventoCola(uid, tipo);
    setEstado(ERROR_WIFI);
  }

  rfid.PICC_HaltA();
}

// ============================================================
// HEARTBEAT
// ============================================================
void enviarHeartbeat() {
  if (millis() - ultimoHeartbeat < INTERVALO_HEARTBEAT) return;
  ultimoHeartbeat = millis();
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(GAS_URL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["tipo"]         = "heartbeat";
  doc["device"]       = "GTech-ESP32-V2";
  doc["estado"]       = estadoActual;
  doc["reconexiones"] = reconexiones;
  doc["cola"]         = colaConteo;
  doc["enviados"]     = eventosEnviados;
  doc["tarjetas"]     = totalTarjetas;
  doc["uptime"]       = millis() / 1000;

  String body;
  serializeJson(doc, body);
  http.POST(body);
  http.end();
  Serial.println("[HEARTBEAT] Enviado");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_VERDE,    OUTPUT);
  pinMode(PIN_LED_ROJO,     OUTPUT);
  pinMode(PIN_LED_AMARILLO, OUTPUT);
  pinMode(PIN_BUZZER,       OUTPUT);

  EEPROM.begin(EEPROM_SIZE);
  lcd.init();
  lcd.backlight();
  SPI.begin();
  rfid.PCD_Init();

  cargarTarjetas();

  if (totalTarjetas == 0) {
    Serial.println("[EEPROM] Cargando tarjetas de prueba...");
    guardarTarjeta("01020304");
    guardarTarjeta("E5F6G7H8");
    guardarTarjeta("12345678");
    listarTarjetas();
  }

  setEstado(BOOT);
  delay(1000);
  connectWiFi();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  switch (estadoActual) {

    case BOOT:
      parpadeoAmarillo(500);
      break;

    case LISTO:
      procesarLectura();
      enviarHeartbeat();
      break;

    case ERROR_WIFI:
      digitalWrite(PIN_LED_ROJO, HIGH);
      procesarLectura();
      if (millis() - tiempoEnEstado >= 30000) {
        setEstado(BOOT);
        connectWiFi();
      }
      break;

    case DIAGNOSTICO:
      parpadeoAmarillo(150);
      break;
  }
}