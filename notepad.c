// notepad.c
// SimpleNote - a minimal Notepad clone using raw Win32 API + the built-in EDIT control.
// No runtime dependencies beyond standard Windows DLLs already on every machine.

#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <string.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define ID_EDIT   1
#define ID_STATUS 2

// Menu / accelerator command IDs
#define IDM_NEW        101
#define IDM_OPEN       102
#define IDM_SAVE       103
#define IDM_SAVEAS     104
#define IDM_EXIT       105
#define IDM_UNDO       201
#define IDM_CUT        202
#define IDM_COPY       203
#define IDM_PASTE      204
#define IDM_SELECTALL  205
#define IDM_FIND       206
#define IDM_REPLACE    207
#define IDM_FINDNEXT   208
#define IDM_DELWORDBACK 209
#define IDM_DELWORDFWD  210
#define IDM_REDO        211
#define IDM_WORDWRAP   301
#define IDM_BGCOLOR    302
#define IDM_CHARCOUNT  303

#define REG_KEY L"Software\\SimpleNote"

HWND hEdit;
HWND hStatus;
HWND hMainWnd;
HINSTANCE g_hInstance;
HFONT hFont;
HBRUSH hBgBrush = NULL;
COLORREF bgColor;
COLORREF customColors[16] = { 0 };
wchar_t currentFile[MAX_PATH] = L"";
BOOL isModified = FALSE;
BOOL wordWrap = FALSE;
BOOL showCharCount = FALSE;
BOOL bgColorCustom = FALSE;

FINDREPLACEW frText;
HWND hFindReplaceDlg = NULL;
UINT_PTR findMsgId;
wchar_t findWhat[128] = L"";
wchar_t replaceWith[128] = L"";

void UpdateTitle() {
    wchar_t title[MAX_PATH + 32];
    const wchar_t* name = (currentFile[0] != L'\0') ? currentFile : L"Untitled";
    wsprintfW(title, L"%s%s - SimpleNote", isModified ? L"*" : L"", name);
    SetWindowTextW(hMainWnd, title);
}

void UpdateStatus() {
    wchar_t buf[64];
    wsprintfW(buf, L"%d characters", GetWindowTextLengthW(hEdit));
    SetWindowTextW(hStatus, buf);
}

// Reads HKCU\...\Personalize\AppsUseLightTheme to detect system dark mode
BOOL IsSystemDarkMode() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS result = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size);
    if (result == ERROR_SUCCESS) return (value == 0);
    return FALSE;
}

void LoadSettings(RECT* windowRect, BOOL* hasWindowRect) {
    DWORD v, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"WordWrap", RRF_RT_REG_DWORD, NULL, &v, &size) == ERROR_SUCCESS) wordWrap = v;
    size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"ShowCharCount", RRF_RT_REG_DWORD, NULL, &v, &size) == ERROR_SUCCESS) showCharCount = v;
    size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"BgColorCustom", RRF_RT_REG_DWORD, NULL, &v, &size) == ERROR_SUCCESS) bgColorCustom = v;
    size = sizeof(v);
    if (bgColorCustom && RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"BgColor", RRF_RT_REG_DWORD, NULL, &v, &size) == ERROR_SUCCESS) bgColor = (COLORREF)v;

    DWORD rectSize = sizeof(RECT);
    *hasWindowRect = (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"WindowRect", RRF_RT_REG_BINARY, NULL, windowRect, &rectSize) == ERROR_SUCCESS);
}

void SaveSettings() {
    DWORD v = wordWrap;
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, L"WordWrap", REG_DWORD, &v, sizeof(v));
    v = showCharCount;
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, L"ShowCharCount", REG_DWORD, &v, sizeof(v));
    v = bgColorCustom;
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, L"BgColorCustom", REG_DWORD, &v, sizeof(v));
    v = (DWORD)bgColor;
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, L"BgColor", REG_DWORD, &v, sizeof(v));

    RECT rc;
    GetWindowRect(hMainWnd, &rc);
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, L"WindowRect", REG_BINARY, &rc, sizeof(rc));
}

void ApplyDarkTitleBar(HWND hwnd) {
    BOOL dark = IsSystemDarkMode();
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void ResizeControls() {
    RECT rc;
    GetClientRect(hMainWnd, &rc);
    if (showCharCount) {
        int statusHeight = 20;
        MoveWindow(hEdit, 0, 0, rc.right, rc.bottom - statusHeight, TRUE);
        MoveWindow(hStatus, 0, rc.bottom - statusHeight, rc.right, statusHeight, TRUE);
    } else {
        MoveWindow(hEdit, 0, 0, rc.right, rc.bottom, TRUE);
    }
}

void ToggleCharCount() {
    showCharCount = !showCharCount;
    ShowWindow(hStatus, showCharCount ? SW_SHOW : SW_HIDE);
    ResizeControls();
    CheckMenuItem(GetMenu(hMainWnd), IDM_CHARCOUNT, MF_BYCOMMAND | (showCharCount ? MF_CHECKED : MF_UNCHECKED));
}

void SaveFile();

BOOL ConfirmSaveChanges() {
    if (!isModified) return TRUE;
    int result = MessageBoxW(hMainWnd, L"Do you want to save changes?",
                              L"SimpleNote", MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDCANCEL) return FALSE;
    if (result == IDYES) {
        SaveFile();
        if (isModified) return FALSE; // save failed or was cancelled
    }
    return TRUE;
}

void NewFile() {
    if (!ConfirmSaveChanges()) return;
    SetWindowTextW(hEdit, L"");
    currentFile[0] = L'\0';
    isModified = FALSE;
    UpdateTitle();
    UpdateStatus();
}

void SaveToFile(const wchar_t* path) {
    int len = GetWindowTextLengthW(hEdit);
    wchar_t* buffer = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!buffer) { MessageBoxW(hMainWnd, L"Out of memory.", L"Error", MB_ICONERROR); return; }
    GetWindowTextW(hEdit, buffer, len + 1);

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL);
        char* utf8Buf = (char*)malloc(utf8Len);
        if (!utf8Buf) { CloseHandle(hFile); free(buffer); MessageBoxW(hMainWnd, L"Out of memory.", L"Error", MB_ICONERROR); return; }
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8Buf, utf8Len, NULL, NULL);

        DWORD written;
        WriteFile(hFile, utf8Buf, utf8Len - 1, &written, NULL);
        CloseHandle(hFile);
        free(utf8Buf);

        wcscpy_s(currentFile, MAX_PATH, path);
        isModified = FALSE;
        UpdateTitle();
    } else {
        MessageBoxW(hMainWnd, L"Could not save file.", L"Error", MB_ICONERROR);
    }
    free(buffer);
}

void SaveFileAs() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        SaveToFile(path);
    }
}

void SaveFile() {
    if (currentFile[0] != L'\0') {
        SaveToFile(currentFile);
    } else {
        SaveFileAs();
    }
}

void LoadFile(const wchar_t* path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        if (size == INVALID_FILE_SIZE) {
            CloseHandle(hFile);
            MessageBoxW(hMainWnd, L"Could not read file.", L"Error", MB_ICONERROR);
            return;
        }
        char* utf8Buf = (char*)malloc(size + 1);
        if (!utf8Buf) {
            CloseHandle(hFile);
            MessageBoxW(hMainWnd, L"Out of memory.", L"Error", MB_ICONERROR);
            return;
        }
        DWORD readBytes;
        ReadFile(hFile, utf8Buf, size, &readBytes, NULL);
        utf8Buf[readBytes] = '\0';
        CloseHandle(hFile);

        // Skip a UTF-8 BOM if present so it doesn't show up as a stray character.
        char* utf8Start = utf8Buf;
        if (readBytes >= 3 && (unsigned char)utf8Buf[0] == 0xEF &&
            (unsigned char)utf8Buf[1] == 0xBB && (unsigned char)utf8Buf[2] == 0xBF) {
            utf8Start = utf8Buf + 3;
        }

        int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Start, -1, NULL, 0);
        wchar_t* wideBuf = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
        if (!wideBuf) {
            free(utf8Buf);
            MessageBoxW(hMainWnd, L"Out of memory.", L"Error", MB_ICONERROR);
            return;
        }
        MultiByteToWideChar(CP_UTF8, 0, utf8Start, -1, wideBuf, wideLen);

        SetWindowTextW(hEdit, wideBuf);
        wcscpy_s(currentFile, MAX_PATH, path);
        isModified = FALSE;
        UpdateTitle();
        UpdateStatus();

        free(utf8Buf);
        free(wideBuf);
    } else {
        MessageBoxW(hMainWnd, L"Could not open file.", L"Error", MB_ICONERROR);
    }
}

void OpenTextFile() {
    if (!ConfirmSaveChanges()) return;

    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        LoadFile(path);
    }
}

BOOL IsWordBreak(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

void DeleteWordBack() {
    DWORD selStart, selEnd;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd > selStart) {
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
        return;
    }

    int len = GetWindowTextLengthW(hEdit);
    wchar_t* text = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!text) return;
    GetWindowTextW(hEdit, text, len + 1);

    int i = (int)selStart;
    while (i > 0 && IsWordBreak(text[i - 1])) i--;
    while (i > 0 && !IsWordBreak(text[i - 1])) i--;
    free(text);

    SendMessageW(hEdit, EM_SETSEL, i, selStart);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
}

void DeleteWordForward() {
    DWORD selStart, selEnd;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd > selStart) {
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
        return;
    }

    int len = GetWindowTextLengthW(hEdit);
    wchar_t* text = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!text) return;
    GetWindowTextW(hEdit, text, len + 1);

    int i = (int)selEnd;
    while (i < len && IsWordBreak(text[i])) i++;
    while (i < len && !IsWordBreak(text[i])) i++;
    free(text);

    SendMessageW(hEdit, EM_SETSEL, selEnd, i);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
}

void ChooseBackgroundColor() {
    CHOOSECOLORW cc = { sizeof(cc) };
    cc.hwndOwner = hMainWnd;
    cc.lpCustColors = customColors;
    cc.rgbResult = bgColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        bgColor = cc.rgbResult;
        bgColorCustom = TRUE;
        if (hBgBrush) DeleteObject(hBgBrush);
        hBgBrush = CreateSolidBrush(bgColor);
        InvalidateRect(hEdit, NULL, TRUE);
    }
}

void ToggleWordWrap() {
    BOOL savedModified = isModified;
    int len = GetWindowTextLengthW(hEdit);
    wchar_t* buf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!buf) { MessageBoxW(hMainWnd, L"Out of memory.", L"Error", MB_ICONERROR); return; }
    GetWindowTextW(hEdit, buf, len + 1);

    DWORD selStart, selEnd;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    int firstVisibleLine = (int)SendMessageW(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    DestroyWindow(hEdit);
    wordWrap = !wordWrap;

    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL;
    if (!wordWrap) style |= WS_HSCROLL | ES_AUTOHSCROLL;

    hEdit = CreateWindowExW(0, L"EDIT", NULL, style, 0, 0, 0, 0,
        hMainWnd, (HMENU)ID_EDIT, g_hInstance, NULL);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SetWindowTextW(hEdit, buf);
    free(buf);

    SendMessageW(hEdit, EM_SETSEL, selStart, selEnd);
    SendMessageW(hEdit, EM_LINESCROLL, 0, firstVisibleLine);

    isModified = savedModified;
    UpdateTitle();
    ResizeControls();
    CheckMenuItem(GetMenu(hMainWnd), IDM_WORDWRAP, MF_BYCOMMAND | (wordWrap ? MF_CHECKED : MF_UNCHECKED));
    SetFocus(hEdit);
}

BOOL FindAndSelect(BOOL matchCase, BOOL down) {
    int len = GetWindowTextLengthW(hEdit);
    wchar_t* text = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!text) return FALSE;
    GetWindowTextW(hEdit, text, len + 1);

    DWORD selStart, selEnd;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);

    int searchLen = (int)wcslen(findWhat);
    int foundPos = -1;
    if (searchLen > 0 && searchLen <= len) {
        if (down) {
            for (int i = (int)selEnd; i <= len - searchLen; i++)
                if ((matchCase ? wcsncmp : _wcsnicmp)(text + i, findWhat, searchLen) == 0) { foundPos = i; break; }
        } else {
            for (int i = (int)selStart - 1; i >= 0; i--)
                if ((matchCase ? wcsncmp : _wcsnicmp)(text + i, findWhat, searchLen) == 0) { foundPos = i; break; }
        }
    }
    free(text);

    if (foundPos < 0) return FALSE;
    SendMessageW(hEdit, EM_SETSEL, foundPos, foundPos + searchLen);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    return TRUE;
}

void DoFind(BOOL matchCase, BOOL down) {
    if (findWhat[0] == L'\0') return;
    if (!FindAndSelect(matchCase, down)) MessageBeep(MB_ICONWARNING);
}

void DoReplace(BOOL matchCase) {
    DWORD selStart, selEnd;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd > selStart) {
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)replaceWith);
    }
    DoFind(matchCase, TRUE);
}

void DoReplaceAll(BOOL matchCase) {
    int count = 0;
    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    while (FindAndSelect(matchCase, TRUE)) {
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)replaceWith);
        count++;
    }
    wchar_t msg[64];
    wsprintfW(msg, L"%d replacement(s) made.", count);
    MessageBoxW(hMainWnd, msg, L"Replace All", MB_OK | MB_ICONINFORMATION);
}

void ShowFindDialog() {
    if (hFindReplaceDlg) { SetFocus(hFindReplaceDlg); return; }
    ZeroMemory(&frText, sizeof(frText));
    frText.lStructSize = sizeof(frText);
    frText.hwndOwner = hMainWnd;
    frText.lpstrFindWhat = findWhat;
    frText.wFindWhatLen = 128;
    frText.Flags = FR_DOWN;
    hFindReplaceDlg = FindTextW(&frText);
}

void ShowReplaceDialog() {
    if (hFindReplaceDlg) { SetFocus(hFindReplaceDlg); return; }
    ZeroMemory(&frText, sizeof(frText));
    frText.lStructSize = sizeof(frText);
    frText.hwndOwner = hMainWnd;
    frText.lpstrFindWhat = findWhat;
    frText.wFindWhatLen = 128;
    frText.lpstrReplaceWith = replaceWith;
    frText.wReplaceWithLen = 128;
    frText.Flags = FR_DOWN;
    hFindReplaceDlg = ReplaceTextW(&frText);
}

HBRUSH PaintThemedControl(HDC hdc) {
    int r = GetRValue(bgColor), g = GetGValue(bgColor), b = GetBValue(bgColor);
    int luminance = (r * 299 + g * 587 + b * 114) / 1000;
    COLORREF textColor = (luminance < 128) ? RGB(255, 255, 255) : RGB(0, 0, 0);
    SetTextColor(hdc, textColor);
    SetBkColor(hdc, bgColor);
    if (!hBgBrush) hBgBrush = CreateSolidBrush(bgColor);
    return hBgBrush;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == findMsgId) {
        FINDREPLACEW* fr = (FINDREPLACEW*)lParam;
        if (fr->Flags & FR_DIALOGTERM) hFindReplaceDlg = NULL;
        else if (fr->Flags & FR_FINDNEXT) DoFind(fr->Flags & FR_MATCHCASE, fr->Flags & FR_DOWN);
        else if (fr->Flags & FR_REPLACE) DoReplace(fr->Flags & FR_MATCHCASE);
        else if (fr->Flags & FR_REPLACEALL) DoReplaceAll(fr->Flags & FR_MATCHCASE);
        return 0;
    }
    switch (msg) {
        case WM_CREATE: {
            if (!bgColorCustom) {
                bgColor = IsSystemDarkMode() ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW);
            }
            ApplyDarkTitleBar(hwnd);

            DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL;
            if (!wordWrap) editStyle |= WS_HSCROLL | ES_AUTOHSCROLL;
            hEdit = CreateWindowExW(0, L"EDIT", NULL, editStyle,
                0, 0, 0, 0, hwnd, (HMENU)ID_EDIT, g_hInstance, NULL);

            hStatus = CreateWindowExW(0, L"STATIC", L"0 characters",
                WS_CHILD | SS_LEFT | (showCharCount ? WS_VISIBLE : 0),
                0, 0, 0, 0, hwnd, (HMENU)ID_STATUS, g_hInstance, NULL);

            hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

            CheckMenuItem(GetMenu(hwnd), IDM_WORDWRAP, MF_BYCOMMAND | (wordWrap ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(GetMenu(hwnd), IDM_CHARCOUNT, MF_BYCOMMAND | (showCharCount ? MF_CHECKED : MF_UNCHECKED));

            UpdateTitle();
            return 0;
        }
        case WM_SIZE:
            ResizeControls();
            return 0;

        case WM_SETFOCUS:
            SetFocus(hEdit);
            return 0;

        case WM_CTLCOLOREDIT: {
            if ((HWND)lParam == hEdit) {
                return (LRESULT)PaintThemedControl((HDC)wParam);
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            if ((HWND)lParam == hStatus) {
                return (LRESULT)PaintThemedControl((HDC)wParam);
            }
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_NEW: NewFile(); break;
                case IDM_OPEN: OpenTextFile(); break;
                case IDM_SAVE: SaveFile(); break;
                case IDM_SAVEAS: SaveFileAs(); break;
                case IDM_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
                case IDM_UNDO: SendMessageW(hEdit, WM_UNDO, 0, 0); break;
                case IDM_REDO: SendMessageW(hEdit, WM_UNDO, 0, 0); break;
                case IDM_CUT: SendMessageW(hEdit, WM_CUT, 0, 0); break;
                case IDM_COPY: SendMessageW(hEdit, WM_COPY, 0, 0); break;
                case IDM_PASTE: SendMessageW(hEdit, WM_PASTE, 0, 0); break;
                case IDM_SELECTALL: SendMessageW(hEdit, EM_SETSEL, 0, -1); break;
                case IDM_DELWORDBACK: DeleteWordBack(); break;
                case IDM_DELWORDFWD: DeleteWordForward(); break;
                case IDM_FIND: ShowFindDialog(); break;
                case IDM_REPLACE: ShowReplaceDialog(); break;
                case IDM_FINDNEXT: DoFind(frText.Flags & FR_MATCHCASE, TRUE); break;
                case IDM_WORDWRAP: ToggleWordWrap(); break;
                case IDM_BGCOLOR: ChooseBackgroundColor(); break;
                case IDM_CHARCOUNT: ToggleCharCount(); break;
                case ID_EDIT:
                    if (HIWORD(wParam) == EN_CHANGE) {
                        isModified = TRUE;
                        UpdateTitle();
                        UpdateStatus();
                    }
                    break;
            }
            return 0;
        }
        case WM_CLOSE: {
            if (ConfirmSaveChanges()) {
                SaveSettings();
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    const wchar_t CLASS_NAME[] = L"SimpleNoteClass";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(1);
    wc.hIcon = LoadIconW(hInstance, L"MAINICON");
    wc.hIconSm = wc.hIcon;

    RegisterClassExW(&wc);

    RECT savedRect;
    BOOL hasSavedRect;
    LoadSettings(&savedRect, &hasSavedRect);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 800, h = 600;
    if (hasSavedRect) {
        int rw = savedRect.right - savedRect.left;
        int rh = savedRect.bottom - savedRect.top;
        if (rw > 100 && rh > 100 &&
            savedRect.left > -32000 && savedRect.top > -32000 &&
            savedRect.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
            savedRect.top < GetSystemMetrics(SM_CYVIRTUALSCREEN)) {
            x = savedRect.left; y = savedRect.top; w = rw; h = rh;
        }
    }

    hMainWnd = CreateWindowExW(0, CLASS_NAME, L"SimpleNote",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        NULL, NULL, hInstance, NULL);

    HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(2));
    findMsgId = RegisterWindowMessageA(FINDMSGSTRING);

    ShowWindow(hMainWnd, nCmdShow);

    if (lpCmdLine && lpCmdLine[0] != L'\0') {
        wchar_t path[MAX_PATH];
        wchar_t* src = lpCmdLine;
        if (src[0] == L'"') {
            src++;
            wchar_t* end = wcschr(src, L'"');
            if (end) *end = L'\0';
        }
        wcsncpy_s(path, MAX_PATH, src, _TRUNCATE);
        LoadFile(path);
    }

    MSG msg = { 0 };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (hFindReplaceDlg && IsDialogMessageW(hFindReplaceDlg, &msg)) continue;
        if (!TranslateAcceleratorW(hMainWnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
