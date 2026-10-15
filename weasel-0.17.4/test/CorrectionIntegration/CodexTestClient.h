#pragma once
#include <CorrectionModel.h>

// Diagnostic-only adapter: not linked into the installed IME or Deployer.
// Only exact synthetic corpus entries are accepted, even with live mode set.
std::unique_ptr<correction::IModelClient> CreateCodexTestClient(
    const std::wstring& executable, const std::wstring& schema,
    const std::wstring& directory);
bool IsSyntheticModelInput(const std::wstring&);
