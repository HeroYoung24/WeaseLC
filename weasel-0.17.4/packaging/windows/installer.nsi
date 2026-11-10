Unicode true
!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!include WinVer.nsh
!include nsDialogs.nsh
!include "${PAYLOAD_INCLUDE}"
Name "WeaseLC ${DISPLAY_VERSION}"
OutFile "${INSTALLER_OUTPUT}"
InstallDir "$PROGRAMFILES64\WeaseLC"
!ifdef WEASELC_UI_PREVIEW
RequestExecutionLevel user
!else
RequestExecutionLevel admin
!endif
SetCompressor /SOLID lzma
ShowUninstDetails nevershow
VIProductVersion "0.1.0.1"
VIAddVersionKey /LANG=2052 "ProductName" "WeaseLC"
VIAddVersionKey /LANG=2052 "FileDescription" "WeaseLC 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${DISPLAY_VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "WeaseLC contributors; RIME Developers"
!define MUI_ICON "..\..\resource\weasel.ico"
!define MUI_ABORTWARNING
!define MUI_CUSTOMFUNCTION_ABORT GuardInstallAbort
!define MUI_CUSTOMFUNCTION_UNABORT un.GuardInstallAbort
!define MUI_FINISHPAGE_TITLE "WeaseLC 已安装"
!define MUI_FINISHPAGE_TEXT "完成后将打开输入方案和配色设置。也可稍后从开始菜单打开 WeaseLC。按 Win+空格切换输入法。$\r$\n$\r$\n自动纠错默认关闭，可从图标的模型管理及管理纠错中配置。"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "选择输入方案和配色"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchFirstRun
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE.txt"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE ValidateDirectory
!insertmacro MUI_PAGE_DIRECTORY
Page custom OptionsCreate OptionsLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
UninstPage custom un.OptionsCreate un.OptionsLeave
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"
Var Dialog
Var Hans
Var Hant
Var DefaultFolder
Var UserFolder
Var CustomFolder
Var Browse
Var Autostart
Var SelectedHant
Var SelectedFolder
Var SelectedStartup
Var ChangingFiles
Var KeepPreferences
Var UninstallOrigin

!macro VerifyRegistrationAbsent
  ; Generic COM E_FAIL is accepted only after both native helpers prove absence.
  StrCpy $2 1
  ClearErrors
  ExecWait '"$INSTDIR\WeaseLC.exe" /unregistered' $0
  ${IfNot} ${Errors}
  ${AndIf} $0 == 0
    ClearErrors
    ExecWait '"$INSTDIR\Win32\WeaseLC.exe" /unregistered' $0
    ${IfNot} ${Errors}
    ${AndIf} $0 == 0
      StrCpy $2 0
    ${EndIf}
  ${EndIf}
!macroend

Function GuardInstallAbort
  ${If} $ChangingFiles == 1
    Abort
  ${EndIf}
FunctionEnd
Function un.GuardInstallAbort
  ${If} $ChangingFiles == 1
    Abort
  ${EndIf}
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "本安装包需要 64 位 Windows。"
    Abort
  ${EndIf}
  ${IfNot} ${IsNativeAMD64}
    MessageBox MB_ICONSTOP "本安装包适用于 x64 Windows。ARM64 请等待对应版本。"
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "本安装包需要 Windows 10 或更高版本。"
    Abort
  ${EndIf}
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=RootGuard.exe "${GUARD_HELPER}"
  SetRegView 64
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "UninstallString"
  ${If} $0 != ""
    MessageBox MB_ICONINFORMATION "请先卸载现有 WeaseLC，然后重新安装。卸载会保留用户词典和个人设置。"
    Abort
  ${EndIf}
  StrCpy $SelectedHant 0
  StrCpy $SelectedStartup 1
  StrCpy $SelectedFolder "%APPDATA%\WeaseLC"
  ReadRegDWORD $0 HKCU "Software\WeaseLC" "Hant"
  ${IfNot} ${Errors}
    StrCpy $SelectedHant $0
  ${EndIf}
  ReadRegStr $0 HKCU "Software\WeaseLC" "RimeUserDir"
  ${If} $0 != ""
    StrCpy $SelectedFolder $0
  ${EndIf}
  ${If} $SelectedFolder == "$APPDATA\WeaseLC"
    StrCpy $SelectedFolder "%APPDATA%\WeaseLC"
  ${EndIf}
FunctionEnd

Function ValidateDirectory
  System::Call 'kernel32::SetEnvironmentVariableW(w "WEASELC_INSTALL_ROOT", w "$INSTDIR")'
  ClearErrors
  ExecWait '"$PLUGINSDIR\RootGuard.exe" /guard-destination' $0
  ${IfNot} ${Errors}
  ${AndIf} $0 == 3
    MessageBox MB_ICONINFORMATION "该位置有等待重启完成的文件操作，请重启 Windows 后再试；也可选择其他独立空目录。"
    Abort
  ${EndIf}
  ${IfNot} ${Errors}
  ${AndIf} $0 == 2
    MessageBox MB_ICONINFORMATION "所选文件夹已有内容，请选择独立空文件夹。若这是旧 WeaseLC 目录，请先完成卸载或重启以释放占用。不要覆盖其他输入法的目录。"
    Abort
  ${EndIf}
  ${If} ${Errors}
  ${OrIf} $0 != 0
    MessageBox MB_ICONSTOP "请选择路径不超过160字符的独立本地程序文件夹，避开盘符根目录、系统目录、用户资料及链接目录。建议通过浏览选择。"
    Abort
  ${EndIf}
FunctionEnd

Function LaunchFirstRun
!ifndef WEASELC_UI_PREVIEW
  ClearErrors
  ExecWait '"$INSTDIR\WeaseLC.exe" /launch-setup' $0
  ${If} ${Errors}
  ${OrIf} $0 != 0
    MessageBox MB_ICONINFORMATION "无法自动打开用户设置。请从开始菜单打开 WeaseLC，完成输入方案和配色设置。"
  ${EndIf}
!endif
FunctionEnd

Function un.onInit
  StrCpy $UninstallOrigin "$INSTDIR"
  StrCpy $SelectedStartup 1
  IfSilent 0 +3
    MessageBox MB_ICONSTOP "请使用交互式卸载，以确认文件占用和个人偏好选项。"
    Abort
  SetRegView 64
  ReadRegStr $INSTDIR HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "InstallLocation"
  ${If} $INSTDIR == ""
    ; Earlier beta builds did not write InstallLocation. The registered worker
    ; root is the only supported legacy source, never the command-line _?= path.
    ReadRegStr $INSTDIR HKLM "Software\WeaseLC" "WeaselRoot"
  ${EndIf}
  ${If} $INSTDIR == ""
    MessageBox MB_ICONSTOP "找不到有效安装记录，可能已重装系统。未删除任何文件。若确认系统已无 WeaseLC 注册，请备份资料后在资源管理器手动删除所属的程序文件夹。需要重新安装时，可先将旧程序文件夹重命名备份，再运行安装包。词典与模型请另行保留。"
    Abort
  ${EndIf}
  ${If} $UninstallOrigin != $INSTDIR
    MessageBox MB_ICONSTOP "卸载器位置与安装记录不一致，可能移动过文件夹。未删除任何文件。请从原快捷方式属性核对原位置，移回后重试；不要把卸载器复制到其他目录运行。若系统已重装、记录已失效，请备份资料后手动处理程序文件。"
    Abort
  ${EndIf}
  ClearErrors
  ExecWait '"$INSTDIR\WeaseLC.exe" /validate-install' $0
  ${If} ${Errors}
  ${OrIf} $0 != 0
    MessageBox MB_ICONSTOP "程序位置、注册、组件或清单无法核实，可能移动过目录或文件不完整。未删除任何文件。请移回原安装位置后重试；缺失组件可从同版本安装包解包恢复后再卸载。若系统已重装且不再有 WeaseLC 注册，请备份资料后手动删除确认属于本程序的文件夹。"
    Abort
  ${EndIf}
FunctionEnd

Function un.OptionsCreate
  !insertmacro MUI_HEADER_TEXT "卸载 WeaseLC" "仅移除此程序，不影响其他输入法"
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 46u "请先切换到其他输入法并保存工作。将停止 WeaseLC 自己启动的模型与后台服务，只移除安装清单中的程序文件。不会强制关闭其他应用，也不会删除词典或模型。"
  Pop $0
  ${NSD_CreateCheckbox} 0 58u 100% 14u "保留个人偏好（纠错及模型配置，推荐）"
  Pop $KeepPreferences
  ${NSD_Check} $KeepPreferences
  ${NSD_CreateLabel} 0 82u 100% 42u "未勾选时，清除本账户的纠错设置、暂停名单和模型路径记录。输入方案、配色、词典、模型文件始终保留。下一步会检查文件占用并给出处理指引。"
  Pop $0
  nsDialogs::Show
FunctionEnd

Function un.OptionsLeave
  ${If} $KeepPreferences == 0
    MessageBox MB_ICONSTOP "无法读取卸载选项，未开始卸载。请重新打开卸载器。"
    Abort
  ${EndIf}
  ${NSD_GetState} $KeepPreferences $SelectedStartup
  ClearErrors
  ExecWait '"$INSTDIR\WeaseLC.exe" /prepare-uninstall' $0
  ${If} ${Errors}
  ${OrIf} $0 != 0
    MessageBox MB_ICONINFORMATION "卸载尚未开始。请先保存并关闭占用应用，或从模型管理停止模型后重试。"
    Abort
  ${EndIf}
FunctionEnd

Function OptionsCreate
  !insertmacro MUI_HEADER_TEXT "基础设置" "选择输入语言和用户资料位置"
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 24u "WeaseLC 独立安装，可与其他输入法共存。自动纠错默认关闭，模型需另行准备。安装后可选择输入方案与配色。"
  Pop $0
  ${NSD_CreateRadioButton} 0 29u 48% 12u "中文（简体，中国大陆）"
  Pop $Hans
  ${NSD_AddStyle} $Hans ${WS_GROUP}
  ${NSD_CreateRadioButton} 50% 29u 48% 12u "中文（繁体，台湾）"
  Pop $Hant
  ${If} $SelectedHant == 1
    ${NSD_Check} $Hant
  ${Else}
    ${NSD_Check} $Hans
  ${EndIf}
  ${NSD_CreateGroupBox} 0 48u 100% 66u "当前安装账户的用户资料"
  Pop $0
  ${NSD_CreateRadioButton} 8u 63u 45% 12u "默认位置（推荐）"
  Pop $DefaultFolder
  ${NSD_AddStyle} $DefaultFolder ${WS_GROUP}
  ${NSD_CreateRadioButton} 50% 63u 47% 12u "指定文件夹"
  Pop $CustomFolder
  ${NSD_OnClick} $DefaultFolder FolderMode
  ${NSD_OnClick} $CustomFolder FolderMode
  ${NSD_CreateDirRequest} 8u 84u 77% 14u "$SelectedFolder"
  Pop $UserFolder
  ${NSD_CreateBrowseButton} 87% 83u 11% 16u "浏览"
  Pop $Browse
  ${NSD_OnClick} $Browse BrowseFolder
  ${If} $SelectedFolder == "%APPDATA%\WeaseLC"
    ${NSD_Check} $DefaultFolder
  ${Else}
    ${NSD_Check} $CustomFolder
  ${EndIf}
  Push 0
  Call FolderMode
  ${NSD_CreateCheckbox} 0 123u 100% 12u "登录 Windows 时启动 WeaseLC"
  Pop $Autostart
  ${If} $SelectedStartup == 1
    ${NSD_Check} $Autostart
  ${EndIf}
  nsDialogs::Show
FunctionEnd
Function FolderMode
  Pop $0
  ${NSD_GetState} $CustomFolder $1
  EnableWindow $UserFolder $1
  EnableWindow $Browse $1
FunctionEnd
Function BrowseFolder
  Pop $0
  nsDialogs::SelectFolderDialog "选择用户资料文件夹" ""
  Pop $0
  ${If} $0 != error
    ${NSD_SetText} $UserFolder $0
  ${EndIf}
FunctionEnd
Function OptionsLeave
  ${NSD_GetState} $Hant $SelectedHant
  ${NSD_GetState} $Autostart $SelectedStartup
  ${NSD_GetState} $CustomFolder $0
  ${If} $0 == 1
    ${NSD_GetText} $UserFolder $SelectedFolder
    ${If} $SelectedFolder == ""
      MessageBox MB_ICONEXCLAMATION "请选择用户资料文件夹，或使用默认位置。"
      Abort
    ${EndIf}
    GetFullPathName $1 "$SelectedFolder"
    ${If} $1 != $SelectedFolder
      MessageBox MB_ICONEXCLAMATION "请通过“浏览”选择完整的文件夹路径。"
      Abort
    ${EndIf}
    StrCpy $2 "$INSTDIR\"
    StrLen $0 $2
    StrCpy $1 "$SelectedFolder\" $0
    ${If} $1 == $2
      MessageBox MB_ICONEXCLAMATION "用户资料请选择程序安装目录以外的独立文件夹。"
      Abort
    ${EndIf}
  ${Else}
    StrCpy $SelectedFolder "%APPDATA%\WeaseLC"
  ${EndIf}
FunctionEnd

Section "WeaseLC"
!ifdef WEASELC_UI_PREVIEW
  MessageBox MB_ICONINFORMATION "界面预览已结束。本程序不执行安装。"
  Abort
!else
  Call ValidateDirectory
  SetRegView 64
  SetShellVarContext all
  FindFirst $0 $1 "$INSTDIR\*"
  empty_check:
  ${If} $1 != ""
    ${If} $1 != "."
    ${AndIf} $1 != ".."
      FindClose $0
      MessageBox MB_ICONSTOP "所选文件夹已有内容，请选择新的独立空文件夹，不要使用其他输入法或个人资料目录。若这是旧 WeaseLC 目录，请先完成其卸载；占用时可重启后重试。"
      Abort
    ${EndIf}
    FindNext $0 $1
    Goto empty_check
  ${EndIf}
  FindClose $0
  StrCpy $ChangingFiles 1
  GetDlgItem $0 $HWNDPARENT 2
  EnableWindow $0 0
  SetOverwrite on
  ClearErrors
  !insertmacro InstallPayload
  ${If} ${Errors}
    Goto failed
  ${EndIf}
  WriteRegStr HKLM "Software\WeaseLC" "WeaselRoot" "$INSTDIR"
  WriteRegDWORD HKLM "Software\WeaseLC" "DefaultHant" $SelectedHant
  SetRegView 32
  ; Both TSF architectures use the single native backend in the main folder.
  ; DLL registration still points separately to Win32\weasel.dll.
  WriteRegStr HKLM "Software\WeaseLC" "WeaselRoot" "$INSTDIR"
  SetRegView 64
  ${If} ${Errors}
    Goto failed
  ${EndIf}
  ${If} $SelectedHant == 1
    System::Call 'kernel32::SetEnvironmentVariableW(w "TEXTSERVICE_PROFILE", w "hant")'
  ${Else}
    System::Call 'kernel32::SetEnvironmentVariableW(w "TEXTSERVICE_PROFILE", w "hans")'
  ${EndIf}
  ClearErrors
  ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\Win32\weasel.dll"' $0
  ${If} ${Errors}
  ${OrIf} $0 != 0
    Goto failed
  ${EndIf}
  ${DisableX64FSRedirection}
  ClearErrors
  ExecWait '"$WINDIR\System32\regsvr32.exe" /s "$INSTDIR\weaselx64.dll"' $0
  ${EnableX64FSRedirection}
  ${If} ${Errors}
  ${OrIf} $0 != 0
    Goto failed
  ${EndIf}
  ClearErrors
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "DisplayName" "WeaseLC"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "DisplayVersion" "${DISPLAY_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "DisplayIcon" "$INSTDIR\WeaselServer.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "Publisher" "WeaseLC contributors"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\WeaseLC"
  CreateShortcut "$SMPROGRAMS\WeaseLC\WeaseLC.lnk" "$INSTDIR\WeaseLC.exe" "/configure" "$INSTDIR\WeaselServer.exe"
  CreateShortcut "$SMPROGRAMS\WeaseLC\模型管理.lnk" "$INSTDIR\WeaselDeployer.exe" "/local-model" "$INSTDIR\WeaselServer.exe"
  CreateShortcut "$SMPROGRAMS\WeaseLC\管理纠错.lnk" "$INSTDIR\WeaselDeployer.exe" "/correction" "$INSTDIR\WeaselServer.exe"
  CreateShortcut "$SMPROGRAMS\WeaseLC\卸载 WeaseLC.lnk" "$INSTDIR\Uninstall.exe"
  ${If} $SelectedStartup == 1
    CreateShortcut "$SMSTARTUP\WeaseLC.lnk" "$INSTDIR\WeaseLC.exe" "/start" "$INSTDIR\WeaselServer.exe"
  ${EndIf}
  ${If} ${Errors}
    Goto failed
  ${EndIf}
  ; Account-local preferences must never become a shared machine data folder.
  WriteRegDWORD HKCU "Software\WeaseLC" "Hant" $SelectedHant
  ${If} $SelectedFolder == "%APPDATA%\WeaseLC"
    ; The existing RimeUserDir reader consumes REG_SZ as a literal path.
    SetShellVarContext current
    StrCpy $SelectedFolder "$APPDATA\WeaseLC"
    SetShellVarContext all
  ${EndIf}
  WriteRegStr HKCU "Software\WeaseLC" "RimeUserDir" "$SelectedFolder"
  ${If} ${Errors}
    MessageBox MB_ICONEXCLAMATION "部分账户设置未能保存，首次启动将使用已保存或默认设置。"
  ${EndIf}
  Goto done
  failed:
    StrCpy $2 0
    ClearErrors
    ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\Win32\weasel.dll"' $0
    ${If} ${Errors}
    ${OrIf} $0 != 0
      StrCpy $2 1
    ${Else}
      WriteRegDWORD HKLM "Software\WeaseLC" "Unregistered32" 1
    ${EndIf}
    ${DisableX64FSRedirection}
    ClearErrors
    ExecWait '"$WINDIR\System32\regsvr32.exe" /u /s "$INSTDIR\weaselx64.dll"' $0
    ${EnableX64FSRedirection}
    ${If} ${Errors}
    ${OrIf} $0 != 0
      StrCpy $2 1
    ${Else}
      WriteRegDWORD HKLM "Software\WeaseLC" "Unregistered64" 1
    ${EndIf}
    ${If} $2 == 1
      !insertmacro VerifyRegistrationAbsent
    ${EndIf}
    ${If} $2 == 1
      ; Preserve DLLs when registration cleanup cannot be confirmed.
      WriteUninstaller "$INSTDIR\Uninstall.exe"
      WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "DisplayName" "WeaseLC（安装未完成）"
      WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC" "UninstallString" '"$INSTDIR\Uninstall.exe"'
      MessageBox MB_ICONSTOP "安装未完成，部分输入组件未能注销。已保留程序文件，请切换到其他输入法，从安装目录运行 Uninstall.exe 后再重试。"
      StrCpy $ChangingFiles 0
      Abort
    ${EndIf}
    Delete "$SMSTARTUP\WeaseLC.lnk"
    Delete "$SMPROGRAMS\WeaseLC\WeaseLC.lnk"
  Delete "$SMPROGRAMS\WeaseLC\模型管理.lnk"
  Delete "$SMPROGRAMS\WeaseLC\管理纠错.lnk"
    Delete "$SMPROGRAMS\WeaseLC\卸载 WeaseLC.lnk"
    RMDir "$SMPROGRAMS\WeaseLC"
    !insertmacro RemovePayload
    Delete /REBOOTOK "$INSTDIR\Uninstall.exe"
    SetRegView 32
    DeleteRegKey HKLM "Software\WeaseLC"
    SetRegView 64
    DeleteRegKey HKLM "Software\WeaseLC"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC"
    MessageBox MB_ICONSTOP "安装未完成，已回退本次安装。请检查权限和文件占用后重试。"
    StrCpy $ChangingFiles 0
    Abort
  done:
  StrCpy $ChangingFiles 0
!endif
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  SetRegView 64
  StrCpy $ChangingFiles 1
  GetDlgItem $0 $HWNDPARENT 2
  EnableWindow $0 0
  ClearErrors
  ExecWait '"$INSTDIR\WeaselServer.exe" /q' $0
  ${If} ${Errors}
  ${OrIf} $0 != 0
    MessageBox MB_ICONSTOP "无法停止 WeaseLC。请关闭它后重试。"
    StrCpy $ChangingFiles 0
    Abort
  ${EndIf}
  ExecWait '"$INSTDIR\WeaseLC.exe" /remove' $0
  ; Successful architecture cleanup is checkpointed so a retry does not
  ; depend on TSF accepting a second unregister of an absent profile.
  StrCpy $2 0
  StrCpy $1 0
  ReadRegDWORD $1 HKLM "Software\WeaseLC" "Unregistered32"
  ${If} $1 != 1
  ClearErrors
  ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\Win32\weasel.dll"' $0
  ${If} ${Errors}
  ${OrIf} $0 != 0
    StrCpy $2 1
  ${Else}
    WriteRegDWORD HKLM "Software\WeaseLC" "Unregistered32" 1
  ${EndIf}
  ${EndIf}
  StrCpy $1 0
  ReadRegDWORD $1 HKLM "Software\WeaseLC" "Unregistered64"
  ${If} $1 != 1
  ${DisableX64FSRedirection}
  ClearErrors
  ExecWait '"$WINDIR\System32\regsvr32.exe" /u /s "$INSTDIR\weaselx64.dll"' $0
  ${EnableX64FSRedirection}
  ${If} ${Errors}
  ${OrIf} $0 != 0
    StrCpy $2 1
  ${Else}
    WriteRegDWORD HKLM "Software\WeaseLC" "Unregistered64" 1
  ${EndIf}
  ${EndIf}
  !insertmacro VerifyRegistrationAbsent
  ${If} $2 == 1
    MessageBox MB_ICONSTOP "输入组件仍有注册残留，或无法确认注销状态。已保留程序，请稍后重试卸载。"
    StrCpy $ChangingFiles 0
    Abort
  ${EndIf}
  SetRegView 32
  DeleteRegKey HKCU "Software\Microsoft\CTF\TIP\{69C4CD90-CA76-4367-A307-EE18A40EE6D9}"
  SetRegView 64
  DeleteRegKey HKCU "Software\Microsoft\CTF\TIP\{69C4CD90-CA76-4367-A307-EE18A40EE6D9}"
  Delete "$SMSTARTUP\WeaseLC.lnk"
  Delete "$SMPROGRAMS\WeaseLC\WeaseLC.lnk"
  Delete "$SMPROGRAMS\WeaseLC\模型管理.lnk"
  Delete "$SMPROGRAMS\WeaseLC\管理纠错.lnk"
  Delete "$SMPROGRAMS\WeaseLC\卸载 WeaseLC.lnk"
  RMDir "$SMPROGRAMS\WeaseLC"
  !insertmacro RemovePayload
  Delete /REBOOTOK "$INSTDIR\Uninstall.exe"
  RMDir /REBOOTOK "$INSTDIR"
  SetRegView 32
  DeleteRegKey HKLM "Software\WeaseLC"
  SetRegView 64
  DeleteRegKey HKLM "Software\WeaseLC"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WeaseLC"
  ${If} $SelectedStartup == 0
    DeleteRegKey HKCU "Software\WeaseLC"
  ${EndIf}
  ${If} ${RebootFlag}
    MessageBox MB_ICONINFORMATION "部分文件将在重启 Windows 后移除。"
  ${EndIf}
  StrCpy $ChangingFiles 0
SectionEnd
