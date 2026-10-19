#pragma once
#include <windows.h>
#include <msctf.h>
#include <richedit.h>
#include <atlcomcli.h>
#include "CorrectionFocus.h"

namespace correction {
// Only query control metadata. Never use WM_GETTEXT, change selection, or send
// messages to an unknown, foreign-process or foreign-thread window.
enum NativeTextEvidence : DWORD {
  NativeWindows=1, NativeWithinView=2, NativeOwnThread=4, NativeKnownClass=8,
  NativeEditableMultiline=16, NativeNoPassword=32, NativePlainMode=64, NativeEnabled=128,
  NativeTextVerified=255
};
inline DWORD InspectNativeTextControl(HWND view,HWND focus) {
  DWORD flags=0;
  if(!view || !focus || !IsWindow(view) || !IsWindow(focus))return flags;
  flags|=NativeWindows;
  if(view!=focus && !IsChild(view,focus))return flags;
  flags|=NativeWithinView;
  DWORD process=0,viewProcess=0;const DWORD thread=GetWindowThreadProcessId(focus,&process);
  GetWindowThreadProcessId(view,&viewProcess);
  if(process!=GetCurrentProcessId() || viewProcess!=process || thread!=GetCurrentThreadId())return flags;
  flags|=NativeOwnThread;
  WCHAR name[64]{};
  if(!GetClassNameW(focus,name,_countof(name)))return flags;
  const bool edit=!_wcsicmp(name,L"Edit");
  const bool rich=!_wcsicmp(name,L"RichEdit20W") || !_wcsicmp(name,L"RICHEDIT50W") ||
      !_wcsicmp(name,L"RichEditD2DPT");
  if(!edit && !rich)return flags;
  flags|=NativeKnownClass;
  const auto style=GetWindowLongPtrW(focus,GWL_STYLE);
  if(!(style&ES_MULTILINE) || (style&(ES_PASSWORD|ES_READONLY)))return flags;
  flags|=NativeEditableMultiline;
  if(!IsWindowEnabled(focus))return flags;
  flags|=NativeEnabled;
  DWORD_PTR password=1;
  if(!SendMessageTimeoutW(focus,EM_GETPASSWORDCHAR,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,10,&password) || password)return flags;
  flags|=NativeNoPassword;
  if(edit)flags|=NativePlainMode;
  else {
    DWORD_PTR mode=0;
    if(SendMessageTimeoutW(focus,EM_GETTEXTMODE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,10,&mode) &&
        (mode&TM_PLAINTEXT) && !(mode&TM_RICHTEXT))flags|=NativePlainMode;
  }
  return flags;
}
inline DWORD NativeTextContextEvidence(ITfContext* context) {
  if(!context)return 0;
  CComPtr<ITfContextView> view;HWND window=nullptr;
  if(context->GetActiveView(&view)!=S_OK || !view || view->GetWnd(&window)!=S_OK)return 0;
  const HWND focus=GetFocus();const auto flags=InspectNativeTextControl(window,focus);
  return GetFocus()==focus?flags:flags&~NativeWithinView;
}
inline bool NativeTextRangeVerified(ITfContext* context,ITfRange* range) {
  CComPtr<ITfContext> owner;
  return context && range && range->GetContext(&owner)==S_OK && SameIdentity(owner,context) &&
      NativeTextContextEvidence(context)==NativeTextVerified;
}
using NativeScopeVerifier=bool(*)(ITfContext*,ITfRange*);
}
