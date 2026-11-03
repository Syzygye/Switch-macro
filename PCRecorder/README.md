# Grabador y puente en directo para Switch Macro

Herramienta web local, sin dependencias, que lee un mando conectado al PC usando Gamepad API, retransmite sus estados por BLE a la Flipper y guarda la secuencia como `macros.txt`. El PC y el mando solo se necesitan durante la sesión; después la Flipper reproduce el archivo desde la microSD por sí sola.

## Uso en directo

1. Conecta el mando al PC por USB o Bluetooth.
2. Conecta la Flipper por USB al dock, ejecuta **Switch Macro** y entra en **Live**.
3. Ejecuta `start-local.cmd` en esta carpeta; abre la página en Chrome o Edge. Pulsa un botón del mando para que el navegador lo detecte y selecciónalo.
4. Pulsa **Conectar Flipper por Bluetooth** y elige la Flipper. Confirma el emparejamiento si lo solicita.
5. Al conectar la Flipper y el mando seleccionado, el relé se activa automáticamente. Si quieres guardar la sesión desde el PC, pulsa **Iniciar grabación**; detenerla no apaga el relé. También puedes grabar desde la propia Flipper mientras Live está activo: pulsa **OK** en su pantalla para iniciar/parar y guarda `live_NNNN.txt` en la microSD. Si detuviste el relé manualmente, pulsa **Reanudar relé**.

## Reproducir la grabación después

1. Si grabaste la sesión, descarga `macros.txt` y cópialo a `/ext/apps_data/switch_macro/` en la microSD.
2. Desconecta el PC y el mando.
3. Conecta la Flipper al dock, abre **Switch Macro → Macros**, usa **Arriba/Abajo** para elegir el archivo y pulsa **OK** para reproducirlo.

Una vez copiadas a la microSD, puedes elegir **Macros** desde el menú de la Flipper para reproducir, renombrar o borrar los archivos. **Live** está en una opción separada del menú inicial.

La página y el servidor HTTP se ejecutan en el PC; `start-local.cmd` requiere Python 3. El servidor escucha solo en `127.0.0.1`; no envía datos a internet. Web Bluetooth requiere el origen local seguro preparado por `start-local.cmd`. Mantén la pestaña visible mientras el relé o la grabación estén activos. La primera conexión puede pedir confirmación de emparejamiento en la Flipper y el PC.

El navegador debe exponer el mando con el mapeo estándar para identificar sus botones por posición; si indica otro mapeo, los nombres pueden variar. Se capturan botones, cruceta y sticks analógicos, con zona muerta y cuantización de ejes.

La cruceta usa el mapa HID estándar, con izquierda=`0x06` y derecha=`0x02`.

El archivo conserva hasta 512 estados y 24 KB, con un máximo de 60000 ms por intervalo. Si se pierde el enlace BLE, la Flipper suelta los controles tras un breve tiempo sin paquetes.
