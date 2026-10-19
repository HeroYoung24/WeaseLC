#include <CorrectionNativeScope.h>
static bool RunNativeScopeChecks() {
  unsigned checks=0,failures=0;
  auto check=[&](bool ok,const char* name){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
  const HWND foreground=GetForegroundWindow(),focus=GetFocus();
  HWND parent=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,400,100,nullptr,nullptr,nullptr,nullptr);
  HWND other=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
  HWND plain=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE,0,0,200,80,parent,nullptr,nullptr,nullptr);
  check(parent && other && plain,"hidden owned native edit fixtures created");
  if(parent && other && plain) {
    check(correction::InspectNativeTextControl(parent,plain)==correction::NativeTextVerified,"native multiline Edit verified without querying text");
    check(correction::InspectNativeTextControl(plain,plain)==correction::NativeTextVerified,"direct native view verified");
    check(correction::InspectNativeTextControl(other,plain)!=correction::NativeTextVerified,"unrelated view rejects otherwise valid edit");
    check(correction::InspectNativeTextControl(parent,parent)!=correction::NativeTextVerified,"generic root window is not treated as a text control");
    check(correction::InspectNativeTextControl(parent,nullptr)==0,"missing focus rejects native fallback");
    HWND single=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,100,20,parent,nullptr,nullptr,nullptr);
    check(single && correction::InspectNativeTextControl(parent,single)!=correction::NativeTextVerified,"single-line account or PIN-style field rejected");
    SendMessageW(plain,EM_SETREADONLY,TRUE,0);
    check(correction::InspectNativeTextControl(parent,plain)!=correction::NativeTextVerified,"readonly native edit rejected");
    SendMessageW(plain,EM_SETREADONLY,FALSE,0);EnableWindow(plain,FALSE);
    check(correction::InspectNativeTextControl(parent,plain)!=correction::NativeTextVerified,"disabled native edit rejected");EnableWindow(plain,TRUE);
    auto crossThread=std::async(std::launch::async,[&]{return correction::InspectNativeTextControl(parent,plain);});
    check(crossThread.get()!=correction::NativeTextVerified,"foreign thread rejected before control messaging");
    HMODULE rich=LoadLibraryExW(L"msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    HWND richEdit=rich?CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|ES_MULTILINE,0,0,200,80,parent,nullptr,nullptr,nullptr):nullptr;
    check(richEdit!=nullptr,"system rich edit fixture created");
    if(richEdit) {
      check(correction::InspectNativeTextControl(parent,richEdit)!=correction::NativeTextVerified,"default rich text mode rejected");
      const auto changed=SendMessageW(richEdit,EM_SETTEXTMODE,TM_PLAINTEXT,0);
      check(changed==0 && correction::InspectNativeTextControl(parent,richEdit)==correction::NativeTextVerified,"rich edit admitted only after plaintext mode confirmed");
      SendMessageW(richEdit,EM_SETPASSWORDCHAR,L'*',0);
      check(correction::InspectNativeTextControl(parent,richEdit)!=correction::NativeTextVerified,"rich edit password character rejected");
      DestroyWindow(richEdit);
    }
    if(rich)FreeLibrary(rich);
  }
  if(parent)DestroyWindow(parent);if(other)DestroyWindow(other);
  check(GetForegroundWindow()==foreground && GetFocus()==focus,"native metadata tests never show windows or change focus");
  std::cout<<"Native scope checks = "<<checks<<", failures = "<<failures<<'\n';return !failures;
}
