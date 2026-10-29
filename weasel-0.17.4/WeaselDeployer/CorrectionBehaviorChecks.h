#pragma once
#include <CorrectionBehavior.h>
#include <functional>

inline void CheckCorrectionBehavior(const std::function<void(bool, const char*)>& check) {
  using namespace correction;
  AutomaticReply::Item item; item.start = 3; item.originalUnits = item.replacementUnits = 2;
  memcpy(item.original, L"错子", 2 * sizeof(wchar_t)); memcpy(item.replacement, L"错字", 2 * sizeof(wchar_t));
  BehaviorDigest identity{}, same{}, other{};
  check(BehaviorIdentity(L"这里有错子。", item, identity) && BehaviorIdentity(L"这里有错子，，，", item, same) && identity == same,
      "behavior identity does not fragment on trailing punctuation");
  check(BehaviorIdentity(L"此处用错子。", item, other) && identity != other, "different local context cannot inherit a fixed expression decision");
  item.originalUnits = 33;
  check(!BehaviorIdentity(L"这里有错子。", item, same), "behavior identity rejects oversized or stale source"); item.originalUnits = 2;
  BehaviorHistory accepted, ignored, rejected;
  const DWORD now = 1700000000;
  check(UpdateBehavior(accepted, identity, L"model-A", BehaviorOutcome::Accepted, now) &&
      UpdateBehavior(ignored, identity, L"model-A", BehaviorOutcome::Ignored, now) &&
      UpdateBehavior(rejected, identity, L"model-A", BehaviorOutcome::Rejected, now), "behavior outcomes update separate bounded counters");
  check(BehaviorBias(accepted.entries[0]) > BehaviorBias(ignored.entries[0]) && BehaviorBias(ignored.entries[0]) > BehaviorBias(rejected.entries[0]),
      "accepted ranks above ignored above rejected");
  check(!SuppressBehavior(rejected.entries[0], now), "one rejection cannot silence a suggestion");
  UpdateBehavior(rejected, identity, L"model-A", BehaviorOutcome::Rejected, now);
  UpdateBehavior(rejected, identity, L"model-A", BehaviorOutcome::Rejected, now);
  check(SuppressBehavior(rejected.entries[0], now) && !SuppressBehavior(rejected.entries[0], now + 86400),
      "repeated explicit rejection suppresses temporarily rather than creating a permanent dictionary entry");
  check(!FindBehavior(rejected, identity, L"model-B") && !FindBehavior(rejected, other, L"model-A"),
      "model and context identities isolate preference history");
  UpdateBehavior(rejected, identity, L"model-A", BehaviorOutcome::Accepted, now);
  check(!SuppressBehavior(rejected.entries[0], now), "a successful acceptance can recover from negative preference");
  for (unsigned i = 0; i < 160; ++i) UpdateBehavior(ignored, identity, L"model-A", BehaviorOutcome::Ignored, now);
  check(BehaviorBias(ignored.entries[0]) == 0 && !SuppressBehavior(ignored.entries[0], now),
      "ignoring alone neither proves correctness nor silently whitelists a typo");
  const auto& saturated = ignored.entries[0];
  check(saturated.accepted + saturated.ignored + saturated.rejected <= 64, "recent counter window prevents unbounded or permanent learning");
  accepted.header.enabled = 0;
  check(!UpdateBehavior(accepted, identity, L"model-A", BehaviorOutcome::Rejected, now) && !FindBehavior(accepted, identity, L"model-A"),
      "disabled personalization neither records nor changes hint ranking");
  BehaviorExposure exposure; exposure.shown = 100;
  check(!exposure.VisibleLongEnough(1599) && exposure.VisibleLongEnough(1600), "weak ignore requires minimum actual visible dwell");
  exposure.resolved = true; check(!exposure.VisibleLongEnough(2000), "resolved suggestion cannot train again");
  BehaviorHistory bounded;
  for (unsigned i = 0; i < 150; ++i) UpdateBehavior(bounded, identity, std::to_wstring(i), BehaviorOutcome::Accepted, now + i);
  check(bounded.entries.size() == kMaxBehaviorEntries && !FindBehavior(bounded, identity, L"0"), "history evicts old groups at a strict capacity");
  const auto path = L"Software\\WeaseLC-Behavior-Check-" + std::to_wstring(GetCurrentProcessId());
  HKEY existing = nullptr;
  const bool absent = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &existing) == ERROR_FILE_NOT_FOUND;
  if (existing) RegCloseKey(existing);
  HKEY key = nullptr;
  const bool opened = absent && RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) == ERROR_SUCCESS;
  check(opened, "behavior persistence test owns a separate temporary key");
  if (opened) {
    BehaviorHistory restored;
    check(WriteBehavior(key, bounded) && ReadBehavior(key, restored) && restored.entries.size() == 128 &&
        restored.header.salt == bounded.header.salt, "bounded salted counters survive persistence without storing text");
    DWORD size = 0; RegGetValueW(key, nullptr, L"BehaviorHistoryV1", RRF_RT_REG_BINARY, nullptr, nullptr, &size);
    std::vector<BYTE> bytes(size); RegGetValueW(key, nullptr, L"BehaviorHistoryV1", RRF_RT_REG_BINARY, nullptr, bytes.data(), &size);
    check(size <= 8192 && std::search(bytes.begin(), bytes.end(), reinterpret_cast<const BYTE*>(L"错子"),
        reinterpret_cast<const BYTE*>(L"错子") + 2 * sizeof(wchar_t)) == bytes.end(), "persisted history is bounded and contains no original expression");
    bytes.push_back(0); check(!DecodeBehavior(bytes, restored), "history decoder rejects trailing bytes"); bytes.pop_back();
    auto* header = reinterpret_cast<BehaviorHeader*>(bytes.data()); header->count = 129;
    check(!DecodeBehavior(bytes, restored), "history decoder rejects oversized entry count");
    header->count = 128; header->enabled = 2;
    check(!DecodeBehavior(bytes, restored), "history decoder rejects invalid learning flag");
    RegCloseKey(key); RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
  }
}
