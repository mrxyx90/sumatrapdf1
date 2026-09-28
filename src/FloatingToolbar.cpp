/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "gui/Dpi.h"
#include "base/Pixmap.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "gui/VirtHost.h"
#include "SvgIcons.h"
#include "Commands.h"
#include "Settings.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "SumatraPDF.h"
#include "FloatingToolbar.h"
#include "ScreenshotCapture.h"
#include "AnnotPlacement.h"
#include "Theme.h"
#include "Notifications.h"
#include "Toolbar.h"
#include "CommandPalette.h"
#include "DocController.h"
#include "Translations.h"

constexpr const WCHAR* kFloatingToolbarClassName = L"SumatraFloatingToolbar";
constexpr int kFloatingToolbarIconSize = 22;
constexpr int kFloatingToolbarButtonSize = 38;
constexpr int kFloatingToolbarMargin = 5;
constexpr int kFloatingToolbarGap = 2;
constexpr int kFloatingToolbarRadius = 9;
constexpr int kFloatingToolbarSeparatorGap = 5;


struct FloatingToolbarButton {
    const char* icon = nullptr;
    int cmdId = 0;
    Str tip{};
};

static constexpr const char* kScreenshotIcon =
    "<svg viewBox=\"0 0 24 24\"><path fill=\"currentColor\" d=\"M9 3l-1.5 2H5c-1.1 0-2 .9-2 2v11c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2h-2.5L15 3H9zm3 5a5 5 0 1 1 0 10 5 5 0 0 1 0-10zm0 2a3 3 0 1 0 0 6 3 3 0 0 0 0-6z\"/></svg>";

static const FloatingToolbarButton gButtons[] = {
    {gIconCommandPalette, CmdCommandPalette, StrL("Command palette")},
    // Use the brush/highlighter glyph, not the selection-toolbar text-marking
    // glyph, so this button is visually the highlighter tool.
    {gIconAnnotHighlightBrush, CmdAnnotationHighlightBrush, StrL("Highlight")},
    {gIconAnnotInk, CmdCreateAnnotInk, StrL("Ink")},
    {gIconAnnotFreeText, CmdCreateAnnotFreeText, StrL("Free text")},
    {gIconEditAnnotations, CmdToggleEditPDF, StrL("Edit PDF")},
};

struct FloatingToolbar {
    MainWindow* win = nullptr;
    VirtHost* host = nullptr;
    Rect lastFrameRect;
    Rect lastToolbarRect;
    bool dragging = false;
    POINT dragStart{};
    Rect dragOrig;
    int activeCmdId = 0;
    bool screenshotAnimating = false;
};

static Color FloatingBg() {
    return ThemeControlBackgroundColor();
}

static Color FloatingBorder() {
    return ThemeEdgeColor();
}

static Color FloatingHover() {
    return ThemeHotBackgroundColor();
}

struct FloatingIconButton : VirtIconButton {
    int sideLen = 0;
    Color hoverBg = kColorUnset;
    FloatingToolbar* toolbar = nullptr;
    Pixmap* pixmapActive = nullptr;

    Size GetIdealSize() override {
        return {sideLen, sideLen};
    }

    void Paint(VirtPaintCtx& ctx) override {
        bool paletteOpen = toolbar && id == CmdCommandPalette && IsCommandPaletteOpen(toolbar->win);
        bool active = (toolbar && toolbar->activeCmdId == id) || paletteOpen;
        bool screenshotFlash = toolbar && toolbar->screenshotAnimating && id == CmdScreenshot;
        bool isHovered = IsEnabled() && HasFlag(vwfHovered);

        // Paint hover first, then the selected state on top. This keeps the
        // hover feedback available without ever covering the selection state.
        if (isHovered && hoverBg != kColorUnset) {
            ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(6), hoverBg);
        }
        if (active || screenshotFlash) {
            ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(6), MkRgb(0x3e, 0x53, 0x68));
        }

        Pixmap* px = (active || screenshotFlash) && pixmapActive ? pixmapActive : pixmap;
        if (px) {
            Size s2 = {px->width, px->height};
            int x = ctx.content.x + (ctx.content.dx - s2.dx) / 2;
            int y = ctx.content.y + (ctx.content.dy - s2.dy) / 2;
            ctx.gfx->DrawPixmap(px, {x, y, s2.dx, s2.dy});
        }
    }
};



static void OnFloatingButton(FloatingToolbar* tb, VirtMouseEvent* ev) {
    if (!tb || !ev || !ev->target) {
        return;
    }
    int cmd = ev->target->id;
    if (!cmd) {
        return;
    }

    if (cmd == CmdCommandPalette) {
        HwndPostCommand(tb->win->hwndFrame, cmd, 0);
        return;
    }

    if (cmd == CmdScreenshot) {
        TempStr savedPath = TakeScreenshotOfWindow(tb->win->hwndCanvas);
        if (len(savedPath) > 0) {
            str::Builder msg;
            msg.Append(fmt("Saved screenshot to '%s'", savedPath));
            NotificationCreateArgs args;
            args.hwndParent = tb->win->hwndCanvas;
            args.font = GetDefaultGuiFont();
            args.timeoutMs = kNotifDefaultTimeOut;
            args.msg = ToStr(msg);
            args.plainText = true;
            ShowNotification(args);
        }
        tb->screenshotAnimating = true;
        SetTimer(tb->host->native, 1, 300, nullptr);
        tb->host->Invalidate(false);
        return;
    }

    bool isAnnotTool = (cmd == CmdAnnotationHighlightBrush || cmd == CmdCreateAnnotInk || cmd == CmdCreateAnnotFreeText ||
                        cmd == CmdCreateAnnotUnderline || cmd == CmdCreateAnnotSquiggly || cmd == CmdCreateAnnotStrikeOut ||
                        (cmd >= CmdCreateAnnotFirst && cmd <= CmdCreateAnnotLast));

    // If Edit PDF temporarily revealed the normal top toolbar, any non-annotation action restores its hidden state.
    if (cmd != CmdToggleEditPDF && !isAnnotTool && tb->win->floatingEditPdfRevealedToolbar) {
        tb->win->floatingEditPdfRevealedToolbar = false;
        tb->win->isToolbarVisible = false;
        if (tb->win->hwndToolbar) {
            ShowWindow(tb->win->hwndToolbar, SW_HIDE);
        }
        ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
    }

    // Clicking the already active tool again deactivates it
    if (tb->activeCmdId == cmd) {
        CancelAnnotationPlacement(tb->win);
        if (cmd == CmdToggleEditPDF) {
            if (tb->win->floatingEditPdfRevealedToolbar) {
                tb->win->floatingEditPdfRevealedToolbar = false;
                tb->win->isToolbarVisible = false;
                if (tb->win->hwndToolbar) {
                    ShowWindow(tb->win->hwndToolbar, SW_HIDE);
                }
                ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
            }
            HwndPostCommand(tb->win->hwndFrame, cmd, 0);
            // the toggle is posted, not sent: clear the highlight now so the
            // button is not lit while the command is still in the queue
            tb->activeCmdId = 0;
        } else if (!tb->win->isToolbarVisible && !tb->win->isToolbarOverlay && tb->win->pdfAnnotationsToolbarEnabled) {
            // toolbar is hidden: deselecting the tool also exits Edit PDF
            // mode, so no half-on state is left behind. The toggle is posted,
            // not sent, so clear the highlight now; the command re-syncs
            // once it lands
            HwndPostCommand(tb->win->hwndFrame, CmdToggleEditPDF, 0);
            tb->activeCmdId = 0;
        } else {
            // the placement tool ended but Edit PDF mode stays on: re-sync so
            // the toolbar highlights Edit PDF instead of ending up with no
            // active button
            UpdateFloatingToolbarActiveState(tb->win);
        }
        tb->host->Invalidate(false);
        return;
    }

    // Switching to a new tool: cancel old placement and activate new tool instantly in 1 click
    bool wasPlacing = IsPlacingAnnotation(tb->win);
    bool wasPdfEditEnabled = tb->win->pdfAnnotationsToolbarEnabled;
    CancelAnnotationPlacement(tb->win);
    tb->activeCmdId = cmd;
    tb->host->Invalidate(false);

    if (cmd == CmdToggleEditPDF) {
        if (!tb->win->isToolbarVisible && !tb->win->isToolbarOverlay && tb->win->hwndToolbar) {
            tb->win->floatingEditPdfRevealedToolbar = true;
            tb->win->isToolbarVisible = true;
            ShowWindow(tb->win->hwndToolbar, SW_SHOW);
            ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
        }
        if (wasPlacing || wasPdfEditEnabled) {
            EnablePdfAnnotationsToolbar(tb->win);
            ToolbarUpdateStateForWindow(tb->win, true);
        } else {
            HwndPostCommand(tb->win->hwndFrame, cmd, 0);
        }
        return;
    }

    if (isAnnotTool) {
        // picking an annotation tool from floating toolbar will not show system edit toolbar
        if (tb->win->floatingEditPdfRevealedToolbar) {
            tb->win->floatingEditPdfRevealedToolbar = false;
            tb->win->isToolbarVisible = false;
            if (tb->win->hwndToolbar) {
                ShowWindow(tb->win->hwndToolbar, SW_HIDE);
            }
            ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
        }
        ToolbarUpdateStateForWindow(tb->win, false);
        HwndSendCommand(tb->win->hwndFrame, cmd, 0);
        return;
    }

    HwndPostCommand(tb->win->hwndFrame, cmd, 0);
}

static void PaintFloatingToolbar(FloatingToolbar*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRoundedRect(ev->clientRect, DpiScale(kFloatingToolbarRadius), FloatingBg(), FloatingBorder());
}

static bool FloatingToolbarIsForPdf(FloatingToolbar* tb) {
    return tb && tb->win && tb->win->IsDocLoaded() && tb->win->AsFixed() != nullptr;
}

static void MoveFloatingToolbar(FloatingToolbar* tb, Rect r) {
    if (!tb || !tb->host) {
        return;
    }
    tb->lastToolbarRect = r;
    // Keep this popup above the document and sidebar child windows. The
    // bookmark sidebar can be created/reordered after the toolbar at startup.
    SetWindowPos(tb->host->native, HWND_TOP, r.x, r.y, r.dx, r.dy, SWP_NOACTIVATE);
}

static int GetFloatingToolbarMinY(MainWindow* win, int frameTop) {
    int topConstraint = frameTop;
    if (win->hwndToolbar && IsWindowVisible(win->hwndToolbar)) {
        RECT tr{};
        GetWindowRect(win->hwndToolbar, &tr);
        POINT pt{tr.left, tr.bottom};
        ScreenToClient(win->hwndFrame, &pt);
        topConstraint = frameTop + pt.y;
    } else if (!win->captionRect.IsEmpty()) {
        topConstraint = frameTop + win->captionRect.y + win->captionRect.dy;
    }
    return topConstraint + DpiScale(10);
}

static void PositionFloatingToolbar(FloatingToolbar* tb) {
    if (!tb || !tb->host) {
        return;
    }

    RECT frame{};
    GetWindowRect(tb->win->hwndFrame, &frame);
    Rect fr(frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top);
    if (fr.dx <= 0 || fr.dy <= 0) {
        return;
    }

    Rect client = tb->host->ClientRect();
    int w = client.dx > 0 ? client.dx : DpiScale(2 * kFloatingToolbarMargin + kFloatingToolbarButtonSize);
    int h = client.dy > 0 ? client.dy : DpiScale(2 * kFloatingToolbarMargin +
                     (int)(dimof(gButtons) + 2) * kFloatingToolbarButtonSize +
                     ((int)dimof(gButtons) + 1) * kFloatingToolbarGap +
                     4 * kFloatingToolbarSeparatorGap);

    if (!FloatingToolbarIsForPdf(tb)) {
        ShowWindow(tb->host->native, SW_HIDE);
        return;
    }
    ShowWindow(tb->host->native, SW_SHOWNOACTIVATE);

    int minY = GetFloatingToolbarMinY(tb->win, fr.y);
    int x = fr.x + DpiScale(12);
    int y = minY;

    // When the application is resized, keep the toolbar anchored to the same
    // horizontal side of the frame. A toolbar in the left half keeps its
    // distance from the left edge; one in the right half keeps its distance
    // from the right edge.
    bool frameWasResized = tb->lastFrameRect.dx > 0 && tb->lastFrameRect.dy > 0 &&
                           (tb->lastFrameRect.dx != fr.dx || tb->lastFrameRect.dy != fr.dy);
    if (frameWasResized && tb->lastToolbarRect.dx > 0) {
        int oldFrameCenter = tb->lastFrameRect.x + tb->lastFrameRect.dx / 2;
        int oldToolbarCenter = tb->lastToolbarRect.x + tb->lastToolbarRect.dx / 2;
        if (oldToolbarCenter <= oldFrameCenter) {
            x = fr.x + (tb->lastToolbarRect.x - tb->lastFrameRect.x);
        } else {
            int oldRightMargin = tb->lastFrameRect.x + tb->lastFrameRect.dx -
                                 (tb->lastToolbarRect.x + tb->lastToolbarRect.dx);
            x = fr.x + fr.dx - w - oldRightMargin;
        }
    }

    // Restore the saved position relative to the frame. The saved X value
    // is a left offset for a toolbar in the left half and a right offset for
    // a toolbar in the right half. Y is always an offset from the top edge.
    if (!frameWasResized && gSettings &&
        (gSettings->floatingToolbarPosition.x != 0 || gSettings->floatingToolbarPosition.y != 0)) {
        int savedX = gSettings->floatingToolbarPosition.x;
        int savedY = gSettings->floatingToolbarPosition.y;
        if (savedX >= 0) {
            x = fr.x + savedX;
        } else {
            int rightOffset = -savedX;
            x = fr.x + fr.dx - w - rightOffset;
        }
        y = fr.y + savedY;
    }
    // Clamp after applying the sidebar position too. A wide sidebar or a
    // restored position must never allow the popup outside the frame.
    int minX = fr.x + DpiScale(12);
    int maxX = std::max(minX, fr.x + fr.dx - w - DpiScale(12));
    int maxY = std::max(minY, fr.y + fr.dy - h - DpiScale(12));
    x = std::clamp(x, minX, maxX);
    y = std::clamp(y, minY, maxY);
    MoveFloatingToolbar(tb, {x, y, w, h});

    // Persist the position relative to the frame. Positive X stores the
    // left offset; negative X stores the right offset. Y always stores the
    // offset from the top edge. This is the canonical saved position used
    // after the next launch.
    if (gSettings) {
        int frameCenter = fr.x + fr.dx / 2;
        int toolbarCenter = x + w / 2;
        if (toolbarCenter <= frameCenter) {
            gSettings->floatingToolbarPosition.x = x - fr.x;
        } else {
            int rightOffset = fr.x + fr.dx - (x + w);
            gSettings->floatingToolbarPosition.x = -rightOffset;
        }
        gSettings->floatingToolbarPosition.y = y - fr.y;
        if (frameWasResized) {
            ScheduleSaveSettings();
            FlushScheduledSaveSettings();
        }
    }

    tb->lastFrameRect = fr;
}

struct FloatingPageInPlaceEdit {
    HWND hwndEdit = nullptr;
    WNDPROC prevWndProc = nullptr;
    FloatingToolbar* tb = nullptr;
};

static FloatingPageInPlaceEdit gFloatingPageEdit;
static bool gInEndFloatingPageEdit = false;

static void OnFloatingNativeMsg(FloatingToolbar* tb, VirtHostNativeMsg* ev) {
    if (!tb || !ev) {
        return;
    }

    switch (ev->msg) {
    case WM_TIMER:
        if (ev->wp == 1) {
            KillTimer(tb->host->native, 1);
            tb->screenshotAnimating = false;
            tb->host->Invalidate(false);
            ev->didHandle = true;
        }
        break;
    case WM_CTLCOLOREDIT: {
        HWND hEdit = (HWND)ev->lp;
        if (hEdit && hEdit == gFloatingPageEdit.hwndEdit) {
            HDC hdc = (HDC)ev->wp;
            Color bg = ThemeControlBackgroundColor();
            Color txt = ThemeWindowTextColor();
            COLORREF bgRef = RGB(GetRed(bg), GetGreen(bg), GetBlue(bg));
            COLORREF txtRef = RGB(GetRed(txt), GetGreen(txt), GetBlue(txt));
            SetBkColor(hdc, bgRef);
            SetTextColor(hdc, txtRef);
            static HBRUSH sBgBrush = nullptr;
            if (sBgBrush) {
                DeleteObject(sBgBrush);
            }
            sBgBrush = CreateSolidBrush(bgRef);
            ev->res = (LRESULT)sBgBrush;
            ev->didHandle = true;
            return;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        Point p(GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp));
        ILayout* hit = ElementFromPoint(tb->host->vroot, p);
        VirtCtrl* hitCtrl = hit ? hit->AsVirtCtrl() : nullptr;
        bool onButton = hitCtrl && hitCtrl->id != 0;
        if (!onButton) {
            GetCursorPos(&tb->dragStart);
            tb->dragging = true;
            tb->dragOrig = tb->host->ScreenRect();
            SetCapture(tb->host->native);
            ev->didHandle = true;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (tb->dragging) {
            POINT screen{};
            GetCursorPos(&screen);
            int dx = screen.x - tb->dragStart.x;
            int dy = screen.y - tb->dragStart.y;
            RECT frame{};
            GetWindowRect(tb->win->hwndFrame, &frame);
            int minX = frame.left + DpiScale(12);
            int minY = GetFloatingToolbarMinY(tb->win, frame.top);
            int maxX = std::max<int>(minX, frame.right - tb->dragOrig.dx - DpiScale(12));
            int maxY = std::max<int>(minY, frame.bottom - tb->dragOrig.dy - DpiScale(12));
            int x = std::clamp(tb->dragOrig.x + dx, minX, maxX);
            int y = std::clamp(tb->dragOrig.y + dy, minY, maxY);
            MoveFloatingToolbar(tb, {x, y, tb->dragOrig.dx, tb->dragOrig.dy});
            ev->didHandle = true;
        }
        break;
    case WM_LBUTTONUP:
        if (tb->dragging) {
            tb->dragging = false;
            ReleaseCapture();
            if (gSettings) {
                RECT frame{};
                GetWindowRect(tb->win->hwndFrame, &frame);
                Rect r = tb->host->ScreenRect();
                int frameCenter = frame.left + (frame.right - frame.left) / 2;
                int toolbarCenter = r.x + r.dx / 2;
                if (toolbarCenter <= frameCenter) {
                    gSettings->floatingToolbarPosition.x = r.x - frame.left;
                } else {
                    gSettings->floatingToolbarPosition.x =
                        -(frame.right - (r.x + r.dx));
                }
                gSettings->floatingToolbarPosition.y = r.y - frame.top;
                ScheduleSaveSettings();
                FlushScheduledSaveSettings();
            }
            ev->didHandle = true;
        }
        break;
    case WM_CAPTURECHANGED:
        tb->dragging = false;
        break;
    }
}

static void PaintFloatingToolbarSeparator(VirtPaintCtx* ctx) {
    if (!ctx) {
        return;
    }
    Rect r = ctx->bounds;
    int y = r.y + r.dy / 2;
    ctx->gfx->DrawLine({r.x, y, r.dx, 1}, FloatingBorder(), DpiScale(1));
}

static VirtCtrl* MakeFloatingToolbarSeparator(int width) {
    auto* sep = new VirtCustom();
    sep->idealSize = {width, DpiScale(1)};
    sep->onPaint = MkFunc1Void(PaintFloatingToolbarSeparator);
    sep->SetFlag(vwfNoHitTest, true);
    return sep;
}

static void EndFloatingPageInPlaceEdit(bool accept) {
    if (!gFloatingPageEdit.hwndEdit || gInEndFloatingPageEdit) {
        return;
    }
    gInEndFloatingPageEdit = true;
    HWND hwndEdit = gFloatingPageEdit.hwndEdit;
    WNDPROC prevProc = gFloatingPageEdit.prevWndProc;
    FloatingToolbar* tb = gFloatingPageEdit.tb;

    if (accept && tb && tb->win && tb->win->IsDocLoaded() && tb->win->ctrl) {
        WCHAR buf[64];
        GetWindowTextW(hwndEdit, buf, dimof(buf));
        Str s = ToUtf8Temp(WStr(buf));
        int newPage = tb->win->ctrl->GetPageByLabel(s);
        if (!tb->win->ctrl->ValidPageNo(newPage)) {
            newPage = ParseInt(s);
        }
        if (tb->win->ctrl->ValidPageNo(newPage)) {
            tb->win->ctrl->GoToPage(newPage, true);
        }
    }

    gFloatingPageEdit = {};
    SetWindowLongPtrW(hwndEdit, GWLP_WNDPROC, (LONG_PTR)prevProc);
    DestroyWindow(hwndEdit);

    if (tb && tb->win) {
        if (tb->win->hwndCanvas) {
            HwndSetFocus(tb->win->hwndCanvas);
        } else if (tb->win->hwndFrame) {
            HwndSetFocus(tb->win->hwndFrame);
        }
        if (tb->host) {
            tb->host->Invalidate(false);
        }
    }
    gInEndFloatingPageEdit = false;
}

static LRESULT CALLBACK WndProcFloatingPageInPlaceEdit(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC prevProc = gFloatingPageEdit.prevWndProc;
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            EndFloatingPageInPlaceEdit(true);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            EndFloatingPageInPlaceEdit(false);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE) {
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (!gInEndFloatingPageEdit) {
            EndFloatingPageInPlaceEdit(false);
        }
        return 0;
    }
    return CallWindowProcW(prevProc, hwnd, msg, wp, lp);
}

static void StartFloatingPageInPlaceEdit(FloatingToolbar* tb, Rect widgetBounds) {
    if (!tb || !tb->win || !tb->win->IsDocLoaded() || !tb->host) {
        return;
    }
    EndFloatingPageInPlaceEdit(false);

    int pageNo = tb->win->ctrl ? tb->win->ctrl->CurrentPageNo() : 1;
    TempStr pageStr = fmt("%d", pageNo);

    HWND parent = tb->host->native;
    int boxH = widgetBounds.dy;
    float fontSizePt = std::max((float)DpiScale(8), (float)boxH * 0.26f);
    int fontPx = (int)(fontSizePt * (float)DpiGet() / 72.f);

    int x = widgetBounds.x + DpiScale(2);
    int w = widgetBounds.dx - DpiScale(4);
    int h = fontPx + DpiScale(4);
    int topHalfH = boxH * 50 / 100;
    int y = widgetBounds.y + (topHalfH - h) / 2;

    HWND hwndEdit = CreateWindowExW(
        0, L"EDIT", ToWStrTemp(pageStr).s,
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL,
        x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!hwndEdit) {
        return;
    }

    HFONT font = CreateFontW(-fontPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)font, TRUE);

    gFloatingPageEdit.hwndEdit = hwndEdit;
    gFloatingPageEdit.prevWndProc = (WNDPROC)SetWindowLongPtrW(hwndEdit, GWLP_WNDPROC, (LONG_PTR)WndProcFloatingPageInPlaceEdit);
    gFloatingPageEdit.tb = tb;

    HwndSetFocus(hwndEdit);
    EditSelectAll(hwndEdit);
}

struct FloatingPageWidget : VirtButton {
    FloatingToolbar* toolbar = nullptr;

    FloatingPageWidget() : VirtButton(StrL("")) {
        id = PageInfoId;
        onMouseEnter = MkMethod0<VirtButton, &VirtButton::OnMouseEnter>(this);
        onMouseLeave = MkMethod0<VirtButton, &VirtButton::OnMouseLeave>(this);
        align = VirtTextAlign::Center;
        SetTooltip(StrL("Go to Page"));
    }

    Size GetIdealSize() override {
        int sideLen = DpiScale(kFloatingToolbarButtonSize);
        return {sideLen, sideLen};
    }

    void OnClick(VirtMouseEvent*) {
        if (toolbar) {
            StartFloatingPageInPlaceEdit(toolbar, bounds);
        }
    }

    void Paint(VirtPaintCtx& ctx) override {
        bool isHovered = IsEnabled() && HasFlag(vwfHovered);
        Color bgCol = isHovered ? FloatingHover() : FloatingBg();
        ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(6), bgCol, bgCol);

        MainWindow* win = toolbar ? toolbar->win : nullptr;
        DocController* ctrl = (win && win->IsDocLoaded()) ? win->ctrl : nullptr;

        int pageNo = ctrl ? ctrl->CurrentPageNo() : 0;
        int pageCount = ctrl ? ctrl->PageCount() : 0;

        TempStr curStr = pageNo > 0 ? fmt("%d", pageNo) : StrL("-");
        TempStr totalStr = pageCount > 0 ? fmt("%d", pageCount) : StrL("");

        Color txtCol = ThemeWindowTextColor();
        Color subCol = MkRgb(0x80, 0x80, 0x80); // Neutral mid-grey visible on light & dark themes

        Rect rc = ctx.bounds;
        int boxH = rc.dy;

        float fontSizePt = std::max((float)DpiScale(8), (float)boxH * 0.26f);

        PlatformFont* fontTop = GetPlatformFont(StrL("Segoe UI"), fontSizePt, PlatformFontStyle::Bold);
        PlatformFont* fontBot = GetPlatformFont(StrL("Segoe UI"), fontSizePt, PlatformFontStyle::Regular);

        Rect rcTop = rc;
        rcTop.dy = boxH * 50 / 100;

        Rect rcBot = rc;
        rcBot.y = rc.y + rcTop.dy;
        rcBot.dy = boxH - rcTop.dy;

        u32 flags = gfxTextCenter | gfxTextVCenter | gfxTextSingleLine;
        ctx.gfx->DrawText(curStr, rcTop, flags, fontTop, txtCol);

        if (pageCount > 0) {
            Size szTot = ctx.gfx->MeasureText(totalStr, fontBot);
            int lineW = std::min(szTot.dx, rc.dx - DpiScale(6));
            int lineX = rc.x + (rc.dx - lineW) / 2;
            int lineY = rc.y + rcTop.dy;

            Color lineCol = MkRgb(0x8c, 0x8c, 0x8c);
            ctx.gfx->DrawLine({lineX, lineY, lineW, 1}, lineCol, DpiScale(1));

            ctx.gfx->DrawText(totalStr, rcBot, flags, fontBot, subCol);
        }
    }
};

static void BuildFloatingToolbar(FloatingToolbar* tb) {
    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;

    int iconSize = DpiScale(kFloatingToolbarIconSize);
    int buttonSize = DpiScale(kFloatingToolbarButtonSize);
    int gap = DpiScale(kFloatingToolbarGap);

    for (int i = 0; i < (int)dimof(gButtons); i++) {
        const auto& b = gButtons[i];
        auto* button = new FloatingIconButton();
        button->sideLen = buttonSize;
        button->hoverBg = FloatingHover();
        button->toolbar = tb;
        button->pixmap = GetCachedPixmapForSvg(Str(b.icon), iconSize, iconSize,
                                                ThemeWindowTextColor(), FloatingBg());
        button->pixmapActive = GetCachedPixmapForSvg(Str(b.icon), iconSize, iconSize,
                                                kColWhite, MkRgb(0x3e, 0x53, 0x68));
        button->SetTooltip(b.tip);
        button->id = b.cmdId;
        button->onClick = MkFunc1(OnFloatingButton, tb);
        box->AddChild(button);
        if (i + 1 != (int)dimof(gButtons)) {
            box->AddChild(new Spacer(gap, gap));
        }
    }

    box->AddChild(new Spacer(DpiScale(kFloatingToolbarSeparatorGap), DpiScale(kFloatingToolbarSeparatorGap)));
    box->AddChild(MakeFloatingToolbarSeparator(buttonSize));
    box->AddChild(new Spacer(DpiScale(kFloatingToolbarSeparatorGap), DpiScale(kFloatingToolbarSeparatorGap)));

    auto* screenshot = new FloatingIconButton();
    screenshot->sideLen = buttonSize;
    screenshot->hoverBg = FloatingHover();
    screenshot->toolbar = tb;
    screenshot->pixmap = GetCachedPixmapForSvg(Str(kScreenshotIcon), iconSize, iconSize,
                                                ThemeWindowTextColor(), FloatingBg());
    screenshot->pixmapActive = GetCachedPixmapForSvg(Str(kScreenshotIcon), iconSize, iconSize,
                                                kColWhite, MkRgb(0x3e, 0x53, 0x68));
    screenshot->SetTooltip(StrL("Screenshot"));
    screenshot->id = CmdScreenshot;
    screenshot->onClick = MkFunc1(OnFloatingButton, tb);
    box->AddChild(screenshot);

    box->AddChild(new Spacer(DpiScale(kFloatingToolbarSeparatorGap), DpiScale(kFloatingToolbarSeparatorGap)));
    box->AddChild(MakeFloatingToolbarSeparator(buttonSize));
    box->AddChild(new Spacer(DpiScale(kFloatingToolbarSeparatorGap), DpiScale(kFloatingToolbarSeparatorGap)));

    auto* pageWidget = new FloatingPageWidget();
    pageWidget->toolbar = tb;
    pageWidget->onClick = MkMethod1<FloatingPageWidget, VirtMouseEvent*, &FloatingPageWidget::OnClick>(pageWidget);
    box->AddChild(pageWidget);

    auto* content = new Padding(box, Insets{kFloatingToolbarMargin, kFloatingToolbarMargin,
                                             kFloatingToolbarMargin, kFloatingToolbarMargin});
    tb->host->SetLayoutSizedToContent(content);
}

void UpdateFloatingToolbarPageText(MainWindow* win) {
    if (!win || !win->floatingToolbar || !win->floatingToolbar->host) {
        return;
    }
    win->floatingToolbar->host->Invalidate(false);
}

void FloatingToolbarCreate(MainWindow* win) {
    if (!win || win->floatingToolbar) {
        return;
    }

    auto* tb = new FloatingToolbar();
    tb->win = win;

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStr(kFloatingToolbarClassName);
    args.isPopup = true;
    args.visible = true;
    args.noActivate = true;
    args.clipSiblings = true;
    args.userData = tb;

    tb->host = VirtHost::Create(args);
    if (!tb->host) {
        delete tb;
        return;
    }

    tb->host->onPaintBackground = MkFunc1(PaintFloatingToolbar, tb);
    tb->host->onNativeMsg = MkFunc1(OnFloatingNativeMsg, tb);
    BuildFloatingToolbar(tb);
    win->floatingToolbar = tb;
    win->floatingToolbarOnWindowMoved = MkFunc1Void(FloatingToolbarOnWindowMoved);
    win->RegisterOnWindowMoved(&win->floatingToolbarOnWindowMoved);

    PositionFloatingToolbar(tb);
}

void FloatingToolbarUpdateTheme() {
    for (int i = 0; i < len(gWindows); i++) {
        MainWindow* win = gWindows[i];
        FloatingToolbar* tb = win ? win->floatingToolbar : nullptr;
        if (!tb || !tb->host) {
            continue;
        }

        BuildFloatingToolbar(tb);
        PositionFloatingToolbar(tb);
        tb->host->Invalidate(false);
    }
}

void FloatingToolbarDestroy(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }
    auto* tb = win->floatingToolbar;
    win->UnregisterOnWindowMoved(&win->floatingToolbarOnWindowMoved);
    delete tb->host;
    tb->host = nullptr;
    delete tb;
    win->floatingToolbar = nullptr;
}

void FloatingToolbarRelayout(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }
    PositionFloatingToolbar(win->floatingToolbar);
}

void FloatingToolbarOnWindowMoved(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }

    auto* tb = win->floatingToolbar;
    if (tb->dragging) {
        return;
    }

    RECT rc{};
    GetWindowRect(win->hwndFrame, &rc);
    Rect frame(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    if (tb->lastFrameRect.dx <= 0 || tb->lastFrameRect.dy <= 0) {
        PositionFloatingToolbar(tb);
        return;
    }

    int dx = frame.x - tb->lastFrameRect.x;
    int dy = frame.y - tb->lastFrameRect.y;
    if (dx || dy) {
        Rect current = tb->host->ScreenRect();
        MoveFloatingToolbar(tb, {current.x + dx, current.y + dy, current.dx, current.dy});
    }
    tb->lastFrameRect = frame;
}

void UpdateFloatingToolbarActiveState(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }
    FloatingToolbar* tb = win->floatingToolbar;
    int activeCmd = 0;
    if (IsPlacingAnnotation(win)) {
        activeCmd = win->annotPlacement.cmdId;
    } else if (win->pdfAnnotationsToolbarEnabled) {
        activeCmd = CmdToggleEditPDF;
    }
    tb->activeCmdId = activeCmd;
    if (tb->host) {
        tb->host->Invalidate(false);
    }
}

bool IsCursorOverFloatingToolbar(MainWindow* win) {
    if (!win || !win->floatingToolbar || !win->floatingToolbar->host) {
        return false;
    }
    POINT ptScreen{};
    GetCursorPos(&ptScreen);
    HWND hwndUnderCursor = WindowFromPoint(ptScreen);
    HWND floatingHwnd = win->floatingToolbar->host->native;
    return hwndUnderCursor == floatingHwnd || IsChild(floatingHwnd, hwndUnderCursor);
}
