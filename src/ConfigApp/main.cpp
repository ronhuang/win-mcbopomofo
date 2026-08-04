// Copyright (c) 2026 and onwards The McBopomofo Authors.
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
// clang-format on

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "../Common/DpiAwareness.h"
#include "../Common/UTFHelper.h"
#include "ControlState.h"
#include "Ipc.h"
#include "NamedPipe.h"
#include "Settings.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment( \
    linker,      \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace McBopomofo;

namespace {

constexpr const wchar_t* kSingleInstanceMutexName =
    L"Local\\WinMcBopomofoConfigSingleInstance";
constexpr int kReloadCommand = IDC_RELOAD_BUTTON;
constexpr int kManualLinkCommand = IDC_MANUAL_LINK;
constexpr int kProjectHomepageCommand = IDC_PROJECT_HOMEPAGE_LINK;
constexpr int kScrollLineHeight = 20;
constexpr const wchar_t* kManualUrl =
    L"https://github.com/openvanilla/McBopomofo/wiki/"
    L"%E4%BD%BF%E7%94%A8%E6%89%8B%E5%86%8A";
constexpr const wchar_t* kProjectHomepageUrl =
    L"https://github.com/openvanilla/win-mcbopomofo";

struct ComboOption {
  UINT labelId;
  const char* value;
};

struct CtrlEnterOption {
  UINT labelId;
  KeyHandlerCtrlEnter value;
};

const std::array<ComboOption, 6> kLayoutOptions = {{
    {IDS_LAYOUT_STANDARD, "Standard"},
    {IDS_LAYOUT_ETEN, "ETen"},
    {IDS_LAYOUT_HSU, "Hsu"},
    {IDS_LAYOUT_ETEN26, "ETen26"},
    {IDS_LAYOUT_HANYUPINYIN, "HanyuPinyin"},
    {IDS_LAYOUT_IBM, "IBM"},
}};

const std::array<UINT, 2> kInputModeLabels = {{
    IDS_MODE_MCBOPOMOFO_AUTO,
    IDS_MODE_PLAIN_BOPOMOFO_MANUAL,
}};

const std::array<ComboOption, 3> kCandidateKeyOptions = {{
    {0, "123456789"},
    {0, "asdfghjkl"},
    {0, "asdfzxcvb"},
}};

const std::array<CtrlEnterOption, 6> kCtrlEnterOptions = {{
    {IDS_CTRL_ENTER_DISABLED, KeyHandlerCtrlEnter::Disabled},
    {IDS_CTRL_ENTER_BPMF_READING, KeyHandlerCtrlEnter::OutputBpmfReadings},
    {IDS_CTRL_ENTER_HTML_RUBY, KeyHandlerCtrlEnter::OutputHTMLRubyText},
    {IDS_CTRL_ENTER_HANYU_PINYIN, KeyHandlerCtrlEnter::OutputHanyuPinyin},
    {IDS_CTRL_ENTER_TAIWAN_BRAILLE_UNICODE,
     KeyHandlerCtrlEnter::OutputTaiwanBrailleUnicode},
    {IDS_CTRL_ENTER_TAIWAN_BRAILLE_ASCII,
     KeyHandlerCtrlEnter::OutputTaiwanBrailleAscii},
}};

const std::array<ComboOption, 3> kSelectionActionOptions = {{
    {IDS_SELECTION_ACTION_NONE, "None"},
    {IDS_SELECTION_ACTION_JK, "JK"},
    {IDS_SELECTION_ACTION_HL, "HL"},
}};

const std::array<int, 8> kCandidateFontSizes = {
    {10, 12, 14, 16, 18, 20, 24, 28}};

HWND hLayoutCombo = nullptr;
HWND hModeCombo = nullptr;
HWND hVerticalRadio = nullptr;
HWND hHorizontalRadio = nullptr;
HWND hCandidateKeysCombo = nullptr;
HWND hSelectBeforeRadio = nullptr;
HWND hSelectAfterRadio = nullptr;
HWND hMoveCursorCheck = nullptr;
HWND hLowercaseRadio = nullptr;
HWND hUppercaseRadio = nullptr;
HWND hEscClearCheck = nullptr;
HWND hShiftEnterCheck = nullptr;
HWND hCtrlEnterCombo = nullptr;
HWND hRepeatedPunctuationCheck = nullptr;
HWND hChooseSpaceCheck = nullptr;
HWND hSelectionActionCombo = nullptr;
HWND hCandidateFontSizeCombo = nullptr;
HWND hShiftToggleCheck = nullptr;
HWND hErrorBeepCheck = nullptr;
HWND hManualLink = nullptr;
HWND hProjectHomepageLink = nullptr;
HWND hReloadBtn = nullptr;
HWND hCandidateKeysCountCombo = nullptr;
HFONT hLinkFont = nullptr;
Settings settings;
int g_ScrollPos = 0;
int g_ContentHeight = 0;
std::vector<std::pair<HWND, RECT>> g_ChildBaseRects;
int g_FixedWidth = 0;

bool IsRadioButton(HWND hwnd) {
  return hwnd == hVerticalRadio || hwnd == hHorizontalRadio ||
         hwnd == hSelectBeforeRadio || hwnd == hSelectAfterRadio ||
         hwnd == hLowercaseRadio || hwnd == hUppercaseRadio;
}

bool IsCheckButton(HWND hwnd) {
  return hwnd == hChooseSpaceCheck || hwnd == hMoveCursorCheck ||
         hwnd == hShiftToggleCheck || hwnd == hShiftEnterCheck ||
         hwnd == hEscClearCheck || hwnd == hRepeatedPunctuationCheck ||
         hwnd == hErrorBeepCheck;
}

void CenterWindow(HWND hwnd) {
  RECT rect;
  GetWindowRect(hwnd, &rect);
  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;

  int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);

  int x = (screenWidth - width) / 2;
  int y = (screenHeight - height) / 2;

  if (x < 0) x = 0;
  if (y < 0) y = 0;

  SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

int MaxScrollPos(const SCROLLINFO& si) {
  return std::max(0, si.nMax - static_cast<int>(si.nPage) + 1);
}

void CacheChildBaseRects(HWND hwnd) {
  g_ChildBaseRects.clear();
  g_ContentHeight = 0;
  for (HWND child = GetWindow(hwnd, GW_CHILD); child != nullptr;
       child = GetWindow(child, GW_HWNDNEXT)) {
    RECT rect{};
    GetWindowRect(child, &rect);
    POINT topLeft{rect.left, rect.top};
    POINT bottomRight{rect.right, rect.bottom};
    ScreenToClient(hwnd, &topLeft);
    ScreenToClient(hwnd, &bottomRight);
    rect.left = topLeft.x;
    rect.top = topLeft.y + g_ScrollPos;
    rect.right = bottomRight.x;
    rect.bottom = bottomRight.y + g_ScrollPos;
    g_ChildBaseRects.emplace_back(child, rect);
    g_ContentHeight = std::max(g_ContentHeight, static_cast<int>(rect.bottom));
  }
  g_ContentHeight += 20;
}

void ReflowChildControls(int scrollPos) {
  HDWP hdwp = BeginDeferWindowPos(static_cast<int>(g_ChildBaseRects.size()));
  if (!hdwp) {
    return;
  }

  for (const auto& [child, rect] : g_ChildBaseRects) {
    if (!IsWindow(child)) {
      continue;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    hdwp = DeferWindowPos(hdwp, child, nullptr, rect.left, rect.top - scrollPos,
                          width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!hdwp) {
      return;
    }
  }

  EndDeferWindowPos(hdwp);
}

void ApplyVerticalScroll(HWND hwnd, int requestedPos) {
  SCROLLINFO si = {sizeof(SCROLLINFO), SIF_ALL};
  GetScrollInfo(hwnd, SB_VERT, &si);

  int newPos = std::max(0, std::min(requestedPos, MaxScrollPos(si)));
  if (newPos == g_ScrollPos) {
    return;
  }

  si.nPos = newPos;
  SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
  g_ScrollPos = newPos;
  ReflowChildControls(g_ScrollPos);
  InvalidateRect(hwnd, nullptr, TRUE);
}

void AddComboString(HWND combo, const wchar_t* text) {
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

int ComboSelection(HWND combo, int fallback) {
  LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  return selection == CB_ERR ? fallback : static_cast<int>(selection);
}

bool IsChecked(HWND control) {
  return McBopomofo::ConfigApp::IsButtonChecked(
      control, IsRadioButton(control) || IsCheckButton(control));
}

void SetChecked(HWND control, bool checked) {
  McBopomofo::ConfigApp::SetButtonChecked(
      control, checked, IsRadioButton(control) || IsCheckButton(control));
}

bool IsDarkModeEnabled() {
  HKEY hKey;
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegOpenKeyExW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                     reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(hKey);
  }
  return value == 0;
}

void ApplyDarkModeToWindow(HWND hwnd) {
  BOOL dark = IsDarkModeEnabled();
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                        sizeof(dark));
}

void ApplyThemeToControl(HWND control) {
  if (!control) {
    return;
  }
  bool dark = IsDarkModeEnabled();
  const wchar_t* theme = dark ? L"DarkMode_Explorer" : L"Explorer";
  SetWindowTheme(control, theme, nullptr);
}

void ApplyThemeToDialogAndChildren(HWND hwnd) {
  ApplyDarkModeToWindow(hwnd);
  bool dark = IsDarkModeEnabled();
  const wchar_t* theme = dark ? L"DarkMode_Explorer" : L"Explorer";

  for (HWND child : {hReloadBtn, hModeCombo, hLayoutCombo, hCandidateKeysCombo,
                     hChooseSpaceCheck, hCandidateKeysCountCombo,
                     hSelectionActionCombo, hSelectBeforeRadio,
                     hSelectAfterRadio, hMoveCursorCheck, hVerticalRadio,
                     hHorizontalRadio, hCandidateFontSizeCombo,
                     hShiftToggleCheck, hUppercaseRadio, hLowercaseRadio,
                     hShiftEnterCheck, hEscClearCheck, hCtrlEnterCombo,
                     hRepeatedPunctuationCheck, hErrorBeepCheck}) {
    if (child) {
      SetWindowTheme(child, theme, nullptr);
    }
  }
  InvalidateRect(hwnd, nullptr, TRUE);
}

void SetLayoutSelection() {
  auto layout = settings.keyboardLayout();
  auto it = std::find_if(
      kLayoutOptions.begin(), kLayoutOptions.end(),
      [&](const ComboOption& option) { return layout == option.value; });
  int index = it == kLayoutOptions.end()
                  ? 0
                  : static_cast<int>(std::distance(kLayoutOptions.begin(), it));
  SendMessageW(hLayoutCombo, CB_SETCURSEL, index, 0);
}

void SetCtrlEnterSelection() {
  auto behavior = settings.ctrlEnterKeyBehavior();
  auto it = std::find_if(
      kCtrlEnterOptions.begin(), kCtrlEnterOptions.end(),
      [&](const CtrlEnterOption& option) { return behavior == option.value; });
  int index =
      it == kCtrlEnterOptions.end()
          ? 0
          : static_cast<int>(std::distance(kCtrlEnterOptions.begin(), it));
  SendMessageW(hCtrlEnterCombo, CB_SETCURSEL, index, 0);
}

void SetCandidateKeysSelection() {
  auto keys = settings.candidateKeys();
  auto it = std::find_if(
      kCandidateKeyOptions.begin(), kCandidateKeyOptions.end(),
      [&](const ComboOption& option) { return keys == option.value; });
  int index =
      it == kCandidateKeyOptions.end()
          ? 0
          : static_cast<int>(std::distance(kCandidateKeyOptions.begin(), it));
  SendMessageW(hCandidateKeysCombo, CB_SETCURSEL, index, 0);
}

void SetCandidateKeysCountSelection() {
  int count = settings.candidateKeysCount();
  SendMessageW(hCandidateKeysCountCombo, CB_SETCURSEL,
               count >= 4 && count <= 9 ? count - 4 : 5, 0);
}

void SetSelectionActionSelection() {
  auto action = settings.selectionAction();
  auto it = std::find_if(
      kSelectionActionOptions.begin(), kSelectionActionOptions.end(),
      [&](const ComboOption& option) { return action == option.value; });
  int index = it == kSelectionActionOptions.end()
                  ? 0
                  : static_cast<int>(
                        std::distance(kSelectionActionOptions.begin(), it));
  SendMessageW(hSelectionActionCombo, CB_SETCURSEL, index, 0);
}

void SetCandidateFontSizeSelection() {
  int fontSize = settings.candidateFontSize();
  auto it = std::find(kCandidateFontSizes.begin(), kCandidateFontSizes.end(),
                      fontSize);
  int index =
      it == kCandidateFontSizes.end()
          ? 3
          : static_cast<int>(std::distance(kCandidateFontSizes.begin(), it));
  SendMessageW(hCandidateFontSizeCombo, CB_SETCURSEL, index, 0);
}

void UpdateUI() {
  settings.load();

  SetLayoutSelection();
  int inputMode = static_cast<int>(settings.inputMode());
  SendMessageW(hModeCombo, CB_SETCURSEL, inputMode == 1 ? 1 : 0, 0);

  bool candidateWindowVertical = settings.candidateWindowVertical();
  SetChecked(hVerticalRadio, candidateWindowVertical);
  SetChecked(hHorizontalRadio, !candidateWindowVertical);
  SetCandidateKeysSelection();
  SetCandidateKeysCountSelection();
  SetSelectionActionSelection();

  bool selectAfterCursor = settings.selectPhraseAfterCursorAsCandidate();
  SetChecked(hSelectBeforeRadio, !selectAfterCursor);
  SetChecked(hSelectAfterRadio, selectAfterCursor);
  SetChecked(hMoveCursorCheck, settings.moveCursorAfterSelection());

  bool putLowercase = settings.putLowercaseLettersToComposingBuffer();
  SetChecked(hUppercaseRadio, !putLowercase);
  SetChecked(hLowercaseRadio, putLowercase);

  SetChecked(hEscClearCheck, settings.escKeyClearsEntireComposingBuffer());
  SetChecked(hShiftEnterCheck, settings.shiftEnterEnabled());
  SetCtrlEnterSelection();
  SetCandidateFontSizeSelection();
  SetChecked(hShiftToggleCheck, settings.shiftToggleOpenClose());
  SetChecked(hRepeatedPunctuationCheck,
             settings.repeatedPunctuationToSelectCandidateEnabled());
  SetChecked(hChooseSpaceCheck, settings.chooseCandidateUsingSpace());
  SetChecked(hErrorBeepCheck, settings.beepOnError());
}

void NotifyServer() {
  IPC::NamedPipeClient client(IPC::PIPE_NAME);
  std::string response;
  client.Call(IPC::SerializeReloadSettings(), response);
  SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 0, SMTO_ABORTIFHUNG,
                      100, nullptr);
}

void SaveAndNotify() {
  int layoutIdx = ComboSelection(hLayoutCombo, 0);
  settings.setKeyboardLayout(kLayoutOptions[layoutIdx].value);

  int modeIdx = ComboSelection(hModeCombo, 0);
  settings.setInputMode(modeIdx == 1 ? InputMode::PlainBopomofo
                                     : InputMode::McBopomofo);

  settings.setCandidateWindowVertical(IsChecked(hVerticalRadio));
  int candidateKeysIdx = ComboSelection(hCandidateKeysCombo, 0);
  settings.setCandidateKeys(kCandidateKeyOptions[candidateKeysIdx].value);
  settings.setCandidateKeysCount(ComboSelection(hCandidateKeysCountCombo, 5) +
                                 4);
  int selectionActionIdx = ComboSelection(hSelectionActionCombo, 0);
  settings.setSelectionAction(
      kSelectionActionOptions[selectionActionIdx].value);

  settings.setSelectPhraseAfterCursorAsCandidate(IsChecked(hSelectAfterRadio));
  settings.setMoveCursorAfterSelection(IsChecked(hMoveCursorCheck));
  settings.setPutLowercaseLettersToComposingBuffer(IsChecked(hLowercaseRadio));
  settings.setEscKeyClearsEntireComposingBuffer(IsChecked(hEscClearCheck));
  settings.setShiftEnterEnabled(IsChecked(hShiftEnterCheck));
  settings.setShiftToggleOpenClose(IsChecked(hShiftToggleCheck));
  int candidateFontIdx = ComboSelection(hCandidateFontSizeCombo, 3);
  settings.setCandidateFontSize(kCandidateFontSizes[candidateFontIdx]);

  int ctrlEnterIdx = ComboSelection(hCtrlEnterCombo, 0);
  settings.setCtrlEnterKeyBehavior(kCtrlEnterOptions[ctrlEnterIdx].value);

  settings.setRepeatedPunctuationToSelectCandidateEnabled(
      IsChecked(hRepeatedPunctuationCheck));
  settings.setChooseCandidateUsingSpace(IsChecked(hChooseSpaceCheck));
  settings.setBeepOnError(IsChecked(hErrorBeepCheck));

  settings.save();
  NotifyServer();
}

HWND BindControl(HWND parent, int id) {
  return GetDlgItem(parent, id);
}

void LocalizeControls(HWND hwnd) {
  HINSTANCE hInst = GetModuleHandle(nullptr);
  auto set = [&](int id, UINT strId) {
    SetDlgItemTextW(hwnd, id, LoadLocalizedStringW(hInst, strId).c_str());
  };

  set(IDC_RELOAD_BUTTON, IDS_RELOAD);
  set(IDC_INPUT_MODE_LABEL, IDS_INPUT_MODE);
  set(IDC_KEYBOARD_LAYOUT_LABEL, IDS_KEYBOARD_LAYOUT);
  set(IDC_SELECTION_KEYS_LABEL, IDS_CANDIDATE_KEYS);
  set(IDC_CHOOSE_SPACE_CHECK, IDS_CHOOSE_SPACE);
  set(IDC_CANDIDATES_PER_PAGE_LABEL, IDS_CANDIDATES_PER_PAGE);
  set(IDC_SELECTION_ACTION_LABEL, IDS_SELECTION_ACTION);
  set(IDC_SELECTION_CURSOR_LABEL, IDS_SELECTION_CURSOR);
  set(IDC_SELECT_BEFORE_RADIO, IDS_SELECT_BEFORE);
  set(IDC_SELECT_AFTER_RADIO, IDS_SELECT_AFTER);
  set(IDC_MOVE_CURSOR_CHECK, IDS_MOVE_CURSOR);
  set(IDC_CANDIDATE_PRESENTATION_LABEL, IDS_CANDIDATE_PRESENTATION);
  set(IDC_VERTICAL_RADIO, IDS_VERTICAL);
  set(IDC_HORIZONTAL_RADIO, IDS_HORIZONTAL);
  set(IDC_CANDIDATE_FONT_SIZE_LABEL, IDS_CANDIDATE_FONT_SIZE);
  set(IDC_SHIFT_TOGGLE_LABEL, IDS_SHIFT_TOGGLE);
  set(IDC_SHIFT_TOGGLE_CHECK, IDS_SHIFT_TOGGLE_OPEN_CLOSE);
  set(IDC_SHIFT_LETTER_LABEL, IDS_SHIFT_LETTER);
  set(IDC_SHIFT_LETTER_UPPER_RADIO, IDS_SHIFT_LETTER_UPPER);
  set(IDC_SHIFT_LETTER_LOWER_RADIO, IDS_SHIFT_LETTER_LOWER);
  set(IDC_SHIFT_ENTER_LABEL, IDS_SHIFT_ENTER);
  set(IDC_SHIFT_ENTER_CHECK, IDS_SHIFT_ENTER);
  set(IDC_ESC_LABEL, IDS_ESC_CLEAR);
  set(IDC_ESC_CLEAR_CHECK, IDS_ESC_CLEAR_CHECK);
  set(IDC_CTRL_ENTER_LABEL, IDS_CTRL_ENTER);
  set(IDC_CANDIDATES_PUNCTUATION_LABEL, IDS_CANDIDATES_PUNCTUATION);
  set(IDC_REPEATED_PUNCTUATION_CHECK, IDS_REPEATED_PUNCTUATION);
  set(IDC_ERROR_BEEP_LABEL, IDS_ERROR_BEEP);
  set(IDC_ERROR_BEEP_CHECK, IDS_ERROR_BEEP);
  set(IDC_MANUAL_LINK, IDS_MANUAL_LINK);
  set(IDC_PROJECT_HOMEPAGE_LINK, IDS_PROJECT_HOMEPAGE);
  SetWindowTextW(hwnd, LoadLocalizedStringW(hInst, IDS_CONFIG_TITLE).c_str());
}

void InitializeComboContents() {
  for (const auto id : kInputModeLabels) {
    AddComboString(hModeCombo,
                   LoadLocalizedStringW(GetModuleHandle(nullptr), id).c_str());
  }
  for (const auto& option : kLayoutOptions) {
    AddComboString(
        hLayoutCombo,
        LoadLocalizedStringW(GetModuleHandle(nullptr), option.labelId).c_str());
  }
  for (const auto& option : kCandidateKeyOptions) {
    std::wstring ws(option.value, option.value + strlen(option.value));
    AddComboString(hCandidateKeysCombo, ws.c_str());
  }
  for (int count = 4; count <= 9; ++count) {
    wchar_t text[4] = {};
    _itow_s(count, text, 10);
    AddComboString(hCandidateKeysCountCombo, text);
  }
  for (const auto& option : kSelectionActionOptions) {
    AddComboString(
        hSelectionActionCombo,
        LoadLocalizedStringW(GetModuleHandle(nullptr), option.labelId).c_str());
  }
  for (int fontSize : kCandidateFontSizes) {
    wchar_t text[4] = {};
    _itow_s(fontSize, text, 10);
    AddComboString(hCandidateFontSizeCombo, text);
  }
  for (const auto& option : kCtrlEnterOptions) {
    AddComboString(
        hCtrlEnterCombo,
        LoadLocalizedStringW(GetModuleHandle(nullptr), option.labelId).c_str());
  }
}

void BindControls(HWND hwnd) {
  hReloadBtn = BindControl(hwnd, IDC_RELOAD_BUTTON);
  hModeCombo = BindControl(hwnd, IDC_INPUT_MODE_COMBO);
  hLayoutCombo = BindControl(hwnd, IDC_KEYBOARD_LAYOUT_COMBO);
  hCandidateKeysCombo = BindControl(hwnd, IDC_SELECTION_KEYS_COMBO);
  hChooseSpaceCheck = BindControl(hwnd, IDC_CHOOSE_SPACE_CHECK);
  hCandidateKeysCountCombo = BindControl(hwnd, IDC_CANDIDATES_PER_PAGE_COMBO);
  hSelectionActionCombo = BindControl(hwnd, IDC_SELECTION_ACTION_COMBO);
  hSelectBeforeRadio = BindControl(hwnd, IDC_SELECT_BEFORE_RADIO);
  hSelectAfterRadio = BindControl(hwnd, IDC_SELECT_AFTER_RADIO);
  hMoveCursorCheck = BindControl(hwnd, IDC_MOVE_CURSOR_CHECK);
  hVerticalRadio = BindControl(hwnd, IDC_VERTICAL_RADIO);
  hHorizontalRadio = BindControl(hwnd, IDC_HORIZONTAL_RADIO);
  hCandidateFontSizeCombo = BindControl(hwnd, IDC_CANDIDATE_FONT_SIZE_COMBO);
  hShiftToggleCheck = BindControl(hwnd, IDC_SHIFT_TOGGLE_CHECK);
  hUppercaseRadio = BindControl(hwnd, IDC_SHIFT_LETTER_UPPER_RADIO);
  hLowercaseRadio = BindControl(hwnd, IDC_SHIFT_LETTER_LOWER_RADIO);
  hShiftEnterCheck = BindControl(hwnd, IDC_SHIFT_ENTER_CHECK);
  hEscClearCheck = BindControl(hwnd, IDC_ESC_CLEAR_CHECK);
  hCtrlEnterCombo = BindControl(hwnd, IDC_CTRL_ENTER_COMBO);
  hRepeatedPunctuationCheck = BindControl(hwnd, IDC_REPEATED_PUNCTUATION_CHECK);
  hErrorBeepCheck = BindControl(hwnd, IDC_ERROR_BEEP_CHECK);
  hManualLink = BindControl(hwnd, IDC_MANUAL_LINK);
  hProjectHomepageLink = BindControl(hwnd, IDC_PROJECT_HOMEPAGE_LINK);

  InitializeComboContents();
  LocalizeControls(hwnd);
  UpdateUI();
  ApplyThemeToDialogAndChildren(hwnd);
  CacheChildBaseRects(hwnd);
  ReflowChildControls(g_ScrollPos);
}

}  // namespace

INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_INITDIALOG:
      {
        HDC hdc = GetDC(hwnd);
        int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
        if (hdc) ReleaseDC(hwnd, hdc);
        hLinkFont = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, TRUE,
                                FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      }
      BindControls(hwnd);
      if (hManualLink) SendMessageW(hManualLink, WM_SETFONT, reinterpret_cast<WPARAM>(hLinkFont), TRUE);
      if (hProjectHomepageLink) SendMessageW(hProjectHomepageLink, WM_SETFONT, reinterpret_cast<WPARAM>(hLinkFont), TRUE);
      CenterWindow(hwnd);
      {
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
          g_FixedWidth = rect.right - rect.left;
        }
      }
      return TRUE;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return TRUE;
    case WM_GETMINMAXINFO: {
      MINMAXINFO* pMinMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
      int fixedWidth = g_FixedWidth > 0 ? g_FixedWidth : 550;
      pMinMaxInfo->ptMinTrackSize.x = fixedWidth;
      pMinMaxInfo->ptMaxTrackSize.x = fixedWidth;
      pMinMaxInfo->ptMinTrackSize.y = 460;
      pMinMaxInfo->ptMaxTrackSize.y = 760;
      break;
    }
    case WM_SIZE: {
      RECT rect;
      GetClientRect(hwnd, &rect);
      int visibleHeight = rect.bottom - rect.top;
      int totalHeight = std::max(g_ContentHeight, visibleHeight);

      SCROLLINFO si = {sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
      si.nMin = 0;
      si.nMax = std::max(0, totalHeight - 1);
      si.nPage = visibleHeight;
      si.nPos = std::min(g_ScrollPos, std::max(0, totalHeight - visibleHeight));
      SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
      ShowScrollBar(hwnd, SB_HORZ, FALSE);
      g_ScrollPos = si.nPos;
      ReflowChildControls(g_ScrollPos);
      InvalidateRect(hwnd, nullptr, TRUE);
      break;
    }
    case WM_MOUSEWHEEL: {
      int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
      int scrollLines = wheelDelta > 0 ? -3 : 3;

      ApplyVerticalScroll(hwnd, g_ScrollPos + scrollLines * kScrollLineHeight);
      break;
    }
    case WM_VSCROLL: {
      SCROLLINFO si = {sizeof(SCROLLINFO), SIF_ALL};
      GetScrollInfo(hwnd, SB_VERT, &si);
      int newPos = si.nPos;

      switch (LOWORD(wParam)) {
        case SB_LINEUP:
          newPos -= kScrollLineHeight;
          break;
        case SB_LINEDOWN:
          newPos += kScrollLineHeight;
          break;
        case SB_PAGEUP:
          newPos -= static_cast<int>(si.nPage);
          break;
        case SB_PAGEDOWN:
          newPos += static_cast<int>(si.nPage);
          break;
        case SB_THUMBTRACK:
          newPos = si.nTrackPos;
          break;
        default:
          break;
      }

      ApplyVerticalScroll(hwnd, newPos);
      break;
    }
    case WM_SETTINGCHANGE:
      if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam),
                           L"ImmersiveColorSet") == 0) {
        ApplyThemeToDialogAndChildren(hwnd);
      }
      break;
    case WM_CTLCOLORSTATIC: {
      HWND control = reinterpret_cast<HWND>(lParam);
      if (control == hManualLink || control == hProjectHomepageLink) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(0, 102, 204));
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
      }
      return FALSE;
    }
    case WM_SETCURSOR:
      if (reinterpret_cast<HWND>(wParam) == hManualLink ||
          reinterpret_cast<HWND>(wParam) == hProjectHomepageLink) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
      }
      return FALSE;
    case WM_COMMAND:
      if (LOWORD(wParam) == kReloadCommand) {
        UpdateUI();
      } else if (HIWORD(wParam) == STN_CLICKED &&
                 LOWORD(wParam) == kManualLinkCommand) {
        ShellExecuteW(hwnd, L"open", kManualUrl, nullptr, nullptr,
                      SW_SHOWNORMAL);
      } else if (HIWORD(wParam) == STN_CLICKED &&
                 LOWORD(wParam) == kProjectHomepageCommand) {
        ShellExecuteW(hwnd, L"open", kProjectHomepageUrl, nullptr, nullptr,
                      SW_SHOWNORMAL);
      } else if (HIWORD(wParam) == BN_CLICKED ||
                 HIWORD(wParam) == CBN_SELCHANGE) {
        HWND clickedControl = reinterpret_cast<HWND>(lParam);
        if (clickedControl == hVerticalRadio) {
          SetChecked(hVerticalRadio, true);
          SetChecked(hHorizontalRadio, false);
        } else if (clickedControl == hHorizontalRadio) {
          SetChecked(hVerticalRadio, false);
          SetChecked(hHorizontalRadio, true);
        } else if (clickedControl == hSelectBeforeRadio) {
          SetChecked(hSelectBeforeRadio, true);
          SetChecked(hSelectAfterRadio, false);
        } else if (clickedControl == hSelectAfterRadio) {
          SetChecked(hSelectBeforeRadio, false);
          SetChecked(hSelectAfterRadio, true);
        } else if (clickedControl == hUppercaseRadio) {
          SetChecked(hUppercaseRadio, true);
          SetChecked(hLowercaseRadio, false);
        } else if (clickedControl == hLowercaseRadio) {
          SetChecked(hUppercaseRadio, false);
          SetChecked(hLowercaseRadio, true);
        } else if (IsCheckButton(clickedControl)) {
          SetChecked(clickedControl, !IsChecked(clickedControl));
        }
        SaveAndNotify();
      }
      return TRUE;
    case WM_DESTROY:
      if (hLinkFont) DeleteObject(hLinkFont);
      PostQuitMessage(0);
      return TRUE;
    default:
      return FALSE;
  }
  return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
  McBopomofo::DpiAwareness::EnablePerMonitorDpiAwareness();

  HANDLE hSingleInstanceMutex =
      CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
  if (hSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    std::wstring windowTitle =
        LoadLocalizedStringW(hInstance, IDS_CONFIG_TITLE);
    HWND existingWindow = FindWindowW(L"#32770", windowTitle.c_str());
    if (existingWindow) {
      ShowWindow(existingWindow, SW_RESTORE);
      SetWindowPos(existingWindow, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
      SetForegroundWindow(existingWindow);
    }
    CloseHandle(hSingleInstanceMutex);
    return 0;
  }

  INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX),
                              ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
  InitCommonControlsEx(&icc);

  std::wstring windowTitle = LoadLocalizedStringW(hInstance, IDS_CONFIG_TITLE);
  HWND hwnd = CreateDialogParamW(hInstance, MAKEINTRESOURCEW(IDD_CONFIG_DIALOG),
                                 nullptr, DlgProc, 0);
  if (hwnd == nullptr) {
    if (hSingleInstanceMutex) {
      ReleaseMutex(hSingleInstanceMutex);
      CloseHandle(hSingleInstanceMutex);
    }
    return 0;
  }
  SetWindowTextW(hwnd, windowTitle.c_str());
  SendMessageW(hwnd, WM_SETICON, ICON_BIG,
               reinterpret_cast<LPARAM>(
                   LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_APP))));
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
               reinterpret_cast<LPARAM>(
                   LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_APP))));
  ShowWindow(hwnd, nCmdShow);
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!IsDialogMessageW(hwnd, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (hSingleInstanceMutex) {
    ReleaseMutex(hSingleInstanceMutex);
    CloseHandle(hSingleInstanceMutex);
  }
  return static_cast<int>(msg.wParam);
}
