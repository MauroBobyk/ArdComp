/*
 * esp32_ble_nus.ino — ESP32 por Bluetooth LE con Nordic UART Service (NUS)
 * =============================================================================
 * ESTE es el sketch que hay que cargar en el ESP32 para que la página web lo
 * pueda ver por Bluetooth. Es la contraparte del transporte 'bluetooth' de
 * arduino-usb.js.
 *
 * -----------------------------------------------------------------------------
 * POR QUÉ NO SIRVE EL EJEMPLO "SerialToSerialBT"
 * -----------------------------------------------------------------------------
 * El core de Arduino para ESP32 trae la clase BluetoothSerial (el ejemplo que
 * trae el IDE). A pesar del nombre, NO implementa un servicio UART sobre BLE:
 * usa el Bluetooth CLÁSICO con el perfil SPP.
 *
 * Y Web Bluetooth es una API **sólo BLE/GATT**: no implementa SPP. Por eso el
 * ESP32 con BluetoothSerial aparece en la app "Serial Bluetooth Terminal" de
 * Android pero NUNCA aparece en el selector del navegador.
 *
 * No es un bug que se pueda sortear con código: son dos radios distintas.
 * Para la web, el ESP32 tiene que exponer un servicio GATT. El estándar de
 * facto es el Nordic UART Service, que es lo que arma este sketch.
 *
 * -----------------------------------------------------------------------------
 * Protocolo
 * -----------------------------------------------------------------------------
 * El mismo que los otros sketches: texto plano, una orden por línea con '\n'.
 *
 *   PING            -> PONG
 *   LED:ON          -> OK:LED=ON
 *   LED:OFF         -> OK:LED=OFF
 *   LED:TOGGLE      -> OK:LED=ON   |   OK:LED=OFF
 *   STATUS          -> STATUS:LED=ON,UPTIME=12,HEAP=245000
 *   INFO            -> INFO:ESP32-D0WD-V3,REV=3,CORES=2,CPU=240MHz
 *   READ:A0         -> A0:2048
 *   READ:GPIO4      -> GPIO4:0   |   GPIO4:1
 *   ECHO:texto      -> ECHO:texto
 *   HELP            -> lista de órdenes
 *   cualquier otra  -> ERR:UNKNOWN:<orden>
 *
 * Además, cada 5 segundos manda sola: HB:<segundos desde el arranque>
 *
 * -----------------------------------------------------------------------------
 * EL PUNTO MÁS DELICADO: FRAGMENTACIÓN POR MTU
 * -----------------------------------------------------------------------------
 * BLE no tiene un flujo continuo como el puerto serie. Cada notificación es un
 * paquete de como máximo (MTU - 3) bytes de datos. Con el MTU mínimo garantizado
 * por la especificación (23) eso son 20 bytes útiles.
 *
 * Como Web Bluetooth NO expone el MTU negociado, la librería web envía de a 20
 * bytes (opción bleChunkSize). Del lado del ESP32 hacemos lo mismo al responder.
 *
 * SI CAMBIÁS UN TAMAÑO, CAMBIÁ EL OTRO. Si mandás notificaciones más grandes
 * que el MTU negociado, el stack BLE descarta los paquetes y no te enterás.
 *
 * -----------------------------------------------------------------------------
 * DETALLE QUE HACE PERDER MUCHAS HORAS: VOLVER A ANUNCIARSE
 * -----------------------------------------------------------------------------
 * Cuando el cliente se desconecta, el ESP32 deja de anunciarse. Si no se llama
 * a startAdvertising() otra vez, el ESP32 queda "invisible" para siempre y hay
 * que reiniciarlo a mano para que el navegador lo vuelva a encontrar.
 * Está resuelto en onDisconnect(), más abajo.
 *
 * -----------------------------------------------------------------------------
 * SE PUEDE DEPURAR POR USB AL MISMO TIEMPO
 * -----------------------------------------------------------------------------
 * Este sketch usa el Serial (115200) para imprimir lo que pasa. Podés tener el
 * Monitor Serie abierto mirando los mensajes mientras el navegador usa el BLE.
 * =============================================================================
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- UUIDs del Nordic UART Service (los mismos que usa la librería web) ------
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // web -> ESP32 (escribe)
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // ESP32 -> web (notifica)

// --- Configuración -----------------------------------------------------------
// Este es el nombre que vas a ver en el selector del navegador.
// Si lo cambiás, acordate de poner el mismo prefijo en el campo "Prefijo del
// nombre BLE" de la página web.
const char*    DEVICE_NAME  = "ESP32-NUS";

// En muchas placas ESP32 (por ejemplo la "ESP32 Dev Module" genérica) la
// constante LED_BUILTIN no viene definida, aunque la placa tenga el LED soldado
// en el GPIO 2. Si en tu placa está en otro pin, cambiá el número acá.
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

const unsigned long BAUD_RATE    = 115200;  // Sólo para el Monitor Serie
const unsigned long HEARTBEAT_MS = 5000;
const uint8_t       MAX_LINE     = 64;

// Bytes de datos por notificación. 20 es el valor seguro con MTU 23.
const uint8_t NOTIFY_CHUNK = 20;

// --- Estado ------------------------------------------------------------------
static BLEServer*         gServer    = nullptr;
static BLECharacteristic* gTxChar    = nullptr;
static bool               gConnected = false;

char          gLine[MAX_LINE];
uint8_t       gLineLen  = 0;
bool          gOverflow = false;
bool          gLedOn    = false;
unsigned long gLastBeat = 0;

// --- Declaraciones adelantadas ------------------------------------------------
// El preprocesador del IDE de Arduino genera los prototipos automáticamente,
// pero deja de hacerlo cuando el archivo tiene clases (como este). Así que los
// declaramos a mano: si no, no compila.
void handleCommand(char* cmd);
void notifyRaw(const char* buf);
void replyLine(const char* text);
void setLed(bool on);
void sendHeartbeat();

// =============================================================================
// Callbacks del servidor: conexión y desconexión del cliente
// =============================================================================
class ServerCallbacks : public BLEServerCallbacks {

  void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    gConnected = true;
    gLineLen   = 0;          // No arrastrar media orden de la sesión anterior
    gOverflow  = false;
    Serial.println(F("BLE: cliente conectado"));
  }

  void onDisconnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    gConnected = false;
    gLineLen   = 0;
    gOverflow  = false;
    Serial.println(F("BLE: cliente desconectado"));

    // ESTO ES LO IMPORTANTE. Sin esto el ESP32 deja de anunciarse y el
    // navegador no lo vuelve a encontrar hasta reiniciar la placa.
    delay(200);
    server->startAdvertising();
    Serial.println(F("BLE: anunciando otra vez"));
  }
};

// =============================================================================
// Callbacks de la característica RX: llega lo que escribe el navegador
// =============================================================================
class RxCallbacks : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic* chr) override {
    // La web manda la orden troceada en paquetes de 20 bytes, así que hay que
    // acumular y recién procesar cuando aparece el '\n'.
    uint8_t* data = chr->getData();
    size_t   len  = chr->getLength();

    for (size_t i = 0; i < len; i++) {
      char c = (char)data[i];

      if (c == '\n' || c == '\r') {
        if (gOverflow) {
          gOverflow = false;
        } else if (gLineLen > 0) {
          gLine[gLineLen] = '\0';
          handleCommand(gLine);
        }
        gLineLen = 0;
        continue;
      }

      if (gLineLen < MAX_LINE - 1) {
        gLine[gLineLen++] = c;
      } else {
        gOverflow = true;
      }
    }
  }
};

// =============================================================================
// setup()
// =============================================================================
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(BAUD_RATE);
  delay(100);
  Serial.println();
  Serial.println(F("READY:ESP32-BLE-NUS"));

  // --- Montaje del servicio GATT ---------------------------------------------
  BLEDevice::init(DEVICE_NAME);

  // Pedimos un MTU más grande. Ayuda al rendimiento, pero la web NO lo consulta:
  // seguimos respondiendo de a 20 bytes por las dudas.
  BLEDevice::setMTU(185);

  gServer = BLEDevice::createServer();
  gServer->setCallbacks(new ServerCallbacks());

  BLEService* service = gServer->createService(NUS_SERVICE_UUID);

  // Característica RX: donde el navegador ESCRIBE.
  BLECharacteristic* rxChar = service->createCharacteristic(
    NUS_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setCallbacks(new RxCallbacks());

  // Característica TX: por donde el ESP32 NOTIFICA.
  gTxChar = service->createCharacteristic(
    NUS_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  gTxChar->addDescriptor(new BLE2902());   // Sin este descriptor el navegador
                                           // no puede activar las notificaciones

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(NUS_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);   // Ayuda a la compatibilidad con iOS
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.print(F("BLE: anunciando como \""));
  Serial.print(DEVICE_NAME);
  Serial.println(F("\""));
  Serial.println(F("Abrí la página web y buscá ese nombre en el selector."));
}

// =============================================================================
// loop()
// =============================================================================
void loop() {
  sendHeartbeat();
}

// =============================================================================
// Envío por BLE, troceado según el MTU
// =============================================================================
void notifyRaw(const char* buf) {
  if (!gConnected || gTxChar == nullptr) return;

  size_t len = strlen(buf);
  for (size_t off = 0; off < len; off += NOTIFY_CHUNK) {
    size_t n = len - off;
    if (n > NOTIFY_CHUNK) n = NOTIFY_CHUNK;

    gTxChar->setValue((uint8_t*)(buf + off), n);
    gTxChar->notify();

    // Le damos aire al stack BLE para que alcance a mandar el paquete.
    delay(8);
  }
}

// Manda una línea completa: por BLE (con '\n') y por Serial para depurar.
void replyLine(const char* text) {
  Serial.println(text);
  if (!gConnected) return;

  char buf[160];
  size_t n = strlen(text);
  if (n > sizeof(buf) - 2) n = sizeof(buf) - 2;

  memcpy(buf, text, n);
  buf[n]     = '\n';
  buf[n + 1] = '\0';
  notifyRaw(buf);
}

// =============================================================================
// Interpretación de las órdenes
// =============================================================================
void handleCommand(char* cmd) {
  char out[160];

  if (strcmp(cmd, "PING") == 0) {
    replyLine("PONG");
    return;
  }

  if (strcmp(cmd, "LED:ON") == 0) {
    setLed(true);
    replyLine("OK:LED=ON");
    return;
  }

  if (strcmp(cmd, "LED:OFF") == 0) {
    setLed(false);
    replyLine("OK:LED=OFF");
    return;
  }

  if (strcmp(cmd, "LED:TOGGLE") == 0) {
    setLed(!gLedOn);
    replyLine(gLedOn ? "OK:LED=ON" : "OK:LED=OFF");
    return;
  }

  if (strcmp(cmd, "STATUS") == 0) {
    snprintf(out, sizeof(out), "STATUS:LED=%s,UPTIME=%lu,HEAP=%u",
             gLedOn ? "ON" : "OFF", millis() / 1000, ESP.getFreeHeap());
    replyLine(out);
    return;
  }

  if (strcmp(cmd, "INFO") == 0) {
    snprintf(out, sizeof(out), "INFO:%s,REV=%u,CORES=%u,CPU=%uMHz",
             ESP.getChipModel(), ESP.getChipRevision(),
             ESP.getChipCores(), getCpuFrequencyMhz());
    replyLine(out);
    return;
  }

  // READ:A0 ... READ:A5  ->  A0:2048
  if (strncmp(cmd, "READ:A", 6) == 0) {
    char n = cmd[6];
    if (n >= '0' && n <= '5' && cmd[7] == '\0') {
      snprintf(out, sizeof(out), "A%c:%u", n, analogRead(A0 + (n - '0')));
      replyLine(out);
      return;
    }
    replyLine("ERR:BAD_PIN");
    return;
  }

  // READ:GPIO4  ->  GPIO4:0
  if (strncmp(cmd, "READ:GPIO", 9) == 0) {
    int pin = atoi(cmd + 9);
    if (pin >= 0 && pin <= 39) {
      pinMode(pin, INPUT);
      snprintf(out, sizeof(out), "GPIO%d:%d", pin, digitalRead(pin));
      replyLine(out);
      return;
    }
    replyLine("ERR:BAD_PIN");
    return;
  }

  if (strncmp(cmd, "ECHO:", 5) == 0) {
    snprintf(out, sizeof(out), "ECHO:%.120s", cmd + 5);
    replyLine(out);
    return;
  }

  if (strcmp(cmd, "HELP") == 0) {
    replyLine("HELP:PING,LED:ON,LED:OFF,LED:TOGGLE,STATUS,INFO,READ:A0-A5,READ:GPIO4,ECHO:x");
    return;
  }

  snprintf(out, sizeof(out), "ERR:UNKNOWN:%.110s", cmd);
  replyLine(out);
}

// =============================================================================
// Auxiliares
// =============================================================================
void setLed(bool on) {
  gLedOn = on;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

// Latido: le sirve al navegador para saber que la placa sigue viva.
void sendHeartbeat() {
  unsigned long now = millis();
  if (now - gLastBeat < HEARTBEAT_MS) return;

  gLastBeat = now;
  char out[24];
  snprintf(out, sizeof(out), "HB:%lu", now / 1000);
  replyLine(out);
}
