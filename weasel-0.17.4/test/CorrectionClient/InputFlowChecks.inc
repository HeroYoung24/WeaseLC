// Deterministic policy checks: no actual TIP activation, model or registry.
#include <CorrectionHintLifetime.h>
#include <CorrectionHintKeyCycle.h>
#include <CorrectionPendingHints.h>
#include <CorrectionLatestCheck.h>

void RunInputFlowChecks() {
  using namespace correction::input_flow;
  const HintIdentity a{1,1,1}, b{1,2,1};
  Check(ValidHintTiming({3000,15000}) && !ValidHintTiming({0,15000}) &&
      !ValidHintTiming({10001,15000}) && !ValidHintTiming({6000,5000}), "flow timing bounds");
  Check(TimingReference(InputConfirmationStyle::ShortPhrases).minimumVisibleMs == 3000 &&
      TimingReference(InputConfirmationStyle::LongPassages).minimumVisibleMs == 5000, "flow manual strategy references");
  HintLifetime clock;
  Check(clock.Begin(0,{}) && !clock.Begin(10,{}), "flow zero tick and no lifetime renewal");
  Check(clock.SetVisible(true,1000) && clock.SetVisible(false,2000) && clock.VisibleMs(6000)==1000,
      "flow hidden time is not visible time");
  Check(clock.SetVisible(true,6000) && clock.MinimumSatisfied(8000) && !clock.Expired(14999) && clock.Expired(15000),
      "flow absolute expiry includes queued and hidden time");
  HintLifetime late; late.Begin(0,{});
  Check(!late.CanReveal(12001) && late.CanReveal(12000), "flow no near-expiry flash");
  late.End(100); Check(!late.Begin(200,{}) && late.Expired(200), "flow explicit end cannot restart");
  HintLifetime backwards; backwards.Begin(1000,{});
  Check(!backwards.SetVisible(true,999) && backwards.Expired(1001), "flow backwards time ends the hint");

  using D=HintKeyCycle::Decision;
  HintKeyCycle key;
  Check(key.Query(1,a,false,false)==D::Pass && key.ActualWithOptionalQuery(1,a,true,false)==D::Pass,
      "flow hint appearing after declined query cannot steal Tab");
  key.Release(1,false);
  Check(key.Query(2,a,true,false)==D::Consume && key.Actual(2,b,true,false)==D::Consume,
      "flow changed hint does not acquire old key offer");
  key.Release(2,false);
  Check(key.Query(3,a,true,false)==D::Consume && key.ActualWithOptionalQuery(3,a,true,false)==D::Attempt,
      "flow exactly one actual action after query");
  for(unsigned i=0;i<32;++i)
    Check(key.Query(3,b,true,true)==D::Consume && key.Actual(3,b,true,true)==D::Consume,
        "flow autorepeat never accepts next suggestion");
  key.InvalidateOffer();
  Check(key.Release(3,true)==D::Consume && key.BlocksQueueAdvance(), "flow query key-up cannot release latch");
  Check(key.Release(3,false)==D::Consume && !key.BlocksQueueAdvance(), "flow actual key-up releases latch");
  Check(key.ActualWithOptionalQuery(4,b,true,false)==D::Attempt &&
      key.ActualWithOptionalQuery(4,b,true,true)==D::Consume,
      "flow actual-only route attempts only once");
  key.Release(4,false);
  Check(key.Actual(5,a,true,false)==D::Pass, "flow unmatched actual cannot create an offer");

  auto proposal = [](unsigned id,unsigned start,std::uint64_t version=1) {
    PendingHint p; p.identity={1,id,1}; p.session=1; p.modelRevision=1; p.sourceVersion=version;
    p.start=start; p.originalUnits=p.replacementUnits=1; return p;
  };
  using A=PendingHints::Admission; using O=PendingHints::Outcome;
  PendingHints queue; Check(queue.Begin(1,1,1,1,100,{}),"flow queue begins with bounded source");
  for(unsigned i=0;i<5;++i) Check(queue.Add(proposal(i+1,10+i*10),0)==A::Added,"flow queue capacity accepts five");
  Check(queue.Add(proposal(6,90),100)==A::Full && queue.Size()==5,"flow sixth late item does not grow queue");
  auto first=queue.Select(100,true);
  Check(first && first->identity==a && queue.Visibility(a,true,100),"flow oldest position selected and shown");
  Check(queue.Add(proposal(7,1),100)==A::ReplacedLater && queue.Select(100,true)->identity==a,
      "flow new earlier item cannot preempt current hint");
  Check(queue.Add(proposal(5,50),100)==A::Processed,"flow capacity eviction cannot renew TTL");
  Check(queue.Add(proposal(1,10),10000)==A::Duplicate,"flow duplicate does not renew creation time");
  Check(queue.Finish(a,O::Rejected,10000) && queue.Add(proposal(1,10),10001)==A::Processed,
      "flow rejected item does not immediately return");
  Check(!queue.Select(10001,false),"flow key gate blocks promotion");
  auto second=queue.Select(10001,true);
  Check(second && second->identity.occurrence==7,"flow next fresh key opportunity uses position order");
  Check(!queue.Finish(second->identity,O::Accepted,10001),"flow accepted requires an explicit write barrier");
  Check(queue.BeginWrite(second->identity,10001) && queue.WritePending(),"flow multi-lock write barrier begins");
  queue.Sweep(16000);
  Check(queue.Size()==4 && !queue.Select(16000,true) && queue.Add(proposal(8,60),16000)==A::Busy,
      "flow expiry and new admissions cannot race pending write");
  Check(queue.Finish(second->identity,O::Accepted,16000) && !queue.WritePending(),"flow confirmed write ends barrier");
  Check(!queue.Select(16000,true),"flow expired or unvalidated remainder cannot advance");

  PendingHints change; change.Begin(1,1,1,1,100,{}); change.Add(proposal(1,10),0); change.Add(proposal(2,20),0);
  Check(change.Select(100,true).has_value() && change.SourceChanged(2,101,200) && !change.Select(200,true),
      "flow source change quarantines all old positions");
  Check(!change.Revalidate(proposal(1,10,1),200) && change.Revalidate(proposal(1,11,2),200),
      "flow explicit new source validation required");
  Check(change.Select(200,true)->start==11,"flow verified coordinate update is used");
  Check(change.Add(proposal(3,11,2),200)==A::Conflict,"flow overlapping proposals are not separately applied");
  PendingHints expiry; expiry.Begin(1,1,1,1,100,{}); expiry.Add(proposal(1,10),0);
  Check(!expiry.Select(12001,true) && expiry.Size()==0 && expiry.Add(proposal(1,10),12002)==A::Processed,
      "flow insufficient remaining display time retires suggestion");
  PendingHints saturated; saturated.Begin(1,1,1,1,100,{});
  for(unsigned i=1;i<=64;++i) {
    const auto p=proposal(i,0);
    Check(saturated.Add(p,0)==A::Added && saturated.Finish(p.identity,O::Invalidated,0),"flow bounded processed identities");
  }
  Check(saturated.AdmissionClosed() && saturated.Add(proposal(65,0),0)==A::Closed,
      "flow processed capacity closes admission without reviving rejection");

  CheckRequest r; r.session=1; r.version=1; r.safeText=true; r.source=Source::CommittedText; r.text=L"这里有错子，";
  auto more=r; more.version=2; more.text=L"这里有错子，，，，，";
  Check(EquivalentAutomaticInputs(r,1,1,more,1,1),"flow repeated trailing comma has same model view");
  auto mixed=more; mixed.text=L"这里有错子，，。";
  Check(!EquivalentAutomaticInputs(r,1,1,mixed,1,1) && !EquivalentAutomaticInputs(r,1,1,more,2,1) &&
      !EquivalentAutomaticInputs(r,1,1,more,1,2),"flow boundary and scope/config changes remain distinct");
  auto manual=more; manual.source=Source::Manual;
  Check(InferenceTarget(manual)==manual.text,"flow manual text is never normalized");
  auto all=more; all.text=L"，。，。"; Check(InferenceTarget(all)==all.text,"flow punctuation-only source unchanged");
  auto nearby=more; nearby.before=L"新语境";
  Check(!EquivalentAutomaticInputs(r,1,1,nearby,1,1),"flow surrounding context participates in equivalence");

  using S=LatestCheck::Submission;
  LatestCheck scheduler; Check(scheduler.Begin(1,1,1,{}),"flow scheduler begins");
  Check(scheduler.Submit(r,1,0)==S::Latest && scheduler.Submit(more,2,200)==S::EquivalentPending && !scheduler.Take(249),
      "flow equivalent punctuation does not restart debounce");
  const auto job=scheduler.Take(250); Check(job && job->serial==2 && scheduler.HasInFlight(),"flow only latest payload is dispatched");
  more.version=3; Check(scheduler.Submit(more,3,300)==S::EquivalentInFlight && !scheduler.Take(500),
      "flow duplicate while busy is one remap subscriber, not another model call");
  Check(!scheduler.Complete(1,true,500).matched && scheduler.HasInFlight(),"flow old work completion cannot finish current job");
  const auto done=scheduler.Complete(2,true,600);
  Check(done.matched && done.remapRecipient && done.remapRecipient->snapshot.version==3 && !scheduler.HasPending(),
      "flow successful raw response offers latest snapshot for strict remapping");
  LatestCheck busy; busy.Begin(1,1,1,{});
  for(unsigned i=0;i<=10;++i) { auto v=r; v.version=i+1; v.text+=std::to_wstring(i); busy.Submit(v,i+1,i*90); }
  Check(!busy.Take(999) && busy.Take(1000).has_value(),"flow no-pause typing has a finite scheduling opportunity");
  Check(!busy.Complete(11,false,1001).remapRecipient && !busy.HasPending(),"flow failure inserts no retry or no-change answer");
  LatestCheck backwardsWork; backwardsWork.Begin(1,1,1,{}); backwardsWork.Submit(r,1,1000);
  Check(!backwardsWork.Take(999) && backwardsWork.Closed(),"flow clock regression closes scheduler");
}
