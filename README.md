# Switch Macro

Switch Macro convierte el Flipper Zero en un mando USB para Nintendo Switch y permite retransmitir los controles de un mando conectado al PC por Bluetooth.

## Funciones

- **Live controller:** retransmite al Flipper Zero las entradas de un mando conectado al PC; la salida del Flipper hacia la consola se hace por USB.
- **Conexion automatica en PC:** busca y reconecta por Bluetooth al Flipper Zero cuando se abre Live controller.
- **Grabacion en el Flipper Zero:** graba macros mientras se usa Live controller, permite pausar y reanudar, y configura un limite de 10, 30, 45 o 60 minutos.
- **Biblioteca de macros:** reproduce macros sin el PC, repite o pone en bucle, y muestra el avance y los controles activos.
- **Edicion de macros:** permite revisar y modificar pasos, duracion, botones, cruceta y sticks.
- **Papelera y copias de seguridad:** conserva hasta 10 macros descartadas o borradas para previsualizarlas y restaurarlas, gestionando nombres duplicados.
- **Favoritos y recientes:** organiza y filtra la lista de macros.
- **Perfiles y calibracion:** configura perfiles de juego y calibra los sticks.
- **Simulator:** permite probar los botones y sticks desde el Flipper Zero.
- **Opciones del Flipper:** ingles y espanol, etiquetas de botones Switch/PS5/Xbox, pantalla encendida, sonido, modo Dev Fake USB y registro compacto de fallos.
- **Aplicacion de PC:** selecciona un mando, muestra el estado de conexion, admite conexion automatica y puede quedarse en la bandeja de Windows. No graba ni gestiona macros.
- **Idiomas de PC:** ingles, espanol, frances, aleman, italiano, chino y japones.

## Contenido

- `FlipperApp/`: codigo C, manifiesto, icono, recursos y script de compilacion del FAP.
- `PCRecorder/`: codigo Python e icono de la aplicacion de escritorio.
- `SwitchMacroPC.spec`: configuracion para compilar la aplicacion de PC como EXE.

El paquete contiene codigo fuente y recursos; no incluye el FAP ni el EXE compilados, carpetas de build, herramientas descargadas o versiones antiguas.

## Compilar el FAP

Instala uFBT y ejecuta en Windows PowerShell desde la raiz del proyecto:

```powershell
.\FlipperApp\build.ps1
```

El resultado se genera en `FlipperApp/dist/SwitchMacro.fap`.

## Compilar la aplicacion de PC

Con Python instalado, desde la raiz del proyecto:

```powershell
py -m pip install -r PCRecorder/requirements.txt
py -m PyInstaller --clean --noconfirm SwitchMacroPC.spec
```

El EXE generado aparecera en `dist/`.
