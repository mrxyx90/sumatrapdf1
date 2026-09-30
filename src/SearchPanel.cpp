/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include <dwmapi.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>
#include "base/CmdLineArgs.h"
#include "base/File.h"
#include "base/Win.h"
#include "base/UITask.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"
#include "gui/win/WebView.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "Theme.h"
#include "DarkMode.h"
#include "resource.h"
#include "SearchPanel.h"
#include "AIChatCommon.h"
#include "AIChatPanel.h"

struct SearchButtonState {
    bool isHovered = false;
    bool isPressed = false;
    bool isTracking = false;
};

static SearchButtonState gBackState;
static SearchButtonState gForwardState;
static SearchButtonState gCloseState;

static COLORREF ColorToCOLORREF(Color c) {
    return RGB(GetRed(c), GetGreen(c), GetBlue(c));
}

static void PaintOwnerDrawButton(HWND hwnd, const WCHAR* label, bool isHovered, bool isPressed, bool isCloseBtn) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    COLORREF bgCol;
    COLORREF txtCol = ColorToCOLORREF(ThemeWindowTextColor());

    if (isCloseBtn) {
        if (isPressed) {
            bgCol = RGB(200, 60, 10); // Darker orange click effect
            txtCol = RGB(255, 255, 255);
        } else if (isHovered) {
            bgCol = RGB(240, 80, 20); // Orange hover effect
            txtCol = RGB(255, 255, 255);
        } else {
            bgCol = ColorToCOLORREF(ThemeControlBackgroundColor());
        }
    } else {
        if (isPressed) {
            bgCol = ColorToCOLORREF(AccentColor(ThemeControlBackgroundColor(), 50)); // Click press effect
        } else if (isHovered) {
            bgCol = ColorToCOLORREF(AccentColor(ThemeControlBackgroundColor(), 30)); // Title bar button hover effect
        } else {
            bgCol = ColorToCOLORREF(ThemeControlBackgroundColor());
        }
    }

    HBRUSH brBg = CreateSolidBrush(bgCol);
    FillRect(hdc, &rc, brBg);
    DeleteObject(brBg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, txtCol);

    int fontHeight = (rc.bottom - rc.top) * 85 / 100;
    HFONT font = CreateFontW(-fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, font);

    RECT rcText = rc;
    int fontLeadingOffset = fontHeight / 10;
    rcText.top -= fontLeadingOffset;
    rcText.bottom -= fontLeadingOffset;

    if (isPressed) {
        OffsetRect(&rcText, DpiScale(1), DpiScale(1)); // Shift text 1px down and right on click!
    }

    DrawTextW(hdc, label, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

static bool IsPointOnVisualButton(HWND hwnd, POINT ptScreen) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    POINT ptClient = ptScreen;
    ScreenToClient(hwnd, &ptClient);
    return PtInRect(&rc, ptClient) != FALSE;
}

static LRESULT CALLBACK WndProcSearchBackBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    switch (msg) {
        case WM_NCHITTEST: {
            POINT ptScreen = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (!IsPointOnVisualButton(hwnd, ptScreen)) {
                return HTTRANSPARENT;
            }
            return HTCLIENT;
        }
        case WM_MOUSEMOVE:
            if (!gBackState.isTracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                gBackState.isTracking = true;
            }
            if (!gBackState.isHovered) {
                gBackState.isHovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            gBackState.isHovered = false;
            gBackState.isPressed = false;
            gBackState.isTracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            gBackState.isPressed = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintOwnerDrawButton(hwnd, L"←", gBackState.isHovered, gBackState.isPressed, false);
            return 0;
        case WM_LBUTTONUP:
            if (gBackState.isPressed) {
                gBackState.isPressed = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                if (win && win->webSearchWebView && win->webSearchWebView->CanGoBack()) {
                    win->webSearchWebView->GoBack();
                }
            }
            return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcSearchForwardBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    switch (msg) {
        case WM_NCHITTEST: {
            POINT ptScreen = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (!IsPointOnVisualButton(hwnd, ptScreen)) {
                return HTTRANSPARENT;
            }
            return HTCLIENT;
        }
        case WM_MOUSEMOVE:
            if (!gForwardState.isTracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                gForwardState.isTracking = true;
            }
            if (!gForwardState.isHovered) {
                gForwardState.isHovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            gForwardState.isHovered = false;
            gForwardState.isPressed = false;
            gForwardState.isTracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            gForwardState.isPressed = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintOwnerDrawButton(hwnd, L"→", gForwardState.isHovered, gForwardState.isPressed, false);
            return 0;
        case WM_LBUTTONUP:
            if (gForwardState.isPressed) {
                gForwardState.isPressed = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                if (win && win->webSearchWebView && win->webSearchWebView->CanGoForward()) {
                    win->webSearchWebView->GoForward();
                }
            }
            return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcSearchCloseBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    switch (msg) {
        case WM_NCHITTEST: {
            POINT ptScreen = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (!IsPointOnVisualButton(hwnd, ptScreen)) {
                return HTTRANSPARENT;
            }
            return HTCLIENT;
        }
        case WM_MOUSEMOVE:
            if (!gCloseState.isTracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                gCloseState.isTracking = true;
            }
            if (!gCloseState.isHovered) {
                gCloseState.isHovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            gCloseState.isHovered = false;
            gCloseState.isPressed = false;
            gCloseState.isTracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            gCloseState.isPressed = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintOwnerDrawButton(hwnd, L"×", gCloseState.isHovered, gCloseState.isPressed, true);
            return 0;
        case WM_LBUTTONUP:
            if (gCloseState.isPressed) {
                gCloseState.isPressed = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                if (win) {
                    WindowTab* tab = win->CurrentTab();
                    if (tab) {
                        AIChatSetTabPanelOpen(tab, AIChatBackend::None);
                    }
                    AIChatSyncPanelsToCurrentTab(win);
                    if (win->hwndCanvas) {
                        HwndSetFocus(win->hwndCanvas);
                    } else if (win->hwndFrame) {
                        HwndSetFocus(win->hwndFrame);
                    }
                    ScheduleUiUpdate(win);
                }
            }
            return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void OnWebSearchWebViewNavigated(void* ctx, Str, bool) {
    MainWindow* win = (MainWindow*)ctx;
    if (!IsMainWindowValidAndNotClosing(win) || !win->hwndAiChatBox) {
        return;
    }
    if (win->webSearchWebView) {
        win->webSearchWebView->SetControllerVisible(true);
        RelayoutSearchPanel(win);
    }
}

static void OnWebSearchHistoryChanged(void* ctx, bool, bool) {
    MainWindow* win = (MainWindow*)ctx;
    if (!IsMainWindowValidAndNotClosing(win) || !win->hwndAiChatBox) {
        return;
    }
    RelayoutSearchPanel(win);
}

static bool OnWebSearchNavigationStarting(void* ctx, Str url, bool newWindow) {
    if (newWindow && ctx) {
        MainWindow* win = (MainWindow*)ctx;
        if (IsMainWindowValidAndNotClosing(win) && win->webSearchWebView) {
            win->webSearchWebView->Navigate(url);
            return false;
        }
    }
    return true;
}

struct EdgeProcessInfo {
    DWORD pid = 0;
    HANDLE hProcess = nullptr;
    Vec<HWND> hwnds;

    EdgeProcessInfo() {}
    EdgeProcessInfo(DWORD pid, HANDLE hProcess) : pid(pid), hProcess(hProcess) {}
};

static Vec<EdgeProcessInfo> gEdgeSearchProcesses;

static bool IsEdgeSearchPid(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    for (const auto& info : gEdgeSearchProcesses) {
        if (info.pid == pid) {
            return true;
        }
    }
    return false;
}

struct FindAllHwndsData {
    Vec<HWND> hwnds;

    FindAllHwndsData() {}
};

// Callback to find all visible top-level windows for Edge search processes
static BOOL CALLBACK FindAllEdgeWindowsProc(HWND hwnd, LPARAM lp) {
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (IsEdgeSearchPid(pid)) {
        auto* data = (FindAllHwndsData*)lp;
        if (!VecContains(data->hwnds, hwnd)) {
            VecAppend(data->hwnds, hwnd);
        }
    }
    return TRUE;
}

static Vec<HWND> FindAllEdgeWindows() {
    FindAllHwndsData data;
    EnumWindows(FindAllEdgeWindowsProc, (LPARAM)&data);
    return data.hwnds;
}

struct EdgeHwndResult {
    Vec<HWND> hwnds;

    EdgeHwndResult() {}
};

// UI thread callback to safely store all discovered HWNDs
static void OnFoundEdgeHwndsUI(EdgeHwndResult* res) {
    if (!res) {
        return;
    }
    for (HWND hwnd : res->hwnds) {
        if (!IsWindow(hwnd)) {
            continue;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        for (auto& info : gEdgeSearchProcesses) {
            if (info.pid == pid) {
                if (!VecContains(info.hwnds, hwnd)) {
                    VecAppend(info.hwnds, hwnd);
                }
                break;
            }
        }
    }
    delete res;
}

// Poll for process HWNDs asynchronously on a background thread
static void PollEdgeHwndOnThread(void* param) {
    DWORD pid = (DWORD)(uintptr_t)param;
    if (pid == 0) {
        return;
    }
    for (int i = 0; i < 40; i++) {
        Vec<HWND> hwnds = FindAllEdgeWindows();
        if (len(hwnds) > 0) {
            auto* res = new EdgeHwndResult();
            res->hwnds = hwnds;
            uitask::Post(MkFunc0(OnFoundEdgeHwndsUI, res), "UpdateEdgeHwnd");
        }
        SleepInMs(50);
    }
}

void CloseAllEdgeSearchProcesses() {
    for (auto& info : gEdgeSearchProcesses) {
        for (HWND hwnd : info.hwnds) {
            if (IsWindow(hwnd)) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        }
        if (info.hProcess) {
            if (WaitForSingleObject(info.hProcess, 200) == WAIT_TIMEOUT) {
                TerminateProcess(info.hProcess, 0);
            }
            CloseHandle(info.hProcess);
        }
        VecReset(info.hwnds);
    }
    VecReset(gEdgeSearchProcesses);
}

void CreateSearchPanel(MainWindow* win) {
    if (!win) {
        return;
    }
    win->webSearchWebView = nullptr;
    win->webSearchWebViewReady = false;
}

void DestroySearchPanel(MainWindow* win) {
    if (!win) {
        return;
    }
    CloseAllEdgeSearchProcesses();
    win->webSearchWebViewReady = false;
    if (win->hwndSearchBack) {
        DestroyWindow(win->hwndSearchBack);
        win->hwndSearchBack = nullptr;
    }
    if (win->hwndSearchForward) {
        DestroyWindow(win->hwndSearchForward);
        win->hwndSearchForward = nullptr;
    }
    if (win->hwndSearchClose) {
        DestroyWindow(win->hwndSearchClose);
        win->hwndSearchClose = nullptr;
    }
    delete win->webSearchWebView;
    win->webSearchWebView = nullptr;
}

void RelayoutSearchPanel(MainWindow* win) {
    if (!win || !win->webSearchWebView) {
        return;
    }
    Rect rc = HwndClientRect(win->hwndAiChatBox);
    if (rc.dx > 0 && rc.dy > 0) {
        // Full page search webview (0, 0, rc.dx, rc.dy) with zero top white bar
        MoveWindow(win->webSearchWebView->hwnd, 0, 0, rc.dx, rc.dy, TRUE);
        win->webSearchWebView->UpdateWebviewSize();

        bool isSearchTab = (win->activeSidebarTab == 1);
        int btnSize = DpiScale(36);
        int pad = DpiScale(4);
        int bottomY = rc.dy - btnSize;

        bool canBack = win->webSearchWebView->CanGoBack();
        bool canFwd = win->webSearchWebView->CanGoForward();

        if (win->hwndSearchBack) {
            SetWindowPos(win->hwndSearchBack, HWND_TOP, pad, bottomY, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab && canBack ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            EnableWindow(win->hwndSearchBack, canBack);
            InvalidateRect(win->hwndSearchBack, nullptr, FALSE);
        }

        int fwdX = pad;
        if (canBack) {
            fwdX += btnSize + DpiScale(2);
        }
        if (win->hwndSearchForward) {
            SetWindowPos(win->hwndSearchForward, HWND_TOP, fwdX, bottomY, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab && canFwd ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            EnableWindow(win->hwndSearchForward, canFwd);
            InvalidateRect(win->hwndSearchForward, nullptr, FALSE);
        }

        if (win->hwndSearchClose) {
            SetWindowPos(win->hwndSearchClose, HWND_TOP, rc.dx - btnSize, bottomY, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            InvalidateRect(win->hwndSearchClose, nullptr, FALSE);
        }
    }
}

void OpenSearchSelectionInSidebar(MainWindow* win, Str engineName, Str url) {
    if (!win) {
        return;
    }
    if (gSettings && (gSettings->searchUIFloating || str::StartsWithI(gSettings->selectionSearchMode, StrL("popup")) || str::StartsWithI(gSettings->selectionSearchMode, StrL("floating")) || str::StartsWithI(gSettings->selectionSearchMode, StrL("window")))) {
        OpenSearchSelectionInPopup(win, engineName, url);
        return;
    }
    if (!HasWebView()) {
        return;
    }
    if (!win->hwndAiChatBox) {
        return;
    }
    win->aiChatUsed = true;
    win->webSearchUsed = true;
    win->activeSidebarTab = 1;
    str::ReplaceWithCopy(&win->webSearchEngineName, engineName);
    win->uiState.aiChatVisible = true;

    if (!win->hwndSearchBack && win->hwndAiChatBox) {
        HINSTANCE hinst = GetModuleHandle(nullptr);
        win->hwndSearchBack = CreateWindowExW(0, WC_BUTTONW, L"←", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, win->hwndAiChatBox, nullptr, hinst, nullptr);
        SetWindowSubclass(win->hwndSearchBack, WndProcSearchBackBtn, NextSubclassId(), (DWORD_PTR)win);

        win->hwndSearchForward = CreateWindowExW(0, WC_BUTTONW, L"→", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, win->hwndAiChatBox, nullptr, hinst, nullptr);
        SetWindowSubclass(win->hwndSearchForward, WndProcSearchForwardBtn, NextSubclassId(), (DWORD_PTR)win);

        win->hwndSearchClose = CreateWindowExW(0, WC_BUTTONW, L"×", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, win->hwndAiChatBox, nullptr, hinst, nullptr);
        SetWindowSubclass(win->hwndSearchClose, WndProcSearchCloseBtn, NextSubclassId(), (DWORD_PTR)win);
    }

    if (!win->webSearchWebView) {
        auto* webView = new WebviewWnd();
        webView->events.ctx = win;
        webView->events.navigationStarting = OnWebSearchNavigationStarting;
        webView->events.navigationCompleted = OnWebSearchWebViewNavigated;
        webView->events.historyChanged = OnWebSearchHistoryChanged;
        TempStr localAppData = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA);
        TempStr safeName = str::ReplaceTemp(engineName, StrL(" "), StrL("_"));
        webView->dataDir = str::Dup(fmt("%s\\Apdf\\Search_%s", localAppData, safeName));
        webView->allowClipboardRead = false;
        webView->defaultBackgroundColor = kColWhite;
        webView->forwardAppAccelerators = true;

        Rect rc = HwndClientRect(win->hwndAiChatBox);
        CreateWebViewArgs wvArgs;
        wvArgs.parent = win->hwndAiChatBox;
        wvArgs.pos = Rect(0, 0, rc.dx, rc.dy);
        webView->Create(wvArgs);
        if (webView->hwnd) {
            webView->Navigate(url);
            webView->SetIsVisible(true);
            win->webSearchWebView = webView;
            win->webSearchWebViewReady = true;
        } else {
            delete webView;
        }
    } else {
        win->webSearchWebView->Navigate(url);
        win->webSearchWebView->SetIsVisible(win->activeSidebarTab == 1);
    }
    RelayoutAIChatPanel(win);
    ScheduleUiUpdate(win);
}

static TempStr GetEdgeExePathTemp() {
    static Str cachedPath = {};
    if (len(cachedPath) > 0) {
        return str::DupTemp(cachedPath);
    }

    TempStr p = ReadRegStrTemp(HKEY_LOCAL_MACHINE, StrL("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe"), {});
    if (len(p) > 0 && file::Exists(p)) {
        cachedPath = str::Dup(p);
        return p;
    }
    p = ReadRegStrTemp(HKEY_CURRENT_USER, StrL("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe"), {});
    if (len(p) > 0 && file::Exists(p)) {
        cachedPath = str::Dup(p);
        return p;
    }

    const Str candidates[] = {
        StrL("C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe"),
        StrL("C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe"),
    };
    for (Str cand : candidates) {
        if (file::Exists(cand)) {
            cachedPath = str::Dup(cand);
            return str::DupTemp(cand);
        }
    }

    TempStr localAppData = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA);
    if (len(localAppData) > 0) {
        p = path::JoinTemp(localAppData, StrL("Microsoft\\Edge\\Application\\msedge.exe"));
        if (file::Exists(p)) {
            cachedPath = str::Dup(p);
            return p;
        }
    }

    cachedPath = StrL("msedge.exe");
    return StrL("msedge.exe");
}

void OpenSearchSelectionInPopup(MainWindow* win, Str engineName, Str url) {
    TempStr edgeExe = GetEdgeExePathTemp();
    if (len(edgeExe) == 0) {
        LaunchBrowser(url);
        return;
    }

    Rect rcWork = GetWorkAreaRect({}, win ? win->hwndFrame : nullptr);

    // Reference values at 100% DPI on 1920x1080: size (450, 815), position (1015, 21)
    int w = DpiScale(450);
    int h = std::min(DpiScale(815), rcWork.dy - DpiScale(40));
    int x = rcWork.x + (int)((i64)1015 * rcWork.dx / 1920);
    int y = rcWork.y + DpiScale(21);

    if (x + w > rcWork.x + rcWork.dx) {
        x = rcWork.x + rcWork.dx - w;
    }
    if (x < rcWork.x) {
        x = rcWork.x;
    }
    if (y + h > rcWork.y + rcWork.dy) {
        y = rcWork.y;
    }

    TempStr localAppData = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA);
    TempStr profileDir = path::JoinTemp(localAppData, StrL("Apdf\\EdgeSearchProfile"));

    TempStr params = fmt("--app=\"%s\" --user-data-dir=\"%s\" --window-size=%d,%d --window-position=%d,%d",
                         url, profileDir, w, h, x, y);

    TempStr cmdLine = fmt("\"%s\" %s", edgeExe, params);
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    TempWStr cmdW = ToWStrTemp(cmdLine);
    if (CreateProcessW(nullptr, cmdW.s, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        EdgeProcessInfo info(pi.dwProcessId, pi.hProcess);
        VecAppend(gEdgeSearchProcesses, info);
        CloseHandle(pi.hThread);
        RunAsync(MkFunc0(PollEdgeHwndOnThread, (void*)(uintptr_t)pi.dwProcessId), StrL("PollEdgeHwnd"));
    } else {
        LaunchBrowser(url);
    }
}

void OnSearchPopupFrameSize(MainWindow*, int sizeType) {
    if (sizeType != SIZE_MINIMIZED) {
        return;
    }
    for (auto& info : gEdgeSearchProcesses) {
        for (HWND hwnd : info.hwnds) {
            if (IsWindow(hwnd)) {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
        }
    }
}
