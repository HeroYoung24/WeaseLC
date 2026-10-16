#pragma once
#include <windows.h>
#include <sddl.h>
#include <vector>
#include <string>
#include <stdexcept>

namespace weasel {
inline std::vector<BYTE> TokenLogonSid(HANDLE token) {
  DWORD bytes = 0;
  GetTokenInformation(token, TokenGroups, nullptr, 0, &bytes);
  if (!bytes || bytes > 1024 * 1024) return {};
  std::vector<BYTE> storage(bytes);
  if (!GetTokenInformation(token, TokenGroups, storage.data(), bytes, &bytes)) return {};
  const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
  for (DWORD i = 0; i < groups->GroupCount; ++i) {
    const auto& group = groups->Groups[i];
    if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID || !IsValidSid(group.Sid)) continue;
    std::vector<BYTE> sid(GetLengthSid(group.Sid));
    if (!CopySid(static_cast<DWORD>(sid.size()), sid.data(), group.Sid)) return {};
    return sid;
  }
  return {};
}
inline constexpr DWORD kPipeClientAccess = FILE_GENERIC_READ | (FILE_GENERIC_WRITE & ~FILE_CREATE_PIPE_INSTANCE);
class PipeAccessPolicy {
 public:
  PipeAccessPolicy() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) throw std::runtime_error("pipe token");
    try { logon_ = TokenLogonSid(token); } catch (...) { CloseHandle(token); throw; }
    CloseHandle(token);
    if (logon_.empty()) throw std::runtime_error("pipe logon");
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(logon_.data(), &sid)) throw std::runtime_error("pipe sid");
    std::wstring descriptor;
    try {
      // Retain AppContainer access, authenticate the logon on every message.
      // The AC grant excludes FILE_CREATE_PIPE_INSTANCE (alias of APPEND_DATA).
      descriptor = L"S:(ML;;NW;;;LW)D:P(A;;0x001f01ff;;;SY)(A;;0x001f01ff;;;" + std::wstring(sid) +
          L")(A;;0x0012019b;;;AC)";
    } catch (...) { LocalFree(sid); throw; }
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
        &descriptor_, nullptr)) throw std::runtime_error("pipe descriptor");
    attributes_ = {sizeof(attributes_), descriptor_, FALSE};
  }
  ~PipeAccessPolicy() { if (descriptor_) LocalFree(descriptor_); }
  PipeAccessPolicy(const PipeAccessPolicy&) = delete;
  PipeAccessPolicy& operator=(const PipeAccessPolicy&) = delete;
  SECURITY_ATTRIBUTES* get_attr() { return &attributes_; }
  bool MatchesToken(HANDLE token) const {
    const auto sid = TokenLogonSid(token);
    return !sid.empty() && EqualSid(const_cast<BYTE*>(logon_.data()), const_cast<BYTE*>(sid.data()));
  }
  bool AllowsClient(HANDLE pipe) const {
    // Identify the last message read; no application code runs impersonated.
    if (!ImpersonateNamedPipeClient(pipe)) return false;
    struct Revert {
      ~Revert() { if (!RevertToSelf()) RaiseFailFastException(nullptr, nullptr, 0); }
    } revert;
    HANDLE token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) return false;
    bool allowed = false;
    try { allowed = MatchesToken(token); } catch (...) { CloseHandle(token); return false; }
    CloseHandle(token);
    return allowed;
  }
 private:
  std::vector<BYTE> logon_;
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
  SECURITY_ATTRIBUTES attributes_{};
};
}  // namespace weasel
