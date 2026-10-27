#include <windows.h>
#include <oleacc.h>
#include <atlcomcli.h>
#include <iostream>
#include <vector>
#include <string>
#include "../../include/CorrectionDocumentScope.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
#pragma comment(lib,"uuid.lib")

// Read only a user-selected, already running test Word's closed metadata.
// Never start Office, select a document, retrieve text, invoke setters or macros.
struct Panes { DWORD process; std::vector<HWND> windows; };
static BOOL CALLBACK FindPane(HWND window, LPARAM parameter) {
  auto& panes = *reinterpret_cast<Panes*>(parameter);
  DWORD process = 0; GetWindowThreadProcessId(window, &process);
  WCHAR name[64]{};
  if (process == panes.process && IsWindowVisible(window) &&
      GetClassNameW(window,name,_countof(name)) && !wcscmp(name,L"_WwG")) panes.windows.push_back(window);
  return TRUE;
}
static BOOL CALLBACK FindTop(HWND window, LPARAM parameter) {
  DWORD process = 0; GetWindowThreadProcessId(window, &process);
  if (process == reinterpret_cast<Panes*>(parameter)->process) EnumChildWindows(window,FindPane,parameter);
  return TRUE;
}
static bool Query(IDispatch* object, const wchar_t* name, CComVariant& value) {
  correction::DocumentMetadataCall call;
  const bool ok = correction::DocumentMetadata(object,name,value,&call);
  std::wcout << name << L": lookup=0x" << std::hex << static_cast<ULONG>(call.lookup)
      << L" invoke=0x" << static_cast<ULONG>(call.invoke)
      << L" exception=0x" << static_cast<ULONG>(call.exception)
      << L" wCode=" << std::dec << call.exceptionCode << L" type=" << call.type;
  if (ok && value.vt == VT_BOOL) std::wcout << L" bool=" << static_cast<int>(value.boolVal);
  if (ok && value.vt == VT_I4) std::wcout << L" integer=" << value.lVal;
  std::wcout << L'\n';
  return ok;
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  wchar_t* tail = nullptr; const auto process = wcstoul(argv[1],&tail,10);
  if (!process || !tail || *tail) return 2;
  HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process);
  WCHAR image[32768]{}; DWORD units = _countof(image);
  const bool known = handle && QueryFullProcessImageNameW(handle,0,image,&units);
  if (handle) CloseHandle(handle);
  const auto base = known ? wcsrchr(image,L'\\') : nullptr;
  if (!base || _wcsicmp(base+1,L"WINWORD.EXE")) return 3;
  const auto initialized = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if (FAILED(initialized)) return 4;
  int result = 5;
  {
    Panes panes{process,{}}; EnumWindows(FindTop,reinterpret_cast<LPARAM>(&panes));
    std::cout << "Visible native panes=" << panes.windows.size() << '\n';
    if (panes.windows.size() == 1) {
      CComPtr<IDispatch> window;
      const auto hr=AccessibleObjectFromWindow(panes.windows[0],static_cast<DWORD>(OBJID_NATIVEOM),
          IID_IDispatch,reinterpret_cast<void**>(&window));
      std::cout << "Native object=0x" << std::hex << static_cast<ULONG>(hr) << std::dec << '\n';
      CComVariant document, readOnly, protection, permission, enabled;
      if (hr == S_OK && window && Query(window,L"Document",document) && document.vt==VT_DISPATCH && document.pdispVal) {
        Query(document.pdispVal,L"ReadOnly",readOnly);
        Query(document.pdispVal,L"ProtectionType",protection);
        if(Query(document.pdispVal,L"Permission",permission) && permission.vt==VT_DISPATCH && permission.pdispVal)
          Query(permission.pdispVal,L"Enabled",enabled);
        result=0;
      }
    }
  }
  CoUninitialize(); return result;
}
