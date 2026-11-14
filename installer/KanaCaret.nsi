; KanaCaret のインストーラー(NSIS 3)。tools\package.ps1 から作る。
;   makensis /INPUTCHARSET UTF8 /DVERSION=<x.y.z> /DROOT=<リポジトリ> /DOUTFILE=<出力先> installer\KanaCaret.nsi
; 利用者ごとに %LOCALAPPDATA%\Programs\KanaCaret へ入れる(管理者権限は要らない)。
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinMessages.nsh"

!ifndef VERSION
  !error "VERSION を /D で渡す"
!endif
!ifndef ROOT
  !error "ROOT を /D で渡す"
!endif
!ifndef OUTFILE
  !error "OUTFILE を /D で渡す"
!endif

!define APP "KanaCaret"
!define EXE "KanaCaret.exe"
!define MAIN_CLASS "HectareBase.KanaCaret.Main"  ; src/main.cpp の kMainClass
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"  ; src/Settings.cpp と同じ
!define SETTINGS_DIR "$APPDATA\${APP}"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel user
BrandingText " "

!define MUI_ICON "${ROOT}\resources\app.ico"
!define MUI_UNICON "${ROOT}\resources\app.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "${APP} を起動する"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Japanese"

VIProductVersion "${VERSION}.0"
VIFileVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_JAPANESE} "ProductName" "${APP}"
VIAddVersionKey /LANG=${LANG_JAPANESE} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_JAPANESE} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_JAPANESE} "FileDescription" "${APP} のインストーラー"
VIAddVersionKey /LANG=${LANG_JAPANESE} "LegalCopyright" "Copyright (c) KanaCaret contributors. MIT License."

; 起動中の KanaCaret を終了させる(exe を上書き・削除できるように)。
; 非表示のメインウィンドウに WM_CLOSE を送ると、通常の「終了」と同じ後始末をして終わる。
!macro CLOSE_APP_FUNC prefix
Function ${prefix}CloseApp
  ${For} $1 1 50
    FindWindow $0 "${MAIN_CLASS}"
    ${If} $0 = 0
      ${Break}
    ${EndIf}
    ${If} $1 = 1
      DetailPrint "起動中の ${APP} を終了しています"
      SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=2000
    ${EndIf}
    Sleep 100
  ${Next}
  ; ウィンドウが消えてからプロセスが終わるまでの間を待つ
  Sleep 300
FunctionEnd
!macroend
!insertmacro CLOSE_APP_FUNC ""
!insertmacro CLOSE_APP_FUNC "un."

Section
  SetShellVarContext current
  Call CloseApp

  SetOutPath "$INSTDIR"
  File "${ROOT}\build\${EXE}"
  File "${ROOT}\README.md"
  File "${ROOT}\LICENSE"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${EXE}"

  ; 「設定 > アプリ > インストールされているアプリ」に出す
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "KanaCaret contributors"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/HectareBase/KanaCaret"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Call un.CloseApp

  ; 自動起動は、このフォルダーの exe を指しているときだけ消す(zip 版を登録していれば残す)
  ReadRegStr $0 HKCU "${RUN_KEY}" "${APP}"
  ${If} $0 == '"$INSTDIR\${EXE}"'
    DeleteRegValue HKCU "${RUN_KEY}" "${APP}"
  ${EndIf}

  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINST_KEY}"

  ; 設定は、入れ直したときに引き継げるよう、尋ねてから消す(サイレントのときは残す)
  ${If} ${FileExists} "${SETTINGS_DIR}\*.*"
    MessageBox MB_YESNO|MB_ICONQUESTION "設定(${SETTINGS_DIR})も削除しますか?$\n$\n入れ直すときに設定を引き継ぐなら「いいえ」を選んでください。" /SD IDNO IDNO keep_settings
    RMDir /r "${SETTINGS_DIR}"
  keep_settings:
  ${EndIf}
SectionEnd
