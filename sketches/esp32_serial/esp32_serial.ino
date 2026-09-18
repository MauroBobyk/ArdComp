/*
  Copyright (C) 2026, Mauro Bobyk.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://gnu.org>.
*/
/*
 * esp32_serial.ino — ESP32 por USB (Web Serial API)
 * =============================================================================
 * Mismo protocolo que uno_serial.ino, más algunas órdenes propias del ESP32.
 *
 * -----------------------------------------------------------------------------
 * Protocolo
 * -----------------------------------------------------------------------------
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
 * OJO CON LOS BAUDIOS
 * -----------------------------------------------------------------------------
 * Este sketch usa 115200, NO 9600. En la página web elegí 115200 en el selector
 * de baudios. Si conectás a 9600 vas a ver basura.
 *
 * -----------------------------------------------------------------------------
 * PLACAS CON USB NATIVO (ESP32-S2 / S3 / C3)
 * -----------------------------------------------------------------------------
 * Estas placas no llevan chip conversor USB-serie: tienen USB interno. Para que
 * Serial salga por ese USB hay que compilar con:
 *
 *     ARDUINO_USB_CDC_ON_BOOT=1
 *
 * En el IDE de Arduino: Herramientas -> USB CDC On Boot -> Enabled
 * Si no lo activás, el navegador va a ver el puerto pero no va a llegar nada.
 *
 * -----------------------------------------------------------------------------
 * UNA LÍNEA DE BASURA AL CONECTAR (es normal)
 * -----------------------------------------------------------------------------
 * Al abrir el puerto, el ESP32 se reinicia e imprime el mensaje del bootloader a
 * 74880 bauds, un baudrate distinto al de la aplicación. Eso produce una línea
 * ilegible al principio del log. No es un error tuyo.
 * =============================================================================
 */

// En muchas placas ESP32 (por ejemplo la "ESP32 Dev Module" genérica) la
// constante LED_BUILTIN no viene definida, aunque la placa tenga el LED soldado
// en el GPIO 2. Si en tu placa está en otro pin, cambiá el número acá.
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// --- Configuración -----------------------------------------------------------
const unsigned long BAUD_RATE    = 115200; // Tiene que coincidir con la web
const unsigned long HEARTBEAT_MS = 5000;
const uint8_t       MAX_LINE     = 64;

// --- Estado ------------------------------------------------------------------
char          gLine[MAX_LINE];
uint8_t       gLineLen  = 0;
bool          gOverflow = false;
bool          gLedOn    = false;
unsigned long gLastBeat = 0;

// =============================================================================
// setup()
// =============================================================================
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(BAUD_RATE);
  delay(100);                 // Dejamos que el USB se estabilice
  Serial.println();
  Serial.println(F("READY:ESP32-SERIAL"));
}

// =============================================================================
// loop()
// =============================================================================
void loop() {
  readSerial();
  sendHeartbeat();
}

// =============================================================================
// Lectura del puerto serie, sin bloquear
// =============================================================================
void readSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

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

// =============================================================================
// Interpretación de las órdenes
// =============================================================================
void handleCommand(char *cmd) {

  if (strcmp(cmd, "PING") == 0) {
    Serial.println(F("PONG"));
    return;
  }

  if (strcmp(cmd, "LED:ON") == 0) {
    setLed(true);
    Serial.println(F("OK:LED=ON"));
    return;
  }

  if (strcmp(cmd, "LED:OFF") == 0) {
    setLed(false);
    Serial.println(F("OK:LED=OFF"));
    return;
  }

  if (strcmp(cmd, "LED:TOGGLE") == 0) {
    setLed(!gLedOn);
    Serial.println(gLedOn ? F("OK:LED=ON") : F("OK:LED=OFF"));
    return;
  }

  if (strcmp(cmd, "STATUS") == 0) {
    Serial.print(F("STATUS:LED="));
    Serial.print(gLedOn ? F("ON") : F("OFF"));
    Serial.print(F(",UPTIME="));
    Serial.print(millis() / 1000);
    Serial.print(F(",HEAP="));
    Serial.println(ESP.getFreeHeap());
    return;
  }

  if (strcmp(cmd, "INFO") == 0) {
    Serial.print(F("INFO:"));
    Serial.print(ESP.getChipModel());
    Serial.print(F(",REV="));
    Serial.print(ESP.getChipRevision());
    Serial.print(F(",CORES="));
    Serial.print(ESP.getChipCores());
    Serial.print(F(",CPU="));
    Serial.print(getCpuFrequencyMhz());
    Serial.println(F("MHz"));
    return;
  }

  // READ:A0 ... READ:A5  (canales analógicos del ESP32)  ->  A0:2048
  if (strncmp(cmd, "READ:A", 6) == 0) {
    char n = cmd[6];
    if (n >= '0' && n <= '5' && cmd[7] == '\0') {
      Serial.print(F("A"));
      Serial.print(n);
      Serial.print(':');
      Serial.println(analogRead(A0 + (n - '0')));
      return;
    }
    Serial.println(F("ERR:BAD_PIN"));
    return;
  }

  // READ:GPIO4  ->  GPIO4:0
  if (strncmp(cmd, "READ:GPIO", 9) == 0) {
    int pin = atoi(cmd + 9);
    if (pin >= 0 && pin <= 39) {
      pinMode(pin, INPUT);
      Serial.print(F("GPIO"));
      Serial.print(pin);
      Serial.print(':');
      Serial.println(digitalRead(pin));
      return;
    }
    Serial.println(F("ERR:BAD_PIN"));
    return;
  }

  if (strncmp(cmd, "ECHO:", 5) == 0) {
    Serial.print(F("ECHO:"));
    Serial.println(cmd + 5);
    return;
  }

  if (strcmp(cmd, "HELP") == 0) {
    Serial.println(F("HELP:PING,LED:ON,LED:OFF,LED:TOGGLE,STATUS,INFO,READ:A0-A5,READ:GPIO4,ECHO:x"));
    return;
  }

  Serial.print(F("ERR:UNKNOWN:"));
  Serial.println(cmd);
}

// =============================================================================
// Auxiliares
// =============================================================================
void setLed(bool on) {
  gLedOn = on;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

void sendHeartbeat() {
  unsigned long now = millis();
  if (now - gLastBeat < HEARTBEAT_MS) return;

  gLastBeat = now;
  Serial.print(F("HB:"));
  Serial.println(now / 1000);
}
