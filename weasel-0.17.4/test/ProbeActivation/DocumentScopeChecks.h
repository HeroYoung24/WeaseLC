#pragma once
// In-memory Office metadata contract, not a claim about the installed Word.
class DocumentMetadataFixture : public IDispatch {
 public:
  ULONG refs = 1;
  CComVariant document, readOnly, protection, permission, enabled;
  bool deny = false, unexpected = false;
  DISPID failureId = DISPID_UNKNOWN;
  HRESULT failure = E_FAIL, exception = S_OK;
  unsigned permissionReads = 0;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr; if (id != IID_IUnknown && id != IID_IDispatch) return E_NOINTERFACE;
    *out = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP GetTypeInfoCount(UINT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetTypeInfo(UINT,LCID,ITypeInfo**) override { return E_NOTIMPL; }
  STDMETHODIMP GetIDsOfNames(REFIID,LPOLESTR* names,UINT count,LCID,DISPID* out) override {
    if (deny) return E_ACCESSDENIED;
    const wchar_t* allowed[]{L"Document",L"ReadOnly",L"ProtectionType",L"Permission",L"Enabled"};
    if (count != 1) { unexpected = true; return E_INVALIDARG; }
    for (unsigned i=0;i<_countof(allowed);++i) if (!wcscmp(*names,allowed[i])) { *out=i+1; return S_OK; }
    unexpected = true; return DISP_E_UNKNOWNNAME;
  }
  STDMETHODIMP Invoke(DISPID id,REFIID,LCID,WORD flags,DISPPARAMS* arguments,VARIANT* out,EXCEPINFO* error,UINT*) override {
    if (flags != DISPATCH_PROPERTYGET || arguments->cArgs || id < 1 || id > 5) { unexpected = true; return E_INVALIDARG; }
    if (id == 4) ++permissionReads;
    if (id == failureId) {
      if (error) { error->scode = exception; error->bstrDescription = SysAllocString(L"discard this error description"); }
      return failure;
    }
    VARIANT* values[]{&document,&readOnly,&protection,&permission,&enabled};
    return VariantCopy(out,values[id-1]);
  }
};
static bool TestDocumentScope() {
  using namespace correction;
  unsigned checks=0,failures=0;
  auto check=[&](bool good,const char* name){++checks;if(!good)++failures;std::cout<<(good?"PASS ":"FAIL ")<<name<<'\n';};
  DocumentMetadataFixture window, document, permission;
  window.document = static_cast<IDispatch*>(&document);
  document.readOnly = false; document.protection = LONG(-1);
  document.permission = static_cast<IDispatch*>(&permission); permission.enabled = false;
  check(EditableWordDocument(&window),"Office closed metadata confirms writable unprotected unrestricted document");
  document.readOnly = true;check(!EditableWordDocument(&window),"readonly document cannot use Office fallback");document.readOnly=false;
  for (LONG value : {0L,1L,2L,3L}) {document.protection=value;check(!EditableWordDocument(&window),"all Word document protection modes rejected");}
  document.protection=LONG(-1);permission.enabled=true;
  check(!EditableWordDocument(&window),"rights-managed document rejected");permission.enabled=false;
  document.readOnly=LONG(0);check(!EditableWordDocument(&window),"numeric false is not a verified boolean");document.readOnly=false;
  document.protection.Clear();check(!EditableWordDocument(&window),"unknown protection state rejected");document.protection=LONG(-1);
  permission.deny=true;check(!EditableWordDocument(&window),"unavailable permission metadata rejected");permission.deny=false;
  document.failureId=4; document.failure=DISP_E_EXCEPTION; document.exception=E_FAIL;
  const DWORD localMetadata=WordDocument|WordWritable|WordUnprotected|WordPermissionUnavailable;
  check(WordMetadataEvidence(&window)==localMetadata && !EditableWordDocument(&window),
      "observed Permission getter exception is classified without granting strict IRM clearance");
  DocumentMetadataCall call; CComVariant failed;
  check(!DocumentMetadata(&document,L"Permission",failed,&call) && call.lookup==S_OK &&
      call.invoke==DISP_E_EXCEPTION && call.exception==E_FAIL && call.type==VT_EMPTY,
      "numeric diagnostics distinguish name lookup from getter exception");
  document.exception=E_ACCESSDENIED;
  check(!(WordMetadataEvidence(&window)&WordPermissionUnavailable),"access denied inside exception cannot enter compatibility");
  document.exception=E_FAIL;document.failure=E_FAIL;
  check(!(WordMetadataEvidence(&window)&WordPermissionUnavailable),"unclassified direct failure cannot enter compatibility");
  document.failure=DISP_E_EXCEPTION;document.deny=true;
  check(!(WordMetadataEvidence(&window)&WordPermissionUnavailable),"name lookup failure cannot enter compatibility");document.deny=false;
  for(bool readonly:{false,true}) {
    document.readOnly=readonly;document.protection=readonly?LONG(-1):LONG(1);
    const auto before=document.permissionReads;
    check(!(WordMetadataEvidence(&window)&WordPermissionUnavailable) && document.permissionReads==before,
        "readonly or protected document rejects before permission compatibility query");
  }
  document.readOnly=false;document.protection=LONG(-1);document.failureId=DISPID_UNKNOWN;
  document.permission.Clear();
  check(!(WordMetadataEvidence(&window)&WordPermissionUnavailable),"missing Permission object is not the observed getter exception");
  document.permission=static_cast<IDispatch*>(&permission);permission.failureId=5;permission.failure=DISP_E_EXCEPTION;permission.exception=E_FAIL;
  check(!(WordMetadataEvidence(&window)&WordPermissionUnavailable),"failing Enabled query is not Permission getter unavailability");
  permission.failureId=DISPID_UNKNOWN;
  window.document.Clear();check(!EditableWordDocument(&window),"missing document rejected");
  check(!window.unexpected && !document.unexpected && !permission.unexpected,
      "metadata contract reads no text selection name file or mutation method");
  document.permission.Clear();
  check(window.refs==1 && document.refs==1 && permission.refs==1,"Office metadata references balanced");
  check(!WordDocumentWindow(nullptr,nullptr),"missing Word pane never grants access");
  std::cout<<"Document scope checks = "<<checks<<", failures = "<<failures<<'\n';return !failures;
}
