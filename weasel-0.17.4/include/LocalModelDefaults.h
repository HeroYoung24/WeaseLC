#pragma once

namespace correction {
inline constexpr wchar_t kManagedModelEndpoint[] = L"http://127.0.0.1:18963/v1/chat/completions";
inline constexpr wchar_t kManagedModelName[] = L"weasel-qwen3-4b";
inline constexpr unsigned short kManagedModelPort = 18963;
inline constexpr unsigned kManagedModelTimeoutSeconds = 5;
}
