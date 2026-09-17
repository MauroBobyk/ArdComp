/*
 * uno_serial.ino — Arduino Uno / Nano / Mega por USB (Web Serial API)
 * =============================================================================
 * Firmware de ejemplo para la clase de integración de Arduino con aplicaciones
 * web. Cargalo con el IDE de Arduino y después abrí la página web: el navegador
 * va a poder hablar con la placa por el cable USB.
 *
 * -----------------------------------------------------------------------------
 * Protocolo
 * -----------------------------------------------------------------------------
 * Texto plano, una orden por línea terminada en '\n'.
 * La web usa sendLine(), que agrega el '\n' automáticamente.
 *
 *   PING            -> PONG
 *   LED:ON          -> OK:LED=ON
 *   LED:OFF         -> OK:LED=OFF
 *   LED:TOGGLE      -> OK:LED=ON   |   OK:LED=OFF
 *   STATUS          -> STATUS:LED=ON,UPTIME=12
 *   READ:A0         -> A0:512              (A0 hasta A5)
 *   ECHO:texto      -> ECHO:texto
 *   HELP            -> lista de órdenes
 *   cualquier otra  -> ERR:UNKNOWN:<orden>
 *
 * Además, cada 5 segundos manda sola: HB:<segundos desde el arranque>
 *
 * -----------------------------------------------------------------------------
 * Tres cosas que conviene saber
 * -----------------------------------------------------------------------------
 * 1. BAUDIOS: 9600, igual que en la página web. Si los cambiás en un lado,
 *    cambialos también en el otro, si no vas a ver símbolos raros.
 *
 * 2. AL CONECTAR, LA PLACA SE REINICIA. Lo provoca el cable USB (la señal DTR).
 *    Es normal: es exactamente lo mismo que pasa al abrir el Monitor Serie.
 *    Por eso la placa manda "READY:ARDUINO-UNO" apenas arranca.
 *
 * 3. NO USES delay() LARGO EN loop(). Mientras un delay corre, la placa no lee
 *    el puerto serie y el navegador puede perder datos. Por eso todo acá está
 *    escrito sin bloqueos.
 * =============================================================================
 */

// --- Configuración -----------------------------------------------------------
const unsigned long BAUD_RATE    = 9600;   // Tiene que coincidir con la web
const unsigned long HEARTBEAT_MS = 5000;   // Cada cuánto mandar el latido
const uint8_t       MAX_LINE     = 48;     // Largo máximo de una orden

// --- Estado ------------------------------------------------------------------
char          gLine[MAX_LINE];   // Buffer de la orden que se está armando
uint8_t       gLineLen  = 0;     // Cuántos caracteres lleva
bool          gOverflow = false; // La orden se pasó de MAX_LINE
bool          gLedOn    = false;
unsigned long gLastBeat = 0;

// =============================================================================
// setup() — corre una sola vez al arrancar (o cada vez que la web conecta)
// =============================================================================
void setup() {
  // Sin pinMode() digitalWrite() no hace nada. Es el error más común.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(BAUD_RATE);

  Serial.println(F("READY:ARDUINO-UNO"));
}

// =============================================================================
// loop() — corre en bucle infinito
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
        // La orden venía cortada, así que no es confiable: la descartamos.
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
      // Se llenó el buffer: marcamos para descartar la orden entera.
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
    Serial.println(millis() / 1000);
    return;
  }

  // READ:A0 ... READ:A5  ->  A0:512
  if (strncmp(cmd, "READ:A", 6) == 0) {
    char n = cmd[6];
    if (n >= '0' && n <= '5' && cmd[7] == '\0') {
      uint8_t pin = A0 + (n - '0');
      Serial.print(F("A"));
      Serial.print(n);
      Serial.print(':');
      Serial.println(analogRead(pin));
      return;
    }
    Serial.println(F("ERR:BAD_PIN"));
    return;
  }

  // ECHO:lo que sea  ->  ECHO:lo que sea
  if (strncmp(cmd, "ECHO:", 5) == 0) {
    Serial.print(F("ECHO:"));
    Serial.println(cmd + 5);
    return;
  }

  if (strcmp(cmd, "HELP") == 0) {
    Serial.println(F("HELP:PING,LED:ON,LED:OFF,LED:TOGGLE,STATUS,READ:A0-A5,ECHO:x"));
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

// Latido: le sirve a la web para saber que la placa sigue viva.
void sendHeartbeat() {
  unsigned long now = millis();
  if (now - gLastBeat < HEARTBEAT_MS) return;

  gLastBeat = now;
  Serial.print(F("HB:"));
  Serial.println(now / 1000);
}
