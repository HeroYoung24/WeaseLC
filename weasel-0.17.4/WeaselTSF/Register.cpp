#include "stdafx.h"
#include "Register.h"
#include "RegistrationWorkflow.h"
#include <strsafe.h>
#include <WeaselUtility.h>

#define CLSID_STRLEN 38  // strlen("{xxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx}")

#ifdef WEASEL_CORRECTION_PROBE_BUILD
static const char c_szInfoKeyPrefix[] = "Software\\Classes\\CLSID\\";
static const HKEY kRegistrationHive = HKEY_LOCAL_MACHINE;
#else
static const char c_szInfoKeyPrefix[] = "CLSID\\";
static const HKEY kRegistrationHive = HKEY_CLASSES_ROOT;
#endif
static const char c_szTipKeyPrefix[] = "Software\\Microsoft\\CTF\\TIP\\";
static const HKEY kTipRegistrationHive = HKEY_LOCAL_MACHINE;
static const char c_szInProcSvr32[] = "InprocServer32";
static const char c_szModelName[] = "ThreadingModel";

HKL FindIME(LANGID langid) {
  HKL hKL = NULL;
  WCHAR key[9];
  HKEY hKey = NULL;
  LSTATUS ret =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", 0,
                    KEY_READ, &hKey);
  if (ret == ERROR_SUCCESS) {
    for (DWORD id = (0xE0200000 | langid);
         hKL == NULL && id <= (0xE0FF0000 | langid); id += 0x10000) {
      StringCchPrintfW(key, _countof(key), L"%08X", id);
      HKEY hSubKey = NULL;
      ret = RegOpenKeyExW(hKey, key, 0, KEY_READ, &hSubKey);
      if (ret == ERROR_SUCCESS) {
        WCHAR data[32] = {};
        DWORD type;
        DWORD size = sizeof data;
        ret = RegQueryValueExW(hSubKey, L"Ime File", NULL, &type, (LPBYTE)data,
                               &size);
        data[ARRAYSIZE(data) - 1] = L'\0';
        if (ret == ERROR_SUCCESS && type == REG_SZ &&
            _wcsicmp(data, L"weasel.ime") == 0)
          hKL = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(id));
        RegCloseKey(hSubKey);
      }
    }
  }
  if (hKey) RegCloseKey(hKey);
  return hKL;
}

HRESULT RegisterProfiles() {
#define CHECK_HR(operation) \
  do { const HRESULT result = (operation); if (FAILED(result)) return result; } while (false)

  CComPtr<ITfInputProcessorProfileMgr> pInputProcessorProfileMgr;
  CHECK_HR(pInputProcessorProfileMgr.CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_ALL));
  WCHAR szProfile[100] = {};
  std::wstring profile{};
  DWORD dwSize = GetEnvironmentVariable(L"TEXTSERVICE_PROFILE", szProfile,
                                        ARRAYSIZE(szProfile));
  if (dwSize >= ARRAYSIZE(szProfile))
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
  if (dwSize > 0) profile.assign(szProfile, dwSize);
  BOOL hansEnable = (profile == L"hans");
  BOOL hantEnable = (profile == L"hant");
  // fallback hans enable
  hansEnable = hansEnable || (!hantEnable && !hansEnable);

#ifdef WEASEL_CORRECTION_PROBE_BUILD
  const std::wstring text_service_desc = L"Weasel Context Probe";
#else
  const auto text_service_desc = get_weasel_ime_name();
#endif
  const WCHAR* text_service_desc_str = text_service_desc.c_str();
  ULONG text_service_desc_len = static_cast<ULONG>(text_service_desc.size());

  WCHAR achIconFile[MAX_PATH] = {};
  ULONG cchIconFile =
      GetModuleFileNameW(g_hInst, achIconFile, ARRAYSIZE(achIconFile));
  if (!cchIconFile) {
    const DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
  }
  if (cchIconFile >= ARRAYSIZE(achIconFile))
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

  const auto register_profile = [&](LANGID langId, HKL hkl, BOOL enable) {
    return pInputProcessorProfileMgr->RegisterProfile(
        c_clsidTextService, langId, c_guidProfile, text_service_desc_str,
        text_service_desc_len, achIconFile, cchIconFile, TEXTSERVICE_ICON_INDEX,
        hkl, 0, enable, 0);
  };

#ifdef WEASEL_CORRECTION_PROBE_BUILD
  const HKL hkl_hans = NULL;
  const HKL hkl_hant = NULL;
#else
  const auto hkl_hans = FindIME(TEXTSERVICE_LANGID_HANS);
  const auto hkl_hant = FindIME(TEXTSERVICE_LANGID_HANT);
#endif
  CHECK_HR(register_profile(TEXTSERVICE_LANGID_HANS, hkl_hans, hansEnable));
  CHECK_HR(register_profile(TEXTSERVICE_LANGID_HANT, hkl_hant, hantEnable));
  // WeaselIME not support these languages, so HKL is NULL
  CHECK_HR(register_profile(TEXTSERVICE_LANGID_HONGKONG, NULL, false));
  CHECK_HR(register_profile(TEXTSERVICE_LANGID_MACAU, NULL, false));
  CHECK_HR(register_profile(TEXTSERVICE_LANGID_SINGAPORE, NULL, false));
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  CComPtr<ITfInputProcessorProfiles> pProfiles;
  CHECK_HR(pProfiles.CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL,
                                      CLSCTX_INPROC_SERVER));
  CHECK_HR(pProfiles->EnableLanguageProfile(
      c_clsidTextService, TEXTSERVICE_LANGID_HANS, c_guidProfile, TRUE));
#endif
#undef CHECK_HR

  return S_OK;
}

HRESULT UnregisterProfiles() {
  HRESULT result = S_OK;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  CComPtr<ITfInputProcessorProfiles> pProfiles;
  const HRESULT legacyHr = pProfiles.CoCreateInstance(CLSID_TF_InputProcessorProfiles,
                                           NULL, CLSCTX_INPROC_SERVER);
  KeepRegistrationFailure(result, legacyHr);
  if (SUCCEEDED(legacyHr)) {
    KeepRegistrationFailure(result, pProfiles->EnableLanguageProfile(c_clsidTextService,
                                     TEXTSERVICE_LANGID_HANS, c_guidProfile,
                                     FALSE));
  }
#endif
  CComPtr<ITfInputProcessorProfileMgr> pInputProcessorProfileMgr;
  const HRESULT managerHr = pInputProcessorProfileMgr.CoCreateInstance(
          CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_ALL);
  KeepRegistrationFailure(result, managerHr);
  if (FAILED(managerHr)) return result;
  const LANGID languages[] = {TEXTSERVICE_LANGID_HANS, TEXTSERVICE_LANGID_HANT,
      TEXTSERVICE_LANGID_HONGKONG, TEXTSERVICE_LANGID_MACAU, TEXTSERVICE_LANGID_SINGAPORE};
  KeepRegistrationFailure(result, RemoveRegistrationItems(_countof(languages),
      [&](unsigned index) {
        return pInputProcessorProfileMgr->UnregisterProfile(c_clsidTextService,
            languages[index], c_guidProfile, 0);
      }));
  return result;
}

const GUID SupportCategories0[] = {
    GUID_TFCAT_CATEGORY_OF_TIP, GUID_TFCAT_TIP_KEYBOARD,
    // GUID_TFCAT_TIP_SPEECH,
    // GUID_TFCAT_TIP_HANDWRITING,
    GUID_TFCAT_TIPCAP_SECUREMODE, GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_WOW16, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, GUID_TFCAT_PROP_AUDIODATA,
    GUID_TFCAT_PROP_INKDATA, GUID_TFCAT_PROPSTYLE_CUSTOM,
    GUID_TFCAT_PROPSTYLE_STATIC, GUID_TFCAT_PROPSTYLE_STATICCOMPACT,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, GUID_TFCAT_DISPLAYATTRIBUTEPROPERTY};

HRESULT RegisterCategories() {
  CComPtr<ITfCategoryMgr> pCategoryMgr = NULL;
  HRESULT result = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                              IID_ITfCategoryMgr, (LPVOID*)&pCategoryMgr);
  if (FAILED(result)) return result;
  for (const auto& guid : SupportCategories0) {
    result = pCategoryMgr->RegisterCategory(c_clsidTextService, guid, c_clsidTextService);
    if (FAILED(result)) return result;
  }
  return S_OK;
}

HRESULT UnregisterCategories() {
  CComPtr<ITfCategoryMgr> pCategoryMgr = NULL;
  const HRESULT result = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                              IID_ITfCategoryMgr, (LPVOID*)&pCategoryMgr);
  if (FAILED(result)) return result;
  return RemoveRegistrationItems(_countof(SupportCategories0), [&](unsigned index) {
    return pCategoryMgr->UnregisterCategory(c_clsidTextService, SupportCategories0[index],
                                           c_clsidTextService);
  });
}

static BOOL CLSIDToStringA(REFGUID refGUID, char* pchA) {
  static const BYTE GuidMap[] = {3,   2, 1, 0,   '-', 5,  4,  '-', 7,  6,
                                 '-', 8, 9, '-', 10,  11, 12, 13,  14, 15};

  static const char szDigits[] = "0123456789ABCDEF";

  int i;
  char* p = pchA;

  const BYTE* pBytes = (const BYTE*)&refGUID;

  *p++ = '{';
  for (i = 0; i < sizeof(GuidMap); i++) {
    if (GuidMap[i] == '-')
      *p++ = '-';
    else {
      *p++ = szDigits[(pBytes[GuidMap[i]] & 0xF0) >> 4];
      *p++ = szDigits[(pBytes[GuidMap[i]] & 0x0F)];
    }
  }
  *p++ = '}';
  *p = '\0';
  return TRUE;
}

HRESULT RegisterServer() {
  DWORD dw = 0;
  HKEY hKey = NULL;
  HKEY hSubKey = NULL;
  char achIMEKey[ARRAYSIZE(c_szInfoKeyPrefix) + CLSID_STRLEN];
  char achFileName[MAX_PATH] = {};

  if (!CLSIDToStringA(c_clsidTextService,
                      achIMEKey + ARRAYSIZE(c_szInfoKeyPrefix) - 1))
    return E_FAIL;
  memcpy(achIMEKey, c_szInfoKeyPrefix, sizeof(c_szInfoKeyPrefix) - 1);

  const DWORD length = GetModuleFileNameA(g_hInst, achFileName, ARRAYSIZE(achFileName));
  if (!length) {
    const DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
  }
  if (length >= ARRAYSIZE(achFileName))
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

#ifdef _M_ARM64
  {
    // On ARM64 we use ARM64X redirection DLL.
    // When loaded, weasel.dll will be redirected to weaselARM64.dll on
    // ARM64 processes, and weaselx64.dll on x64 processes.
    //
    // GetModuleFileNameA returns the actual loaded DLL name; point COM
    // registration at the redirector instead.

    char wrapperPath[MAX_PATH] = {};
    const HRESULT appended = StringCbCatA(achFileName, sizeof(achFileName), "\\..\\weasel.dll");
    if (FAILED(appended)) return appended;
    const DWORD wrapperLength = GetFullPathNameA(achFileName, ARRAYSIZE(wrapperPath), wrapperPath, NULL);
    if (!wrapperLength) {
      const DWORD error = GetLastError();
      return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    if (wrapperLength >= ARRAYSIZE(wrapperPath))
      return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    const HRESULT copied = StringCchCopyA(achFileName, ARRAYSIZE(achFileName), wrapperPath);
    if (FAILED(copied)) return copied;
  }
#endif

  LSTATUS status = RegCreateKeyExA(kRegistrationHive, achIMEKey, 0, NULL,
      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &dw);
  if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
  status = RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)TEXTSERVICE_DESC_A,
      sizeof TEXTSERVICE_DESC_A);
  if (status == ERROR_SUCCESS)
    status = RegCreateKeyExA(hKey, c_szInProcSvr32, 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_WRITE, NULL, &hSubKey, &dw);
  if (status == ERROR_SUCCESS) {
    status = RegSetValueExA(hSubKey, NULL, 0, REG_SZ, (const BYTE*)achFileName,
        static_cast<DWORD>((strlen(achFileName) + 1) * sizeof(char)));
    if (status == ERROR_SUCCESS)
      status = RegSetValueExA(hSubKey, c_szModelName, 0, REG_SZ,
          (const BYTE*)TEXTSERVICE_MODEL, sizeof TEXTSERVICE_MODEL);
  }
  HRESULT result = HRESULT_FROM_WIN32(status);
  if (hSubKey) KeepRegistrationFailure(result, HRESULT_FROM_WIN32(RegCloseKey(hSubKey)));
  KeepRegistrationFailure(result, HRESULT_FROM_WIN32(RegCloseKey(hKey)));
  return result;
}

HRESULT UnregisterServer() {
  char achIMEKey[ARRAYSIZE(c_szInfoKeyPrefix) + CLSID_STRLEN];
  if (!CLSIDToStringA(c_clsidTextService,
                      achIMEKey + ARRAYSIZE(c_szInfoKeyPrefix) - 1))
    return E_FAIL;
  memcpy(achIMEKey, c_szInfoKeyPrefix, sizeof(c_szInfoKeyPrefix) - 1);

  // On Windows 8, we need to manually delete the registry key for our TIP
  char tipKey[ARRAYSIZE(c_szTipKeyPrefix) + CLSID_STRLEN];
  if (!CLSIDToStringA(c_clsidTextService,
                      tipKey + ARRAYSIZE(c_szTipKeyPrefix) - 1))
    return E_FAIL;
  memcpy(tipKey, c_szTipKeyPrefix, sizeof(c_szTipKeyPrefix) - 1);
  // TSF metadata lives under HKLM, not under the merged COM Classes root.
  HRESULT result = DeleteRegistrationKey(kRegistrationHive, achIMEKey);
  KeepRegistrationFailure(result, DeleteRegistrationKey(kTipRegistrationHive, tipKey));
  return result;
}
