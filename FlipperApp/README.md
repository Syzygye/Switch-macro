# Switch Macro para Flipper Zero

FAP nativo que presenta la Flipper como mando USB compatible con Switch. Puede retransmitir por USB los estados de un mando conectado al PC por BLE o reproducir macros desde la microSD. La propia Flipper permite elegir, renombrar y borrar esos archivos; para reproducir una macro guardada no hace falta el PC ni el mando usado para grabar.

## Relé en directo y grabación desde la Flipper o el PC

En Windows, abre `../PCRecorder/start-local.cmd`, conecta el mando al PC por USB o Bluetooth y selecciónalo en Chrome o Edge. Conecta la Flipper por USB al dock y elige **Live** en el menú del FAP: el modo BLE queda activo. En el navegador, conecta la Flipper por Bluetooth; al detectar la conexión y el mando seleccionado, el relé se enciende automáticamente.

También puedes grabar desde la Flipper mientras el relé sigue activo: pulsa **OK** en la pantalla **Live** para empezar y vuelve a pulsarlo para guardar. **Atrás** detiene Live y guarda automáticamente si estabas grabando. La macro se guarda en la microSD como `live_NNNN.txt` y se selecciona al entrar después en **Macros**. Mientras juegas, los estados se acumulan en RAM; la microSD solo se escribe al guardar. La grabación desde el PC sigue siendo independiente y no controla el botón **OK** de la Flipper. La pantalla muestra los contadores BLE/USB, los paquetes descartados y el estado de grabación; los errores de transmisión muestran el endpoint.

En la distribución estándar del Gamepad API, los botones de cara se traducen por posición al diseño de Switch: abajo→B, derecha→A, izquierda→Y y arriba→X. Si el navegador indica que el mando no tiene mapeo estándar, los nombres pueden variar. La herramienta también captura cruceta y sticks; aplica zona muerta y cuantiza ejes para evitar ruido.

La cruceta usa el mapa HID estándar: izquierda→`0x06`, derecha→`0x02`, arriba-izquierda→`0x07` y arriba-derecha→`0x01` (abajo-izquierda→`0x05`, abajo-derecha→`0x03`).

Copia los archivos `.txt` de macro a esta carpeta de la microSD. Después podrás administrarlos y reproducirlos desde la pantalla de la Flipper:

```text
/ext/apps_data/switch_macro/
```

En la pantalla inicial, **Arriba/Abajo** elige entre **Macros** y **Live**; **OK** entra en el modo seleccionado. Al entrar en **Macros**, **Arriba/Abajo** elige el archivo, **OK** lo reproduce y **Derecha** abre las opciones de reproducir, renombrar o borrar; el borrado pide confirmación. **Atrás** vuelve al menú anterior. El gestor muestra hasta 24 archivos `.txt` de la carpeta.

Para renombrar, **Arriba/Abajo** cambia el carácter, **Derecha** lo aplica y avanza, **Izquierda** vuelve al carácter anterior, **OK** guarda y **Atrás** cancela. La opción `END` trunca el nombre desde la posición actual. La extensión `.txt` se conserva.

El FAP solo lista archivos que ya están en la microSD. Para transferir una macro desde otro dispositivo o instalar el FAP, todavía hay que copiar el archivo a la microSD por un medio externo.

## Formato

El formato admite líneas de pulsación sencilla y estados completos:

```text
# Pulsación sencilla: se suelta al terminar
A 120
WAIT 250
A+B 100
UP 300

# Estado persistente: el siguiente STATE cambia el mando sin soltarlo entre medias
STATE A+LX:224+LY:128+RX:128+RY:128 150
STATE A+B+UP+LX:224+LY:0+RX:128+RY:128 200
STATE NONE 80
```

`STATE` conserva los botones, la cruceta y los ejes durante el intervalo indicado; la siguiente línea cambia al nuevo estado. `NONE` significa botones y cruceta sueltos. Los ejes admiten valores de 0 a 255; 128 es el centro. `WAIT` pone el mando en neutral durante su intervalo. `#` inicia un comentario.

Botones: `A`, `B`, `X`, `Y`, `L`, `R`, `ZL`, `ZR`, `PLUS`, `MINUS`, `L3`, `R3`, `HOME` y `CAPTURE`. Direcciones: `UP`, `DOWN`, `LEFT`, `RIGHT`, `UP_LEFT`, `UP_RIGHT`, `DOWN_LEFT` y `DOWN_RIGHT` (también se aceptan sin guion bajo). Usa `+` para mantener varios controles juntos. Ejes: `LX`, `LY`, `RX`, `RY`.

Cada duración debe estar entre 1 y 60000 ms. El archivo admite hasta 24 KB y 512 estados. Tras editarlo, cierra y vuelve a abrir el FAP para cargar la versión nueva.

## Reproducción independiente

1. Copia `dist/SwitchMacro.fap` a `apps/USB/` en la microSD.
2. Conecta la Flipper al dock de la Switch por USB y abre **Apps → USB → Switch Macro**.
3. En el menú inicial entra en **Macros**, elige un `.txt` con **Arriba/Abajo** y pulsa **OK**. **Derecha** abre las opciones de renombrar y borrar. Para relé en directo, vuelve al menú y entra en **Live**. **OK** inicia/detiene la grabación en la Flipper; **Atrás** detiene Live, guarda una grabación activa y libera botones, cruceta y sticks.

El relé usa el perfil BLE Serial del firmware y un paquete de estado compacto; el navegador necesita ejecutarse desde `127.0.0.1` para usar Web Bluetooth. Si se pierde el enlace BLE, el FAP suelta los controles al dejar de recibir paquetes. El FAP conserva las dos interfaces HID del port anterior (mando Switch y teclado boot), pero solo genera informes del mando. La consola debe aceptar entradas de mando y estar conectada por el dock en modo TV.

## Compilar

Instala uFBT y ejecuta desde esta carpeta:

```powershell
./build.ps1
```

El resultado se copia a `dist/SwitchMacro.fap`. El FAP debe validarse en la Flipper y la Switch concretas.
