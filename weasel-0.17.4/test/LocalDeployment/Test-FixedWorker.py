"""Exercise both independent workers with compiled text, without TSF or HTTP.

Requires no installed test identity/settings. Creates and removes only its own
private test key; refuses existing settings rather than overwriting them.
"""
import json
import os
from pathlib import Path
import struct
import subprocess
import winreg

SOURCE = Path(__file__).resolve().parents[2]
ROOT = r"Software\Rime\WeaseLC-LocalTest"
CLSID = "{893169EA-8480-4965-9F2F-695ED51E4B43}"


def absent(hive, path, view):
    try:
        with winreg.OpenKey(hive, path, 0, winreg.KEY_READ | view):
            pass
    except FileNotFoundError:
        return
    raise RuntimeError("Existing independent test state; stop/uninstall it first.")


for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        absent(hive, ROOT, view)
        absent(hive, rf"Software\Microsoft\CTF\TIP\{CLSID}", view)
        absent(hive, rf"Software\Classes\CLSID\{CLSID}", view)

profile = dict(endpoint="http://127.0.0.1:9/v1/chat/completions",
               model="compiled-fixture", timeout=2, sensitivity=0,
               custom_threshold=False, threshold=0.9, json_mode=True)
settings = dict(version=3, mode=1, timing=0,
                automatic=dict(enabled=True, tab=True, side=0, gap=8,
                               width=320, font=14, debounce=250),
                local=profile, online=profile.copy())


def save_settings():
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, ROOT + r"\Correction") as key:
        winreg.SetValueEx(key, "SettingsV2", 0, winreg.REG_BINARY,
                         json.dumps(settings).encode())


def invoke(exe, text):
    encoded = text.encode("utf-16-le")
    wire = struct.pack("<III", 0x31434357, 1, len(encoded) // 2)
    wire += encoded + bytes(2050 - len(encoded))
    env = os.environ.copy()
    dependencies = SOURCE / "output" / ("Win32" if exe.parent.name == "Win32" else "")
    env["PATH"] = str(dependencies) + os.pathsep + env.get("PATH", "")
    result = subprocess.run([str(exe), "/correction-worker"], input=wire, env=env,
                            capture_output=True, timeout=5,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode in (0, 4), f"worker exit {result.returncode:#x}"
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, ROOT + r"\Correction\Diagnostics") as key:
        diagnostic, kind = winreg.QueryValueEx(key, "LastWorker")
    assert kind == winreg.REG_BINARY and len(diagnostic) == 40
    values = struct.unpack("<8IQ", diagnostic)
    assert values[0] == 1 and values[2] == len(encoded) // 2 and values[3] == 1
    return result, values


try:
    save_settings()
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, ROOT + r"\Correction\Diagnostics") as key:
        winreg.SetValueEx(key, "UseFixedReplyV1", 0, winreg.REG_DWORD, 1)
    for arch in ("x64", "Win32"):
        exe = SOURCE / "output" / "LocalTest" / arch / "WeaselDeployer.exe"
        samples = [("这里有错子。", 1, 3)]
        samples += [("这里有错子。"[:units], 0, 1) for units in range(1, 6)]
        for text, count, state in samples:
            result, meta = invoke(exe, text)
            assert result.returncode == 0 and len(result.stdout) == 1228
            assert struct.unpack_from("<III", result.stdout) == (0x31524357, 1, count)
            assert meta[1] == 4 and meta[4] == state and meta[6] == 200
            if count:
                assert struct.unpack_from("<IIId", result.stdout, 12) == (3, 2, 2, 0.99)
                assert result.stdout[32:36].decode("utf-16-le") == "错子"
                assert result.stdout[98:102].decode("utf-16-le") == "错字"
        result, meta = invoke(exe, "拒绝其他合成文本。")
        assert result.returncode == 4 and not result.stdout and meta[1] == 5
        # Explicit user-kept terms suppress corrections; Rime frequency does not
        # appear in this worker's preferences, parser or suppression path.
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, ROOT + r"\Correction", 0, winreg.KEY_SET_VALUE) as key:
            winreg.SetValueEx(key, "KeptTermsV1", 0, winreg.REG_BINARY,
                             json.dumps(dict(version=1, terms=["错子"])).encode())
        result, meta = invoke(exe, "这里有错子。")
        assert result.returncode == 0 and struct.unpack_from("<I", result.stdout, 8)[0] == 0
        assert meta[4] == 2  # Filtered, not a model no-change verdict
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, ROOT + r"\Correction", 0, winreg.KEY_SET_VALUE) as key:
            winreg.DeleteValue(key, "KeptTermsV1")
        profile["custom_threshold"] = True
        profile["threshold"] = 1.0
        save_settings()
        result, meta = invoke(exe, "这里有错子。")
        assert result.returncode == 0 and struct.unpack_from("<I", result.stdout, 8)[0] == 0
        assert meta[4] == 2
        profile["custom_threshold"] = False
        save_settings()
        for field in ("mode", "automatic"):
            if field == "mode":
                settings["mode"] = 0
            else:
                settings["automatic"]["enabled"] = False
            save_settings()
            result, meta = invoke(exe, "这里有错子。")
            assert result.returncode == 0 and struct.unpack_from("<I", result.stdout, 8)[0] == 0
            assert meta[4] == 0xffffffff  # No model verdict: gate remained closed
            settings["mode"] = 1
            settings["automatic"]["enabled"] = True
            save_settings()
        print(f"{arch}: suggestion, no-change, corpus rejection, explicit kept term, threshold, both off gates: passed")
finally:
    # Preflight established that these exact keys did not exist before this run.
    for suffix in (r"\Correction\Diagnostics", r"\Correction", ""):
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, ROOT + suffix)
        except FileNotFoundError:
            pass
print("Fixed worker checks passed; private test settings removed. No model service used.")
