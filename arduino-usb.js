/**
 * arduino-usb.js
 * -----------------------------------------------------------------------------
 * Conecta un Arduino (UNO/Nano/Mega o ESP32) a una página web desde el navegador.
 *
 * Dos formas de uso:
 *
 *   A) Web Component <arduino-usb> (RECOMENDADO para alumnos):
 *        <script src="./arduino-usb.js"></script>
 *        <arduino-usb id="arduino" eventos="componentes"></arduino-usb>
 *
 *      Métodos: conectarUSB(), conectarBluetooth(), enviar(), enviarLinea(),
 *               desconectar(), camara(), capturarFoto(), detenerCamara().
 *      La documentación completa está en README.md.
 *
 *   B) Clase ArduinoUSB (uso avanzado):
 *        import ArduinoUSB from './arduino-usb.js';
 *
 * Soporta dos transportes:
 *   1. USB / puerto serie  ->  Web Serial API  (navigator.serial)
 *   2. Bluetooth (BLE)     ->  Web Bluetooth API (navigator.bluetooth)
 *
 * Uso (clase):
 *   import ArduinoUSB from './arduino-usb.js';
 *
 *   const arduino = new ArduinoUSB();
 *   arduino.addEventListener('connect',    e => console.log('Conectado', e.detail));
 *   arduino.addEventListener('data',       e => console.log('Datos:', e.detail));
 *   arduino.addEventListener('line',       e => console.log('Línea:', e.detail));
 *   arduino.addEventListener('disconnect', e => console.log('Desconectado'));
 *   arduino.addEventListener('error',      e => console.error(e.detail));
 *
 *   await arduino.connectUSB({ baudRate: 9600 });       // Cable USB
 *   await arduino.connectBluetooth({ namePrefix: 'HM' }); // Bluetooth BLE
 *
 *   await arduino.sendLine('HOLA');   // Envía "HOLA\n"
 *   await arduino.disconnect();
 * -----------------------------------------------------------------------------
 */

/** UUIDs del servicio UART nórdico (NUS), estándar en módulos BLE tipo HM-10, HC-08, nRF, ESP32-BLE. */
const NORDIC_UART_SERVICE   = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NORDIC_UART_RX_CHAR  = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // Escribimos aquí (RX del módulo)
const NORDIC_UART_TX_CHAR  = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // Leemos aquí (TX del módulo, notifica)

class ArduinoUSB extends EventTarget {
  /**
   * @param {Object} [options] Configuración por defecto.
   * @param {number} [options.baudRate=9600]           Velocidad en baudios para USB.
   * @param {string} [options.lineEnding='\n']         Fin de línea usado por sendLine() y el evento 'line'.
   * @param {boolean} [options.dispatchLines=true]     Emitir también el evento 'line' por cada línea completa.
   * @param {string} [options.bluetoothService]        UUID del servicio Bluetooth UART.
   * @param {string} [options.bluetoothRxCharacteristic] UUID de la característica donde ESCRIBIMOS.
   * @param {string} [options.bluetoothTxCharacteristic] UUID de la característica que LEEMOS (notificaciones).
   */
  constructor(options = {}) {
    super();

    this._options = {
      baudRate: 9600,
      lineEnding: '\n',
      dispatchLines: true,
      bluetoothService: NORDIC_UART_SERVICE,
      bluetoothRxCharacteristic: NORDIC_UART_RX_CHAR,
      bluetoothTxCharacteristic: NORDIC_UART_TX_CHAR,
      ...options,
    };

    // Estado interno
    this._transport = null; // 'usb' | 'bluetooth' | null

    // Web Serial
    this._port = null;
    this._reader = null;
    this._writer = null;
    this._readLoopRunning = false;
    this._textDecoder = new TextDecoder();

    // Web Bluetooth
    this._btDevice = null;
    this._btService = null;
    this._btWriteChar = null; // característica donde escribimos
    this._btReadChar = null;  // característica con notificaciones que leemos
    this._btNotifyHandler = null;

    // Buffer para reconstruir líneas
    this._lineBuffer = '';
  }

  // -------------------------------------------------------------------------
  // Propiedades de estado
  // -------------------------------------------------------------------------

  /** Indica si Web Serial (cable USB) está disponible en este navegador. */
  get usbSupported() {
    return typeof navigator !== 'undefined' && 'serial' in navigator;
  }

  /** Indica si Web Bluetooth está disponible en este navegador. */
  get bluetoothSupported() {
    return typeof navigator !== 'undefined' && 'bluetooth' in navigator;
  }

  /** Devuelve true si hay una conexión activa (USB o Bluetooth). */
  get connected() {
    return this._transport !== null;
  }

  /** Transporte activo: 'usb', 'bluetooth' o null. */
  get transport() {
    return this._transport;
  }

  /** Información del puerto serie conectado (solo USB). */
  get portInfo() {
    return this._port ? this._port.getInfo() : null;
  }

  /** Nombre/ID del dispositivo Bluetooth conectado (solo Bluetooth). */
  get bluetoothDevice() {
    if (!this._btDevice) return null;
    return { name: this._btDevice.name, id: this._btDevice.id };
  }

  // -------------------------------------------------------------------------
  // Conexión por USB (Web Serial API)
  // -------------------------------------------------------------------------

  /**
   * Conecta por cable USB / puerto serie. Muestra el selector de puertos del navegador.
   * @param {Object} [options]
   * @param {number} [options.baudRate]        Baudios (por defecto los del constructor).
   * @param {Array}  [options.filters]         Filtros de Web Serial (p. ej. [{ usbVendorId: 0x2341 }]).
   * @returns {Promise<Object>} Información del puerto conectado.
   */
  async connectUSB(options = {}) {
    if (!this.usbSupported) {
      throw new Error('Web Serial API no está disponible en este navegador. Usa Chrome/Edge y sirve la página por HTTPS o localhost.');
    }
    if (this.connected) await this.disconnect();

    const { baudRate = this._options.baudRate, filters = [] } = options;

    let port;
    try {
      port = filters.length
        ? await navigator.serial.requestPort({ filters })
        : await navigator.serial.requestPort();
    } catch (err) {
      if (err && err.name === 'NotFoundError') {
        throw new Error('No se seleccionó ningún puerto serie.');
      }
      throw err;
    }

    await port.open({ baudRate });

    this._port = port;
    this._transport = 'usb';
    this._writer = null; // se crea al primer envío

    this._emit('connect', { transport: 'usb', baudRate, port: port.getInfo() });

    // La lectura corre en segundo plano; no bloquea connectUSB().
    this._startReadLoop();

    return port.getInfo();
  }

  // -------------------------------------------------------------------------
  // Conexión por Bluetooth (Web Bluetooth API)
  // -------------------------------------------------------------------------

  /**
   * Conecta por Bluetooth Low Energy (BLE) a un módulo UART (HM-10, HC-08, nRF, ESP32-BLE...).
   * @param {Object} [options]
   * @param {string} [options.name]              Nombre exacto del dispositivo.
   * @param {string} [options.namePrefix]        Prefijo del nombre (p. ej. 'HM', 'HC-08').
   * @param {string} [options.service]           UUID del servicio UART.
   * @param {string} [options.rxCharacteristic]  UUID donde ESCRIBIMOS (RX del módulo).
   * @param {string} [options.txCharacteristic]  UUID que LEEMOS por notificaciones (TX del módulo).
   * @returns {Promise<Object>} { name, id } del dispositivo.
   */
  async connectBluetooth(options = {}) {
    if (!this.bluetoothSupported) {
      throw new Error('Web Bluetooth API no está disponible en este navegador.');
    }
    if (this.connected) await this.disconnect();

    const {
      name = '',
      namePrefix = '',
      service = this._options.bluetoothService,
      rxCharacteristic = this._options.bluetoothRxCharacteristic,
      txCharacteristic = this._options.bluetoothTxCharacteristic,
    } = options;

    const request = { optionalServices: [service] };
    const filters = [];
    if (name) filters.push({ name });
    if (namePrefix) filters.push({ namePrefix });

    // Web Bluetooth exige EXACTAMENTE UNO de los dos:
    //   - 'filters'          (cuando se pidió un nombre/prefijo)
    //   - 'acceptAllDevices' (cuando no hay filtros)
    // Nunca los dos juntos y nunca ninguno de los dos.
    // 'optionalServices' se puede usar en ambos casos.
    if (filters.length) {
      request.filters = filters;
    } else {
      request.acceptAllDevices = true;
    }

    let device;
    try {
      device = await navigator.bluetooth.requestDevice(request);
    } catch (err) {
      if (err && err.name === 'NotFoundError') {
        throw new Error('No se seleccionó ningún dispositivo Bluetooth.');
      }
      throw err;
    }

    const server = await device.gatt.connect();
    const btService = await server.getPrimaryService(service);
    const writeChar = await btService.getCharacteristic(rxCharacteristic);
    const readChar = await btService.getCharacteristic(txCharacteristic);

    await readChar.startNotifications();
    this._btNotifyHandler = (event) => {
      const value = event.target.value;
      if (value) this._handleIncoming(this._textDecoder.decode(value));
    };
    readChar.addEventListener('characteristicvaluechanged', this._btNotifyHandler);

    this._btDevice = device;
    this._btService = btService;
    this._btWriteChar = writeChar;
    this._btReadChar = readChar;
    this._transport = 'bluetooth';

    // Si el módulo se apaga o se aleja, el navegador cierra la conexión.
    device.addEventListener('gattserverdisconnected', () => this._onBluetoothDisconnected());

    const info = { name: device.name, id: device.id };
    this._emit('connect', { transport: 'bluetooth', device: info });
    return info;
  }

  // -------------------------------------------------------------------------
  // Envío de datos
  // -------------------------------------------------------------------------

  /**
   * Envía datos al dispositivo conectado.
   * @param {string|Uint8Array|ArrayBuffer|ArrayBufferView|number} data Datos a enviar.
   * @returns {Promise<void>}
   */
  async send(data) {
    if (!this.connected) {
      throw new Error('No hay conexión activa. Llama a connectUSB() o connectBluetooth() primero.');
    }

    const bytes = this._toBytes(data);

    if (this._transport === 'usb') {
      if (!this._writer) {
        this._writer = this._port.writable.getWriter();
      }
      await this._writer.write(bytes);
    } else if (this._transport === 'bluetooth') {
      // Web Bluetooth no expone el MTU negociado: partimos en trozos seguros
      // (20 bytes con MTU 23). El ESP32 suele aceptar más, pero así no se
      // pierden paquetes con cualquier dispositivo.
      const chunk = this._options.bleChunkSize || BLE_SAFE_CHUNK;
      for (let i = 0; i < bytes.length; i += chunk) {
        await this._btWriteChar.writeValue(bytes.subarray(i, i + chunk));
        if (this._options.bleWriteDelay) {
          await new Promise((resolve) => setTimeout(resolve, this._options.bleWriteDelay));
        }
      }
    }
  }

  /**
   * Envía texto y agrega el fin de línea configurado (por defecto '\n').
   * Equivale a Serial.println() del lado de Arduino.
   * @param {string} text
   * @returns {Promise<void>}
   */
  sendLine(text) {
    return this.send(String(text) + this._options.lineEnding);
  }

  // -------------------------------------------------------------------------
  // Desconexión
  // -------------------------------------------------------------------------

  /** Cierra la conexión activa (USB o Bluetooth). */
  async disconnect() {
    if (!this.connected) return;

    const transport = this._transport;

    try {
      if (transport === 'usb') {
        await this._cancelReadLoop();
        if (this._writer) {
          try { await this._writer.close(); } catch (err) { /* ignorar */ }
          this._writer = null;
        }
        if (this._port) {
          try { await this._port.close(); } catch (err) { /* ignorar */ }
          this._port = null;
        }
      } else if (transport === 'bluetooth') {
        this._cleanupBluetooth();
        if (this._btDevice && this._btDevice.gatt && this._btDevice.gatt.connected) {
          this._btDevice.gatt.disconnect();
        }
      }
    } finally {
      this._transport = null;
      this._emit('disconnect', { transport });
    }
  }

  // -------------------------------------------------------------------------
  // Internos
  // -------------------------------------------------------------------------

  /** Convierte cualquier entrada soportada a Uint8Array. */
  _toBytes(data) {
    if (typeof data === 'string') {
      return new TextEncoder().encode(data);
    }
    if (data instanceof Uint8Array) {
      return data;
    }
    if (ArrayBuffer.isView(data)) {
      return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }
    if (data instanceof ArrayBuffer) {
      return new Uint8Array(data);
    }
    return new Uint8Array([data]);
  }

  /** Emite un evento con detalle. */
  _emit(type, detail) {
    this.dispatchEvent(new CustomEvent(type, { detail }));
  }

  /** Procesa texto entrante: emite 'data' y, si corresponde, 'line'. */
  _handleIncoming(text) {
    this._emit('data', text);

    if (this._options.dispatchLines !== false) {
      this._lineBuffer += text;
      const lines = this._lineBuffer.split(this._options.lineEnding);
      this._lineBuffer = lines.pop() || '';
      for (const line of lines) {
        this._emit('line', line.replace(/\r$/, ''));
      }
    }
  }

  /** Bucle de lectura del puerto serie (corre en segundo plano). */
  async _startReadLoop() {
    if (this._readLoopRunning) return;
    this._readLoopRunning = true;

    try {
      this._reader = this._port.readable.getReader();
      while (true) {
        const { value, done } = await this._reader.read();
        if (done) break;
        this._handleIncoming(this._textDecoder.decode(value, { stream: true }));
      }
    } catch (err) {
      if (this._transport === 'usb') {
        this._emit('error', err);
      }
    } finally {
      try { this._reader && this._reader.releaseLock(); } catch (err) { /* ignorar */ }
      this._reader = null;
      this._readLoopRunning = false;

      // Si el puerto desapareció sin que llamáramos a disconnect(), el cable se desconectó.
      if (this._transport === 'usb' && (!this._port || !this._port.readable)) {
        this._transport = null;
        this._port = null;
        this._emit('disconnect', { transport: 'usb', reason: 'device-unplugged' });
      }
    }
  }

  /** Detiene el bucle de lectura del puerto serie. */
  async _cancelReadLoop() {
    if (this._reader) {
      try { await this._reader.cancel(); } catch (err) { /* ignorar */ }
      try { this._reader.releaseLock(); } catch (err) { /* ignorar */ }
      this._reader = null;
    }
  }

  /** Limpia referencias y suscripciones Bluetooth. */
  _cleanupBluetooth() {
    if (this._btReadChar && this._btNotifyHandler) {
      try {
        this._btReadChar.removeEventListener('characteristicvaluechanged', this._btNotifyHandler);
      } catch (err) { /* ignorar */ }
    }
    if (this._btReadChar) {
      try { this._btReadChar.stopNotifications(); } catch (err) { /* ignorar */ }
    }
    this._btNotifyHandler = null;
    this._btReadChar = null;
    this._btWriteChar = null;
    this._btService = null;
    this._btDevice = null;
  }

  /** Se ejecuta cuando el navegador informa que el BLE se desconectó. */
  _onBluetoothDisconnected() {
    if (this._transport !== 'bluetooth') return;
    this._cleanupBluetooth();
    this._transport = null;
    this._emit('disconnect', { transport: 'bluetooth', reason: 'gattserverdisconnected' });
  }
}

// Acceso global de la clase (útil tanto con <script> clásico como con módulos).
if (typeof window !== 'undefined' && !window.ArduinoUSB) {
  window.ArduinoUSB = ArduinoUSB;
}

/* ============================================================================
 * Web Component <arduino-usb>
 * ============================================================================
 * Envoltorio pensado para alumnos: no hace falta escribir una clase ni manejar
 * promesas complejas. Se usa con una etiqueta HTML y métodos simples.
 *
 *   <script src="./arduino-usb.js"></script>
 *   <arduino-usb id="arduino" eventos="componentes"></arduino-usb>
 *
 *   <script>
 *     const arduino = document.getElementById('arduino');
 *     arduino.conectarUSB();          // abre el selector de puertos
 *     arduino.enviarLinea('LED:ON');  // envía "LED:ON\n"
 *   </script>
 *
 * Cámara:
 *   arduino.camara();               // webcam de la PC o cámara del celular
 *   arduino.capturarFoto();         // devuelve dataURL de la imagen
 *   arduino.video                   // <video> actual (para librerías de reconocimiento)
 *
 * Atributos opcionales:
 *   eventos="nombreGlobal"  -> objeto con handlers onConnect/onData/onLine/
 *                              onDisconnect/onError (ver README).
 *   baudios="9600"          -> velocidad por defecto del puerto USB.
 *   prefijo-ble="ESP32"     -> prefijo del nombre BLE a buscar.
 *   fin-linea="\n"          -> carácter(es) de fin de línea para enviarLinea().
 *   servicio/rx/tx          -> UUIDs del servicio UART (avanzado).
 *
 * Eventos DOM que emite (CustomEvent en el elemento):
 *   arduino:connect, arduino:data, arduino:line, arduino:disconnect,
 *   arduino:error, arduino:camera, arduino:photo
 * ============================================================================ */

const EV_CONNECT    = 'arduino:connect';
const EV_DATA       = 'arduino:data';
const EV_LINE       = 'arduino:line';
const EV_DISCONNECT = 'arduino:disconnect';
const EV_ERROR      = 'arduino:error';
const EV_CAMERA     = 'arduino:camera';
const EV_PHOTO      = 'arduino:photo';

// Mapeo entre eventos del componente y funciones del objeto "eventos".
const EVENTO_A_HANDLER = {
  [EV_CONNECT]:    'onConnect',
  [EV_DATA]:       'onData',
  [EV_LINE]:       'onLine',
  [EV_DISCONNECT]: 'onDisconnect',
  [EV_ERROR]:      'onError',
};

class ArduinoUSBElement extends HTMLElement {
  constructor() {
    super();
    this.__arduino = null;
    this.__eventos = null;
    this._camaraStream = null;
    this._fuente = null; // 'local' (webcam) | null
    this._video = null;
    this._canvas = null;
  }

  /* ------------------------------- atributos ------------------------------ */

  /** Velocidad en baudios por defecto (atributo `baudios`, por defecto 9600). */
  get baudios() {
    const v = this.getAttribute('baudios');
    const n = Number(v);
    return v !== null && Number.isFinite(n) && n > 0 ? n : 9600;
  }

  /** Prefijo del nombre BLE (atributo `prefijo-ble`). */
  get prefijoBle() {
    return this.getAttribute('prefijo-ble') || '';
  }

  /** Fin de línea usado por enviarLinea() (atributo `fin-linea`). */
  get finLinea() {
    return this.getAttribute('fin-linea') || '\n';
  }

  get servicio() { return this.getAttribute('servicio') || null; }
  get rx()       { return this.getAttribute('rx') || null; }
  get tx()       { return this.getAttribute('tx') || null; }

  /** Objeto de handlers (resuelto desde el atributo `eventos`). */
  get eventos() {
    if (this.__eventos) return this.__eventos;
    const nombre = this.getAttribute('eventos');
    if (!nombre) return null;
    const obj = window[nombre];
    return obj && typeof obj === 'object' ? obj : null;
  }

  set eventos(objONombre) {
    if (typeof objONombre === 'string') {
      this.setAttribute('eventos', objONombre);
      this.__eventos = null;
    } else if (objONombre && typeof objONombre === 'object') {
      this.__eventos = objONombre;
      this.setAttribute('eventos', '');
    }
  }

  /* -------------------------------- estado -------------------------------- */

  get conectado()         { return this._arduino.connected; }
  get transporte()        { return this._arduino.transport; }
  get soportaUSB()        { return this._arduino.usbSupported; }
  get soportaBluetooth()  { return this._arduino.bluetoothSupported; }
  get camaraActiva()      { return this._fuente !== null; }

  /**
   * Elemento <video> de la cámara (local o remota). Útil para pasárselo a
   * librerías de reconocimiento como Teachable Machine, o null si no hay cámara.
   */
  get video()             { return this._video; }

  /** Instancia interna de ArduinoUSB (se crea la primera vez que se usa). */
  get _arduino() {
    if (!this.__arduino) {
      const opciones = { baudRate: this.baudios, lineEnding: this.finLinea };
      if (this.servicio) opciones.bluetoothService = this.servicio;
      if (this.rx) opciones.bluetoothRxCharacteristic = this.rx;
      if (this.tx) opciones.bluetoothTxCharacteristic = this.tx;

      this.__arduino = new ArduinoUSB(opciones);

      this.__arduino.addEventListener('connect', (e) => {
        const d = e.detail;
        this._emit(EV_CONNECT, {
          transporte: d.transport,
          baudios: d.baudRate,
          puerto: d.port,
          dispositivo: d.device,
        });
      });
      this.__arduino.addEventListener('data',       (e) => this._emit(EV_DATA, e.detail));
      this.__arduino.addEventListener('line',       (e) => this._emit(EV_LINE, e.detail));
      this.__arduino.addEventListener('disconnect', (e) => this._emit(EV_DISCONNECT, {
        transporte: e.detail.transport,
        motivo: e.detail.reason,
      }));
      this.__arduino.addEventListener('error',      (e) => this._emit(EV_ERROR, e.detail));
    }
    return this.__arduino;
  }

  /** Emite el evento DOM y, si hay handler en "eventos", lo llama. */
  _emit(tipo, detalle) {
    this.dispatchEvent(new CustomEvent(tipo, { detail: detalle }));

    const handler = EVENTO_A_HANDLER[tipo];
    if (!handler) return;

    const eventos = this.eventos;
    if (eventos && typeof eventos[handler] === 'function') {
      try {
        eventos[handler](detalle);
      } catch (err) {
        console.error(`[arduino-usb] Error en el handler "${handler}":`, err);
      }
    }
  }

  /* ------------------------------- conexión ------------------------------- */

  /**
   * Conecta por cable USB (Web Serial). Abre el selector de puertos.
   * @param {number} [baudios] Velocidad (si no se indica, usa el atributo `baudios`).
   * @returns {Promise<Object>} Información del puerto.
   */
  conectarUSB(baudios) {
    const rate = Number(baudios);
    return this._arduino.connectUSB({
      baudRate: Number.isFinite(rate) && rate > 0 ? rate : this.baudios,
    });
  }

  /**
   * Conecta por Bluetooth (BLE). Abre el selector de dispositivos.
   * @param {string} [prefijo] Prefijo del nombre BLE (si no, usa `prefijo-ble`).
   * @returns {Promise<Object>} { nombre, id } del dispositivo.
   */
  conectarBluetooth(prefijo) {
    const opts = {};
    const p = prefijo || this.prefijoBle;
    if (p) opts.namePrefix = p;
    return this._arduino.connectBluetooth(opts);
  }

  /** Envía datos al dispositivo conectado. */
  enviar(datos) {
    return this._arduino.send(datos);
  }

  /** Envía texto y agrega el fin de línea configurado (por defecto '\n'). */
  enviarLinea(texto) {
    return this._arduino.sendLine(texto);
  }

  /** Cierra la conexión activa (USB o Bluetooth). */
  desconectar() {
    return this._arduino.disconnect();
  }

  /* -------------------------------- cámara -------------------------------- */

  /** Crea (si hace falta) el <video> interno donde se muestra la cámara. */
  _asegurarVideo() {
    if (this._video) return this._video;

    this._video = document.createElement('video');
    this._video.setAttribute('autoplay', '');
    this._video.setAttribute('playsinline', '');
    this._video.setAttribute('muted', '');
    this._video.style.width = '100%';
    this._video.style.maxWidth = '640px';
    this.appendChild(this._video);
    return this._video;
  }

  /**
   * Enciende la cámara del dispositivo (webcam de la PC o cámara del celular,
   * según dónde se abra la página) y muestra la vista previa dentro del
   * componente.
   * @returns {Promise<MediaStream>} El stream de la cámara local.
   */
  async camara() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      throw new Error('La cámara no está disponible en este navegador. Sirve la página por HTTPS o localhost.');
    }
    if (this._fuente === 'local' && this._camaraStream) return this._camaraStream;

    const stream = await navigator.mediaDevices.getUserMedia({ video: true, audio: false });
    this._camaraStream = stream;
    this._fuente = 'local';

    const video = this._asegurarVideo();
    video.srcObject = stream;
    try { await video.play(); } catch (err) { /* el autoplay puede requerir interacción */ }

    this._emit(EV_CAMERA, { activa: true, fuente: 'local', stream });
    return stream;
  }

  /**
   * Captura una foto de la cámara y devuelve su URL
   * (dataURL PNG). Si la cámara no está encendida, la enciende primero.
   * @returns {Promise<string>} Imagen en formato data:image/png;base64,...
   */
  async capturarFoto() {
    if (this._fuente === null) await this.camara();

    // Damos un instante a que el <video> tenga dimensiones reales.
    if (!this._video || !this._video.videoWidth) {
      await new Promise((resolve) => setTimeout(resolve, 250));
    }

    const video = this._video;
    if (!this._canvas) this._canvas = document.createElement('canvas');

    this._canvas.width  = video.videoWidth  || 1280;
    this._canvas.height = video.videoHeight || 720;
    const ctx = this._canvas.getContext('2d');
    ctx.drawImage(video, 0, 0, this._canvas.width, this._canvas.height);

    const foto = this._canvas.toDataURL('image/png');
    this._emit(EV_PHOTO, { foto });
    return foto;
  }

  /** Apaga la cámara (local o remota) y quita la vista previa. */
  detenerCamara() {
    if (this._camaraStream) {
      this._camaraStream.getTracks().forEach((track) => track.stop());
      this._camaraStream = null;
    }
    if (this._video) {
      this._video.pause();
      this._video.srcObject = null;
      this._video.src = '';
      if (this._video.parentNode === this) this.removeChild(this._video);
      this._video = null;
    }
    this._fuente = null;
    this._emit(EV_CAMERA, { activa: false });
  }
}

// Registrar el componente solo si no existe ya.
if (typeof customElements !== 'undefined' && !customElements.get('arduino-usb')) {
  customElements.define('arduino-usb', ArduinoUSBElement);
}

if (typeof window !== 'undefined' && !window.ArduinoUSBElement) {
  window.ArduinoUSBElement = ArduinoUSBElement;
}
