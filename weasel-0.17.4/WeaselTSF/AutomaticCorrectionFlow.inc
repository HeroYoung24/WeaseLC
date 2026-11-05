// Included in AutomaticCorrection.cpp: apartment-local adapter, no new writer.
void AutomaticCorrection::FlowReset() {
  flowQueue_.ResetForNewScope(); flowScheduler_.ResetForNewScope(); flowWork_.reset();
  SecureZeroMemory(flowRecords_.data(),sizeof(flowRecords_)); flowRecordCount_=0;
  flowActive_={}; flowInitialized_=false; flowVersion_=flowSerial_=0; flowLastView_.clear();flowShownCount_=0;
  flowEscBlocked_=false;flowEngaged_=false; if(!++flowEpoch_) ++flowEpoch_;
}
void AutomaticCorrection::FlowRevalidate() {
  const auto now=GetTickCount64();
  for(size_t i=0;i<flowRecordCount_;++i) {
    auto& record=flowRecords_[i]; auto& item=record.item;
    if(!flowQueue_.Contains(record.identity)) continue;
    if(item.start>text_.size() || item.originalUnits>text_.size()-item.start ||
        text_.compare(item.start,item.originalUnits,item.original,item.originalUnits)) {
      flowQueue_.Finish(record.identity,correction::input_flow::PendingHints::Outcome::Invalidated,now); continue;
    }
    correction::input_flow::PendingHint hint;
    hint.identity=record.identity; hint.session=flowEpoch_; hint.modelRevision=brokerRevision_?brokerRevision_:1;
    hint.sourceVersion=flowVersion_; hint.start=item.start; hint.originalUnits=item.originalUnits; hint.replacementUnits=item.replacementUnits;
    if(!flowQueue_.Revalidate(hint,now))
      flowQueue_.Finish(record.identity,correction::input_flow::PendingHints::Outcome::Invalidated,now);
  }
}
void AutomaticCorrection::FlowCapture() {
  const auto now=GetTickCount64(); const auto model=brokerRevision_?brokerRevision_:1;
  if(!flowInitialized_) {
    ++flowVersion_;
    correction::input_flow::HintTimingOptions timing{preferences_.automatic.minimumVisibleMs,preferences_.automatic.maximumAgeMs};
    if(!flowQueue_.Begin(flowEpoch_,flowEpoch_,model,flowVersion_,text_.size(),timing) ||
        !flowScheduler_.Begin(flowEpoch_,flowEpoch_,model,{preferences_.automatic.debounce,(std::max)(1000U,preferences_.automatic.debounce)})) {
      Cancel(); return;
    }
    flowInitialized_=true;
  } else {
    if(!flowQueue_.SourceChanged(++flowVersion_,text_.size(),now)) { Cancel(); return; }
    FlowRevalidate();
  }
  correction::CheckRequest request;
  request.session=flowEpoch_; request.version=flowVersion_; request.source=correction::Source::CommittedText;
  request.safeText=true; request.text=text_;
  const auto view=correction::input_flow::InferenceTarget(request);
  // A completed check's equivalent punctuation extension needs no new request.
  if(view!=flowLastView_ || !checked_ || flowScheduler_.HasInFlight() || flowScheduler_.HasPending()) {
    flowScheduler_.Submit(request,++flowSerial_,now); checked_=false;
  }
  flowLastView_=view;
  FlowPresent();
}
bool AutomaticCorrection::FlowReply(const correction::AutomaticReply& reply) {
  using Q=correction::input_flow::PendingHints;
  const auto now=GetTickCount64();
  // Verified adjacent commits may trigger a new check. A reply that omits the
  // hint already shown to the user must not truncate its display lifetime.
  for(size_t r=0;r<flowRecordCount_;++r) if(flowQueue_.Contains(flowRecords_[r].identity)) {
    const auto& old=flowRecords_[r].item; bool found=false;
    for(DWORD i=0;i<reply.count;++i) {
      const auto& item=reply.items[i];
      if(old.start==item.start && old.originalUnits==item.originalUnits && old.replacementUnits==item.replacementUnits &&
          !wcscmp(old.original,item.original) && !wcscmp(old.replacement,item.replacement)) {found=true;break;}
    }
    const auto& record = flowRecords_[r];
    const bool carryShown = record.identity == flowActive_ &&
        record.sourceVersion < flowVersion_ && flowQueue_.WasShown(record.identity);
    if (!found && !carryShown)
      flowQueue_.Finish(record.identity,Q::Outcome::Invalidated,now);
  }
  for(DWORD i=0;i<reply.count;++i) {
    const auto& item=reply.items[i]; size_t index=flowRecordCount_;
    for(size_t r=0;r<flowRecordCount_;++r) {
      const auto& old=flowRecords_[r].item;
      if(old.start==item.start && !wcscmp(old.original,item.original) && !wcscmp(old.replacement,item.replacement)) {index=r;break;}
    }
    if(index==flowRecordCount_) {
      if(flowRecordCount_==flowRecords_.size()) continue;
      flowRecords_[index].identity={flowEpoch_,index+1,1}; ++flowRecordCount_;
    }
    auto& record=flowRecords_[index]; record.item=item; record.sourceVersion=flowVersion_;
    correction::input_flow::PendingHint hint;
    hint.identity=record.identity; hint.session=flowEpoch_; hint.modelRevision=brokerRevision_?brokerRevision_:1;
    hint.sourceVersion=flowVersion_; hint.start=item.start; hint.originalUnits=item.originalUnits; hint.replacementUnits=item.replacementUnits;
    flowQueue_.Add(hint,now);
  }
  checked_=true; FlowPresent(); return true;
}
void AutomaticCorrection::FlowPresent() {
  if(flowWriting_ || !flowInitialized_) return;
  const auto selected=flowQueue_.Select(GetTickCount64(),!flowTab_.BlocksQueueAdvance() && !flowEscBlocked_);
  if(!selected) {ready_=false;flowActive_={};if(!flowQueue_.Size())flowEngaged_=false;Hide();return;}
  for(size_t i=0;i<flowRecordCount_;++i) if(flowRecords_[i].identity==selected->identity) {
    const bool same=ready_ && flowActive_==selected->identity;
    item_=flowRecords_[i].item; flowActive_=selected->identity; ready_=true;
    if(same && flowShownCount_==flowQueue_.Size()) return;
    flowShownCount_=flowQueue_.Size();
    exposure_={}; // Advanced queue does not manufacture legacy learning tickets.
    measuredDpi_=0; measuredSize_={};
    const bool zh=PRIMARYLANGID(GetThreadUILanguage())==LANG_CHINESE;
    label_=std::wstring(item_.original,item_.originalUnits)+L" → "+std::wstring(item_.replacement,item_.replacementUnits);
    label_+=preferences_.automatic.tab?(zh?L"\nTab 接受 · Esc 忽略":L"\nTab accepts · Esc dismisses"):(zh?L"\nTab 已关闭":L"\nTab disabled");
    if(flowQueue_.Size()>1) label_+=(zh?L" · 另有 ":L" · more: ")+std::to_wstring(flowQueue_.Size()-1);
    SetWindowTextW(window_,(std::wstring(correction::HintTitle(zh))+L": "+label_).c_str());
    if(!same) correction::AnnounceCorrectionHint(window_);
    InvalidateRect(window_,nullptr,TRUE);
    Report(correction::AutomaticStage::Ready); return;
  }
  Cancel();
}
void AutomaticCorrection::FlowComplete(bool success) {
  if(flowWork_) flowScheduler_.Complete(flowWork_->serial,false,GetTickCount64());
  flowWork_.reset();
  if(!success) {StopWorker();checked_=true;Report(correction::AutomaticStage::WorkerFailed);}
}
bool AutomaticCorrection::FlowRestore(ITfContext* context,const WriteVerification& pending) {
  if(diagnostic_.stage!=correction::AutomaticStage::TabApplied || !pending.source || !Focused(context)) return false;
  if(!flowQueue_.Finish(flowActive_,correction::input_flow::PendingHints::Outcome::Accepted,GetTickCount64())) return false;
  const LONG delta=static_cast<LONG>(pending.text.size())-static_cast<LONG>(pending.original.size());
  for(size_t i=0;i<flowRecordCount_;++i) {
    auto& item=flowRecords_[i].item;
    if(flowQueue_.Contains(flowRecords_[i].identity) && item.start>=pending.offset+pending.original.size())
      item.start=static_cast<DWORD>(static_cast<LONG>(item.start)+delta);
  }
  com_ptr<AutomaticStep> restore;
  restore.Attach(new AutomaticStep(owner_,context,[&](TfEditCookie cookie) {
    if(!Focused(context) || !owner_->_IsEditSessionCurrent(pending.generation) ||
        !SettingsCurrent() || !ReadExact(pending.source,cookie,pending.sourceText)) return S_FALSE;
    context_=context; range_=pending.source; text_=pending.sourceText; started_=GetTickCount64();
    if(range_->SetGravity(cookie,TF_GRAVITY_BACKWARD,TF_GRAVITY_BACKWARD)!=S_OK) return S_FALSE;
    expectedSelection_.Release();expectedPreedit_.clear();expectedComposing_=false;
    ready_=false;checked_=true;flowActive_={};
    if(!flowQueue_.SourceChanged(++flowVersion_,text_.size(),GetTickCount64())) return S_FALSE;
    FlowRevalidate(); RecordInput(context,cookie);
    if(!context_ || !Validate(cookie,context)) return S_FALSE;
    flowScheduler_.ResetForNewScope();flowWork_.reset();
    if(!flowScheduler_.Begin(flowEpoch_,flowEpoch_,brokerRevision_?brokerRevision_:1,
        {preferences_.automatic.debounce,(std::max)(1000U,preferences_.automatic.debounce)})) return E_FAIL;
    correction::CheckRequest latest;latest.source=correction::Source::CommittedText;latest.text=text_;
    flowLastView_=correction::input_flow::InferenceTarget(latest);
    return Arm()?S_OK:E_FAIL;
  }));
  HRESULT done=E_FAIL,requested=E_FAIL;
  try {requested=context->RequestEditSession(Client(),restore,TF_ES_SYNC|TF_ES_READWRITE,&done);}
  catch(...) {restore->Retire();throw;}
  restore->Retire(); return requested==S_OK && done==S_OK;
}
bool AutomaticCorrection::FlowKey(ITfContext* context,WPARAM key,LPARAM flags,bool test,bool up,BOOL* eaten) {
  using Gate=correction::input_flow::HintKeyCycle;
  auto& gate=key==VK_TAB?flowTab_:flowEsc_;
  auto& stroke=key==VK_TAB?flowTabStroke_:flowEscStroke_;
  if(up) {
    const auto result=gate.Release(stroke,test);
    if(!test || result==Gate::Decision::Pass) {stroke=0;if(result==Gate::Decision::Pass) gate.ResetAfterFocusLoss();}
    if(result==Gate::Decision::Pass) return false;
    *eaten=TRUE;return true;
  }
  const bool repeat=!!(flags&(1LL<<30));
  if(!stroke) stroke=++flowKeySerial_;
  const bool modifiers=!!((GetKeyState(VK_SHIFT)|GetKeyState(VK_CONTROL)|GetKeyState(VK_MENU)|GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000);
  bool eligible=!modifiers && !repeat && ready_ && window_ && IsWindowVisible(window_) && Focused(context) &&
      (key!=VK_TAB || preferences_.automatic.tab) && (key!=VK_ESCAPE || !owner_->_IsComposing());
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if(!testing_)
#endif
  if(eligible && (owner_->_status.ascii_mode || owner_->_status.disabled || owner_->_IsKeyboardDisabled() ||
      (owner_->_isToOpenClose && !owner_->_IsKeyboardOpen()))) eligible=false;
  if(eligible && (!Request(false,context) || !ready_ || !IsWindowVisible(window_))) eligible=false;
  const auto identity=flowActive_;
  const auto result=test?gate.Query(stroke,identity,eligible,repeat):
      gate.ActualWithOptionalQuery(stroke,identity,eligible,repeat);
  if(result==Gate::Decision::Pass) return false;
  *eaten=TRUE;
  if(result!=Gate::Decision::Attempt) return true;
  if(key==VK_ESCAPE) {
    flowQueue_.Finish(identity,correction::input_flow::PendingHints::Outcome::Rejected,GetTickCount64());
    ready_=false;flowActive_={};flowEscBlocked_=true;Hide();return true;
  }
  if(!flowQueue_.BeginWrite(identity,GetTickCount64())) return true;
  flowWriting_=true;
  bool applied=false;
  try {applied=Request(true,context);} catch(...) {flowWriting_=false;Cancel();throw;}
  flowWriting_=false;
  if(!applied || flowQueue_.WritePending()) {Cancel();return true;}
  return true;
}
