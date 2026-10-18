#pragma once
// A test-owned tear-off interface over the same real TSF context. No TIP
// activation: every edit lock/range still belongs to the scratch document.
class OwnedContextAlias final : public ITfContext {
 public:
  explicit OwnedContextAlias(ITfContext* context) : context_(context) {}
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id == IID_ITfContext) { *out = this; AddRef(); return S_OK; }
    return context_->QueryInterface(id, out);
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override { auto n=--refs_;if(!n)delete this;return n; }
  STDMETHODIMP RequestEditSession(TfClientId c,ITfEditSession* s,DWORD f,HRESULT* r) override { return context_->RequestEditSession(c,s,f,r); }
  STDMETHODIMP InWriteSession(TfClientId c,BOOL* v) override { return context_->InWriteSession(c,v); }
  STDMETHODIMP GetSelection(TfEditCookie c,ULONG i,ULONG n,TF_SELECTION* s,ULONG* f) override { return context_->GetSelection(c,i,n,s,f); }
  STDMETHODIMP SetSelection(TfEditCookie c,ULONG n,const TF_SELECTION* s) override { return context_->SetSelection(c,n,s); }
  STDMETHODIMP GetStart(TfEditCookie c,ITfRange** r) override { return context_->GetStart(c,r); }
  STDMETHODIMP GetEnd(TfEditCookie c,ITfRange** r) override { return context_->GetEnd(c,r); }
  STDMETHODIMP GetActiveView(ITfContextView** v) override { return context_->GetActiveView(v); }
  STDMETHODIMP EnumViews(IEnumTfContextViews** v) override { return context_->EnumViews(v); }
  STDMETHODIMP GetStatus(TF_STATUS* s) override { return context_->GetStatus(s); }
  STDMETHODIMP GetProperty(REFGUID id,ITfProperty** p) override { return context_->GetProperty(id,p); }
  STDMETHODIMP GetAppProperty(REFGUID id,ITfReadOnlyProperty** p) override { return context_->GetAppProperty(id,p); }
  STDMETHODIMP TrackProperties(const GUID** a,ULONG n,const GUID** b,ULONG m,ITfReadOnlyProperty** p) override { return context_->TrackProperties(a,n,b,m,p); }
  STDMETHODIMP EnumProperties(IEnumTfProperties** p) override { return context_->EnumProperties(p); }
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** d) override { return context_->GetDocumentMgr(d); }
  STDMETHODIMP CreateRangeBackup(TfEditCookie c,ITfRange* r,ITfRangeBackup** b) override { return context_->CreateRangeBackup(c,r,b); }
 private:
  ULONG refs_=1;
  Microsoft::WRL::ComPtr<ITfContext> context_;
};
