"""Read only this project's bounded metadata in the calling registry view.

Run normally and via Invoke-CommandInDesktopPackage to compare MSIX views.
Never enumerate another app's settings or save text/model payloads/credentials.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import json
import hashlib
from pathlib import Path
import struct
import winreg

parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True)
args = parser.parse_args()
output = Path(args.output).resolve()
directory = (Path(__file__).resolve().parents[2] / "output" / "LocalTest").resolve()
if output.parent != directory or output.exists():
    raise RuntimeError("Use a new file in this workspace's output/LocalTest directory.")

units = ctypes.c_uint32(512)
buffer = ctypes.create_unicode_buffer(512)
status = ctypes.windll.kernel32.GetCurrentPackageFullName(ctypes.byref(units), buffer)
report = dict(packageStatus=status, package=buffer.value if status == 0 else None,
              observedUtc=datetime.now(timezone.utc).isoformat(), settings=None, diagnostics={})
root = r"Software\Rime\WeaseLC-LocalTest\Correction"
# Match the frontend's exact RegGetValueW size query, not only Python's OpenKey.
native = ctypes.windll.advapi32.RegGetValueW
native.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32,
                   ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
native.restype = ctypes.c_long
length = ctypes.c_uint32(0)
hkcu = ctypes.c_void_p(0xffffffff80000001 if ctypes.sizeof(ctypes.c_void_p) == 8 else 0x80000001)
report["nativeSettingsQuery"] = native(hkcu, root, "SettingsV2", 8, None, None, ctypes.byref(length))
report["nativeSettingsBytes"] = length.value
# Compare supported current-user handles and this token's own SID. No other
# user's hive is opened; these calls request read access only.
advapi = ctypes.windll.advapi32
advapi.RegOpenCurrentUser.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
current = ctypes.c_void_p()
report["openCurrentUser"] = advapi.RegOpenCurrentUser(0x20019, ctypes.byref(current))
if current.value:
    length.value = 0
    report["currentUserHandleQuery"] = native(current, root, "SettingsV2", 8, None, None, ctypes.byref(length))
    report["currentUserHandleBytes"] = length.value
    advapi.RegCloseKey.argtypes = [ctypes.c_void_p]
    advapi.RegCloseKey(current)
token = ctypes.c_void_p()
advapi.OpenProcessToken.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
advapi.GetTokenInformation.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                     ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
advapi.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
ctypes.windll.kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
if advapi.OpenProcessToken(ctypes.c_void_p(-1), 8, ctypes.byref(token)):
    try:
        user = ctypes.create_string_buffer(4096)
        count = ctypes.c_uint32()
        container = ctypes.c_uint32()
        if advapi.GetTokenInformation(token, 29, ctypes.byref(container), 4, ctypes.byref(count)):
            report["appContainer"] = bool(container.value)
        if advapi.GetTokenInformation(token, 25, user, len(user), ctypes.byref(count)):
            label = ctypes.cast(user, ctypes.POINTER(ctypes.c_void_p)).contents.value
            label_text = ctypes.c_void_p()
            if advapi.ConvertSidToStringSidW(label, ctypes.byref(label_text)):
                try:
                    report["integrity"] = int(ctypes.wstring_at(label_text).rsplit("-", 1)[1])
                finally:
                    ctypes.windll.kernel32.LocalFree(label_text)
        if advapi.GetTokenInformation(token, 1, user, len(user), ctypes.byref(count)):
            sid = ctypes.cast(user, ctypes.POINTER(ctypes.c_void_p)).contents.value
            sid_text = ctypes.c_void_p()
            if advapi.ConvertSidToStringSidW(sid, ctypes.byref(sid_text)):
                try:
                    own_sid = ctypes.wstring_at(sid_text)
                    report["userSidHash"] = hashlib.sha256(own_sid.encode()).hexdigest()
                    try:
                        with winreg.OpenKey(winreg.HKEY_USERS, own_sid + "\\" + root) as key:
                            raw, kind = winreg.QueryValueEx(key, "SettingsV2")
                            if kind != winreg.REG_BINARY or len(raw) > 16384:
                                raise ValueError("Invalid settings type/size")
                            data = json.loads(raw)
                            report["ownUserHive"] = dict(mode=data.get("mode"), enabled=data.get("automatic", {}).get("enabled"))
                    except (OSError, ValueError, TypeError) as error:
                        report["ownUserHiveError"] = getattr(error, "winerror", type(error).__name__)
                finally:
                    ctypes.windll.kernel32.LocalFree(sid_text)
    finally:
        ctypes.windll.kernel32.CloseHandle(token)
try:
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, root) as key:
        raw, kind = winreg.QueryValueEx(key, "SettingsV2")
        if kind != winreg.REG_BINARY or len(raw) > 16384:
            raise ValueError("Invalid settings type/size")
        settings = json.loads(raw)
        report["settings"] = dict(mode=settings.get("mode"), timing=settings.get("timing"),
                                  enabled=settings.get("automatic", {}).get("enabled"), bytes=len(raw))
except (OSError, ValueError) as error:
    report["settingsError"] = getattr(error, "winerror", type(error).__name__)

formats = {"LastKey": "<6IQ", "LastCommit": "<6IQ", "LastGate": "<6IQ",
           "LastCapture": "<4IQ", "LastWorker": "<8IQ", "LastAutomatic": "<4IQ",
           "LastScope": "<5I"}
try:
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, root + r"\Diagnostics") as key:
        for name, layout in formats.items():
            try:
                raw, kind = winreg.QueryValueEx(key, name)
                if kind != winreg.REG_BINARY or len(raw) != struct.calcsize(layout):
                    report["diagnostics"][name] = dict(error="invalid type/size")
                else:
                    report["diagnostics"][name] = list(struct.unpack(layout, raw))
            except FileNotFoundError:
                report["diagnostics"][name] = None
        try:
            flag, kind = winreg.QueryValueEx(key, "UseFixedReplyV1")
            report["fixedReply"] = kind == winreg.REG_DWORD and flag == 1
        except FileNotFoundError:
            report["fixedReply"] = False
except OSError as error:
    report["diagnosticsError"] = error.winerror
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
