#pragma once
#include <windows.h>
#include <msctf.h>
#include <oleacc.h>
#include <atlcomcli.h>
#include "CorrectionFocus.h"
#pragma comment(lib, "oleacc.lib")

namespace correction {
enum WordDocumentEvidence : DWORD {
  WordRange = 1, WordView = 2, WordPane = 4, WordObject = 8,
  WordDocument = 16, WordWritable = 32, WordUnprotected = 64,
  WordUnrestricted = 128, WordStableFocus = 256, WordVerified = 511
};
// Office's native object model is used ONLY for these closed metadata getters.
// No Application/ActiveDocument lookup, text, selection, names, files, setters,
// macros or Office activation. The object must belong to the focused pane.
inline bool DocumentMetadata(IDispatch* object, LPCOLESTR name, VARIANT& value) {
  VariantInit(&value);
  if (!object) return false;
  DISPID id = DISPID_UNKNOWN;
  auto member = const_cast<LPOLESTR>(name);
  if (object->GetIDsOfNames(IID_NULL, &member, 1, LOCALE_USER_DEFAULT, &id) != S_OK) return false;
  DISPPARAMS arguments{};
  return object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
                        &arguments, &value, nullptr, nullptr) == S_OK;
}
inline DWORD WordMetadataEvidence(IDispatch* window) {
  DWORD evidence = 0;
  CComVariant document, readOnly, protection, permission, restricted;
  if (!DocumentMetadata(window, L"Document", document) || document.vt != VT_DISPATCH || !document.pdispVal)
    return evidence;
  evidence |= WordDocument;
  if (!DocumentMetadata(document.pdispVal, L"ReadOnly", readOnly) || readOnly.vt != VT_BOOL ||
      readOnly.boolVal != VARIANT_FALSE) return evidence;
  evidence |= WordWritable;
  if (!DocumentMetadata(document.pdispVal, L"ProtectionType", protection) ||
      protection.vt != VT_I4 || protection.lVal != -1) return evidence;  // wdNoProtection
  evidence |= WordUnprotected;
  if (DocumentMetadata(document.pdispVal, L"Permission", permission) && permission.vt == VT_DISPATCH &&
      permission.pdispVal && DocumentMetadata(permission.pdispVal, L"Enabled", restricted) &&
      restricted.vt == VT_BOOL && restricted.boolVal == VARIANT_FALSE) evidence |= WordUnrestricted;
  return evidence;
}
inline bool EditableWordDocument(IDispatch* window) {
  return WordMetadataEvidence(window) == (WordDocument | WordWritable | WordUnprotected | WordUnrestricted);
}
inline bool WordDocumentWindow(HWND view, HWND focus) {
  if (!view || !focus || !IsWindow(view) || !IsWindow(focus) ||
      (view != focus && !IsChild(view, focus)) || !IsWindowEnabled(focus)) return false;
  DWORD process = 0, viewProcess = 0;
  const auto thread = GetWindowThreadProcessId(focus, &process);
  GetWindowThreadProcessId(view, &viewProcess);
  if (process != GetCurrentProcessId() || viewProcess != process || thread != GetCurrentThreadId()) return false;
  WCHAR name[64]{}, image[MAX_PATH]{};
  if (!GetClassNameW(focus, name, _countof(name)) || _wcsicmp(name, L"_WwG") ||
      (GetWindowLongPtrW(focus, GWL_STYLE) & ES_PASSWORD)) return false;
  const auto size = GetModuleFileNameW(nullptr, image, _countof(image));
  if (!size || size >= _countof(image)) return false;
  const auto base = wcsrchr(image, L'\\');
  return base && !_wcsicmp(base + 1, L"WINWORD.EXE");
}
inline DWORD WordDocumentRangeEvidence(ITfContext* context, ITfRange* range) {
  // Office getters may pump messages. Nested metadata inspection must fail
  // closed, rather than recursively entering the same native object model.
  static thread_local bool reading = false;
  if (reading) return 0;
  reading = true;
  struct Guard { bool& flag; ~Guard() { flag = false; } } guard{reading};
  CComPtr<ITfContext> owner;
  CComPtr<ITfContextView> view;
  HWND window = nullptr;
  DWORD evidence = 0;
  if (!context || !range || range->GetContext(&owner) != S_OK || !SameIdentity(owner, context)) return evidence;
  evidence |= WordRange;
  if (context->GetActiveView(&view) != S_OK || !view || view->GetWnd(&window) != S_OK) return evidence;
  evidence |= WordView;
  const auto focus = GetFocus();
  if (!WordDocumentWindow(window, focus)) return evidence;
  evidence |= WordPane;
  CComPtr<IDispatch> documentWindow;
  if (AccessibleObjectFromWindow(focus, static_cast<DWORD>(OBJID_NATIVEOM), IID_IDispatch,
      reinterpret_cast<void**>(&documentWindow)) != S_OK || !documentWindow) return evidence;
  evidence |= WordObject;
  evidence |= WordMetadataEvidence(documentWindow);
  // Getters can reenter Office. Recheck the exact focused window afterwards.
  if (GetFocus() == focus && WordDocumentWindow(window, focus)) evidence |= WordStableFocus;
  return evidence;
}
using DocumentScopeVerifier = DWORD(*)(ITfContext*, ITfRange*);
}  // namespace correction
