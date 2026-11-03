# CivSwitch Flipper

FAP para Flipper Zero que presenta un mando USB compatible con Switch, retransmite por BLE el mando conectado al PC y reproduce macros `.txt` guardadas en la microSD. Incluye el puente web local usado por el modo Live.

## Compilar el FAP

Requisitos: PowerShell y [uFBT](https://github.com/flipperdevices/flipperzero-ufbt) instalado y disponible en `PATH`, con el toolchain de Flipper Zero.

```powershell
cd FlipperApp
./build.ps1
```

El archivo se genera en `FlipperApp/dist/SwitchMacro.fap`. Copia ese FAP a `apps/USB/` en la microSD.

## Usar Live

En Windows, inicia `PCRecorder/start-local.cmd`, conecta el mando al PC, abre el FAP en la Flipper y selecciona **Live**. La página web permite conectar la Flipper por Bluetooth. No requiere dependencias de Node ni paquetes web.

La documentación de controles, grabación y macros está en `FlipperApp/README.md` y `PCRecorder/README.md`.