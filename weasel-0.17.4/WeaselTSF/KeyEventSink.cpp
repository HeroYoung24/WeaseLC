#include "stdafx.h"
#include "WeaselIPC.h"
#include "WeaselTSF.h"
#include <KeyEvent.h>
#include "CandidateList.h"
#include <CorrectionPipelineDiagnostics.h>

static weasel::KeyEvent prevKeyEvent;
static BOOL prevfEaten = FALSE;
static int keyCountToSimulate = 0;

void WeaselTSF::_ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (_probeKeySink) { _probeKeySink(wParam, lParam, pfEaten, _probeKeySinkContext); return; }
#endif
  // when _IsKeyboardDisabled don't eat the key,
  // when keyboard closable and keyboard closed, don't eat the key
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled()) {
    *pfEaten = FALSE;
    return;
  }

  // if server connection is Not OK, don't eat it.
  if (!_EnsureServerConnected()) {
    *pfEaten = FALSE;
    return;
  }
  weasel::KeyEvent ke;
  GetKeyboardState(_lpbKeyState);
  if (!ConvertKeyEvent(static_cast<UINT>(wParam), lParam, _lpbKeyState, ke)) {
    /* Unknown key event */
    *pfEaten = FALSE;
  } else {
    // cheet key code when vertical auto reverse happened, swap up and down
    if (_cand->GetIsReposition()) {
      if (ke.keycode == ibus::Up)
        ke.keycode = ibus::Down;
      else if (ke.keycode == ibus::Down)
        ke.keycode = ibus::Up;
    }
    if (!keyCountToSimulate)
      *pfEaten = (BOOL)m_client.ProcessKeyEvent(ke);

    if (ke.keycode == ibus::Caps_Lock) {
      if (prevKeyEvent.keycode == ibus::Caps_Lock && prevfEaten == TRUE &&
          (ke.mask & ibus::RELEASE_MASK) && (!keyCountToSimulate)) {
        if ((GetKeyState(VK_CAPITAL) & 0x01)) {
          if (_committed || (!*pfEaten && _status.composing)) {
            keyCountToSimulate = 2;
            INPUT inputs[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki = {VK_CAPITAL, 0, 0, 0, 0};
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki = {VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0, 0};
            ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
          }
        }
        *pfEaten = TRUE;
      }
      if (keyCountToSimulate)
        keyCountToSimulate--;
    }

    prevfEaten = *pfEaten;
    prevKeyEvent = ke;
  }
}

STDAPI WeaselTSF::OnSetFocus(BOOL fForeground) {
  com_ptr<WeaselTSF> keepAlive = this;
  if (fForeground) {
    if (_pThreadMgr) m_client.FocusIn();
  }
  else {
    if (_pThreadMgr) m_client.FocusOut();
    _RetireComposition();
  }

  return S_OK;
}

// Query and actual callbacks may be duplicated, omitted or interleaved. Reuse
// a processed query only for this exact key message and context.
HRESULT WeaselTSF::_HandleKey(ITfContext* context, WPARAM key, LPARAM flags,
                               BOOL* eaten, bool test, bool up) {
  com_ptr<WeaselTSF> keepAlive = this;
  if (!eaten) return E_POINTER;
  *eaten = FALSE;
  // Capture the host message identity BEFORE any callback/IPC/edit work can
  // pump another Windows message. The paired actual callback is still for
  // this original message, not the most recently pumped one.
  const LONG messageTime = _KeyMessageTime();
#ifdef WEASELC_LOCAL_TEST_BUILD
  // Prove entry independently of Capture. Never record the key or modifiers.
  static thread_local ULONGLONG nextDiagnostic = 0;
  const auto now = GetTickCount64();
  if (now >= nextDiagnostic) {
    correction::FrontendDiagnostic entered;
    entered.stage = 1; entered.flags = (test ? 1 : 0) | (up ? 2 : 0) | (context ? 4 : 0);
    correction::SaveFrontendDiagnostic(L"LastKey", entered);
    nextDiagnostic = now + 1000;
  }
#endif
  if (_automatic->Key(context, key, flags, test, up, eaten)) {
    _pendingKeyDown.Reset(); _pendingKeyUp.Reset();
    return S_OK;
  }
  auto& pending = up ? _pendingKeyUp : _pendingKeyDown;
  (up ? _pendingKeyDown : _pendingKeyUp).Reset();
  if (pending.Matches(context, key, flags, messageTime)) {
    if (!test) pending.Reset();
    *eaten = TRUE;
    return S_OK;
  }
  pending.Reset();
  // A new document must retire the old Rime composition before dispatching
  // this key, rather than clearing the just-processed response afterwards.
  _BindEditContext(context);
  const auto generation = _GetEditGeneration();
  _ProcessKeyEvent(key, flags, eaten);
  _UpdateComposition(context);
  if (test && *eaten && _IsEditSessionCurrent(generation))
    pending.Set(context, key, flags, messageTime);
  return S_OK;
}

STDAPI WeaselTSF::OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM flags, BOOL* eaten) {
  return _HandleKey(context, key, flags, eaten, true, false);
}
STDAPI WeaselTSF::OnKeyDown(ITfContext* context, WPARAM key, LPARAM flags, BOOL* eaten) {
  return _HandleKey(context, key, flags, eaten, false, false);
}
STDAPI WeaselTSF::OnTestKeyUp(ITfContext* context, WPARAM key, LPARAM flags, BOOL* eaten) {
  return _HandleKey(context, key, flags, eaten, true, true);
}
STDAPI WeaselTSF::OnKeyUp(ITfContext* context, WPARAM key, LPARAM flags, BOOL* eaten) {
  return _HandleKey(context, key, flags, eaten, false, true);
}

STDAPI WeaselTSF::OnPreservedKey(ITfContext* pContext,
                                 REFGUID rguid,
                                 BOOL* pfEaten) {
  *pfEaten = FALSE;
  return S_OK;
}

#ifdef WEASEL_CORRECTION_PROBE_BUILD
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeSetKeyMessageTime(
    ITfTextInputProcessorEx* service, LONG value) {
  if (!service) return E_POINTER;
  static_cast<WeaselTSF*>(service)->ProbeMessageTime(value); return S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeSetKeySink(
    ITfTextInputProcessorEx* service, void(WINAPI* sink)(WPARAM, LPARAM, BOOL*, void*), void* state) {
  if (!service) return E_POINTER;
  auto* owner = static_cast<WeaselTSF*>(service);
  owner->ProbeSetKeySink(sink, state);
  return S_OK;
}
#endif

BOOL WeaselTSF::_InitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  HRESULT hr;

  hr = _pThreadMgr->QueryInterface(&pKeystrokeMgr);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(hr);
#endif
  if (hr != S_OK)
    return FALSE;

  hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this,
                                         TRUE);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(hr);
#endif

  _keyEventSinkAdvised = (hr == S_OK);
  return _keyEventSinkAdvised;
}

void WeaselTSF::_UninitKeyEventSink() {
  if (!_keyEventSinkAdvised || !_pThreadMgr)
    return;
  _keyEventSinkAdvised = false;

  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return;

  pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);
}

BOOL WeaselTSF::_InitPreservedKey() {
  return TRUE;
#if 0
	com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
	if (_pThreadMgr->QueryInterface(pKeystrokeMgr.GetAddressOf()) != S_OK)
	{
		return FALSE;
	}
	TF_PRESERVEDKEY preservedKeyImeMode;

	/* Define SHIFT ONLY for now */
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = TF_MOD_ON_KEYUP;

	auto hr = pKeystrokeMgr->PreserveKey(
		_tfClientId,
		GUID_IME_MODE_PRESERVED_KEY,
		&preservedKeyImeMode, L"", 0);
	
	return SUCCEEDED(hr);
#endif
}

void WeaselTSF::_UninitPreservedKey() {}
