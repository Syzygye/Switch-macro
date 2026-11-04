# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['PCRecorder/switch_macro_pc.py'],
    pathex=[],
    binaries=[],
    datas=[('PCRecorder/switch_macro.ico', '.')],
    hiddenimports=['bleak.backends.winrt.client', 'bleak.backends.winrt.scanner'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='SwitchMacroPC-v0.9.13',
    icon='PCRecorder/switch_macro.ico',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
