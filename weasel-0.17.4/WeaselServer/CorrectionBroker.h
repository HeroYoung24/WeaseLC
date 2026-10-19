#pragma once
#include <memory>
#include <functional>
#include <string>
#include <CorrectionModel.h>

namespace correction {
// Runs in the normal user backend, outside packaged document hosts.
class CorrectionBroker {
 public:
  explicit CorrectionBroker(std::wstring settingsPath = {},
      std::function<std::unique_ptr<IModelTransport>()> transport = {});
  ~CorrectionBroker();
  bool Start();
  void Stop();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
