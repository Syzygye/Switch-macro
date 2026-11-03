# CivSwitch

XSS toolchain for the Switch 1 edition of Civilization 7.  
Only the **1.0.0** version of the game in physical cartridge is supported.  

Every switch 1/2 firmware 20.x.x and up to current version (23.0.1) should work.  

Tested at Switch 1 fw 22.5.0, Switch 2 fw 20.4.0 and 23.0.1  

Four components:

- **PicoSwitch** — clean, compilable Pico firmware (RP2040 + RP2350) that types a chat-XSS payload into the Switch (USB HID macro).

  Default payload = the Remote JS Loader bootstrap (exploit core + TCP loader).

  Ships with `compress.js` (chat transport: terser -> deflateRaw -> base-25 chunked `img/onerror` lines with censor armor).
- **installer-creator** — given any JavaScript payload, produces the boot-persistent installer (`XSTRIG.Civ7Save` trigger written to savedata) to send through the Remote JS Loader.
- **FlipperApp** — native Flipper Zero FAP that relays PC controller input over BLE to the Switch-compatible USB virtual gamepad, manages `.txt` macros on-device, and replays them from the SD card. See `FlipperApp/README.md` for setup and macro format.
- **PCRecorder** — local browser tool that captures a PC-connected controller, relays it live through the Flipper, and exports the recording for standalone playback. See `PCRecorder/README.md`.

## Layout

`core.js` at the root is the shared payload both components build from, and `send_js.py` at the root sends any payload to a running loader.

## Workflow

[Video Tutorial](https://www.reddit.com/r/switch2hacks/comments/1xll4wf/civswitch_hack_start_to_finish/) from switch2hacks reddit

1. **Build + flash** the loader UF2 (see PicoSwitch/README.md).

   On the Switch, go to "System" menu and change system language to English(US).  
   Go to "Controllers & Accessories" menu and change USB Keyboard to English(US).  
   
   These are required to type "English" letters (code) using USB keyboard.  
   (Higher fw might not need to change system language as they support typing english without language change)  
   
   NOTE for JP-only Switch2 : 
   1. If you have another Switch, you can connect the two consoles via local wireless multiplayer and plug the Pico on the other device, allowing it to send the payload over the network.  
   2. If you're on a higher fw version (I'm not sure exact version), you can simply try plugging the Pico and see if it can type English text without changing the system language.  
   
   Connect to Internet using [90DNS](https://gitlab.com/a/90dns) to block Nintendo servers.  
   
   Then launch game and navigate main menu -> MULTIPLAYER -> WIRELESS -> CREATE GAME -> HOST LOBBY -> CHAT then plug the Pico in (in docked mode, focus the chat box first to receive Pico input).  

   The Pico types the chat XSS (~87 lines, ~15 min); the Remote JS Loader comes up on the console (TCP :9026) for that session.  
2. **Install the persistent boot trigger** from the PC: `python installer-creator/install.py <console-ip>` — uploads the save template, splices the compressed payload into it on-console, wipes `save:/saves/Single/`, and writes the trigger.
3. From then on, every boot: main menu -> Load Game -> the payload runs from savedata (and if the payload is the loader, :9026 is up at boot — and survives console sleep/wake).  
4. You can revert system language after installer finished.  

## Sending payloads (`send_js.py`)

```
python send_js.py <console-ip> [port] file.js
```

Sends a JS file as-is to the Remote JS Loader (default port 9026), then prints the log output the console streams back (read until close or timeout).  

## Build (Pico firmware)

On Ubuntu 24.04:

```
sudo apt install -y git build-essential cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib python3 nodejs npm
git clone --branch 2.3.1 --depth 1 --recurse-submodules --shallow-submodules https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
export PICO_SDK_PATH=$HOME/pico-sdk
cd PicoSwitch
npm install                    # terser for compress.js
node compress.js               # reads ../core.js -> payload.txt
cmake --preset pico2 && cmake --build --preset pico2    # RP2350 -> build-pico2/src/civ7-pico2.uf2
cmake --preset pico1 && cmake --build --preset pico1    # RP2040 -> build-pico1/src/civ7-pico1.uf2
```

## Credits

- The JS exploit core in `core.js` referenced [inject.js](https://github.com/NetflixNHack/Netflix-N-Hack/blob/main/inject.js) from [Netflix-N-Hack](https://github.com/NetflixNHack/Netflix-N-Hack) and its fork by [ufm42](https://github.com/ufm42/Netflix-N-Hack/blob/main/inject.js) 
- The PicoSwitch firmware is based on [Pico-Switch-Controller](https://github.com/Element2424/Pico-Switch-Controller) by [Element2424](https://github.com/Element2424)

## Fuentes y atribuciones

- **CivSwitch original**, de Gezine: [repositorio](https://github.com/Gezine/CivSwitch). Este proyecto parte de ese trabajo; `core.js`, la cadena de payloads y el firmware PicoSwitch proceden del proyecto original o se adaptan de él. El repositorio original publica una licencia MIT; se conserva su aviso en [`LICENSE`](LICENSE).
- **Netflix-N-Hack**, de NetflixNHack, y la versión de **ufm42**: referencias técnicas citadas en `core.js` y en el README original de CivSwitch. El repositorio de Netflix-N-Hack indica GPL-3.0; consulta sus avisos y condiciones en [NetflixNHack](https://github.com/NetflixNHack/Netflix-N-Hack) y [ufm42](https://github.com/ufm42/Netflix-N-Hack). La atribución no sustituye ni amplía la licencia de esos proyectos.
- **Pico-Switch-Controller**, de Element2424: referencia para la interfaz y los informes USB del mando, adaptados para el port de Flipper Zero. [Repositorio](https://github.com/Element2424/Pico-Switch-Controller). En la revisión del 3 de noviembre de 2026 no se identificó un archivo de licencia en su página; la atribución por sí sola no concede permiso para redistribuir material protegido de ese proyecto.
- **Firmware de Flipper Zero**, de Flipper Devices: el port externo usa sus API públicas de Furi/USB y se compila como FAP. [Código fuente](https://github.com/flipperdevices/flipperzero-firmware). Este proyecto no es oficial ni está afiliado a Flipper Devices.

La licencia del archivo raíz `LICENSE` se conserva del proyecto CivSwitch original. No debe interpretarse como una concesión de derechos sobre marcas, código o recursos de terceros que tengan avisos o condiciones propios. Antes de redistribuir componentes derivados de terceros, revisa sus licencias y conserva sus avisos.

## Descargo de responsabilidad

Este software se proporciona «tal cual», sin garantías de funcionamiento, seguridad, compatibilidad o idoneidad para un fin concreto, en los términos indicados en [`LICENSE`](LICENSE). En la medida permitida por la ley aplicable, los autores y colaboradores no responden por daños derivados de su uso.

Está destinado a investigación y pruebas en dispositivos, juegos y redes que sean tuyos o para los que tengas autorización expresa. Tú eres responsable de comprobar y cumplir la legislación aplicable, los derechos de terceros y las condiciones de los servicios que uses. Este aviso no es asesoramiento jurídico ni garantiza que un uso concreto esté permitido.

El FAP emite entradas USB HID en el dispositivo que tenga conectado y puede escribir en la aplicación que esté enfocada. Además, `installer-creator` modifica los datos de guardado y elimina el contenido de `save:/saves/Single/` antes de escribir el archivo de instalación. Haz copias de seguridad y verifica cuidadosamente el dispositivo y la pantalla antes de ejecutar esos pasos.

Civilization, Nintendo Switch, Flipper Zero y sus nombres y marcas pertenecen a sus respectivos titulares. Este proyecto no está patrocinado, aprobado ni afiliado a Nintendo, 2K, Firaxis Games o Flipper Devices.

El descargo no limita derechos que no puedan excluirse conforme a la legislación aplicable.

