#include "stdafx.h"
#include "Globals.h"
#include <CorrectionBrokerClient.h>
#include <CorrectionDeployment.h>

namespace correction {
std::wstring CorrectionBrokerServerPath() {
  // Both TSF architectures use the normal user's one backend. The registered
  // machine root is authoritative even when the x86 DLL lives under Win32.
  auto root = InstalledCorrectionRoot();
  if (root.empty()) {
    WCHAR module[32768]{};
    const auto size = GetModuleFileNameW(g_hInst, module, _countof(module));
    if (!size || size >= _countof(module)) return {};
    root = module; const auto slash = root.find_last_of(L'\\');
    if (slash == std::wstring::npos) return {};
    root.resize(slash);
    if (root.size() > 6 && !_wcsicmp(root.c_str() + root.size() - 6, L"\\Win32")) root.resize(root.size() - 6);
  }
  return LocalAbsoluteDirectory(root) ? TrimDirectory(root) + L"\\WeaselDeployer.exe" : L"";
}
void RelayCorrectionMetadata(BrokerMetadata kind, const void* data, DWORD bytes) noexcept {
  try {
    if (bytes != MetadataSize(kind)) return;
    BrokerRequest request; request.operation = BrokerOperation::Metadata;
    request.metadata = kind; request.metadataBytes = bytes;
    memcpy(request.data, data, bytes);
    BrokerCall call; call.SendMetadata(CorrectionBrokerServerPath(), request);
  } catch (...) { /* Diagnostics must never unwind an input callback. */ }
}
}
