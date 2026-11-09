#pragma once

// A side-by-side build must separate every stateful endpoint from upstream
// Weasel. Keep these defaults identical for ordinary builds.
#if defined(WEASELC_DISTRIBUTION_BUILD) && (defined(WEASELC_LOCAL_TEST_BUILD) || defined(WEASEL_CORRECTION_PROBE_BUILD))
#error Distribution, local test and probe identities cannot be combined
#endif
#ifdef WEASELC_DISTRIBUTION_BUILD
#define WEASEL_PRODUCT_REG_KEY L"Software\\WeaseLC"
#define WEASEL_PRODUCT_REG_KEY_32 L"Software\\WOW6432Node\\WeaseLC"
#define WEASEL_CORRECTION_REG_KEY L"Software\\WeaseLC\\Correction"
#define WEASEL_DEFAULT_USER_DIR L"%AppData%\\WeaseLC"
#define WEASEL_DEFAULT_LOG_DIR L"%TEMP%\\rime.weaselc"
#define WEASEL_DEPLOYER_MUTEX L"WeaseLC-DeployerMutex"
#define WEASEL_DEPLOYER_EXCLUSIVE_MUTEX L"WeaseLC-DeployerExclusiveMutex"
#define WEASEL_SERVER_MUTEX_PREFIX L"WeaseLC-Server-"
#define WEASEL_PIPE_NAME L"WeaseLC-Pipe"
#define WEASEL_WINDOW_CLASS L"WeaseLC-IPC"
#define WEASELC_PRODUCT_CLSID {0x69c4cd90,0xca76,0x4367,{0xa3,0x07,0xee,0x18,0xa4,0x0e,0xe6,0xd9}}
#define WEASELC_PRODUCT_PROFILE {0x7db86cc4,0xccaa,0x43fc,{0x86,0x01,0x85,0x00,0x1f,0x2a,0xd0,0xe0}}
#define WEASELC_PRODUCT_CLSID_TEXT L"{69C4CD90-CA76-4367-A307-EE18A40EE6D9}"
#define WEASELC_PRODUCT_PROFILE_TEXT L"{7DB86CC4-CCAA-43FC-8601-85001F2AD0E0}"
#elif defined(WEASELC_LOCAL_TEST_BUILD)
#ifdef WEASEL_CORRECTION_PROBE_BUILD
#error Local deployment cannot include diagnostic exports
#endif
#define WEASEL_PRODUCT_REG_KEY L"Software\\Rime\\WeaseLC-LocalTest"
#define WEASEL_PRODUCT_REG_KEY_32 L"Software\\WOW6432Node\\Rime\\WeaseLC-LocalTest"
#define WEASEL_CORRECTION_REG_KEY L"Software\\Rime\\WeaseLC-LocalTest\\Correction"
#define WEASEL_DEFAULT_USER_DIR L"%AppData%\\WeaseLC-LocalTest"
#define WEASEL_DEFAULT_LOG_DIR L"%TEMP%\\rime.weaselc-localtest"
#define WEASEL_DEPLOYER_MUTEX L"WeaseLC-LocalTest-DeployerMutex"
#define WEASEL_DEPLOYER_EXCLUSIVE_MUTEX L"WeaseLC-LocalTest-DeployerExclusiveMutex"
#define WEASEL_SERVER_MUTEX_PREFIX L"WeaseLC-LocalTest-Server-"
#define WEASEL_PIPE_NAME L"WeaseLC-LocalTest-Pipe"
#define WEASEL_WINDOW_CLASS L"WeaseLC-LocalTest-IPC"
#define WEASELC_TEST_CLSID {0x893169ea,0x8480,0x4965,{0x9f,0x2f,0x69,0x5e,0xd5,0x1e,0x4b,0x43}}
#define WEASELC_TEST_PROFILE {0xfaf832b8,0x6cdd,0x4f9a,{0x99,0x3d,0x22,0xb4,0x76,0x72,0xba,0x27}}
#else
#define WEASEL_PRODUCT_REG_KEY L"Software\\Rime\\Weasel"
#define WEASEL_PRODUCT_REG_KEY_32 L"Software\\WOW6432Node\\Rime\\Weasel"
#define WEASEL_CORRECTION_REG_KEY L"Software\\Rime\\Weasel\\Correction"
#define WEASEL_DEFAULT_USER_DIR L"%AppData%\\Rime"
#define WEASEL_DEFAULT_LOG_DIR L"%TEMP%\\rime.weasel"
#define WEASEL_DEPLOYER_MUTEX L"WeaselDeployerMutex"
#define WEASEL_DEPLOYER_EXCLUSIVE_MUTEX L"WeaselDeployerExclusiveMutex"
#define WEASEL_SERVER_MUTEX_PREFIX L"(WEASEL)Furandōru-Sukāretto-"
#define WEASEL_PIPE_NAME L"WeaselNamedPipe"
#define WEASEL_WINDOW_CLASS L"WeaselIPCWindow_1.0"
#endif
