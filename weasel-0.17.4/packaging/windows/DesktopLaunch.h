#pragma once
#include <windows.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlobj.h>
#include <servprov.h>
#include <shlguid.h>
#include <wrl/client.h>
#include <string>
#pragma comment(lib,"oleaut32.lib")
namespace distribution {
// Ask the existing desktop Explorer, never reuse the installer's admin token.
inline bool LaunchFromDesktop(const std::wstring& executable) {
  using Microsoft::WRL::ComPtr;
  ComPtr<IShellWindows> windows;
  if (FAILED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows)))) return false;
  VARIANT location{},empty{}; location.vt=VT_I4;location.lVal=CSIDL_DESKTOP;
  long handle=0;ComPtr<IDispatch> dispatch;
  if(FAILED(windows->FindWindowSW(&location,&empty,SWC_DESKTOP,&handle,SWFO_NEEDDISPATCH,&dispatch)) || !dispatch) return false;
  ComPtr<IServiceProvider> service;ComPtr<IShellBrowser> browser;
  ComPtr<IShellView> view;ComPtr<IDispatch> background;ComPtr<IShellFolderViewDual> folder;
  ComPtr<IDispatch> application;ComPtr<IShellDispatch2> shell;
  if(FAILED(dispatch.As(&service)) || FAILED(service->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser))) ||
      FAILED(browser->QueryActiveShellView(&view)) || FAILED(view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&background))) ||
      FAILED(background.As(&folder)) || FAILED(folder->get_Application(&application)) || FAILED(application.As(&shell))) return false;
  BSTR file=SysAllocString(executable.c_str()); if(!file)return false;
  VARIANT show{};show.vt=VT_I4;show.lVal=SW_SHOWNORMAL;
  const HRESULT result=shell->ShellExecute(file,empty,empty,empty,show);
  SysFreeString(file);return SUCCEEDED(result);
}
}
