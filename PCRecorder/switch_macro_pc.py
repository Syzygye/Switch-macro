"""Minimal Windows desktop relay from an SDL gamepad to Switch Macro over BLE."""

from __future__ import annotations

import asyncio
import ctypes
from ctypes import wintypes
import json
import math
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import ttk

# SDL reads this hint during initialization. Set it before importing pygame so
# controller input continues while the desktop window is in the background.
os.environ["SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"] = "1"

import pygame
from bleak import BleakClient, BleakScanner


SERIAL_SERVICE_UUID = "8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000"
DESKTOP_VERSION = "0.9.13"
LANGUAGE_NAMES = {
    "en": "English",
    "es": "Español",
    "fr": "Français",
    "de": "Deutsch",
    "it": "Italiano",
    "zh": "中文",
    "ja": "日本語",
}
LANGUAGE_ORDER = tuple(LANGUAGE_NAMES)
TRANSLATED_LANGUAGE_ORDER = ("en", "fr", "de", "it", "zh", "ja")

# The Spanish phrases are stable translation keys. Device names and diagnostic
# details remain untouched, while all app-owned copy follows the selected UI.
TRANSLATIONS = {
    "Mando de PC": ("PC Controller", "Manette PC", "PC-Controller", "Controller PC", "电脑手柄", "PCコントローラー"),
    "Pon Switch Macro en Live controller en el Flipper Zero.": ("Open Live controller in Switch Macro on the Flipper Zero.", "Ouvrez Live controller dans Switch Macro sur le Flipper Zero.", "Öffne Live controller in Switch Macro auf dem Flipper Zero.", "Apri Live controller in Switch Macro sul Flipper Zero.", "请在 Flipper Zero 的 Switch Macro 中打开 Live controller。", "Flipper ZeroのSwitch MacroでLive controllerを開いてください。"),
    "On": ("ON", "Activé", "Ein", "Attivo", "开", "オン"),
    "Off": ("OFF", "Désactivé", "Aus", "Disattivo", "关", "オフ"),
    "Conexión automática": ("Automatic connection", "Connexion automatique", "Automatische Verbindung", "Connessione automatica", "自动连接", "自動接続"),
    "Busca y reconecta al entrar en Live controller.": ("Finds and reconnects when you open Live controller.", "Recherche et reconnecte à l’ouverture de Live controller.", "Sucht und verbindet erneut, wenn Live controller geöffnet wird.", "Cerca e si riconnette quando apri Live controller.", "进入 Live controller 时搜索并重新连接。", "Live controllerを開くと検索して再接続します。"),
    "Conexión automática activa; esperando al Flipper Zero en Live controller…": ("Auto-connect is on; waiting for the Flipper Zero in Live controller…", "Connexion auto activée ; attente du Flipper Zero dans Live controller…", "Automatische Verbindung aktiv; warte in Live controller auf den Flipper Zero…", "Connessione automatica attiva; in attesa del Flipper Zero in Live controller…", "自动连接已开启；正在 Live controller 中等待 Flipper Zero…", "自動接続中：Live controllerでFlipper Zeroを待機しています…"),
    "Mando seleccionado. Puedes activar la conexión automática.": ("Controller selected. You can turn on auto-connect.", "Manette sélectionnée. Vous pouvez activer la connexion auto.", "Controller ausgewählt. Du kannst die automatische Verbindung einschalten.", "Controller selezionato. Puoi attivare la connessione automatica.", "已选择手柄。你可以开启自动连接。", "コントローラーを選択しました。自動接続を有効にできます。"),
    "MANDO DE PC  ·  BLUETOOTH": ("PC CONTROLLER  ·  BLUETOOTH", "MANETTE PC  ·  BLUETOOTH", "PC-CONTROLLER  ·  BLUETOOTH", "CONTROLLER PC  ·  BLUETOOTH", "电脑手柄  ·  蓝牙", "PCコントローラー  ·  Bluetooth"),
    "Tu mando, conectado al Flipper Zero. Sin pasos de más.": ("Your controller, connected to Flipper Zero. Nothing extra.", "Votre manette, connectée au Flipper Zero. Sans étape inutile.", "Dein Controller, verbunden mit dem Flipper Zero. Ohne Umwege.", "Il tuo controller, connesso al Flipper Zero. Senza passaggi extra.", "将手柄连接到 Flipper Zero，简单直接。", "コントローラーをFlipper Zeroに接続。余計な手順はありません。"),
    "01   /   MANDO DEL PC": ("01   /   PC CONTROLLER", "01   /   MANETTE PC", "01   /   PC-CONTROLLER", "01   /   CONTROLLER PC", "01   /   电脑手柄", "01   /   PCコントローラー"),
    "02   /   FLIPPER ZERO": ("02   /   FLIPPER ZERO", "02   /   FLIPPER ZERO", "02   /   FLIPPER ZERO", "02   /   FLIPPER ZERO", "02   /   FLIPPER ZERO", "02   /   FLIPPER ZERO"),
    "Actualizar": ("Refresh", "Actualiser", "Aktualisieren", "Aggiorna", "刷新", "更新"),
    "Buscar": ("Search", "Rechercher", "Suchen", "Cerca", "搜索", "検索"),
    "Mostrar icono en bandeja y ocultar al cerrar": ("Show tray icon and hide on close", "Afficher l’icône de zone et masquer à la fermeture", "Infobereichsymbol anzeigen und beim Schließen ausblenden", "Mostra l’icona nell’area di notifica e nascondi alla chiusura", "显示托盘图标并在关闭时隐藏", "通知領域に表示し、閉じると非表示"),
    "ESTADO DEL ENLACE": ("LINK STATUS", "ÉTAT DE LA CONNEXION", "VERBINDUNGSSTATUS", "STATO DEL COLLEGAMENTO", "连接状态", "接続状態"),
    "La ventana puede quedarse en segundo plano mientras juegas.": ("The window can stay in the background while you play.", "La fenêtre peut rester en arrière-plan pendant que vous jouez.", "Das Fenster kann während des Spielens im Hintergrund bleiben.", "La finestra può restare in secondo piano mentre giochi.", "游戏时窗口可以留在后台。", "プレイ中はウィンドウをバックグラウンドにできます。"),
    "Mando detectado, pero SDL no reconoce su mapeo. Prueba un mando Xbox, PlayStation o Switch Pro.": ("Controller found, but SDL has no mapping for it. Try an Xbox, PlayStation, or Switch Pro controller.", "Manette détectée, mais SDL ne reconnaît pas sa configuration. Essayez une manette Xbox, PlayStation ou Switch Pro.", "Controller erkannt, aber SDL kennt keine Tastenbelegung. Probiere Xbox-, PlayStation- oder Switch-Pro-Controller.", "Controller rilevato, ma SDL non ne riconosce la mappatura. Prova un controller Xbox, PlayStation o Switch Pro.", "已检测到手柄，但 SDL 不识别其映射。请尝试 Xbox、PlayStation 或 Switch Pro 手柄。", "コントローラーを検出しましたがSDLの割り当てがありません。Xbox、PlayStation、Switch Proをお試しください。"),
    "Abrir Switch Macro": ("Open Switch Macro", "Ouvrir Switch Macro", "Switch Macro öffnen", "Apri Switch Macro", "打开 Switch Macro", "Switch Macroを開く"),
    "Salir": ("Exit", "Quitter", "Beenden", "Esci", "退出", "終了"),
    "On": ("ON", "Activé", "Ein", "Attivo", "开", "オン"),
    "Off": ("OFF", "Désactivé", "Aus", "Disattivo", "关", "オフ"),
    "Entrada del mando: esperando": ("Controller input: waiting", "Entrée manette : en attente", "Controller-Eingabe: wartet", "Ingresso controller: in attesa", "手柄输入：等待中", "コントローラー入力：待機中"),
    "Entrada del mando: activa": ("Controller input: active", "Entrée manette : active", "Controller-Eingabe: aktiv", "Ingresso controller: attivo", "手柄输入：活动", "コントローラー入力：入力中"),
    "Bluetooth: sin tramas": ("Bluetooth: no frames", "Bluetooth : aucune trame", "Bluetooth: keine Datenpakete", "Bluetooth: nessun frame", "蓝牙：无数据帧", "Bluetooth：フレームなし"),
    "Bluetooth: conectado · esperando tramas": ("Bluetooth: connected · waiting for frames", "Bluetooth : connecté · attente des trames", "Bluetooth: verbunden · warte auf Datenpakete", "Bluetooth: connesso · in attesa dei frame", "蓝牙：已连接 · 等待数据帧", "Bluetooth：接続済み · フレーム待機中"),
    "Bluetooth: desconectado · buscando el Flipper Zero": ("Bluetooth: disconnected · searching for Flipper Zero", "Bluetooth : déconnecté · recherche du Flipper Zero", "Bluetooth: getrennt · suche Flipper Zero", "Bluetooth: disconnesso · ricerca del Flipper Zero", "蓝牙：已断开 · 正在搜索 Flipper Zero", "Bluetooth：切断 · Flipper Zeroを検索中"),
    "Bluetooth: sin conexión · reintentando": ("Bluetooth: disconnected · retrying", "Bluetooth : déconnecté · nouvelle tentative", "Bluetooth: getrennt · neuer Versuch", "Bluetooth: disconnesso · nuovo tentativo", "蓝牙：未连接 · 正在重试", "Bluetooth：未接続 · 再試行中"),
    "Bluetooth: buscando el Flipper Zero…": ("Bluetooth: searching for Flipper Zero…", "Bluetooth : recherche du Flipper Zero…", "Bluetooth: suche Flipper Zero…", "Bluetooth: ricerca del Flipper Zero…", "蓝牙：正在搜索 Flipper Zero…", "Bluetooth：Flipper Zeroを検索中…"),
    "Bluetooth": ("Bluetooth", "Bluetooth", "Bluetooth", "Bluetooth", "蓝牙", "Bluetooth"),
    "tramas enviadas": ("frames sent", "trames envoyées", "Datenpakete gesendet", "frame inviati", "已发送数据帧", "フレームを送信"),
    "mando activo": ("controller active", "manette active", "Controller aktiv", "controller attivo", "手柄活动", "コントローラー入力中"),
    "mando neutro": ("controller idle", "manette au repos", "Controller neutral", "controller inattivo", "手柄空闲", "コントローラー待機中"),
    "Bluetooth: emparejamiento pendiente en Windows": ("Bluetooth: Windows pairing required", "Bluetooth : jumelage Windows requis", "Bluetooth: Windows-Kopplung erforderlich", "Bluetooth: abbinamento Windows richiesto", "蓝牙：需要在 Windows 中配对", "Bluetooth：Windowsでのペアリングが必要です"),
    "Entrada del mando: error": ("Controller input: error", "Entrée manette : erreur", "Controller-Eingabe: Fehler", "Ingresso controller: errore", "手柄输入：错误", "コントローラー入力：エラー"),
    "Buscando dispositivos Bluetooth…": ("Searching for Bluetooth devices…", "Recherche des appareils Bluetooth…", "Suche nach Bluetooth-Geräten…", "Ricerca dei dispositivi Bluetooth…", "正在搜索蓝牙设备…", "Bluetoothデバイスを検索中…"),
    "Conectando y emparejando con ": ("Connecting and pairing with ", "Connexion et jumelage avec ", "Verbindung und Kopplung mit ", "Connessione e abbinamento con ", "正在连接并配对：", "接続してペアリング中："),
    "Conectando de nuevo con todos los servicios GATT…": ("Retrying with all GATT services…", "Nouvelle tentative avec tous les services GATT…", "Neuer Versuch mit allen GATT-Diensten…", "Nuovo tentativo con tutti i servizi GATT…", "正在尝试全部 GATT 服务…", "すべてのGATTサービスで再試行中…"),
    "Conectando…": ("Connecting…", "Connexion…", "Verbindung wird hergestellt…", "Connessione…", "正在连接…", "接続中…"),
    "Conectando " : ("Connecting ", "Connexion à ", "Verbinde mit ", "Connessione a ", "正在连接：", "接続中："),
    "Se reconectará al volver a Live controller.": ("It will reconnect when Live controller is opened again.", "La reconnexion se fera à la réouverture de Live controller.", "Die Verbindung wird beim erneuten Öffnen von Live controller wiederhergestellt.", "Si riconnetterà quando riaprirai Live controller.", "再次打开 Live controller 时会重新连接。", "Live controllerを再度開くと再接続します。"),
    "Buscando el Flipper Zero para conectar en Live controller…": ("Searching for Flipper Zero to connect in Live controller…", "Recherche du Flipper Zero pour se connecter dans Live controller…", "Suche Flipper Zero zur Verbindung in Live controller…", "Ricerca del Flipper Zero per connettersi in Live controller…", "正在搜索 Flipper Zero，以连接 Live controller…", "Live controllerに接続するFlipper Zeroを検索中…"),
    "Encontré el Flipper Zero (": ("Found Flipper Zero (", "Flipper Zero trouvé (", "Flipper Zero gefunden (", "Flipper Zero trovato (", "已找到 Flipper Zero（", "Flipper Zeroが見つかりました（"),
    "); conectando…": ("); connecting…", "); connexion…", "); wird verbunden…", "); connessione…", "）；正在连接…", "）；接続中…"),
    "Bluetooth conectado a ": ("Bluetooth connected to ", "Bluetooth connecté à ", "Bluetooth verbunden mit ", "Bluetooth connesso a ", "蓝牙已连接到", "Bluetooth接続先："),
    "Bluetooth conectado a": ("Bluetooth connected to", "Bluetooth connecté à", "Bluetooth verbunden mit", "Bluetooth connesso a", "蓝牙已连接到", "Bluetooth接続先"),
    "Conexión automática detenida.": ("Auto-connect stopped.", "Connexion auto arrêtée.", "Automatische Verbindung gestoppt.", "Connessione automatica arrestata.", "自动连接已停止。", "自動接続を停止しました。"),
    "Desconectando Bluetooth…": ("Disconnecting Bluetooth…", "Déconnexion du Bluetooth…", "Bluetooth wird getrennt…", "Disconnessione Bluetooth…", "正在断开蓝牙…", "Bluetoothを切断中…"),
    "Bluetooth ya está conectado. La reconexión automática queda activa.": ("Bluetooth is already connected. Auto-reconnect remains on.", "Bluetooth est déjà connecté. La reconnexion auto reste active.", "Bluetooth ist bereits verbunden. Automatische Wiederverbindung bleibt aktiv.", "Bluetooth è già connesso. La riconnessione automatica resta attiva.", "蓝牙已连接。自动重连仍开启。", "Bluetoothはすでに接続済みです。自動再接続は有効です。"),
    "Selecciona un mando para activar la conexión automática.": ("Select a controller to enable auto-connect.", "Sélectionnez une manette pour activer la connexion auto.", "Wähle einen Controller, um die automatische Verbindung einzuschalten.", "Seleziona un controller per attivare la connessione automatica.", "选择一个手柄以启用自动连接。", "自動接続を有効にするにはコントローラーを選択してください。"),
    "Selecciona un mando; después buscaré el Flipper Zero automáticamente.": ("Select a controller; then I’ll find the Flipper Zero automatically.", "Sélectionnez une manette ; je rechercherai ensuite le Flipper Zero.", "Wähle einen Controller; danach suche ich automatisch den Flipper Zero.", "Seleziona un controller; poi cercherò automaticamente il Flipper Zero.", "选择手柄后，我会自动搜索 Flipper Zero。", "コントローラーを選ぶと、Flipper Zeroを自動検索します。"),
    "Conecta un mando al PC y pulsa Actualizar.": ("Connect a controller to the PC and press Refresh.", "Connectez une manette au PC et appuyez sur Actualiser.", "Schließe einen Controller an den PC an und klicke auf Aktualisieren.", "Collega un controller al PC e premi Aggiorna.", "将手柄连接到电脑并点击刷新。", "コントローラーをPCに接続して更新を押してください。"),
    "Mando detectado, pero SDL no reconoce su mapeo. Prueba un mando Xbox, PlayStation o Switch Pro.": ("Controller found, but SDL has no mapping for it. Try an Xbox, PlayStation, or Switch Pro controller.", "Manette détectée, mais SDL ne reconnaît pas sa configuration. Essayez une manette Xbox, PlayStation ou Switch Pro.", "Controller erkannt, aber SDL kennt keine Tastenbelegung. Probiere Xbox-, PlayStation- oder Switch-Pro-Controller.", "Controller rilevato, ma SDL non ne riconosce la mappatura. Prova un controller Xbox, PlayStation o Switch Pro.", "已检测到手柄，但 SDL 不识别其映射。请尝试 Xbox、PlayStation 或 Switch Pro 手柄。", "コントローラーを検出しましたがSDLの割り当てがありません。Xbox、PlayStation、Switch Proをお試しください。"),
    "Encontré el Flipper Zero. Activa la conexión automática cuando quieras.": ("Flipper Zero found. Turn on auto-connect when ready.", "Flipper Zero trouvé. Activez la connexion automatique quand vous le souhaitez.", "Flipper Zero gefunden. Aktiviere die automatische Verbindung, wenn du bereit bist.", "Flipper Zero trovato. Attiva la connessione automatica quando vuoi.", "已找到 Flipper Zero。准备好后开启自动连接。", "Flipper Zeroが見つかりました。必要なときに自動接続を有効にしてください。"),
    "Se detectó el perfil serie de Switch Macro; selecciónalo para conectar.": ("Switch Macro serial profile found; select it to connect.", "Profil série Switch Macro détecté ; sélectionnez-le pour vous connecter.", "Switch-Macro-Serienprofil gefunden; zum Verbinden auswählen.", "Profilo seriale Switch Macro rilevato; selezionalo per connetterti.", "已检测到 Switch Macro 串口配置文件；选择它以连接。", "Switch Macroのシリアルプロファイルが見つかりました。選択して接続してください。"),
    "Esperando el anuncio del perfil serie de Switch Macro en Live controller; volveré a buscar…": ("Waiting for the Switch Macro serial profile in Live controller; I’ll search again…", "En attente du profil série Switch Macro dans Live controller ; nouvelle recherche…", "Warte auf das Switch-Macro-Serienprofil in Live controller; suche erneut…", "In attesa del profilo seriale Switch Macro in Live controller; cercherò di nuovo…", "正在等待 Live controller 中的 Switch Macro 串口配置文件；将重新搜索…", "Live controllerのSwitch Macroシリアルプロファイルを待機中。再検索します…"),
    "No se encontró el perfil serie. Pon el Flipper Zero en Live controller y vuelve a buscar.": ("Serial profile not found. Open Live controller on the Flipper Zero and search again.", "Profil série introuvable. Ouvrez Live controller sur le Flipper Zero et relancez la recherche.", "Serienprofil nicht gefunden. Öffne Live controller auf dem Flipper Zero und suche erneut.", "Profilo seriale non trovato. Apri Live controller sul Flipper Zero e cerca di nuovo.", "未找到串口配置文件。请在 Flipper Zero 上打开 Live controller 后重新搜索。", "シリアルプロファイルが見つかりません。Flipper ZeroでLive controllerを開いて再検索してください。"),
    "Conexión automática detenida.": ("Auto-connect stopped.", "Connexion auto arrêtée.", "Automatische Verbindung gestoppt.", "Connessione automatica arrestata.", "自动连接已停止。", "自動接続を停止しました。"),
    "El Flipper Zero se desconectó; lo buscaré y reconectaré cuando vuelva a Live controller…": ("Flipper Zero disconnected; I’ll find and reconnect when Live controller is opened again…", "Flipper Zero déconnecté ; je le rechercherai et me reconnecterai à la réouverture de Live controller…", "Flipper Zero getrennt; ich suche erneut und verbinde, sobald Live controller wieder geöffnet wird…", "Flipper Zero disconnesso; lo cercherò e mi riconnetterò quando riaprirai Live controller…", "Flipper Zero 已断开；再次打开 Live controller 时我会搜索并重连…", "Flipper Zeroが切断されました。Live controllerを再度開くと検索して再接続します…"),
    "Windows no pudo emparejar. En Configuración > Bluetooth y dispositivos, ": ("Windows could not pair. In Settings > Bluetooth & devices, ", "Windows n’a pas pu effectuer le jumelage. Dans Paramètres > Bluetooth et appareils, ", "Windows konnte nicht koppeln. Öffne Einstellungen > Bluetooth und Geräte, ", "Windows non è riuscito ad abbinare. In Impostazioni > Bluetooth e dispositivi, ", "Windows 配对失败。请在“设置 > 蓝牙和设备”中", "Windowsでペアリングできませんでした。「設定 > Bluetoothとデバイス」で"),
    "empareja el Flipper Zero y luego pulsa Buscar.": ("pair Flipper Zero, then press Search.", "associez le Flipper Zero, puis appuyez sur Rechercher.", "kopple den Flipper Zero und klicke danach auf Suchen.", "abbina il Flipper Zero e poi premi Cerca.", "配对 Flipper Zero，然后点击搜索。", "Flipper Zeroをペアリングしてから検索を押してください。"),
    "Encontré el Flipper Zero, pero no ofrece el canal de mando: ": ("Flipper Zero found, but it does not offer the controller channel: ", "Flipper Zero trouvé, mais le canal de commande est absent : ", "Flipper Zero gefunden, aber der Controller-Kanal fehlt: ", "Flipper Zero trovato, ma manca il canale controller: ", "已找到 Flipper Zero，但未提供手柄通道：", "Flipper Zeroが見つかりましたが、コントローラーチャンネルがありません："),
    "aún no disponible": ("not available yet", "pas encore disponible", "noch nicht verfügbar", "non ancora disponibile", "暂不可用", "まだ利用できません"),
    "Reintentaré en Live controller…": ("I’ll retry in Live controller…", "Je réessaierai dans Live controller…", "Ich versuche es in Live controller erneut…", "Riproverò in Live controller…", "将在 Live controller 中重试…", "Live controllerで再試行します…"),
    "No se pudo guardar la configuración en AppData: ": ("Could not save settings in AppData: ", "Impossible d’enregistrer les paramètres dans AppData : ", "Einstellungen konnten nicht in AppData gespeichert werden: ", "Impossibile salvare le impostazioni in AppData: ", "无法将设置保存到 AppData：", "AppDataに設定を保存できませんでした："),
    "No se pudieron leer los mandos: ": ("Could not read controllers: ", "Impossible de lire les manettes : ", "Controller konnten nicht gelesen werden: ", "Impossibile leggere i controller: ", "无法读取手柄：", "コントローラーを読み取れませんでした："),
    "No se pudo activar el icono de bandeja: ": ("Could not enable the tray icon: ", "Impossible d’activer l’icône de zone : ", "Infobereichsymbol konnte nicht aktiviert werden: ", "Impossibile attivare l’icona nell’area di notifica: ", "无法启用托盘图标：", "通知領域アイコンを有効にできませんでした："),
    "No se pudo conectar: ": ("Could not connect: ", "Connexion impossible : ", "Verbindung fehlgeschlagen: ", "Connessione non riuscita: ", "连接失败：", "接続できませんでした："),
    "Error al buscar Bluetooth: ": ("Bluetooth search error: ", "Erreur de recherche Bluetooth : ", "Bluetooth-Suchfehler: ", "Errore di ricerca Bluetooth: ", "蓝牙搜索错误：", "Bluetooth検索エラー："),
    "Se perdió la conexión BLE: ": ("BLE connection lost: ", "Connexion BLE perdue : ", "BLE-Verbindung verloren: ", "Connessione BLE persa: ", "BLE 连接已丢失：", "BLE接続が失われました："),
    "; acepta el aviso de Windows si aparece…": ("; accept the Windows prompt if it appears…", "; acceptez la demande Windows si elle apparaît…", "; bestätige die Windows-Abfrage, falls sie erscheint…", "; accetta la richiesta di Windows se appare…", "；如果出现 Windows 提示，请确认…", "；Windowsの確認が表示されたら許可してください…"),
    " · reintentando en Live controller…": (" · retrying in Live controller…", " · nouvelle tentative dans Live controller…", " · neuer Versuch in Live controller…", " · nuovo tentativo in Live controller…", " · 将在 Live controller 中重试…", " · Live controllerで再試行中…"),
    " · volveré a buscar automáticamente…": (" · I’ll search again automatically…", " · nouvelle recherche automatique…", " · ich suche automatisch erneut…", " · cercherò di nuovo automaticamente…", " · 将自动重新搜索…", " · 自動で再検索します…"),
}


def translated_text(language: str, text: str) -> str:
    if language == "es":
        if text == "On":
            return "Sí"
        if text == "Off":
            return "No"
        return text
    forms = TRANSLATIONS.get(text)
    if forms:
        return forms[TRANSLATED_LANGUAGE_ORDER.index(language)]
    for source in sorted(TRANSLATIONS, key=len, reverse=True):
        if text.startswith(source):
            forms = TRANSLATIONS[source]
            return forms[TRANSLATED_LANGUAGE_ORDER.index(language)] + translated_text(language, text[len(source):])
    return text


class LocalizedStringVar(tk.StringVar):
    def __init__(self, app, value: str):
        self.app = app
        self.source_text = value
        super().__init__(master=app.root, value=app.tr(value))

    def set(self, value):
        self.source_text = str(value)
        super().set(self.app.tr(self.source_text))

    def refresh_language(self):
        super().set(self.app.tr(self.source_text))
SERIAL_RX_UUID = "19ed82ae-ed21-4c9d-4145-228e62fe0000"
# Flipper's serial profile advertises a 16-bit product UUID, with the two
# least-significant bits identifying the device colour. The custom GATT
# service itself is not the UUID used in the advertisement.
SERIAL_ADVERTISEMENT_UUIDS = {
    f"0000308{color}-0000-1000-8000-00805f9b34fb" for color in range(4)
}
FRAME_MAGIC = 0xC7
FRAME_INTERVAL_SECONDS = 0.02
FRAME_HEARTBEAT_SECONDS = 0.1
AUTO_SCAN_RETRY_MS = 2500
TRAY_CALLBACK_MESSAGE = 0x8000 + 37
TRAY_WINDOW_CLASS = "SwitchMacroDesktopTrayWindow"


def preferences_path() -> str:
    local_app_data = os.environ.get("LOCALAPPDATA") or os.path.join(
        os.path.expanduser("~"), "AppData", "Local"
    )
    return os.path.join(local_app_data, "SwitchMacro", "settings.json")


def load_preferences() -> dict:
    try:
        with open(preferences_path(), "r", encoding="utf-8") as settings_file:
            value = json.load(settings_file)
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError, TypeError):
        return {}


def save_preferences(value: dict) -> None:
    path = preferences_path()
    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    temporary_path = path + ".tmp"
    with open(temporary_path, "w", encoding="utf-8") as settings_file:
        json.dump(value, settings_file, ensure_ascii=False, indent=2)
        settings_file.write("\n")
    os.replace(temporary_path, path)


class WindowsTrayIcon:
    """Small Shell_NotifyIcon wrapper; keeps the EXE dependency-free."""

    WM_CLOSE = 0x0010
    WM_DESTROY = 0x0002
    WM_LBUTTONUP = 0x0202
    WM_LBUTTONDBLCLK = 0x0203
    WM_RBUTTONUP = 0x0205
    NIM_ADD = 0x00000000
    NIM_DELETE = 0x00000002
    NIM_SETVERSION = 0x00000004
    NIF_MESSAGE = 0x00000001
    NIF_ICON = 0x00000002
    NIF_TIP = 0x00000004
    NIF_SHOWTIP = 0x00000080
    NOTIFYICON_VERSION_4 = 4
    IMAGE_ICON = 1
    LR_LOADFROMFILE = 0x0010
    LR_DEFAULTSIZE = 0x0040
    HWND_MESSAGE = ctypes.c_void_p(-3).value

    def __init__(self, icon_path: str):
        self.events: queue.SimpleQueue = queue.SimpleQueue()
        self._icon_path = os.path.abspath(icon_path)
        self._ready = threading.Event()
        self._thread = threading.Thread(target=self._run, name="SwitchMacroTray", daemon=True)
        self._startup_error = None
        self._hwnd = None
        self._icon = None
        self._data = None
        self._thread.start()
        if not self._ready.wait(5.0):
            self.close()
            raise RuntimeError("Windows no respondió al crear el icono de bandeja.")
        if self._startup_error:
            raise RuntimeError(f"No se pudo crear el icono de bandeja: {self._startup_error}")

    def _run(self):
        user32 = ctypes.WinDLL("user32", use_last_error=True)
        shell32 = ctypes.WinDLL("shell32", use_last_error=True)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        wnd_proc_type = ctypes.WINFUNCTYPE(
            ctypes.c_ssize_t,
            wintypes.HWND,
            wintypes.UINT,
            ctypes.c_size_t,
            ctypes.c_ssize_t,
        )

        class Guid(ctypes.Structure):
            _fields_ = [
                ("Data1", wintypes.DWORD),
                ("Data2", wintypes.WORD),
                ("Data3", wintypes.WORD),
                ("Data4", wintypes.BYTE * 8),
            ]

        class NotifyIconData(ctypes.Structure):
            _fields_ = [
                ("cbSize", wintypes.DWORD),
                ("hWnd", wintypes.HWND),
                ("uID", wintypes.UINT),
                ("uFlags", wintypes.UINT),
                ("uCallbackMessage", wintypes.UINT),
                ("hIcon", wintypes.HANDLE),
                ("szTip", wintypes.WCHAR * 128),
                ("dwState", wintypes.DWORD),
                ("dwStateMask", wintypes.DWORD),
                ("szInfo", wintypes.WCHAR * 256),
                ("uTimeoutOrVersion", wintypes.UINT),
                ("szInfoTitle", wintypes.WCHAR * 64),
                ("dwInfoFlags", wintypes.DWORD),
                ("guidItem", Guid),
                ("hBalloonIcon", wintypes.HANDLE),
            ]

        class WindowClassEx(ctypes.Structure):
            _fields_ = [
                ("cbSize", wintypes.UINT),
                ("style", wintypes.UINT),
                ("lpfnWndProc", wnd_proc_type),
                ("cbClsExtra", ctypes.c_int),
                ("cbWndExtra", ctypes.c_int),
                ("hInstance", wintypes.HINSTANCE),
                ("hIcon", wintypes.HANDLE),
                ("hCursor", wintypes.HANDLE),
                ("hbrBackground", wintypes.HANDLE),
                ("lpszMenuName", wintypes.LPCWSTR),
                ("lpszClassName", wintypes.LPCWSTR),
                ("hIconSm", wintypes.HANDLE),
            ]

        class Point(ctypes.Structure):
            _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]

        class Message(ctypes.Structure):
            _fields_ = [
                ("hwnd", wintypes.HWND),
                ("message", wintypes.UINT),
                ("wParam", ctypes.c_size_t),
                ("lParam", ctypes.c_ssize_t),
                ("time", wintypes.DWORD),
                ("pt", Point),
                ("lPrivate", wintypes.DWORD),
            ]

        callback_message = TRAY_CALLBACK_MESSAGE
        class_name = f"{TRAY_WINDOW_CLASS}_{os.getpid()}"
        kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
        kernel32.GetModuleHandleW.restype = wintypes.HINSTANCE
        instance = kernel32.GetModuleHandleW(None)
        user32.RegisterClassExW.argtypes = [ctypes.POINTER(WindowClassEx)]
        user32.RegisterClassExW.restype = wintypes.ATOM
        user32.CreateWindowExW.argtypes = [
            wintypes.DWORD,
            wintypes.LPCWSTR,
            wintypes.LPCWSTR,
            wintypes.DWORD,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            wintypes.HWND,
            wintypes.HANDLE,
            wintypes.HINSTANCE,
            wintypes.LPVOID,
        ]
        user32.DestroyWindow.argtypes = [wintypes.HWND]
        user32.PostQuitMessage.argtypes = [ctypes.c_int]
        user32.PostMessageW.argtypes = [
            wintypes.HWND,
            wintypes.UINT,
            ctypes.c_size_t,
            ctypes.c_ssize_t,
        ]
        user32.DefWindowProcW.argtypes = [
            wintypes.HWND,
            wintypes.UINT,
            ctypes.c_size_t,
            ctypes.c_ssize_t,
        ]
        user32.DefWindowProcW.restype = ctypes.c_ssize_t
        user32.GetCursorPos.argtypes = [ctypes.POINTER(Point)]
        user32.GetMessageW.argtypes = [ctypes.POINTER(Message), wintypes.HWND, wintypes.UINT, wintypes.UINT]
        user32.TranslateMessage.argtypes = [ctypes.POINTER(Message)]
        user32.DispatchMessageW.argtypes = [ctypes.POINTER(Message)]
        user32.DispatchMessageW.restype = ctypes.c_ssize_t

        def window_proc(hwnd, message, wparam, lparam):
            if message == callback_message:
                event = int(lparam) & 0xFFFF
                if event in (self.WM_LBUTTONUP, self.WM_LBUTTONDBLCLK):
                    self.events.put(("show", None))
                elif event == self.WM_RBUTTONUP:
                    point = Point()
                    if user32.GetCursorPos(ctypes.byref(point)):
                        self.events.put(("menu", (point.x, point.y)))
                return 0
            if message == self.WM_CLOSE:
                user32.DestroyWindow(hwnd)
                return 0
            if message == self.WM_DESTROY:
                user32.PostQuitMessage(0)
                return 0
            return user32.DefWindowProcW(hwnd, message, wparam, lparam)

        self._wnd_proc = wnd_proc_type(window_proc)
        registered = False
        try:
            window_class = WindowClassEx()
            window_class.cbSize = ctypes.sizeof(WindowClassEx)
            window_class.lpfnWndProc = self._wnd_proc
            window_class.hInstance = instance
            window_class.lpszClassName = class_name
            if not user32.RegisterClassExW(ctypes.byref(window_class)):
                raise ctypes.WinError(ctypes.get_last_error())
            registered = True

            user32.CreateWindowExW.restype = wintypes.HWND
            self._hwnd = user32.CreateWindowExW(
                0,
                class_name,
                "Switch Macro tray",
                0,
                0,
                0,
                0,
                0,
                self.HWND_MESSAGE,
                None,
                instance,
                None,
            )
            if not self._hwnd:
                raise ctypes.WinError(ctypes.get_last_error())

            user32.LoadImageW.argtypes = [
                wintypes.HANDLE,
                wintypes.LPCWSTR,
                wintypes.UINT,
                ctypes.c_int,
                ctypes.c_int,
                wintypes.UINT,
            ]
            user32.LoadImageW.restype = wintypes.HANDLE
            self._icon = user32.LoadImageW(
                None,
                self._icon_path,
                self.IMAGE_ICON,
                0,
                0,
                self.LR_LOADFROMFILE | self.LR_DEFAULTSIZE,
            )
            if not self._icon:
                raise ctypes.WinError(ctypes.get_last_error())

            self._data = NotifyIconData()
            self._data.cbSize = ctypes.sizeof(NotifyIconData)
            self._data.hWnd = self._hwnd
            self._data.uID = 1
            self._data.uFlags = self.NIF_MESSAGE | self.NIF_ICON | self.NIF_TIP | self.NIF_SHOWTIP
            self._data.uCallbackMessage = callback_message
            self._data.hIcon = self._icon
            self._data.szTip = "Switch Macro · Mando de PC"
            shell32.Shell_NotifyIconW.argtypes = [wintypes.DWORD, ctypes.POINTER(NotifyIconData)]
            shell32.Shell_NotifyIconW.restype = wintypes.BOOL
            if not shell32.Shell_NotifyIconW(self.NIM_ADD, ctypes.byref(self._data)):
                raise ctypes.WinError(ctypes.get_last_error())
            self._data.uTimeoutOrVersion = self.NOTIFYICON_VERSION_4
            shell32.Shell_NotifyIconW(self.NIM_SETVERSION, ctypes.byref(self._data))
            self._ready.set()

            message = Message()
            while user32.GetMessageW(ctypes.byref(message), None, 0, 0) > 0:
                user32.TranslateMessage(ctypes.byref(message))
                user32.DispatchMessageW(ctypes.byref(message))
        except Exception as error:
            self._startup_error = error
            self._ready.set()
        finally:
            if self._data is not None:
                shell32.Shell_NotifyIconW(self.NIM_DELETE, ctypes.byref(self._data))
            if self._icon:
                user32.DestroyIcon.argtypes = [wintypes.HANDLE]
                user32.DestroyIcon(self._icon)
                self._icon = None
            if self._hwnd:
                user32.DestroyWindow(self._hwnd)
                self._hwnd = None
            if registered:
                user32.UnregisterClassW.argtypes = [wintypes.LPCWSTR, wintypes.HINSTANCE]
                user32.UnregisterClassW(class_name, instance)

    def close(self):
        if self._hwnd:
            user32 = ctypes.WinDLL("user32", use_last_error=True)
            user32.PostMessageW.argtypes = [
                wintypes.HWND,
                wintypes.UINT,
                ctypes.c_size_t,
                ctypes.c_ssize_t,
            ]
            user32.PostMessageW(self._hwnd, self.WM_CLOSE, 0, 0)
        if self._thread.is_alive():
            self._thread.join(timeout=2.0)


def advertises_switch_macro_serial(advertisement) -> bool:
    for service_uuid in getattr(advertisement, "service_uuids", ()) or ():
        normalized = str(service_uuid).strip("{} ").casefold()
        if normalized in SERIAL_ADVERTISEMENT_UUIDS or normalized in {"3080", "3081", "3082", "3083"}:
            return True
    return False


def neutral_state() -> tuple[int, int, tuple[int, int, int, int]]:
    return 0, 0x08, (0x80, 0x80, 0x80, 0x80)


def resource_path(name: str) -> str:
    base_path = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base_path, name)


def encode_frame(state: tuple[int, int, tuple[int, int, int, int]]) -> bytes:
    buttons, hat, axes = state
    frame = bytearray(9)
    frame[0] = FRAME_MAGIC
    frame[1] = buttons & 0xFF
    frame[2] = (buttons >> 8) & 0xFF
    frame[3] = hat
    frame[4:8] = bytes(axes)
    for value in frame[1:8]:
        frame[8] ^= value
    return bytes(frame)


def quantized_axis(value: int, invert: bool = False) -> int:
    normalized = value / (32767 if value >= 0 else 32768)
    if invert:
        normalized = -normalized
    normalized = max(-1.0, min(1.0, normalized))
    if abs(normalized) < 0.12:
        return 128
    raw = math.floor((normalized + 1.0) * 127.5 + 0.5)
    return max(0, min(255, math.floor(raw / 4.0 + 0.5) * 4))


def read_controller_state(controller) -> tuple[int, int, tuple[int, int, int, int]]:
    button_map = (
        (pygame.CONTROLLER_BUTTON_A, 0x0002),       # bottom face button -> Switch B
        (pygame.CONTROLLER_BUTTON_B, 0x0004),       # right face button -> Switch A
        (pygame.CONTROLLER_BUTTON_X, 0x0001),       # left face button -> Switch Y
        (pygame.CONTROLLER_BUTTON_Y, 0x0008),       # top face button -> Switch X
        (pygame.CONTROLLER_BUTTON_LEFTSHOULDER, 0x0010),
        (pygame.CONTROLLER_BUTTON_RIGHTSHOULDER, 0x0020),
        (pygame.CONTROLLER_BUTTON_BACK, 0x0100),
        (pygame.CONTROLLER_BUTTON_START, 0x0200),
        (pygame.CONTROLLER_BUTTON_LEFTSTICK, 0x0400),
        (pygame.CONTROLLER_BUTTON_RIGHTSTICK, 0x0800),
        (pygame.CONTROLLER_BUTTON_GUIDE, 0x1000),
    )
    buttons = 0
    for button, mask in button_map:
        if controller.get_button(button):
            buttons |= mask

    if controller.get_axis(pygame.CONTROLLER_AXIS_TRIGGERLEFT) > 16384:
        buttons |= 0x0040
    if controller.get_axis(pygame.CONTROLLER_AXIS_TRIGGERRIGHT) > 16384:
        buttons |= 0x0080

    directions = (
        (pygame.CONTROLLER_BUTTON_DPAD_UP, 0x01),
        (pygame.CONTROLLER_BUTTON_DPAD_RIGHT, 0x02),
        (pygame.CONTROLLER_BUTTON_DPAD_DOWN, 0x04),
        (pygame.CONTROLLER_BUTTON_DPAD_LEFT, 0x08),
    )
    hat_bits = 0
    for button, bit in directions:
        if controller.get_button(button):
            hat_bits |= bit
    hat = {
        0x01: 0x00,
        0x03: 0x01,
        0x02: 0x02,
        0x06: 0x03,
        0x04: 0x04,
        0x0C: 0x05,
        0x08: 0x06,
        0x09: 0x07,
    }.get(hat_bits, 0x08)

    axes = (
        quantized_axis(controller.get_axis(pygame.CONTROLLER_AXIS_LEFTX)),
        quantized_axis(controller.get_axis(pygame.CONTROLLER_AXIS_LEFTY)),
        quantized_axis(controller.get_axis(pygame.CONTROLLER_AXIS_RIGHTX)),
        quantized_axis(controller.get_axis(pygame.CONTROLLER_AXIS_RIGHTY)),
    )
    return buttons, hat, axes


class BleRelay:
    def __init__(self):
        self.events: queue.SimpleQueue = queue.SimpleQueue()
        self._latest_frame = encode_frame(neutral_state())
        self._frame_lock = threading.Lock()
        self._loop = None
        self._thread = threading.Thread(target=self._run, name="SwitchMacroBLE", daemon=True)
        self._client = None
        self._characteristic = None
        self._write_response = False
        self._stopping = False
        self._frames_sent = 0

    def start(self):
        self._thread.start()

    def _run(self):
        loop = asyncio.new_event_loop()
        self._loop = loop
        asyncio.set_event_loop(loop)
        loop.create_task(self._writer())
        loop.run_forever()
        pending = asyncio.all_tasks(loop)
        for task in pending:
            task.cancel()
        if pending:
            loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))
        loop.close()

    def set_state(self, state):
        frame = encode_frame(state)
        with self._frame_lock:
            self._latest_frame = frame

    def _post(self, kind, value=None):
        self.events.put((kind, value))

    def scan(self):
        if self._loop:
            asyncio.run_coroutine_threadsafe(self._scan(), self._loop)

    async def _scan(self):
        self._post("status", "Buscando dispositivos Bluetooth…")
        try:
            found = await BleakScanner.discover(timeout=8.0, return_adv=True)
            devices = [
                device
                for device, advertisement in found.values()
                if device.address and advertises_switch_macro_serial(advertisement)
            ]
            devices.sort(key=lambda d: ("flipper" not in (d.name or "").casefold(), (d.name or "").casefold()))
            self._post("devices", devices)
        except Exception as error:
            self._post("status", f"Error al buscar Bluetooth: {error}")

    def connect(self, device):
        if self._loop:
            asyncio.run_coroutine_threadsafe(self._connect(device), self._loop)

    async def _connect_attempt(self, device, services, pair):
        client = None

        def disconnected_callback():
            # Bleak's WinRT backend invokes this callback with no arguments.
            # Capture this client so a late disconnect from an older attempt
            # cannot clear a newer active connection.
            self._on_disconnected(client)

        client = BleakClient(
            device,
            disconnected_callback=disconnected_callback,
            services=services,
            timeout=60.0,
            pair=pair,
            winrt={"use_cached_services": False},
        )
        self._client = client
        await client.connect()
        service = client.services.get_service(SERIAL_SERVICE_UUID)
        characteristic = service.get_characteristic(SERIAL_RX_UUID) if service else None
        if service is None or characteristic is None:
            try:
                visible_services = ", ".join(sorted(item.uuid for item in client.services)) or "ninguno"
            except Exception:
                visible_services = "no disponibles"
            raise RuntimeError(
                "No aparece el canal serie de Switch Macro; "
                f"servicios GATT encontrados: {visible_services}."
            )

        properties = {property_name.casefold() for property_name in characteristic.properties}
        if "write-without-response" in properties:
            write_response = False
        elif "write" in properties:
            write_response = True
        else:
            raise RuntimeError("La característica BLE encontrada no permite enviar entradas del mando.")

        # Give the Flipper BT service time to install its serial callback after
        # GATT connects, before the writer sends any controller frames.
        await asyncio.sleep(0.4)
        if not client.is_connected:
            raise RuntimeError("El Flipper Zero cerró Bluetooth durante la preparación de la conexión.")
        return characteristic, write_response

    async def _connect(self, device):
        await self._disconnect()
        self._post("status", f"Conectando y emparejando con {device.name or device.address}; acepta el aviso de Windows si aparece…")
        attempts = (([SERIAL_SERVICE_UUID], True), (None, False))
        errors = []
        for attempt_index, (services, pair) in enumerate(attempts):
            try:
                characteristic, write_response = await self._connect_attempt(device, services, pair)
            except Exception as error:
                errors.append(error)
                await self._disconnect()
                if attempt_index == 0:
                    self._post("status", "Conectando de nuevo con todos los servicios GATT…")
                    await asyncio.sleep(0.5)
                    continue
                first_error = f"{type(errors[0]).__name__}: {errors[0]}"
                final_error = f"{type(error).__name__}: {error}"
                self._post("status", f"No se pudo conectar: {final_error} (primer intento: {first_error})")
                return False

            self._characteristic = characteristic
            self._write_response = write_response
            self._frames_sent = 0
            self._post("connected", device.name or device.address)
            return True

    def _on_disconnected(self, client):
        if self._client is client:
            self._client = None
            self._characteristic = None
            self._post("disconnected", None)

    def disconnect(self):
        if self._loop:
            asyncio.run_coroutine_threadsafe(self._disconnect_requested(), self._loop)

    async def _disconnect_requested(self):
        await self._disconnect()
        self._post("disconnected", None)

    async def _disconnect(self):
        client = self._client
        self._client = None
        self._characteristic = None
        if client:
            try:
                await client.disconnect()
            except Exception:
                pass

    async def _writer(self):
        last_frame = None
        last_send = 0.0
        while not self._stopping:
            client = self._client
            characteristic = self._characteristic
            if client and characteristic and client.is_connected:
                with self._frame_lock:
                    frame = self._latest_frame
                now = asyncio.get_running_loop().time()
                if frame != last_frame or now - last_send >= FRAME_HEARTBEAT_SECONDS:
                    try:
                        await client.write_gatt_char(
                            characteristic,
                            frame,
                            response=self._write_response,
                        )
                        last_frame = frame
                        last_send = asyncio.get_running_loop().time()
                        self._frames_sent += 1
                        if self._frames_sent == 1 or self._frames_sent % 10 == 0:
                            self._post("traffic", self._frames_sent)
                    except Exception as error:
                        if self._client is client:
                            await self._disconnect()
                            self._post("status", f"Se perdió la conexión BLE: {error}")
                            self._post("disconnected", None)
            await asyncio.sleep(FRAME_INTERVAL_SECONDS)

    def stop(self):
        if not self._loop or not self._thread.is_alive():
            return
        future = asyncio.run_coroutine_threadsafe(self._disconnect(), self._loop)
        try:
            future.result(timeout=3.0)
        except Exception:
            pass
        self._stopping = True
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(timeout=3.0)


class SwitchMacroDesktopApp:
    COLORS = {
        "background": "#0B1220",
        "surface": "#111C2D",
        "surface_alt": "#17253A",
        "border": "#23334A",
        "text": "#F4F7FB",
        "muted": "#91A3BA",
        "accent": "#56D8C5",
        "accent_dark": "#173B3D",
        "warm": "#FF866B",
    }

    def __init__(self, root, controller_api):
        self.root = root
        self.controller_api = controller_api
        self.preferences = load_preferences()
        requested_language = self.preferences.get("language", "es")
        self.language = requested_language if requested_language in LANGUAGE_NAMES else "es"
        self.localized_widgets = []
        self.localized_vars = []
        self.ble = BleRelay()
        self.ble.start()
        self.gamepads = []
        self.gamepad_signature = ()
        self.ble_devices = []
        self.connected = False
        self.ble_activity = False
        self.auto_connect_enabled = bool(self.preferences.get("auto_connect_enabled", True))
        self.auto_flipper_address = self.preferences.get("flipper_address")
        self.saved_gamepad_name = self.preferences.get("gamepad_name")
        self.tray_enabled = bool(self.preferences.get("tray_enabled", False))
        self.tray_icon = None
        self.tray_hidden = False
        self.auto_scan_job = None
        self.closing = False
        self.last_state = neutral_state()
        self.last_input_active = False
        self.last_tx_frames = 0
        self.last_poll_error = None

        root.title(f"Switch Macro · {self.tr('Mando de PC')} v{DESKTOP_VERSION}")
        root.resizable(False, False)
        root.configure(bg=self.COLORS["background"])
        root.geometry("610x682")
        root.minsize(610, 682)
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.bind("<Unmap>", self._on_window_unmap, add="+")
        try:
            root.iconbitmap(resource_path("switch_macro.ico"))
        except tk.TclError:
            pass

        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("TFrame", background=self.COLORS["background"])
        style.configure(
            "TLabel",
            background=self.COLORS["surface"],
            foreground=self.COLORS["text"],
            font=("Segoe UI", 10),
        )
        style.configure(
            "Muted.TLabel",
            background=self.COLORS["surface"],
            foreground=self.COLORS["muted"],
            font=("Segoe UI", 9),
        )
        style.configure(
            "Eyebrow.TLabel",
            background=self.COLORS["surface"],
            foreground=self.COLORS["accent"],
            font=("Segoe UI", 8, "bold"),
        )
        style.configure(
            "Primary.TButton",
            background=self.COLORS["accent"],
            foreground=self.COLORS["background"],
            borderwidth=0,
            focusthickness=0,
            font=("Segoe UI", 9, "bold"),
            padding=(13, 9),
        )
        style.map(
            "Primary.TButton",
            background=[("active", "#75E4D2"), ("disabled", "#38525C")],
            foreground=[("disabled", "#83949D")],
        )
        style.configure(
            "Secondary.TButton",
            background=self.COLORS["surface_alt"],
            foreground=self.COLORS["text"],
            borderwidth=1,
            focusthickness=0,
            font=("Segoe UI", 9, "bold"),
            padding=(12, 8),
        )
        style.map("Secondary.TButton", background=[("active", "#233650")])
        style.configure(
            "Tray.TCheckbutton",
            background=self.COLORS["surface"],
            foreground=self.COLORS["text"],
            font=("Segoe UI", 9),
            padding=(0, 2),
        )
        style.map(
            "Tray.TCheckbutton",
            background=[("active", self.COLORS["surface"])],
            foreground=[("disabled", self.COLORS["muted"])],
        )
        style.configure(
            "Dark.TCombobox",
            fieldbackground=self.COLORS["surface_alt"],
            background=self.COLORS["surface_alt"],
            foreground=self.COLORS["text"],
            arrowcolor=self.COLORS["accent"],
            bordercolor=self.COLORS["border"],
            lightcolor=self.COLORS["border"],
            darkcolor=self.COLORS["border"],
            insertcolor=self.COLORS["text"],
            padding=(11, 8),
            arrowsize=13,
            font=("Segoe UI", 10),
        )
        style.map(
            "Dark.TCombobox",
            fieldbackground=[("readonly", self.COLORS["surface_alt"])],
            foreground=[("readonly", self.COLORS["text"])],
            selectbackground=[("readonly", self.COLORS["surface_alt"])],
            selectforeground=[("readonly", self.COLORS["text"])],
        )

        main = tk.Frame(root, bg=self.COLORS["background"], padx=26, pady=22)
        main.pack(fill="both", expand=True)

        header = tk.Frame(main, bg=self.COLORS["background"])
        header.pack(fill="x")
        self._draw_brand_mark(header)
        brand = tk.Frame(header, bg=self.COLORS["background"])
        brand.pack(side="left", padx=(13, 0), pady=(2, 0))
        tk.Label(
            brand,
            text="SWITCH MACRO",
            bg=self.COLORS["background"],
            fg=self.COLORS["text"],
            font=("Segoe UI", 18, "bold"),
        ).pack(anchor="w")
        self._localized_label(
            brand,
            "MANDO DE PC  ·  BLUETOOTH",
            ttk_label=False,
            bg=self.COLORS["background"],
            fg=self.COLORS["muted"],
            font=("Segoe UI", 8, "bold"),
        ).pack(anchor="w", pady=(1, 0))
        language_box = tk.Frame(header, bg=self.COLORS["background"])
        language_box.pack(side="right", padx=(8, 0))
        language_icon = tk.Canvas(
            language_box,
            width=30,
            height=30,
            bg=self.COLORS["background"],
            highlightthickness=0,
        )
        language_icon.pack(side="left", padx=(0, 6))
        self._draw_language_icon(language_icon)
        self.language_picker = ttk.Combobox(
            language_box,
            state="readonly",
            width=10,
            values=[LANGUAGE_NAMES[code] for code in LANGUAGE_ORDER],
            style="Dark.TCombobox",
        )
        self.language_picker.set(LANGUAGE_NAMES[self.language])
        self.language_picker.pack(side="left")
        self.language_picker.bind("<<ComboboxSelected>>", self._on_language_selected)
        self._localized_label(
            main,
            "Tu mando, conectado al Flipper Zero. Sin pasos de más.",
            ttk_label=False,
            bg=self.COLORS["background"],
            fg=self.COLORS["muted"],
            font=("Segoe UI", 10),
        ).pack(anchor="w", pady=(14, 2))

        gamepad_card = self._new_card(main, pady=(12, 0))
        self._localized_label(gamepad_card, "01   /   MANDO DEL PC", style="Eyebrow.TLabel").grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 12)
        )
        self.gamepad_picker = ttk.Combobox(
            gamepad_card, state="readonly", width=42, style="Dark.TCombobox"
        )
        self.gamepad_picker.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(0, 12))
        self.gamepad_picker.bind("<<ComboboxSelected>>", self._on_gamepad_selected)
        self.refresh_button = ttk.Button(
            gamepad_card,
            text=self.tr("Actualizar"),
            style="Secondary.TButton",
            command=lambda: self.refresh_gamepads(force=True),
        )
        self._register_localized_widget(self.refresh_button, "Actualizar")
        self.refresh_button.grid(row=1, column=2, sticky="ew", padx=(9, 0), pady=(0, 12))
        self.input_var = LocalizedStringVar(self, "Entrada del mando: esperando")
        self.localized_vars.append(self.input_var)
        self.input_dot = tk.Canvas(
            gamepad_card, width=9, height=9, bg=self.COLORS["surface"], highlightthickness=0
        )
        self.input_dot.grid(row=2, column=0, sticky="w", padx=(0, 7))
        self.input_dot_id = self.input_dot.create_oval(
            1, 1, 8, 8, fill=self.COLORS["muted"], outline=""
        )
        ttk.Label(gamepad_card, textvariable=self.input_var, style="Muted.TLabel").grid(
            row=2, column=1, columnspan=2, sticky="w"
        )
        gamepad_card.columnconfigure(0, weight=1)
        gamepad_card.columnconfigure(1, weight=1)

        flipper_card = self._new_card(main, pady=(13, 0))
        self._localized_label(flipper_card, "02   /   FLIPPER ZERO", style="Eyebrow.TLabel").grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 12)
        )
        self.flipper_picker = ttk.Combobox(
            flipper_card, state="readonly", width=42, style="Dark.TCombobox"
        )
        self.flipper_picker.grid(row=1, column=0, columnspan=2, sticky="ew")
        self.flipper_picker.bind("<<ComboboxSelected>>", self._on_flipper_selected)
        self.search_button = ttk.Button(
            flipper_card,
            text=self.tr("Buscar"),
            style="Secondary.TButton",
            command=self.scan_flippers,
        )
        self.search_button.grid(row=1, column=2, sticky="ew", padx=(9, 0))
        self._register_localized_widget(self.search_button, "Buscar")
        flipper_card.columnconfigure(0, weight=1)
        flipper_card.columnconfigure(1, weight=1)

        auto_card = self._new_card(main, pady=(13, 0))
        auto_copy = tk.Frame(auto_card, bg=self.COLORS["surface"])
        auto_copy.grid(row=0, column=0, sticky="w")
        self._localized_label(auto_copy, "Conexión automática", font=("Segoe UI", 11, "bold")).pack(
            anchor="w"
        )
        ttk.Label(
            auto_copy,
            text=self.tr("Busca y reconecta al entrar en Live controller."),
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(4, 0))
        self._register_localized_widget(
            auto_copy.winfo_children()[-1], "Busca y reconecta al entrar en Live controller."
        )
        self.auto_switch = tk.Canvas(
            auto_card,
            width=96,
            height=38,
            bg=self.COLORS["surface"],
            highlightthickness=0,
            cursor="hand2",
            takefocus=True,
        )
        self.auto_switch.grid(row=0, column=1, sticky="e", padx=(10, 0))
        self.auto_switch.bind("<Button-1>", self._toggle_auto_from_ui)
        self.auto_switch.bind("<Return>", self._toggle_auto_from_ui)
        self.auto_switch.bind("<space>", self._toggle_auto_from_ui)
        auto_card.columnconfigure(0, weight=1)
        self.tray_var = tk.BooleanVar(value=self.tray_enabled)
        self.tray_checkbox = ttk.Checkbutton(
            auto_card,
            text=self.tr("Mostrar icono en bandeja y ocultar al cerrar"),
            variable=self.tray_var,
            command=self._toggle_tray_from_ui,
            style="Tray.TCheckbutton",
        )
        self._register_localized_widget(self.tray_checkbox, "Mostrar icono en bandeja y ocultar al cerrar")
        self.tray_checkbox.grid(row=1, column=0, columnspan=2, sticky="w", pady=(10, 0))
        self._draw_auto_switch()

        self.traffic_var = LocalizedStringVar(self, "Bluetooth: sin tramas")
        self.localized_vars.append(self.traffic_var)
        traffic_card = self._new_card(main, pady=(13, 0))
        self._localized_label(traffic_card, "ESTADO DEL ENLACE", style="Eyebrow.TLabel").pack(anchor="w")
        ttk.Label(traffic_card, textvariable=self.traffic_var, style="Muted.TLabel").pack(
            anchor="w", pady=(7, 0)
        )

        self.status_var = LocalizedStringVar(
            self, "Pon Switch Macro en Live controller en el Flipper Zero."
        )
        self.localized_vars.append(self.status_var)
        self.status_label = tk.Label(
            main,
            textvariable=self.status_var,
            bg=self.COLORS["background"],
            fg=self.COLORS["muted"],
            font=("Segoe UI", 9),
            justify="left",
            wraplength=520,
            anchor="w",
        )
        self.status_label.pack(fill="x", pady=(13, 0))
        tk.Label(
            main,
            text=self.tr("La ventana puede quedarse en segundo plano mientras juegas."),
            bg=self.COLORS["background"],
            fg="#71839A",
            font=("Segoe UI", 8),
        ).pack(anchor="w", pady=(10, 0))
        self._register_localized_widget(
            main.winfo_children()[-1], "La ventana puede quedarse en segundo plano mientras juegas."
        )

        self.tray_menu = tk.Menu(root, tearoff=0)
        self.tray_menu.add_command(label=self.tr("Abrir Switch Macro"), command=self._show_window)
        self.tray_menu.add_separator()
        self.tray_menu.add_command(label=self.tr("Salir"), command=self._exit_from_tray)

        self.refresh_gamepads()
        self.root.after(15, self.poll_controller)
        self.root.after(75, self.poll_ble_events)
        self.root.after(1500, self._periodic_device_refresh)
        if self.tray_enabled:
            self._set_tray_enabled(True, persist=False)

    def _new_card(self, parent, pady=(10, 0)):
        card = tk.Frame(
            parent,
            bg=self.COLORS["surface"],
            padx=16,
            pady=14,
            highlightthickness=1,
            highlightbackground=self.COLORS["border"],
            highlightcolor=self.COLORS["border"],
        )
        card.pack(fill="x", pady=pady)
        return card

    def tr(self, text):
        if self.language != "es" and text.startswith("Bluetooth: ") and " tramas enviadas · " in text:
            frames, state = text[len("Bluetooth: "):].split(" tramas enviadas · ", 1)
            return f"{translated_text(self.language, 'Bluetooth')}: {frames} {translated_text(self.language, 'tramas enviadas')} · {translated_text(self.language, state)}"
        if self.language != "es" and text.startswith("Bluetooth conectado a ") and ". Se reconectará al volver a Live controller." in text:
            device_name = text[len("Bluetooth conectado a "):].split(". Se reconectará al volver a Live controller.", 1)[0]
            return f"{translated_text(self.language, 'Bluetooth conectado a')} {device_name}. {translated_text(self.language, 'Se reconectará al volver a Live controller.')}"
        if self.language != "es" and text.startswith("Bluetooth conectado a "):
            return translated_text(self.language, "Bluetooth conectado a ") + text[len("Bluetooth conectado a "):]
        if self.language != "es" and text.startswith("Conectando y emparejando con ") and "; acepta el aviso de Windows si aparece…" in text:
            device_name = text[len("Conectando y emparejando con "):].split("; acepta el aviso de Windows si aparece…", 1)[0]
            return translated_text(self.language, "Conectando y emparejando con ") + device_name + translated_text(self.language, "; acepta el aviso de Windows si aparece…")
        if self.language != "es" and text.startswith("Encontré el Flipper Zero (") and "); conectando…" in text:
            device_name = text[len("Encontré el Flipper Zero ("):].split("); conectando…", 1)[0]
            return translated_text(self.language, "Encontré el Flipper Zero (") + device_name + translated_text(self.language, "); conectando…")
        return translated_text(self.language, text)

    def _register_localized_widget(self, widget, source_text):
        self.localized_widgets.append((widget, source_text))

    def _localized_label(self, parent, source_text, ttk_label=True, **options):
        if ttk_label:
            widget = ttk.Label(parent, text=self.tr(source_text), **options)
        else:
            widget = tk.Label(parent, text=self.tr(source_text), **options)
        self._register_localized_widget(widget, source_text)
        return widget

    def _draw_language_icon(self, canvas):
        ink = self.COLORS["accent"]
        canvas.create_oval(3, 3, 27, 27, outline=ink, width=2)
        canvas.create_arc(8, 3, 22, 27, start=90, extent=180, style="arc", outline=ink, width=1)
        canvas.create_arc(8, 3, 22, 27, start=270, extent=180, style="arc", outline=ink, width=1)
        canvas.create_line(4, 11, 26, 11, fill=ink, width=1)
        canvas.create_line(4, 19, 26, 19, fill=ink, width=1)

    def _on_language_selected(self, _event=None):
        selected_name = self.language_picker.get()
        selected_code = next(
            (code for code, name in LANGUAGE_NAMES.items() if name == selected_name), None
        )
        if selected_code not in LANGUAGE_NAMES:
            return
        self.language = selected_code
        self.preferences["language"] = selected_code
        self._persist_preferences()
        self._apply_language()

    def _apply_language(self):
        self.root.title(f"Switch Macro · {self.tr('Mando de PC')} v{DESKTOP_VERSION}")
        for widget, source_text in self.localized_widgets:
            try:
                widget.configure(text=self.tr(source_text))
            except tk.TclError:
                pass
        for variable in self.localized_vars:
            variable.refresh_language()
        if hasattr(self, "tray_menu"):
            self.tray_menu.entryconfigure(0, label=self.tr("Abrir Switch Macro"))
            self.tray_menu.entryconfigure(2, label=self.tr("Salir"))
        self._draw_auto_switch()

    def _draw_brand_mark(self, parent):
        mark = tk.Canvas(parent, width=48, height=48, bg=self.COLORS["background"], highlightthickness=0)
        mark.pack(side="left")
        self._draw_rounded_rectangle(mark, 1, 1, 47, 47, 12, fill=self.COLORS["surface_alt"])
        mark.create_rectangle(18, 8, 30, 40, fill="#EAF4F5", outline="")
        self._draw_rounded_rectangle(mark, 8, 9, 18, 39, 5, fill=self.COLORS["accent"])
        self._draw_rounded_rectangle(mark, 30, 9, 40, 39, 5, fill=self.COLORS["warm"])
        mark.create_oval(10, 13, 14, 17, fill=self.COLORS["surface"], outline="")
        mark.create_oval(34, 13, 38, 17, fill=self.COLORS["surface"], outline="")
        mark.create_oval(33, 24, 36, 27, fill=self.COLORS["surface"], outline="")
        mark.create_oval(37, 28, 40, 31, fill=self.COLORS["surface"], outline="")

    @staticmethod
    def _draw_rounded_rectangle(canvas, x1, y1, x2, y2, radius, **options):
        points = []
        for center_x, center_y, start_angle in (
            (x2 - radius, y1 + radius, -90),
            (x2 - radius, y2 - radius, 0),
            (x1 + radius, y2 - radius, 90),
            (x1 + radius, y1 + radius, 180),
        ):
            for step in range(7):
                angle = math.radians(start_angle + step * 15)
                points.extend(
                    (
                        center_x + radius * math.cos(angle),
                        center_y + radius * math.sin(angle),
                    )
                )
        return canvas.create_polygon(points, smooth=True, splinesteps=12, **options)

    def _draw_auto_switch(self):
        if not hasattr(self, "auto_switch"):
            return
        enabled = self.auto_connect_enabled
        available = self.gamepad_picker.current() >= 0 if hasattr(self, "gamepad_picker") else True
        track = self.COLORS["accent_dark"] if enabled else self.COLORS["surface_alt"]
        knob = self.COLORS["accent"] if enabled else self.COLORS["muted"]
        if not available:
            track = "#202A39"
            knob = "#596576"
        self.auto_switch.delete("all")
        self.auto_switch.create_oval(1, 4, 31, 34, fill=track, outline="")
        self.auto_switch.create_oval(31, 4, 61, 34, fill=track, outline="")
        self.auto_switch.create_rectangle(16, 4, 46, 34, fill=track, outline="")
        knob_x = 5 if not enabled else 35
        self.auto_switch.create_oval(knob_x, 8, knob_x + 22, 30, fill=knob, outline="")
        self.auto_switch.create_text(
            77,
            19,
            text=self.tr("On") if enabled else self.tr("Off"),
            fill=self.COLORS["accent"] if enabled else self.COLORS["muted"],
            font=("Segoe UI", 8, "bold"),
        )
        self.auto_switch.configure(cursor="hand2" if available else "arrow")

    def _toggle_auto_from_ui(self, _event=None):
        self.toggle_ble()

    def _persist_preferences(self):
        self.preferences["auto_connect_enabled"] = bool(self.auto_connect_enabled)
        self.preferences["tray_enabled"] = bool(self.tray_enabled)
        self.preferences["language"] = self.language
        if self.auto_flipper_address:
            self.preferences["flipper_address"] = self.auto_flipper_address
        if hasattr(self, "gamepad_picker") and self.gamepad_picker.current() >= 0:
            self.preferences["gamepad_name"] = self.gamepads[self.gamepad_picker.current()][1]
        try:
            save_preferences(self.preferences)
        except OSError as error:
            if hasattr(self, "status_var"):
                self.status_var.set(f"No se pudo guardar la configuración en AppData: {error}")

    def _set_tray_enabled(self, enabled, persist=True):
        enabled = bool(enabled)
        if enabled and self.tray_icon is None:
            try:
                self.tray_icon = WindowsTrayIcon(resource_path("switch_macro.ico"))
            except Exception as error:
                self.tray_enabled = False
                self.tray_var.set(False)
                self.status_var.set(f"No se pudo activar el icono de bandeja: {error}")
                return False
        elif not enabled and self.tray_icon is not None:
            self.tray_icon.close()
            self.tray_icon = None

        self.tray_enabled = enabled
        self.tray_var.set(enabled)
        if not enabled and self.tray_hidden:
            self._show_window()
        if persist:
            self._persist_preferences()
        return True

    def _toggle_tray_from_ui(self):
        self._set_tray_enabled(self.tray_var.get())

    def _hide_to_tray(self):
        if not self.tray_enabled or self.tray_icon is None or self.closing:
            return
        self.tray_hidden = True
        self.root.withdraw()

    def _on_window_unmap(self, event):
        if (
            event.widget is self.root
            and self.tray_enabled
            and not self.tray_hidden
            and not self.closing
            and self.root.state() == "iconic"
        ):
            self.root.after_idle(self._hide_to_tray)

    def _show_window(self):
        if self.closing:
            return
        self.tray_hidden = False
        self.root.deiconify()
        self.root.state("normal")
        self.root.lift()
        self.root.focus_force()

    def _exit_from_tray(self):
        self.tray_enabled = False
        self.tray_var.set(False)
        self._persist_preferences()
        if self.tray_icon is not None:
            self.tray_icon.close()
            self.tray_icon = None
        self.close(force=True)

    def _process_tray_events(self):
        if self.tray_icon is None:
            return
        while True:
            try:
                action, value = self.tray_icon.events.get_nowait()
            except queue.Empty:
                break
            if action == "show":
                self._show_window()
            elif action == "menu":
                x, y = value
                try:
                    self.tray_menu.tk_popup(x, y)
                finally:
                    self.tray_menu.grab_release()

    def _periodic_device_refresh(self):
        self.refresh_gamepads()
        self.root.after(1500, self._periodic_device_refresh)

    def refresh_gamepads(self, force=False):
        api = self.controller_api
        try:
            pygame.event.pump()
            signature = tuple(
                (index, api.name_forindex(index) or f"Mando {index + 1}")
                for index in range(api.get_count())
                if api.is_controller(index)
            )
        except Exception as error:
            self.status_var.set(f"No se pudieron leer los mandos: {error}")
            return
        existing_handles_attached = len(self.gamepads) == len(signature) and all(
            controller.attached() for _, _, controller in self.gamepads
        )
        if signature == self.gamepad_signature and existing_handles_attached and not force:
            return

        previous = self.gamepad_picker.current()
        previous_key = self.gamepads[previous][:2] if 0 <= previous < len(self.gamepads) else None
        for _, _, controller in self.gamepads:
            try:
                controller.quit()
            except Exception:
                pass
        self.gamepads = []
        labels = []
        for index, name in signature:
            try:
                gamepad = api.Controller(index)
                labels.append(name)
                self.gamepads.append((index, name, gamepad))
            except Exception:
                continue
        self.gamepad_signature = signature
        self.gamepad_picker.configure(values=labels)

        selected = next(
            (i for i, (index, name, _) in enumerate(self.gamepads) if (index, name) == previous_key),
            -1,
        )
        if selected < 0 and self.saved_gamepad_name:
            selected = next(
                (i for i, (_, name, _) in enumerate(self.gamepads) if name == self.saved_gamepad_name),
                -1,
            )
        if selected < 0 and len(self.gamepads) == 1:
            selected = 0
        if selected >= 0:
            self.gamepad_picker.current(selected)
            self.saved_gamepad_name = self.gamepads[selected][1]
            self._persist_preferences()
        else:
            self.gamepad_picker.set("")
            raw_count = pygame.joystick.get_count()
            if raw_count:
                self.status_var.set("Mando detectado, pero SDL no reconoce su mapeo. Prueba un mando Xbox, PlayStation o Switch Pro.")
            else:
                self.status_var.set("Conecta un mando al PC y pulsa Actualizar.")
        self._draw_auto_switch()
        self._update_link_status()
        if self.gamepad_picker.current() >= 0:
            self._schedule_auto_scan(350)

    def _on_gamepad_selected(self, _event=None):
        index = self.gamepad_picker.current()
        if index >= 0:
            self.saved_gamepad_name = self.gamepads[index][1]
            self._persist_preferences()
        self._update_link_status()
        if self.gamepad_picker.current() >= 0:
            self._schedule_auto_scan(200)
        else:
            self._cancel_auto_scan()
        self._draw_auto_switch()

    def _on_flipper_selected(self, _event=None):
        index = self.flipper_picker.current()
        if 0 <= index < len(self.ble_devices):
            self.auto_flipper_address = self.ble_devices[index].address
            self._persist_preferences()
        if self.auto_connect_enabled and not self.connected and not self.ble_activity:
            self._connect_selected_flipper(index)

    def _cancel_auto_scan(self):
        if self.auto_scan_job is not None:
            try:
                self.root.after_cancel(self.auto_scan_job)
            except tk.TclError:
                pass
            self.auto_scan_job = None

    def _schedule_auto_scan(self, delay_ms=AUTO_SCAN_RETRY_MS):
        if (
            self.closing
            or not self.auto_connect_enabled
            or self.connected
            or self.ble_activity
            or self.gamepad_picker.current() < 0
            or self.auto_scan_job is not None
        ):
            return
        self.auto_scan_job = self.root.after(delay_ms, self._run_auto_scan)

    def _run_auto_scan(self):
        self.auto_scan_job = None
        if self.closing or not self.auto_connect_enabled or self.connected or self.ble_activity:
            return
        if self.gamepad_picker.current() < 0:
            self.status_var.set("Selecciona un mando; después buscaré el Flipper Zero automáticamente.")
            return
        self._start_flipper_scan()

    def _start_flipper_scan(self):
        if self.connected or self.ble_activity:
            return
        self.ble_activity = True
        self.status_var.set("Buscando el Flipper Zero para conectar en Live controller…")
        self.ble.scan()

    def _connect_selected_flipper(self, index=None):
        if self.connected or self.ble_activity or not self.auto_connect_enabled:
            return
        if self.gamepad_picker.current() < 0:
            self.status_var.set("Selecciona un mando para activar la conexión automática.")
            return
        if index is None:
            index = self.flipper_picker.current()
        if index < 0 or index >= len(self.ble_devices):
            self._schedule_auto_scan(0)
            return
        device = self.ble_devices[index]
        self.auto_flipper_address = device.address
        self.ble_activity = True
        self.status_var.set(f"Encontré el Flipper Zero ({device.name or device.address}); conectando…")
        self.ble.connect(device)

    def scan_flippers(self):
        if self.connected:
            self.status_var.set("Bluetooth ya está conectado. La reconexión automática queda activa.")
            return
        self.auto_connect_enabled = True
        self._draw_auto_switch()
        self._persist_preferences()
        self._cancel_auto_scan()
        self._start_flipper_scan()

    def toggle_ble(self):
        if self.connected:
            self.auto_connect_enabled = False
            self._cancel_auto_scan()
            self._draw_auto_switch()
            self._persist_preferences()
            self.status_var.set("Desconectando Bluetooth…")
            self.ble.disconnect()
            return
        if self.auto_connect_enabled:
            self.auto_connect_enabled = False
            self._cancel_auto_scan()
            self._draw_auto_switch()
            self._persist_preferences()
            self.status_var.set("Conexión automática detenida.")
            return
        if self.gamepad_picker.current() < 0:
            self.status_var.set("Selecciona un mando para activar la conexión automática.")
            return
        self.auto_connect_enabled = True
        self._draw_auto_switch()
        self._persist_preferences()
        self._schedule_auto_scan(0)

    def poll_controller(self):
        try:
            pygame.event.pump()
            index = self.gamepad_picker.current()
            if 0 <= index < len(self.gamepads):
                controller = self.gamepads[index][2]
                state = read_controller_state(controller) if controller.attached() else neutral_state()
            else:
                state = neutral_state()
            self.last_state = state
            self.ble.set_state(state)
            self.last_poll_error = None
            active = (
                state[0] != 0
                or state[1] != 0x08
                or any(abs(axis - 128) >= 8 for axis in state[2])
            )
            input_text = "Entrada del mando: activa" if active else "Entrada del mando: esperando"
            if self.input_var.source_text != input_text:
                self.input_var.set(input_text)
            self.input_dot.itemconfigure(
                self.input_dot_id,
                fill=self.COLORS["accent"] if active else "#60738B",
            )
            self.last_input_active = active
        except Exception as error:
            self.last_state = neutral_state()
            self.ble.set_state(self.last_state)
            self.input_dot.itemconfigure(self.input_dot_id, fill=self.COLORS["warm"])
            message = f"Entrada del mando: error ({type(error).__name__}: {error})"
            if message != self.last_poll_error:
                self.input_var.set(message[:110])
                self.last_poll_error = message
        self.root.after(15, self.poll_controller)

    def poll_ble_events(self):
        self._process_tray_events()
        while True:
            try:
                kind, value = self.ble.events.get_nowait()
            except queue.Empty:
                break
            if kind == "devices":
                self.ble_activity = False
                self.ble_devices = value
                labels = [
                    f"{device.name or 'Dispositivo BLE'} · {device.address[-5:]}"
                    for device in self.ble_devices
                ]
                self.flipper_picker.configure(values=labels)
                # _scan only returns devices advertising Flipper's serial
                # profile UUID, so an unnamed device is still a safe match.
                flipper_indices = list(range(len(self.ble_devices)))
                if flipper_indices:
                    preferred_index = next(
                        (
                            i
                            for i in flipper_indices
                            if self.ble_devices[i].address == self.auto_flipper_address
                        ),
                        flipper_indices[0],
                    )
                    self.flipper_picker.current(preferred_index)
                    self.auto_flipper_address = self.ble_devices[preferred_index].address
                    self._persist_preferences()
                    if self.auto_connect_enabled and self.gamepad_picker.current() >= 0:
                        self._connect_selected_flipper(preferred_index)
                    else:
                        self.status_var.set("Encontré el Flipper Zero. Activa la conexión automática cuando quieras.")
                elif labels:
                    self.flipper_picker.current(0)
                    self.status_var.set("Se detectó el perfil serie de Switch Macro; selecciónalo para conectar.")
                elif self.auto_connect_enabled:
                    self.status_var.set(
                        "Esperando el anuncio del perfil serie de Switch Macro en Live controller; volveré a buscar…"
                    )
                    self._schedule_auto_scan()
                else:
                    self.status_var.set(
                        "No se encontró el perfil serie. Pon el Flipper Zero en Live controller y vuelve a buscar."
                    )
            elif kind == "connected":
                if not self.auto_connect_enabled:
                    self.ble.disconnect()
                    self.status_var.set("Conexión automática detenida.")
                    continue
                self.connected = True
                self.ble_activity = False
                self.last_tx_frames = 0
                self.traffic_var.set("Bluetooth: conectado · esperando tramas")
                self._cancel_auto_scan()
                self.status_var.set(f"Bluetooth conectado a {value}. Se reconectará al volver a Live controller.")
            elif kind == "disconnected":
                self.connected = False
                self.ble_activity = False
                self.last_tx_frames = 0
                self.traffic_var.set("Bluetooth: desconectado · buscando el Flipper Zero")
                if self.auto_connect_enabled:
                    self.status_var.set("El Flipper Zero se desconectó; lo buscaré y reconectaré cuando vuelva a Live controller…")
                    self._schedule_auto_scan(900)
                else:
                    self._update_link_status()
            elif kind == "status":
                self.status_var.set(value or "")
                self.ble_activity = bool(
                    value
                    and value.startswith(("Buscando", "Conectando", "Reconectando"))
                )
                if value and value.startswith(("No se encontraron", "Error al buscar", "No se pudo conectar", "Se perdió la conexión")):
                    self.ble_activity = False
                    if self.auto_connect_enabled and not self.connected:
                        if "could not pair" in value.casefold():
                            self.auto_connect_enabled = False
                            self._cancel_auto_scan()
                            self._draw_auto_switch()
                            self._persist_preferences()
                            self.traffic_var.set("Bluetooth: emparejamiento pendiente en Windows")
                            self.status_var.set(
                                "Windows no pudo emparejar. En Configuración > Bluetooth y dispositivos, "
                                "empareja el Flipper Zero y luego pulsa Buscar."
                            )
                        elif "No aparece el canal serie" in value:
                            self.status_var.set(f"Encontré el Flipper Zero, pero no ofrece el canal de mando: {value.split('servicios GATT encontrados:', 1)[-1].strip() if 'servicios GATT encontrados:' in value else 'aún no disponible'}. Reintentaré en Live controller…")
                        elif "GATT" in value or "Unreachable" in value:
                            self.status_var.set(
                                f"{value[:120]} · reintentando en Live controller…"
                            )
                        else:
                            self.status_var.set(
                                f"{value[:120]} · volveré a buscar automáticamente…"
                            )
                        if self.auto_connect_enabled:
                            self.traffic_var.set("Bluetooth: sin conexión · reintentando")
                            self._schedule_auto_scan(2500)
                elif value and value.startswith("Buscando") and not self.connected:
                    self.traffic_var.set("Bluetooth: buscando el Flipper Zero…")
            elif kind == "traffic":
                self.last_tx_frames = value
                if self.connected:
                    state = "mando activo" if self.last_input_active else "mando neutro"
                    self.traffic_var.set(f"Bluetooth: {value} tramas enviadas · {state}")
        self.root.after(75, self.poll_ble_events)

    def _update_link_status(self):
        if self.connected or self.ble_activity:
            return
        pad_index = self.gamepad_picker.current()
        if pad_index < 0:
            self._draw_auto_switch()
            return
        if self.auto_connect_enabled:
            self.status_var.set("Conexión automática activa; esperando al Flipper Zero en Live controller…")
        else:
            self.status_var.set("Mando seleccionado. Puedes activar la conexión automática.")

    def close(self, force=False):
        if self.tray_enabled and not force:
            self._persist_preferences()
            self._hide_to_tray()
            return
        self.closing = True
        self._cancel_auto_scan()
        self._persist_preferences()
        if self.tray_icon is not None:
            self.tray_icon.close()
            self.tray_icon = None
        self.ble.stop()
        self.traffic_var.set("Bluetooth: desconectado")
        for _, _, controller in self.gamepads:
            try:
                controller.quit()
            except Exception:
                pass
        pygame.quit()
        self.root.destroy()


def main():
    pygame.display.init()
    pygame.display.set_mode((1, 1), pygame.HIDDEN | pygame.NOFRAME)
    pygame.joystick.init()
    from pygame._sdl2 import controller as controller_api

    controller_api.init()
    controller_api.set_eventstate(True)
    root = tk.Tk()
    SwitchMacroDesktopApp(root, controller_api)
    root.mainloop()


if __name__ == "__main__":
    main()
