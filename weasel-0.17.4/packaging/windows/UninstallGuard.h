#pragma once
#include <windows.h>
#include <shlobj.h>
#include <restartmanager.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <LocalTestIdentity.h>
#pragma comment(lib,"rstrtmgr.lib")
namespace distribution {
namespace fs = std::filesystem;
inline fs::path DefaultProgramRoot() {
  wchar_t folder[32768]{};
  DWORD bytes=sizeof(folder);
  if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",L"ProgramFilesDir",
      RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,folder,&bytes)==ERROR_SUCCESS) return fs::path(folder)/L"WeaseLC";
  return {};
}
inline bool Under(const fs::path& child, const fs::path& parent) {
  const auto a = child.wstring(), b = parent.wstring();
  return !_wcsicmp(a.c_str(),b.c_str()) || (a.size()>b.size() &&
      !_wcsnicmp(a.c_str(),b.c_str(),b.size()) && a[b.size()]==L'\\');
}
inline bool SafeProgramRoot(const fs::path& input) try {
  if (!input.is_absolute() || input.has_root_name()==false) return false;
  const auto root = input.lexically_normal();
  const auto nativeDefault=DefaultProgramRoot();
  if(nativeDefault.empty() || Under(nativeDefault.parent_path(),root)) return false;
  const auto text = root.wstring();
  if (text.size()<4 || text.size()>160 || text[1]!=L':' || text.find_first_of(L"/\"*?<>|\r\n")!=std::wstring::npos ||
      text.find(L':',2)!=std::wstring::npos || root==root.root_path() || input!=root) return false;
  if (root.filename().empty() || GetDriveTypeW(root.root_path().c_str())!=DRIVE_FIXED) return false;
  for(const auto& component : root) {
    const auto part=component.wstring();
    if(!part.empty() && (part.back()==L'.' || part.back()==L' ')) return false;
    for(const auto* reserved : {L"System Volume Information",L"$Recycle.Bin",L"Recovery",L"Config.Msi"})
      if(!_wcsicmp(part.c_str(),reserved)) return false;
  }
  wchar_t windows[32768]{}; if (!GetWindowsDirectoryW(windows,_countof(windows))) return false;
  if (Under(root,windows) || Under(windows,root)) return false;
  for (const auto* id : {&FOLDERID_ProgramFiles,&FOLDERID_ProgramFilesX86,&FOLDERID_Profile,&FOLDERID_ProgramData,
      &FOLDERID_Desktop,&FOLDERID_Documents,&FOLDERID_Public}) {
    PWSTR folder=nullptr;
    if (FAILED(SHGetKnownFolderPath(*id,KF_FLAG_DONT_VERIFY,nullptr,&folder))) return false;
    const fs::path protectedPath(folder); CoTaskMemFree(folder);
    if (Under(protectedPath,root)) return false;
    if (id==&FOLDERID_Profile && Under(root,protectedPath.parent_path())) return false;
    if ((id==&FOLDERID_Profile || id==&FOLDERID_ProgramData || id==&FOLDERID_Desktop ||
        id==&FOLDERID_Documents || id==&FOLDERID_Public) && Under(root,protectedPath)) return false;
  }
  for (auto at=root;!at.empty();at=at.parent_path()) {
    const DWORD attributes=GetFileAttributesW(at.c_str());
    if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    if (attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (attributes==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) return false;
    if (attributes!=INVALID_FILE_ATTRIBUTES) {
      wchar_t expanded[32768]{};
      const auto count=GetLongPathNameW(at.c_str(),expanded,_countof(expanded));
      if(!count || count>=_countof(expanded) || _wcsicmp(expanded,at.c_str())) return false;
    }
    if (at==at.parent_path()) break;
  }
  return true;
} catch (...) { return false; }
inline fs::path ModuleRoot() {
  wchar_t module[32768]{}; const auto count=GetModuleFileNameW(nullptr,module,_countof(module));
  return count && count<_countof(module) ? fs::path(module).parent_path() : fs::path{};
}
inline bool PendingFileOperation(const fs::path& root) try {
  constexpr auto key=L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
  DWORD bytes=0;
  auto status=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"PendingFileRenameOperations",RRF_RT_REG_MULTI_SZ,nullptr,nullptr,&bytes);
  if(status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND) return false;
  if(status==ERROR_SUCCESS && !bytes) return false;
  if(status!=ERROR_SUCCESS || bytes>1024*1024 || bytes%sizeof(wchar_t)) return true;
  std::vector<wchar_t> data(bytes/sizeof(wchar_t)+2);
  status=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"PendingFileRenameOperations",RRF_RT_REG_MULTI_SZ,nullptr,data.data(),&bytes);
  if(status!=ERROR_SUCCESS || bytes%sizeof(wchar_t)) return true;
  const size_t limit=bytes/sizeof(wchar_t);
  for(size_t at=0;at<limit;) {
    size_t end=at;while(end<limit && data[end])++end;
    if(end==limit) return true;
    const std::wstring item(data.data()+at,end-at);at=end+1;
    const auto colon=item.find(L":\\");
    if(colon!=std::wstring::npos && colon && Under(item.substr(colon-1),root)) return true;
  }
  return false;
} catch (...) { return true; }
inline bool InstalledRoot() try {
  const auto root=ModuleRoot(); if (!SafeProgramRoot(root)) return false;
  if(PendingFileOperation(root)) return false;
  // Reject links anywhere below the root too: a replaced data/Win32 folder
  // must not redirect exact payload deletions into another installation.
  std::error_code walkError;
  for(fs::recursive_directory_iterator at(root,walkError),end;at!=end && !walkError;at.increment(walkError)) {
    const auto attributes=GetFileAttributesW(at->path().c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !Under(at->path(),root)) return false;
  }
  if(walkError) return false;
  for(const auto* relative : {L"WeaseLC.exe",L"WeaselDeployer.exe",L"WeaselServer.exe",L"rime.dll",
      L"weaselx64.dll",L"Win32\\WeaseLC.exe",L"Win32\\WeaselDeployer.exe",L"Win32\\rime.dll",L"Win32\\weasel.dll"}) {
    std::error_code error;
    if(!fs::is_regular_file(root/relative,error) || error) return false;
  }
  DWORD removed32=0,removed64=0,numberBytes=sizeof(DWORD);
  RegGetValueW(HKEY_LOCAL_MACHINE,WEASEL_PRODUCT_REG_KEY,L"Unregistered32",RRF_RT_REG_DWORD|RRF_SUBKEY_WOW6464KEY,nullptr,&removed32,&numberBytes);
  numberBytes=sizeof(DWORD);
  RegGetValueW(HKEY_LOCAL_MACHINE,WEASEL_PRODUCT_REG_KEY,L"Unregistered64",RRF_RT_REG_DWORD|RRF_SUBKEY_WOW6464KEY,nullptr,&removed64,&numberBytes);
  const bool interrupted=removed32==1 || removed64==1;
  for (const REGSAM view : {KEY_WOW64_32KEY,KEY_WOW64_64KEY}) {
    HKEY key=nullptr; wchar_t saved[32768]{}; DWORD bytes=sizeof(saved);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,WEASEL_PRODUCT_REG_KEY,0,KEY_QUERY_VALUE|view,&key)!=ERROR_SUCCESS) return false;
    const auto status=RegGetValueW(key,nullptr,L"WeaselRoot",RRF_RT_REG_SZ,nullptr,saved,&bytes);RegCloseKey(key);
    if (status!=ERROR_SUCCESS || _wcsicmp(saved,root.c_str())) return false;
    const std::wstring clsid=std::wstring(L"Software\\Classes\\CLSID\\")+WEASELC_PRODUCT_CLSID_TEXT+L"\\InprocServer32";
    HKEY registration=nullptr;
    const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,clsid.c_str(),0,KEY_QUERY_VALUE|view,&registration);
    if(opened==ERROR_SUCCESS) {
      wchar_t binary[32768]{};DWORD length=sizeof(binary);
      const auto read=RegGetValueW(registration,nullptr,nullptr,RRF_RT_REG_SZ,nullptr,binary,&length);RegCloseKey(registration);
      const auto expected=view==KEY_WOW64_32KEY ? root/L"Win32"/L"weasel.dll" : root/L"weaselx64.dll";
      std::error_code existsError;
      if(read!=ERROR_SUCCESS || _wcsicmp(binary,expected.c_str()) || !fs::is_regular_file(expected,existsError) || existsError) return false;
    } else if(!interrupted || (opened!=ERROR_FILE_NOT_FOUND && opened!=ERROR_PATH_NOT_FOUND)) return false;
  }
  const auto marker=root/L"distribution-manifest.json";
  std::error_code error; const auto size=fs::file_size(marker,error);
  if(error || size>1024*1024 || GetFileAttributesW(marker.c_str())&FILE_ATTRIBUTE_REPARSE_POINT) return false;
  std::ifstream file(marker,std::ios::binary); std::string text(static_cast<size_t>(size),'\0');
  if(!file.read(text.data(),static_cast<std::streamsize>(text.size())) || file.peek()!=std::char_traits<char>::eof()) return false;
  return text.find("weaselc-distribution")!=std::string::npos &&
      text.find("69C4CD90-CA76-4367-A307-EE18A40EE6D9")!=std::string::npos;
} catch (...) { return false; }
// Enumerate blockers only. Never call RmShutdown or terminate document hosts.
inline bool ConfirmBlockers() try {
  const auto root=ModuleRoot();
  const std::wstring paths[]{(root/L"weaselx64.dll").wstring(),(root/L"Win32"/L"weasel.dll").wstring(),
      (root/L"rime.dll").wstring(),(root/L"Win32"/L"rime.dll").wstring(),
      (root/L"WeaselDeployer.exe").wstring(),(root/L"Win32"/L"WeaselDeployer.exe").wstring(),
      (root/L"WeaselServer.exe").wstring()};
  LPCWSTR files[_countof(paths)]{};
  for(unsigned i=0;i<_countof(paths);++i) files[i]=paths[i].c_str();
  DWORD session=0; wchar_t key[CCH_RM_SESSION_KEY+1]{};
  if(RmStartSession(&session,0,key)!=ERROR_SUCCESS) return MessageBoxW(nullptr,
      L"无法检查文件占用。请先保存工作并切换输入法。继续卸载后，仍占用的文件将在重启后移除。",L"WeaseLC",MB_OKCANCEL|MB_ICONWARNING)==IDOK;
  struct Close {DWORD session;~Close(){RmEndSession(session);}} close{session};
  UINT needed=0,count=0;DWORD reason=0;
  DWORD status=RmRegisterResources(session,static_cast<UINT>(_countof(files)),files,0,nullptr,0,nullptr);
  if(status==ERROR_SUCCESS) status=RmGetList(session,&needed,&count,nullptr,&reason);
  std::vector<RM_PROCESS_INFO> apps;
  if(status==ERROR_MORE_DATA && needed<=256) {apps.resize(needed);count=needed;status=RmGetList(session,&needed,&count,apps.data(),&reason);}
  if(status==ERROR_SUCCESS && !count && !reason) return true;
  std::wstring message=L"以下应用可能仍占用 WeaseLC 输入组件：\n";
  if(status==ERROR_SUCCESS) for(UINT i=0;i<count && i<12;++i)
    message+=std::wstring(apps[i].strAppName)+L"（PID "+std::to_wstring(apps[i].Process.dwProcessId)+L"）\n";
  else message+=L"占用列表暂不可用。\n";
  message+=L"\n请先切换到其他输入法，保存并关闭相关应用。资源管理器可在任务管理器中重启；无法释放时可重启电脑。\n\n不会强制关闭应用。确定：继续卸载，占用文件重启后删除；取消：返回，释放后重试。";
  return MessageBoxW(nullptr,message.c_str(),L"WeaseLC",MB_OKCANCEL|MB_ICONINFORMATION)==IDOK;
} catch (...) { return false; }
}
