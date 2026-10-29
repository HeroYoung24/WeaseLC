#pragma once
#include "CorrectionAutomatic.h"
#include "CorrectionProbe.h"
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <string>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace correction {
using BehaviorDigest = std::array<BYTE, 32>;
enum class BehaviorOutcome : DWORD { Accepted = 1, Ignored, Rejected };
inline bool HashBehavior(const void* data, size_t size, BehaviorDigest& digest) {
  BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
  if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0 &&
      BCryptHashData(hash, const_cast<BYTE*>(static_cast<const BYTE*>(data)), static_cast<ULONG>(size), 0) >= 0 &&
      BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  return ok;
}
// The transient identity includes the proposed pair and up to eight preceding
// units. It contains no document/app identity and is never saved unsalted.
inline bool BehaviorIdentity(const std::wstring& text, const AutomaticReply::Item& item, BehaviorDigest& digest) {
  if (!ValidAutomaticText(item.original, item.originalUnits, 32) ||
      !ValidAutomaticText(item.replacement, item.replacementUnits, 32) || item.start > text.size() ||
      item.originalUnits > text.size() - item.start || text.compare(item.start, item.originalUnits, item.original, item.originalUnits)) return false;
  const size_t before = (std::min)(size_t(8), size_t(item.start));
  std::wstring key = text.substr(item.start - before, before);
  key.push_back(0); key.append(item.original, item.originalUnits); key.push_back(0);
  key.append(item.replacement, item.replacementUnits);
  const bool ok = HashBehavior(key.data(), key.size() * sizeof(wchar_t), digest);
  SecureZeroMemory(key.data(), key.size() * sizeof(wchar_t)); return ok;
}
inline DWORD BehaviorNow() {
  FILETIME time{}; GetSystemTimeAsFileTime(&time); ULARGE_INTEGER value{};
  value.LowPart = time.dwLowDateTime; value.HighPart = time.dwHighDateTime;
  return static_cast<DWORD>(value.QuadPart / 10000000ULL - 11644473600ULL);
}
struct BehaviorExposure {
  BehaviorDigest digest{};
  ULONGLONG request = 0, revision = 0, shown = 0;
  bool resolved = false;
  bool VisibleLongEnough(ULONGLONG now) const { return !resolved && shown && now >= shown && now - shown >= 1500; }
};
#pragma pack(push, 1)
struct BehaviorEntry {
  BehaviorDigest key{};
  DWORD accepted = 0, ignored = 0, rejected = 0, updated = 0;
};
struct BehaviorHeader {
  DWORD magic = 0x31484243, version = 1, enabled = 1, count = 0;
  BehaviorDigest salt{};
};
struct BehaviorNotice {
  BehaviorDigest digest{};
  BehaviorOutcome outcome = BehaviorOutcome::Ignored;
  DWORD version = 1;
};
#pragma pack(pop)
static_assert(sizeof(BehaviorNotice) == 40, "bounded feedback metadata");
constexpr size_t kMaxBehaviorEntries = 128;
struct BehaviorHistory { BehaviorHeader header; std::vector<BehaviorEntry> entries; };
inline bool EmptyBehaviorDigest(const BehaviorDigest& digest) {
  return std::all_of(digest.begin(), digest.end(), [](BYTE byte) { return byte == 0; });
}
inline bool DecodeBehavior(const std::vector<BYTE>& bytes, BehaviorHistory& history) {
  if (bytes.size() < sizeof(BehaviorHeader) || bytes.size() > sizeof(BehaviorHeader) + kMaxBehaviorEntries * sizeof(BehaviorEntry)) return false;
  BehaviorHistory decoded; memcpy(&decoded.header, bytes.data(), sizeof(decoded.header));
  if (decoded.header.magic != 0x31484243 || decoded.header.version != 1 || decoded.header.enabled > 1 ||
      decoded.header.count > kMaxBehaviorEntries || EmptyBehaviorDigest(decoded.header.salt) ||
      bytes.size() != sizeof(BehaviorHeader) + decoded.header.count * sizeof(BehaviorEntry)) return false;
  decoded.entries.resize(decoded.header.count);
  if (!decoded.entries.empty()) memcpy(decoded.entries.data(), bytes.data() + sizeof(BehaviorHeader), decoded.entries.size() * sizeof(BehaviorEntry));
  for (size_t i = 0; i < decoded.entries.size(); ++i) {
    const auto& entry = decoded.entries[i];
    if (EmptyBehaviorDigest(entry.key) || entry.accepted > 64 || entry.ignored > 64 || entry.rejected > 64 ||
        entry.accepted + entry.ignored + entry.rejected > 64 || !entry.updated) return false;
    for (size_t j = 0; j < i; ++j) if (entry.key == decoded.entries[j].key) return false;
  }
  history = std::move(decoded); return true;
}
inline bool ReadBehavior(HKEY key, BehaviorHistory& history) {
  DWORD size = 0; auto status = RegGetValueW(key, nullptr, L"BehaviorHistoryV1", RRF_RT_REG_BINARY, nullptr, nullptr, &size);
  if (status == ERROR_FILE_NOT_FOUND) { history = {}; return true; }
  if (status != ERROR_SUCCESS || size < sizeof(BehaviorHeader) || size > sizeof(BehaviorHeader) + kMaxBehaviorEntries * sizeof(BehaviorEntry)) return false;
  std::vector<BYTE> bytes(size); const auto expected = size;
  return RegGetValueW(key, nullptr, L"BehaviorHistoryV1", RRF_RT_REG_BINARY, nullptr, bytes.data(), &size) == ERROR_SUCCESS &&
      size == expected && DecodeBehavior(bytes, history);
}
inline bool WriteBehavior(HKEY key, BehaviorHistory history) {
  if (EmptyBehaviorDigest(history.header.salt) && BCryptGenRandom(nullptr, history.header.salt.data(), 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
  history.header.count = static_cast<DWORD>(history.entries.size());
  std::vector<BYTE> bytes(sizeof(BehaviorHeader) + history.entries.size() * sizeof(BehaviorEntry));
  memcpy(bytes.data(), &history.header, sizeof(history.header));
  if (!history.entries.empty()) memcpy(bytes.data() + sizeof(history.header), history.entries.data(), history.entries.size() * sizeof(BehaviorEntry));
  BehaviorHistory verified;
  return DecodeBehavior(bytes, verified) && RegSetValueExW(key, L"BehaviorHistoryV1", 0, REG_BINARY, bytes.data(), static_cast<DWORD>(bytes.size())) == ERROR_SUCCESS;
}
// All writers (controller and management window) use the same bounded mutex.
// Never take this lock from the input thread: it sends one-way metadata only.
class BehaviorStoreLock {
 public:
  explicit BehaviorStoreLock(const std::wstring& path) {
    BehaviorDigest digest{}; if (!HashBehavior(path.data(), path.size() * sizeof(wchar_t), digest)) return;
    std::wstring name = L"Local\\WeaseLC.Behavior.";
    for (BYTE byte : digest) { name += L"0123456789abcdef"[byte >> 4]; name += L"0123456789abcdef"[byte & 15]; }
    handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (handle_) { const auto status = WaitForSingleObject(handle_, 100); held_ = status == WAIT_OBJECT_0 || status == WAIT_ABANDONED; }
  }
  ~BehaviorStoreLock() { if (held_) ReleaseMutex(handle_); if (handle_) CloseHandle(handle_); }
  explicit operator bool() const { return held_; }
 private: HANDLE handle_ = nullptr; bool held_ = false;
};
inline bool LoadBehaviorHistory(BehaviorHistory& history, const std::wstring& root = kRegistryKey) {
  HKEY key = nullptr; const auto path = root + L"\\Diagnostics";
  const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key);
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) { history = {}; return true; }
  if (status != ERROR_SUCCESS) return false;
  const bool ok = ReadBehavior(key, history); RegCloseKey(key); return ok;
}
inline bool SaveBehaviorOption(bool enabled, bool clear) {
  BehaviorStoreLock lock(kRegistryKey); if (!lock) return false;
  BehaviorHistory history;
  if (!LoadBehaviorHistory(history)) { if (!clear) return false; history.header.enabled = 0; }
  if (clear) { history.entries.clear(); history.header.salt = {}; } else history.header.enabled = enabled;
  HKEY key = nullptr; const auto path = std::wstring(kRegistryKey) + L"\\Diagnostics";
  if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const bool ok = WriteBehavior(key, history); RegCloseKey(key); return ok;
}
inline bool StoredBehaviorKey(const BehaviorHistory& history, const BehaviorDigest& identity,
    const std::wstring& modelNamespace, BehaviorDigest& key) {
  std::vector<BYTE> data(history.header.salt.begin(), history.header.salt.end());
  data.insert(data.end(), identity.begin(), identity.end());
  const auto* bytes = reinterpret_cast<const BYTE*>(modelNamespace.data());
  data.insert(data.end(), bytes, bytes + modelNamespace.size() * sizeof(wchar_t));
  return !EmptyBehaviorDigest(history.header.salt) && HashBehavior(data.data(), data.size(), key);
}
inline double BehaviorBias(const BehaviorEntry& entry) {
  return (3.0 * entry.accepted - 2.0 * entry.rejected) / (6.0 + 3.0 * entry.accepted + entry.ignored + 2.0 * entry.rejected);
}
inline const BehaviorEntry* FindBehavior(const BehaviorHistory& history, const BehaviorDigest& identity, const std::wstring& modelNamespace) {
  if (!history.header.enabled) return nullptr;
  BehaviorDigest key{}; if (!StoredBehaviorKey(history, identity, modelNamespace, key)) return nullptr;
  for (const auto& entry : history.entries) if (entry.key == key) return &entry;
  return nullptr;
}
inline bool SuppressBehavior(const BehaviorEntry& entry, DWORD now) {
  return now >= entry.updated && now - entry.updated < 86400 && entry.rejected >= 3 &&
      entry.rejected >= entry.accepted + 2 && BehaviorBias(entry) <= -.45;
}
inline bool UpdateBehavior(BehaviorHistory& history, const BehaviorDigest& identity, const std::wstring& modelNamespace,
    BehaviorOutcome outcome, DWORD now) {
  if (!history.header.enabled || !now || outcome < BehaviorOutcome::Accepted || outcome > BehaviorOutcome::Rejected) return false;
  if (EmptyBehaviorDigest(history.header.salt) && BCryptGenRandom(nullptr, history.header.salt.data(), 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
  BehaviorDigest key{}; if (!StoredBehaviorKey(history, identity, modelNamespace, key)) return false;
  auto found = std::find_if(history.entries.begin(), history.entries.end(), [&](const BehaviorEntry& entry) { return entry.key == key; });
  if (found == history.entries.end()) {
    if (history.entries.size() >= kMaxBehaviorEntries) history.entries.erase(std::min_element(history.entries.begin(), history.entries.end(),
        [](const BehaviorEntry& a, const BehaviorEntry& b) { return a.updated < b.updated; }));
    history.entries.push_back({key}); found = history.entries.end() - 1;
  }
  if (found->accepted + found->ignored + found->rejected >= 64 || (now >= found->updated && now - found->updated > 30 * 86400)) {
    found->accepted /= 2; found->ignored /= 2; found->rejected /= 2;
  }
  if (outcome == BehaviorOutcome::Accepted) ++found->accepted;
  else if (outcome == BehaviorOutcome::Rejected) ++found->rejected;
  else ++found->ignored;
  found->updated = now; return true;
}
}  // namespace correction
