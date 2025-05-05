#include <windows.h>
#include <commctrl.h>
#include <Richedit.h>
#include <shellapi.h>
#include <sddl.h>

// required libz for linking 
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

#define IDC_DISABLE 101
#define IDC_ENABLE  102
#define IDC_CONSOLE 103
#define IDC_STATUS  104

HWND hConsole, hStatus;
WNDPROC OriginalStatusProc;

void AppendText(const wchar_t* text) {
    int len = GetWindowTextLength(hConsole);
    SendMessage(hConsole, EM_SETSEL, len, len);
    SendMessage(hConsole, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessage(hConsole, EM_SCROLLCARET, 0, 0);
}

void LogLastError(const wchar_t* prefix) {
    DWORD err = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, err, 0, (LPWSTR)&msg, 0, NULL);
    if (msg) {
        wchar_t buffer[1024];
        wsprintf(buffer, L"%s: %s\r\n", prefix, msg);
        AppendText(buffer);
        LocalFree(msg);
    }
}

BOOL WriteRegValue(HKEY root, LPCWSTR subkey, LPCWSTR name, DWORD value) {
    HKEY hKey;
    if (RegCreateKeyEx(root, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        LogLastError(L"RegCreateKeyEx failed");
        return FALSE;
    }
    if (RegSetValueEx(hKey, name, 0, REG_DWORD, (BYTE*)&value, sizeof(DWORD)) != ERROR_SUCCESS) {
        LogLastError(L"RegSetValueEx failed");
        RegCloseKey(hKey);
        return FALSE;
    }
    RegCloseKey(hKey);
    return TRUE;
}

BOOL DeleteRegValue(HKEY root, LPCWSTR subkey, LPCWSTR name) {
    HKEY hKey;
    if (RegOpenKeyEx(root, subkey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        LogLastError(L"RegOpenKeyEx failed");
        return FALSE;
    }
    if (RegDeleteValue(hKey, name) != ERROR_SUCCESS) {
        LogLastError(L"RegDeleteValue failed");
        RegCloseKey(hKey);
        return FALSE;
    }
    RegCloseKey(hKey);
    return TRUE;
}

BOOL IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_LBUTTONDOWN) {
        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);

        HDC hdc = GetDC(hwnd);
        HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SIZE textSize;
        LPCWSTR urlText = L"https://piotrbania.com/?app=1";
        GetTextExtentPoint32(hdc, urlText, lstrlen(urlText), &textSize);

        SelectObject(hdc, hOldFont);
        ReleaseDC(hwnd, hdc);

        RECT rect;
        SendMessage(hwnd, SB_GETRECT, 1, (LPARAM)&rect);

        
        rect.left = rect.right - textSize.cx;

        if (PtInRect(&rect, pt)) {
            ShellExecute(NULL, L"open", urlText, NULL, NULL, SW_SHOWNORMAL);
        }
    }
    return CallWindowProc(OriginalStatusProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        LoadLibrary(L"Msftedit.dll");

        CreateWindow(L"BUTTON", L"DISABLE UPGRADES",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 150, 30, hwnd, (HMENU)IDC_DISABLE, NULL, NULL);

        CreateWindow(L"BUTTON", L"ENABLE UPGRADES",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            170, 10, 150, 30, hwnd, (HMENU)IDC_ENABLE, NULL, NULL);

        hConsole = CreateWindowEx(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            10, 50, 480, 200, hwnd, (HMENU)IDC_CONSOLE, NULL, NULL);

        InitCommonControls();
        hStatus = CreateWindow(STATUSCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, NULL, NULL);

        int parts[] = { -1, 200 };
        SendMessage(hStatus, SB_SETPARTS, 2, (LPARAM)parts);
        SendMessage(hStatus, SB_SETTEXT, 1, (LPARAM)L"https://piotrbania.com");

        OriginalStatusProc = (WNDPROC)SetWindowLongPtr(hStatus, GWLP_WNDPROC, (LONG_PTR)StatusBarProc);

        if (!IsRunAsAdmin()) {
            AppendText(L"WARNING: Application is NOT running as administrator.\r\n"
                L"Registry changes may fail.\r\n\r\n");
        }
        else {
            AppendText(L"Running with administrator privileges.\r\n\r\n");
        }

        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_DISABLE) {
            AppendText(L"Disabling Windows Upgrades...\r\n");

            WriteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate",
                L"DisableOSUpgrade", 1);
            WriteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Policies\\Microsoft\\WindowsStore",
                L"DisableOSUpgrade", 1);
            WriteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\OSUpgrade",
                L"AllowOSUpgrade", 0);
            WriteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\OSUpgrade",
                L"ReservationsAllowed", 0);
            WriteRegValue(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\Setup\\UpgradeNotification",
                L"UpgradeAvailable", 0);

            AppendText(L"Done.\r\n\r\n");
        }
        else if (LOWORD(wParam) == IDC_ENABLE) {
            AppendText(L"Enabling Windows Upgrades...\r\n");

            DeleteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate",
                L"DisableOSUpgrade");
            DeleteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Policies\\Microsoft\\WindowsStore",
                L"DisableOSUpgrade");
            DeleteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\OSUpgrade",
                L"AllowOSUpgrade");
            DeleteRegValue(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\OSUpgrade",
                L"ReservationsAllowed");
            DeleteRegValue(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\Setup\\UpgradeNotification",
                L"UpgradeAvailable");

            AppendText(L"Done.\r\n\r\n");
        }
        break;
    }
    case WM_SIZE:
        if (hConsole) {
            MoveWindow(hConsole, 10, 50, LOWORD(lParam) - 20, HIWORD(lParam) - 100, TRUE);
        }
        if (hStatus) {
            SendMessage(hStatus, WM_SIZE, 0, 0);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"UpgradeDisablerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName, L"Windows 11 Upgrade Disabler",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 360,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
