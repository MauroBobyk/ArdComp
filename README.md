# arduino-usb-web

Conecta un **Arduino (UNO/Nano/Mega o ESP32)** a una página web desde el navegador, usando el **puerto serie USB** o **Bluetooth (BLE)**. Además permite **encender la cámara web y capturar fotos**.

Pensado para alumnos: se usa con una **etiqueta HTML** (`<arduino-usb>`) y unos pocos métodos. No hace falta instalar nada, compilar nada ni escribir JavaScript avanzado. Es un único archivo.

- **Enviar** mensajes a Arduino (ESP32 y UNO)
- **Recibir** mensajes de Arduino (ESP32 y UNO)
- **Cámara web**: vista previa y captura de fotos

---

## IMPORTANTE — LEER ANTES DE IMPLEMENTAR

Antes de que los alumnos escriban una sola línea, es **obligatorio** que tengan en cuenta esto:

1. **Navegador:** solo funciona en **Chrome, Edge u Opera** (Firefox y Safari no tienen Web Serial ni Web Bluetooth).
2. **Contexto seguro:** la página debe abrirse por **`https://` o `http://localhost`**. Si se abre con doble clic (`file://`) **no funciona**.
3. **Baudios:** la velocidad de la **página** y del **sketch** deben ser iguales. UNO = `9600`, ESP32 = `115200`.
4. **Monitor Serie cerrado:** antes de usar la web hay que cerrar el Monitor Serie del IDE de Arduino (si no, el puerto queda bloqueado).
5. **ESP32 por Bluetooth:** se debe cargar el sketch `esp32_ble_nus`. El ejemplo `BluetoothSerial` clásico **no** se ve desde el navegador.
6. **Celular:** no tiene Web Serial (el USB no funciona). Desde el celular se usa **Bluetooth BLE** y la cámara.

Si algo no anda, mirá [Problemas comunes](#problemas-comunes).

---

## Índice

0. [IMPORTANTE — leer antes de implementar](#importante--leer-antes-de-implementar)
1. [Requisitos](#requisitos)
2. [Cómo importarlo](#cómo-importarlo)
3. [Uso mínimo](#uso-mínimo)
4. [La etiqueta `<arduino-usb>`](#la-etiqueta-arduino-usb)
5. [Métodos](#métodos)
6. [Propiedades](#propiedades)
7. [Eventos](#eventos)
8. [El atributo `eventos` (handlers)](#el-atributo-eventos-handlers)
9. [Cámara web](#cámara-web)
10. [Ejemplos completos](#ejemplos-completos)
11. [Sketches de Arduino (firmware)](#sketches-de-arduino-firmware)
12. [Protocolo de mensajes](#protocolo-de-mensajes)
13. [Cómo subirlo a GitHub](#cómo-subirlo-a-github)
14. [Problemas comunes](#problemas-comunes)

---

## Requisitos

- Navegador **Chrome, Edge u Opera** (Web Serial y Web Bluetooth no están en Firefox/Safari).
- La página debe servirse por **HTTPS o `localhost`** (contexto seguro). Un archivo abierto con `file://` **no** funciona.
- Una placa:
  - **Arduino UNO / Nano / Mega** → por cable USB.
  - **ESP32** → por cable USB o por Bluetooth (BLE).
- Para USB: que **no esté abierto el Monitor Serie** del IDE de Arduino mientras usás la web (el puerto queda bloqueado).

---

## Cómo importarlo

### Opción A — Desde GitHub (CDN, recomendado)

Subí el archivo a un repositorio de GitHub (ver [Cómo subirlo a GitHub](#cómo-subirlo-a-github)) y los alumnos lo importan así, reemplazando `USUARIO/REPO`:

```html
<script src="https://cdn.jsdelivr.net/gh/USUARIO/REPO@main/arduino-usb.js"></script>
```

Por ejemplo:

```html
<script src="https://cdn.jsdelivr.net/gh/MauroBobyk/ArdComp@main/arduino-usb.js"></script>
```

### Opción B — Archivo local

Descargá `arduino-usb.js` al lado de tu HTML:

```html
<script src="./arduino-usb.js"></script>
```

> El `<script>` es **normal** (no lleva `type="module"`).

---

## Uso mínimo

```html
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8" />
  <title>Mi proyecto</title>
  <script src="https://cdn.jsdelivr.net/gh/USUARIO/REPO@main/arduino-usb.js"></script>
</head>
<body>
  <!-- La etiqueta que conecta todo -->
  <arduino-usb id="arduino" eventos="componentes" baudios="9600"></arduino-usb>

  <button onclick="document.getElementById('arduino').conectarUSB()">Conectar</button>
  <button onclick="document.getElementById('arduino').enviarLinea('LED:ON')">Prender LED</button>

  <script>
    // Objeto con las funciones que se ejecutan cuando pasa algo
    window.componentes = {
      onConnect:    () => console.log('Conectado'),
      onLine:       (linea) => console.log('Arduino dice:', linea),
      onDisconnect: () => console.log('Desconectado'),
      onError:      (error) => console.error(error.message),
    };
  </script>
</body>
</html>
```

---

## La etiqueta `<arduino-usb>`

Se usa como cualquier etiqueta HTML. El atributo `eventos` apunta a un objeto global con las funciones que se ejecutan automáticamente (ver [handlers](#el-atributo-eventos-handlers)).

```html
<arduino-usb id="arduino" eventos="componentes" baudios="9600" prefijo-ble="ESP32"></arduino-usb>
```

### Atributos

| Atributo | Por defecto | Descripción |
| -------- | ----------- | ----------- |
| `eventos` | *(ninguno)* | Nombre del objeto global (sin `window.`) que contiene los handlers. |
| `baudios` | `9600` | Velocidad del puerto USB. **UNO = 9600**, **ESP32 = 115200**. |
| `prefijo-ble` | *(ninguno)* | Prefijo del nombre del dispositivo BLE a buscar (ej. `ESP32`). |
| `fin-linea` | `\n` | Fin de línea que agrega `enviarLinea()`. |
| `servicio` | UART nórdico | UUID del servicio BLE (avanzado). |
| `rx` | UART nórdico | UUID de la característica de escritura (avanzado). |
| `tx` | UART nórdico | UUID de la característica de lectura (avanzado). |

---

## Métodos

Todos los métodos se llaman sobre el elemento, por ejemplo:

```js
const arduino = document.getElementById('arduino');
arduino.conectarUSB();
```

| Método | Devuelve | Descripción |
| ------ | -------- | ----------- |
| `conectarUSB(baudios?)` | `Promise<Object>` | Abre el selector de puertos y conecta por USB. Podés pasar la velocidad: `conectarUSB(115200)`. |
| `conectarBluetooth(prefijo?)` | `Promise<Object>` | Abre el selector de dispositivos y conecta por BLE. Podés pasar el prefijo: `conectarBluetooth('ESP32')`. |
| `enviar(datos)` | `Promise<void>` | Envía datos. Acepta texto, `Uint8Array`, `ArrayBuffer` o número. |
| `enviarLinea(texto)` | `Promise<void>` | Envía texto y agrega el fin de línea (equivale a `Serial.println()`). Ej.: `enviarLinea('LED:ON')` envía `LED:ON\n`. |
| `desconectar()` | `Promise<void>` | Cierra la conexión activa. |
| `camara()` | `Promise<MediaStream>` | Enciende la **cámara del dispositivo** (webcam de la PC o cámara del celular) y muestra la vista previa. |
| `capturarFoto()` | `Promise<string>` | Captura una foto de la cámara y devuelve su URL (`data:image/png;base64,...`). |
| `detenerCamara()` | `void` | Apaga la cámara y quita la vista previa. |

> Como devuelven `Promise`, conviene usar `await` o `.then()`/`.catch()`. Los errores (puerto bloqueado, permiso denegado, etc.) lanzan un `Error` con un mensaje claro.

---

## Propiedades

| Propiedad | Tipo | Descripción |
| --------- | ---- | ----------- |
| `conectado` | `boolean` | `true` si hay conexión activa. |
| `transporte` | `'usb' \| 'bluetooth' \| null` | Transporte en uso. |
| `soportaUSB` | `boolean` | Si el navegador soporta Web Serial. |
| `soportaBluetooth` | `boolean` | Si el navegador soporta Web Bluetooth. |
| `camaraActiva` | `boolean` | `true` si la cámara (local o remota) está encendida. |
| `video` | `HTMLVideoElement \| null` | El `<video>` actual, para pasárselo a librerías de reconocimiento (ej. Teachable Machine). |
| `baudios` / `prefijoBle` / `finLinea` | `number` / `string` / `string` | Valores de los atributos correspondientes. |

---

## Eventos

El componente emite **eventos DOM** (tipo `CustomEvent`) sobre el elemento `<arduino-usb>`. Se escuchan con `addEventListener`.

| Evento | `event.detail` | Cuándo ocurre |
| ------ | -------------- | ------------- |
| `arduino:connect` | `{ transporte, baudios?, puerto?, dispositivo? }` | Al conectarse (USB o BLE). |
| `arduino:data` | `string` — texto crudo tal cual llega | Cada vez que llegan datos. |
| `arduino:line` | `string` — una línea completa (sin el fin de línea) | Por cada línea recibida. |
| `arduino:disconnect` | `{ transporte, motivo? }` | Al desconectarse. |
| `arduino:error` | `Error` | Ante un error. |
| `arduino:camera` | `{ activa, stream? }` | Al encender/apagar la cámara. |
| `arduino:photo` | `{ foto }` — URL de la imagen | Al capturar una foto. |

### Ejemplo con eventos DOM

```js
const arduino = document.getElementById('arduino');

arduino.addEventListener('arduino:connect',    () => console.log('¡Conectado!'));
arduino.addEventListener('arduino:data',       (e) => console.log('Datos:', e.detail));
arduino.addEventListener('arduino:line',       (e) => console.log('Línea:', e.detail));
arduino.addEventListener('arduino:disconnect', () => console.log('Desconectado'));
arduino.addEventListener('arduino:error',      (e) => console.error(e.detail.message));
arduino.addEventListener('arduino:photo',      (e) => mostrarFoto(e.detail.foto));
```

---

## El atributo `eventos` (handlers)

Es la forma más simple: en vez de usar `addEventListener`, definís un objeto global con funciones y se lo pasás al componente por el atributo `eventos`.

```html
<arduino-usb id="arduino" eventos="componentes"></arduino-usb>
```

```js
window.componentes = {
  onConnect:    (detalle) => { /* detalle = { transporte, ... } */ },
  onData:       (texto)   => { /* texto crudo recibido */ },
  onLine:       (linea)   => { /* una línea completa */ },
  onDisconnect: (detalle) => { /* detalle = { transporte, motivo? } */ },
  onError:      (error)   => { /* objeto Error */ },
};
```

Cada función recibe **un único argumento** (el `detail` del evento). Ninguna es obligatoria: si no definís `onData`, simplemente no se llama.

| Handler | Recibe | Equivale al evento |
| ------- | ------ | ------------------ |
| `onConnect` | `{ transporte, baudios?, puerto?, dispositivo? }` | `arduino:connect` |
| `onData` | `string` (texto crudo) | `arduino:data` |
| `onLine` | `string` (línea completa) | `arduino:line` |
| `onDisconnect` | `{ transporte, motivo? }` | `arduino:disconnect` |
| `onError` | `Error` | `arduino:error` |

> También podés asignar el objeto directamente por JavaScript: `document.getElementById('arduino').eventos = componentes;`

---

## Cámara web

`camara()` enciende la cámara **del dispositivo donde se abre la página**:

- Página en la **PC** → usa la webcam de la PC.
- Página en el **celular** → usa la cámara del celular (el código es el mismo).

```js
const arduino = document.getElementById('arduino');

async function prenderCamara() {
  try {
    await arduino.camara();      // enciende la cámara y muestra el video
    console.log('Cámara encendida');
  } catch (err) {
    console.error(err.message);  // p. ej. permiso denegado o sin cámara
  }
}
```

El video se inserta **dentro del propio `<arduino-usb>`**. Por eso conviene darle un lugar visible:

```html
<div>
  <arduino-usb id="arduino"></arduino-usb>
</div>
```

```css
arduino-usb { display: block; width: 100%; max-width: 640px; }
```

> ⚠️ En el celular **no hay Web Serial** (`conectarUSB()` no funciona). Para conectar un ESP32 desde el celular usá Bluetooth: `conectarBluetooth()`. Y la página tiene que estar publicada en **HTTPS** (en el celular no sirve `localhost`).

### Capturar foto (`capturarFoto`)

Devuelve una URL lista para un `<img>`:

```js
async function sacarFoto() {
  const url = await arduino.capturarFoto();
  document.getElementById('foto').src = url;   // <img id="foto">
}
```

### El elemento `video` (para reconocimiento)

Si los alumnos usan **Teachable Machine** u otra librería, pueden pasarle el `<video>` activo:

```js
const arduino = document.getElementById('arduino');
await arduino.camara();
const video = arduino.video;         // <video> con la cámara

// Teachable Machine: modelo.predict(video, ...)
```

### Apagar (`detenerCamara`)

```js
arduino.detenerCamara();
```

> La cámara (local o remota) requiere HTTPS o `localhost`, igual que Web Serial/Bluetooth.

---

## Ejemplos completos

### Ejemplo 1 — UNO por USB (9600)

```html
<arduino-usb id="arduino" eventos="componentes" baudios="9600"></arduino-usb>

<button onclick="arduino.conectarUSB()">Conectar</button>
<button onclick="arduino.enviarLinea('LED:ON')">LED ON</button>
<button onclick="arduino.enviarLinea('LED:OFF')">LED OFF</button>

<script>
  window.componentes = {
    onLine: (linea) => console.log('Recibido:', linea),
    onError: (error) => console.error(error.message),
  };
</script>
```

### Ejemplo 2 — ESP32 por USB (115200)

```html
<arduino-usb id="arduino" eventos="componentes" baudios="115200"></arduino-usb>
```

Igual que el anterior, pero con `baudios="115200"` (o `arduino.conectarUSB(115200)`).

### Ejemplo 3 — ESP32 por Bluetooth (BLE)

```html
<arduino-usb id="arduino" eventos="componentes" prefijo-ble="ESP32"></arduino-usb>

<button onclick="arduino.conectarBluetooth()">Conectar BLE</button>
```

El sketch `esp32_ble_nus` anuncia el nombre `ESP32-NUS`, por eso se busca el prefijo `ESP32`.

### Ejemplo 4 — Todo junto con cámara

```html
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8" />
  <title>Proyecto integrador</title>
  <script src="https://cdn.jsdelivr.net/gh/USUARIO/REPO@main/arduino-usb.js"></script>
</head>
<body>
  <arduino-usb id="arduino" eventos="componentes" baudios="9600" prefijo-ble="ESP32"></arduino-usb>

  <button onclick="arduino.conectarUSB()">Conectar USB</button>
  <button onclick="arduino.conectarBluetooth()">Conectar Bluetooth</button>
  <button onclick="arduino.enviarLinea('PING')">Enviar PING</button>
  <button onclick="arduino.desconectar()">Desconectar</button>
  <button onclick="prenderCamara()">Cámara</button>
  <button onclick="sacarFoto()">Foto</button>
  <button onclick="arduino.detenerCamara()">Apagar cámara</button>

  <img id="foto" alt="Foto" style="max-width: 320px; display: none;" />

  <script>
    window.componentes = {
      onConnect:    () => console.log('Conectado'),
      onLine:       (linea) => console.log('Arduino:', linea),
      onDisconnect: () => console.log('Desconectado'),
      onError:      (error) => console.error(error.message),
    };

    async function prenderCamara() {
      try { await arduino.camara(); } catch (err) { console.error(err.message); }
    }

    async function sacarFoto() {
      try {
        const url = await arduino.capturarFoto();
        const img = document.getElementById('foto');
        img.src = url;
        img.style.display = 'block';
      } catch (err) { console.error(err.message); }
    }
  </script>
</body>
</html>
```

> Ojo: en estos ejemplos los `onclick` usan la variable global `arduino` (el `id` del elemento). Eso funciona en los navegadores modernos, pero lo más prolijo es `document.getElementById('arduino')`.

---

## Sketches de Arduino (firmware)

Dentro de `sketches/` hay tres firmwares listos para cargar con el IDE de Arduino:

| Sketch | Placa | Transporte | Baudios | Dónde |
| ------ | ----- | ---------- | ------- | ----- |
| `uno_serial` | Arduino UNO / Nano / Mega | USB | 9600 | `sketches/uno_serial/uno_serial.ino` |
| `esp32_serial` | ESP32 | USB | 115200 | `sketches/esp32_serial/esp32_serial.ino` |
| `esp32_ble_nus` | ESP32 | Bluetooth (BLE) | — (115200 solo para el Monitor Serie) | `sketches/esp32_ble_nus/esp32_ble_nus.ino` |

### Cómo cargarlos

1. Abrí el `.ino` en el IDE de Arduino (o Arduino Web Editor).
2. Elegí la placa:
   - UNO: **Herramientas → Placa → Arduino Uno**.
   - ESP32: instalá el soporte de ESP32 (URL de placas `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`) y elegí tu placa ESP32.
3. Conectá la placa por USB y subí el sketch (**→**).
4. Cerrá el **Monitor Serie** antes de usar la página web (si no, el puerto queda bloqueado).

> **ESP32-S2/S3/C3 (USB nativo):** en el sketch `esp32_serial`, activá **Herramientas → USB CDC On Boot → Enabled**, si no el navegador ve el puerto pero no llegan datos.

---

## Protocolo de mensajes

La web y el Arduino se comunican con **texto plano, una orden por línea**, terminada en `\n`. La web manda con `enviarLinea()` y recibe con `onLine` / `arduino:line`.

| Comando (web → Arduino) | Respuesta (Arduino → web) |
| ----------------------- | ------------------------- |
| `PING` | `PONG` |
| `LED:ON` | `OK:LED=ON` |
| `LED:OFF` | `OK:LED=OFF` |
| `LED:TOGGLE` | `OK:LED=ON` o `OK:LED=OFF` |
| `STATUS` | `STATUS:LED=ON,UPTIME=12` (+`HEAP` en ESP32) |
| `READ:A0` … `READ:A5` | `A0:512` |
| `READ:GPIO4` (solo ESP32) | `GPIO4:0` o `GPIO4:1` |
| `ECHO:texto` | `ECHO:texto` |
| `HELP` | lista de órdenes |
| cualquier otra | `ERR:UNKNOWN:<orden>` |

Además, cada 5 segundos la placa envía sola `HB:<segundos>` (latido, sirve para saber que sigue viva).

---

## Cómo subirlo a GitHub

### Paso 1 — Crear el repositorio

1. Entrá a [github.com](https://github.com) e iniciá sesión.
2. Arriba a la derecha, botón **`+` → New repository**.
3. Nombre (ej. `Arduino`), descripción opcional, **público** (para que los alumnos puedan usarlo).
4. **Create repository**. **No** marques "Add a README" si vas a subir uno propio.

### Paso 2 — Subir los archivos

En la carpeta del proyecto, en una terminal:

```bash
git init                                  # solo si todavía no es un repo git
git add .
git commit -m "Componente arduino-usb + sketches"
git branch -M main
git remote add origin https://github.com/TU_USUARIO/TU_REPO.git
git push -u origin main
```

> Si el repo ya tiene remoto, solo: `git add . && git commit -m "..." && git push`.

### Paso 3 — Obtener la URL de importación (jsDelivr)

Con el repo subido, el CDN **jsDelivr** sirve los archivos gratis y sin configuración. La URL sigue este patrón:

```
https://cdn.jsdelivr.net/gh/TU_USUARIO/TU_REPO@main/arduino-usb.js
```

Los alumnos la pegan en su HTML:

```html
<script src="https://cdn.jsdelivr.net/gh/TU_USUARIO/TU_REPO@main/arduino-usb.js"></script>
```

### (Opcional) GitHub Pages

Para publicar el `index.html` de demo:

1. Repo → **Settings → Pages**.
2. En *Source* elegí **Deploy from a branch** y la rama **main** (carpeta `/root`).
3. Guardá. En un minuto tendrás una URL tipo `https://TU_USUARIO.github.io/TU_REPO/`.

### Importante: la página de los alumnos también necesita HTTPS

Web Serial, Web Bluetooth y la cámara exigen **contexto seguro**. Si un alumno abre su HTML con `file://`, no va a funcionar. Opciones:
- Servirlo por `localhost` (por ejemplo con la extensión *Live Server* de VS Code).
- Subirlo a GitHub Pages, Netlify, Vercel, etc.

---

## Problemas comunes

| Problema | Causa probable | Solución |
| -------- | -------------- | -------- |
| No aparece el selector de puertos | Navegador sin Web Serial o página servida por `file://` | Usá Chrome/Edge y serví por `localhost` o HTTPS. |
| "El puerto ya está abierto o bloqueado" | El Monitor Serie del IDE está abierto | Cerrá el Monitor Serie (o el IDE). |
| Se ven símbolos raros | Baudios mal configurados | UNO = 9600, ESP32 = 115200. |
| El ESP32 no aparece en el selector BLE | Firmware con `BluetoothSerial` (clásico) en vez de BLE/NUS | Cargá el sketch `esp32_ble_nus`. |
| No llegan datos por BLE | MTU muy chico o mensajes largos | La librería ya envía de a 20 bytes; usá órdenes cortas. |
| La cámara no enciende | Permiso denegado o sin HTTPS | Permití la cámara y usá HTTPS/localhost. |
| La foto sale en negro | Se capturó antes de que el video cargara | Volvé a capturar; el componente espera un instante antes de capturar. |

---

## ⚖️ Licencia y Limitación de Responsabilidad

Este proyecto está publicado bajo la licencia **GNU General Public License v3.0 (GPL-3.0)**. Podés consultar los términos completos en el archivo [LICENSE](LICENSE).

### ¿Qué significa esto para las clases y proyectos?
* **Libertad de uso:** Sos libre de descargar, modificar, usar y distribuir este código para tus trabajos prácticos, proyectos personales o profesionales.
* **Código abierto obligado:** Si modificás este software y decidís compartirlo o publicarlo, estás obligado a hacerlo de forma pública y bajo esta misma licencia GPLv3.
* **Sin garantías ("As Is"):** El software se entrega **tal cual está**, con fines puramente educativos. No se ofrece ninguna garantía de funcionamiento.
* **Exención de responsabilidad:** El autor no se hace responsable por códigos que no compilen, fallas en el sistema, ni por cualquier daño físico o rotura de componentes de hardware (como placas Arduino, sensores o actuadores) derivados del uso de este programa. **El uso corre por cuenta y riesgo del usuario.**

