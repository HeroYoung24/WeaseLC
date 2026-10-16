#pragma once

// A side-by-side build must separate every stateful endpoint from upstream
// Weasel. Keep these defaults identical for ordinary builds.
#ifdef WEASELC_LOCAL_TEST_BUILD
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
