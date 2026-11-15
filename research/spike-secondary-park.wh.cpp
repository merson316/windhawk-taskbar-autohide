// ==WindhawkMod==
// @id              spike-secondary-park
// @name            Spike: hidden secondary taskbar stays on its own display
// @description     Throwaway experiment for issue #11. Not for release.
// @version         0.5
// @author          merson316
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -lgdi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Spike: hidden secondary taskbar stays on its own display

Throwaway experiment, not a mod to keep installed.

Windows parks an auto-hidden secondary taskbar 2 px on its own display and the
rest on whatever display lies beyond that edge. Measured on 26100.9974 with a
125% display stacked above a 100% primary: hidden at (988,-2,2428,58), where
the window takes the primary's DPI (96), then back to 120 once shown. That DPI
flip is the wrong-size icons on reveal.

- Spike 1 held the window at its shown position and wiped it with a window
  region: it stayed put at its own DPI with no position loop, but the slide was
  gone and the reveal was hard to trigger.
- Spike 2 let the window follow Windows for half its thickness and had the mod
  trigger the reveal from a strip on the taskbar's own display. The DPI held,
  and sending the window hover messages was enough to reveal it. The motion was
  juddery: it followed Windows' own few coarse steps and changed from a slide
  to a wipe halfway.

Spike 3 (this one) asks: is the motion smooth when the mod runs it?

Windows' slide is turned off for the taskbars this mod holds
(TrayUI::SlideWindow is called with animate = false), so Windows asks for the
end position once. The mod then moves from where the taskbar is to there on its
own clock: one step per refresh of that taskbar's display, eased, from a
high-resolution timer. Every step is timed, and each animation writes a summary
to %TEMP%\spike-secondary-park.log: step count, the spread of the gaps between
steps, and the largest jump in pixels. Result: steps landed within about 2 ms
of each 16.7 ms frame with none late, and the DPI never changed.

Spike 4 adds the primary taskbar, so every taskbar moves the same way. A
taskbar whose edge has no display beyond it has nowhere to spill onto, so it
slides the whole way off the desktop whatever the motion setting says; the
hold, and the wipe or half slide, apply only where a display lies beyond.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- hold: true
  $name: Hold the hidden taskbar on its own display
  $description: Off changes nothing and only logs the positions Windows asks for.
- motion: halfslide
  $name: Motion
  $options:
  - wipe: Wipe (the window stays still)
  - halfslide: Slide for half the thickness, then wipe
- hideMs: 250
  $name: Hide duration (ms)
- showMs: 200
  $name: Reveal duration (ms)
- maxFps: 0
  $name: Frame rate cap
  $description: 0 steps once per refresh of the taskbar's display. A lower number caps it.
- stripDepth: 12
  $name: Reveal strip depth
  $description: Pixels at 100% scaling, along the docked edge of the taskbar's own display.
- dwellMs: 150
  $name: Reveal dwell (ms)
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <initializer_list>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>

const UINT_PTR kTimerId = 0x7AB5C1D3;
const UINT kTimerMs = 30;
const int kMaxSteps = 256;

struct Step {
    LONG us;    // since the animation began
    short off;  // pixels hidden
};

struct Bar {
    HWND hwnd;
    RECT monitor;      // the display this taskbar belongs to
    int hz;            // that display's refresh rate
    bool beyond;       // another display lies past the docked edge
    int shownX, shownY;
    int sx, sy;        // direction the taskbar leaves in: one of them is +-1
    RECT region;       // last region applied, in window coordinates
    bool clipped;
    bool ownMove;      // the mod itself is moving the window
    int overruled;     // own moves the taskbar tried to replace, this animation

    int asked;         // pixels hidden that Windows last asked for
    int cur;           // pixels hidden right now
    bool animating;
    int from, to;
    LONGLONG start;    // QueryPerformanceCounter
    int durationMs;
    Step steps[kMaxSteps];
    int stepCount;

    DWORD dwellStart;
    DWORD hoverSent;
};

Bar g_bars[8];
int g_barCount = 0;
UINT g_startMsg = 0;
UINT g_stopMsg = 0;
UINT g_tickMsg = 0;

bool g_hold = true;
bool g_halfSlide = false;
int g_hideMs = 250;
int g_showMs = 200;
int g_maxFps = 0;
int g_stripDepth = 12;
int g_dwellMs = 150;

HANDLE g_animThread = nullptr;
HANDLE g_wake = nullptr;
volatile LONG g_quit = 0;
volatile LONG g_frameUs = 16667;
LONGLONG g_qpf = 1;

using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis, HWND hWnd,
                                           const RECT* rect, HMONITOR monitor,
                                           bool show, bool animate);
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original;
volatile LONG g_slideCalls = 0;

void Log(PCWSTR format, ...) {
    WCHAR text[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf(text, ARRAYSIZE(text) - 1, format, args);
    va_end(args);
    text[ARRAYSIZE(text) - 1] = 0;
    Wh_Log(L"%s", text);

    WCHAR path[MAX_PATH];
    DWORD n = GetTempPathW(ARRAYSIZE(path), path);
    if (!n || n > ARRAYSIZE(path) - 32) {
        return;
    }
    wcscat(path, L"spike-secondary-park.log");
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1400];
    int len = _snprintf(line, sizeof(line) - 1, "%02d:%02d:%02d.%03d %ls\r\n",
                        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
    if (len > 0) {
        DWORD written;
        WriteFile(f, line, (DWORD)len, &written, nullptr);
    }
    CloseHandle(f);
}

LONGLONG Now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

void LoadSettings() {
    g_hold = Wh_GetIntSetting(L"hold") != 0;
    PCWSTR motion = Wh_GetStringSetting(L"motion");
    g_halfSlide = wcscmp(motion, L"halfslide") == 0;
    Wh_FreeStringSetting(motion);
    g_hideMs = Wh_GetIntSetting(L"hideMs");
    g_showMs = Wh_GetIntSetting(L"showMs");
    g_maxFps = Wh_GetIntSetting(L"maxFps");
    g_stripDepth = Wh_GetIntSetting(L"stripDepth");
    g_dwellMs = Wh_GetIntSetting(L"dwellMs");
    if (g_hideMs < 0) g_hideMs = 0;
    if (g_showMs < 0) g_showMs = 0;
    if (g_stripDepth < 2) g_stripDepth = 2;
    if (g_stripDepth > 200) g_stripDepth = 200;
    if (g_dwellMs < 0) g_dwellMs = 0;
    Log(L"settings: hold %d, motion %s, hide %d ms, show %d ms, cap %d fps, "
        L"strip %d px, dwell %d ms",
        g_hold, g_halfSlide ? L"halfslide" : L"wipe", g_hideMs, g_showMs,
        g_maxFps, g_stripDepth, g_dwellMs);
}

Bar* FindBar(HWND hwnd) {
    for (int i = 0; i < g_barCount; i++) {
        if (g_bars[i].hwnd == hwnd) {
            return &g_bars[i];
        }
    }
    return nullptr;
}

// The edge every taskbar is docked to, as the primary reports it.
UINT TaskbarEdge() {
    APPBARDATA abd{sizeof(abd)};
    return SHAppBarMessage(ABM_GETTASKBARPOS, &abd) ? abd.uEdge : ABE_BOTTOM;
}

struct OwnerSearch {
    RECT bar;
    UINT edge;
    RECT found;
    int hz;
    bool ok;
};

// A taskbar spans its display along the docked edge, and whether shown or
// hidden it reaches that edge of the display: shown it ends on it, hidden it
// straddles it. The display beyond the edge matches neither.
BOOL CALLBACK OwnerProc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* s = (OwnerSearch*)data;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return TRUE;
    }
    const RECT& m = mi.rcMonitor;
    const RECT& b = s->bar;
    bool match = false;
    switch (s->edge) {
        case ABE_BOTTOM:
            match = b.left == m.left && b.right == m.right && b.top >= m.top &&
                    b.top < m.bottom && b.bottom >= m.bottom;
            break;
        case ABE_TOP:
            match = b.left == m.left && b.right == m.right &&
                    b.bottom <= m.bottom && b.bottom > m.top && b.top <= m.top;
            break;
        case ABE_RIGHT:
            match = b.top == m.top && b.bottom == m.bottom && b.left >= m.left &&
                    b.left < m.right && b.right >= m.right;
            break;
        case ABE_LEFT:
            match = b.top == m.top && b.bottom == m.bottom &&
                    b.right <= m.right && b.right > m.left && b.left <= m.left;
            break;
    }
    if (match) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        s->hz = EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
                        dm.dmDisplayFrequency > 1
                    ? (int)dm.dmDisplayFrequency
                    : 60;
        s->found = m;
        s->ok = true;
        return FALSE;
    }
    return TRUE;
}

void ApplyRegion(Bar* bar, const RECT* region) {
    if (!region) {
        if (bar->clipped) {
            SetWindowRgn(bar->hwnd, nullptr, TRUE);
            bar->clipped = false;
        }
        return;
    }
    if (bar->clipped && EqualRect(&bar->region, region)) {
        return;
    }
    // The system owns the region after a successful SetWindowRgn.
    HRGN rgn = CreateRectRgnIndirect(region);
    if (rgn && !SetWindowRgn(bar->hwnd, rgn, TRUE)) {
        DeleteObject(rgn);
        return;
    }
    bar->region = *region;
    bar->clipped = true;
}

// How far the window itself travels when `off` pixels are hidden: nothing in
// wipe mode, and just under half its thickness otherwise, so most of it stays
// on its own display and it keeps that display's DPI. With no display past the
// edge there is nothing to protect, and the window slides all the way.
int Travel(const Bar* bar, int off, int thickness) {
    if (!bar->beyond) {
        return off;   // nothing to spill onto: slide the whole way
    }
    if (!g_halfSlide) {
        return 0;
    }
    int limit = thickness / 2 - 2;
    if (limit < 0) limit = 0;
    return off < limit ? off : limit;
}

// Put the window and its region where `off` hidden pixels call for.
void ApplyOffset(Bar* bar, int off) {
    RECT cur{};
    GetWindowRect(bar->hwnd, &cur);
    const int w = cur.right - cur.left, h = cur.bottom - cur.top;
    const int thickness = bar->sx ? w : h;
    if (off < 0) off = 0;
    if (off > thickness) off = thickness;
    const int move = Travel(bar, off, thickness);
    const int visible = thickness - off;

    const int x = bar->shownX + bar->sx * move;
    const int y = bar->shownY + bar->sy * move;
    if (x != cur.left || y != cur.top) {
        bar->ownMove = true;
        SetWindowPos(bar->hwnd, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        bar->ownMove = false;
    }

    if (off == 0) {
        ApplyRegion(bar, nullptr);
    } else {
        RECT region{0, 0, w, h};
        if (bar->sx > 0) {          // right edge
            region.right = w - move;
            region.left = region.right - visible;
        } else if (bar->sx < 0) {   // left edge
            region.left = move;
            region.right = move + visible;
        } else if (bar->sy > 0) {   // bottom edge
            region.bottom = h - move;
            region.top = region.bottom - visible;
        } else {                    // top edge
            region.top = move;
            region.bottom = move + visible;
        }
        ApplyRegion(bar, &region);
    }
    bar->cur = off;
}

void FinishAnimation(Bar* bar) {
    bar->animating = false;
    int n = bar->stepCount;
    LONG minGap = 0x7FFFFFFF, maxGap = 0, late = 0;
    int maxJump = 0;
    const LONG frame = InterlockedCompareExchange(&g_frameUs, 0, 0);
    for (int i = 1; i < n; i++) {
        LONG gap = bar->steps[i].us - bar->steps[i - 1].us;
        int jump = bar->steps[i].off - bar->steps[i - 1].off;
        if (jump < 0) jump = -jump;
        if (gap < minGap) minGap = gap;
        if (gap > maxGap) maxGap = gap;
        if (gap > frame + frame / 2) late++;
        if (jump > maxJump) maxJump = jump;
    }
    WCHAR list[700];
    int len = 0;
    for (int i = 0; i < n && len < (int)ARRAYSIZE(list) - 16; i++) {
        len += _snwprintf(list + len, ARRAYSIZE(list) - len, L"%d:%d ",
                          (int)(bar->steps[i].us / 100), bar->steps[i].off);
    }
    list[len] = 0;
    Log(L"%p %s %d->%d px in %d ms at %d Hz: %d steps, gap %d..%d us "
        L"(frame %d), %d late, biggest jump %d px, dpi %u, "
        L"%d own moves overruled by the taskbar and put back",
        bar->hwnd, bar->to > bar->from ? L"hide" : L"reveal", bar->from,
        bar->to, n ? (int)(bar->steps[n - 1].us / 1000) : 0, bar->hz, n,
        n > 1 ? (int)minGap : 0, (int)maxGap, (int)frame, (int)late, maxJump,
        GetDpiForWindow(bar->hwnd), bar->overruled);
    Log(L"%p steps (0.1 ms:px) %s", bar->hwnd, list);
}

// One animation step, on the taskbar's thread. Returns whether more follow.
bool Tick(Bar* bar) {
    if (!bar->animating) {
        return false;
    }
    const LONGLONG us = (Now() - bar->start) * 1000000 / g_qpf;
    double t = bar->durationMs > 0 ? (double)us / (bar->durationMs * 1000.0)
                                   : 1.0;
    if (t > 1.0) t = 1.0;
    // Ease out, both ways: quick to start, settling into the end position.
    const double e = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    const int off = bar->from + (int)((bar->to - bar->from) * e +
                                      (bar->to > bar->from ? 0.5 : -0.5));
    ApplyOffset(bar, t >= 1.0 ? bar->to : off);
    if (bar->stepCount < kMaxSteps) {
        bar->steps[bar->stepCount++] = Step{(LONG)us, (short)bar->cur};
    }
    if (t >= 1.0) {
        FinishAnimation(bar);
        return false;
    }
    return true;
}

void StartAnimation(Bar* bar, int to) {
    bar->from = bar->cur;
    bar->to = to;
    bar->start = Now();
    bar->durationMs = to > bar->cur ? g_hideMs : g_showMs;
    bar->stepCount = 0;
    bar->overruled = 0;
    bar->animating = true;
    int fps = bar->hz;
    if (g_maxFps > 0 && g_maxFps < fps) {
        fps = g_maxFps;
    }
    InterlockedExchange(&g_frameUs, 1000000 / fps);
    SetEvent(g_wake);
}

void OnPosChanging(Bar* bar, WINDOWPOS* wp) {
    RECT cur{};
    GetWindowRect(bar->hwnd, &cur);
    const int w = (wp->flags & SWP_NOSIZE) ? cur.right - cur.left : wp->cx;
    const int h = (wp->flags & SWP_NOSIZE) ? cur.bottom - cur.top : wp->cy;
    const RECT asked{wp->x, wp->y, wp->x + w, wp->y + h};
    const RECT& m = bar->monitor;

    // How far the asked position must move to sit inside its own display.
    const int dx = asked.left < m.left     ? m.left - asked.left
                   : asked.right > m.right ? m.right - asked.right
                                           : 0;
    const int dy = asked.top < m.top         ? m.top - asked.top
                   : asked.bottom > m.bottom ? m.bottom - asked.bottom
                                             : 0;
    const int off = dx ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);

    bar->shownX = asked.left + dx;
    bar->shownY = asked.top + dy;
    if (off) {
        bar->sx = dx < 0 ? 1 : dx > 0 ? -1 : 0;
        bar->sy = dy < 0 ? 1 : dy > 0 ? -1 : 0;
    }

    if (off != bar->asked) {
        Log(L"%p Windows asks (%d,%d,%d,%d): %d px hidden (was %d), flags %#x, "
            L"dpi %u, SlideWindow calls so far %d",
            bar->hwnd, asked.left, asked.top, asked.right, asked.bottom, off,
            bar->asked, wp->flags, GetDpiForWindow(bar->hwnd),
            (int)InterlockedCompareExchange(&g_slideCalls, 0, 0));
        bar->asked = off;
        if (g_hold) {
            bar->dwellStart = 0;
            bar->hoverSent = 0;
            StartAnimation(bar, off);
        }
    }

    if (!g_hold) {
        return;
    }

    // Windows gets the position the animation is at, not the one it asked for.
    const int move = Travel(bar, bar->cur, dx || bar->sx ? w : h);
    wp->x = bar->shownX + bar->sx * move;
    wp->y = bar->shownY + bar->sy * move;
}

// The strip on the taskbar's own display, along its docked edge, where a
// resting pointer reveals it.
RECT RevealStrip(const Bar* bar) {
    RECT cur{};
    GetWindowRect(bar->hwnd, &cur);
    const int depth = MulDiv(g_stripDepth, GetDpiForWindow(bar->hwnd), 96);
    const RECT& m = bar->monitor;
    RECT s = m;
    if (bar->sy > 0) {
        s.left = cur.left; s.right = cur.right; s.top = m.bottom - depth;
    } else if (bar->sy < 0) {
        s.left = cur.left; s.right = cur.right; s.bottom = m.top + depth;
    } else if (bar->sx > 0) {
        s.top = cur.top; s.bottom = cur.bottom; s.left = m.right - depth;
    } else {
        s.top = cur.top; s.bottom = cur.bottom; s.right = m.left + depth;
    }
    return s;
}

void OnTimer(Bar* bar) {
    if (!g_hold || bar->asked == 0 || !bar->clipped) {
        return;
    }

    POINT pt{};
    GetCursorPos(&pt);
    const RECT strip = RevealStrip(bar);
    const DWORD now = GetTickCount();

    if (!PtInRect(&strip, pt)) {
        bar->dwellStart = 0;
        bar->hoverSent = 0;
        return;
    }
    if (!bar->dwellStart) {
        bar->dwellStart = now ? now : 1;
        return;
    }
    if (bar->hoverSent) {
        if (now - bar->hoverSent >= 1000) {
            Log(L"%p still hidden a second after the hover messages",
                bar->hwnd);
            bar->hoverSent = 0;
            bar->dwellStart = 0;
        }
        return;
    }
    if (now - bar->dwellStart < (DWORD)g_dwellMs) {
        return;
    }

    // The messages a hover over the visible sliver delivers, aimed at a point
    // inside the part of the window that is still there.
    RECT cur{};
    GetWindowRect(bar->hwnd, &cur);
    POINT in{cur.left + (bar->region.left + bar->region.right) / 2,
             cur.top + (bar->region.top + bar->region.bottom) / 2};
    if (bar->sx == 0) {
        in.x = pt.x;
    } else {
        in.y = pt.y;
    }
    bar->hoverSent = now ? now : 1;
    Log(L"%p pointer (%d,%d) rested in the strip: hover messages at (%d,%d)",
        bar->hwnd, pt.x, pt.y, in.x, in.y);
    SendMessageW(bar->hwnd, WM_NCHITTEST, 0, MAKELPARAM(in.x, in.y));
    PostMessageW(bar->hwnd, WM_MOUSEMOVE, 0,
                 MAKELPARAM(in.x - cur.left, in.y - cur.top));
}

LRESULT CALLBACK BarProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                         DWORD_PTR) {
    Bar* bar = FindBar(hWnd);

    if (uMsg == WM_NCDESTROY) {
        if (bar) {
            bar->animating = false;
            bar->hwnd = nullptr;
        }
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    if (bar && uMsg == g_startMsg) {
        SetTimer(hWnd, kTimerId, kTimerMs, nullptr);
        return 0;
    }
    if (bar && uMsg == g_stopMsg) {
        KillTimer(hWnd, kTimerId);
        bar->animating = false;
        return 0;
    }
    if (bar && uMsg == g_tickMsg) {
        return Tick(bar) ? 1 : 0;
    }
    if (bar && uMsg == WM_TIMER && wParam == kTimerId) {
        OnTimer(bar);
        return 0;
    }

    if (uMsg == WM_WINDOWPOSCHANGING && lParam && bar) {
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        const int reqX = wp->x, reqY = wp->y;
        LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        if (wp->flags & SWP_NOMOVE) {
            return r;
        }
        if (bar->ownMove) {
            // The taskbar's own handler replaces any position with the one it
            // believes in. Put back what the mod asked for, or the window
            // stays put while the region moves, and the wipe runs backwards.
            if (wp->x != reqX || wp->y != reqY) {
                bar->overruled++;
            }
            wp->x = reqX;
            wp->y = reqY;
        } else {
            OnPosChanging(bar, wp);
        }
        return r;
    }

    if (uMsg == WM_DPICHANGED && bar) {
        Log(L"%p WM_DPICHANGED to %u", hWnd, (UINT)LOWORD(wParam));
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Windows' slide is a loop on the taskbar's thread that moves the window in a
// few coarse steps and blocks the thread meanwhile. For a taskbar this mod
// holds, ask for no animation: Windows then asks for the end position once,
// and the mod animates there itself.
void WINAPI TrayUI_SlideWindow_Hook(void* pThis, HWND hWnd, const RECT* rect,
                                    HMONITOR monitor, bool show, bool animate) {
    if (g_hold && FindBar(hWnd)) {
        InterlockedIncrement(&g_slideCalls);
        animate = false;
    }
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
}

// Paces the animation: one tick per frame of the taskbar's display, from a
// high-resolution timer. The step itself runs on the taskbar's thread.
DWORD WINAPI AnimThreadProc(void*) {
    HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
        TIMER_ALL_ACCESS);
    if (!timer) {
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    while (WaitForSingleObject(g_wake, INFINITE) == WAIT_OBJECT_0 &&
           !InterlockedCompareExchange(&g_quit, 0, 0)) {
        LONGLONG next = Now();
        for (;;) {
            bool more = false;
            for (int i = 0; i < g_barCount; i++) {
                HWND w = g_bars[i].hwnd;
                DWORD_PTR result = 0;
                if (w && SendMessageTimeoutW(w, g_tickMsg, 0, 0,
                                             SMTO_ABORTIFHUNG, 200, &result) &&
                    result) {
                    more = true;
                }
            }
            if (!more || InterlockedCompareExchange(&g_quit, 0, 0)) {
                break;
            }
            // Aim at the next frame boundary, not at "one frame from now", so
            // a late tick does not push every later one back.
            const LONGLONG frame =
                InterlockedCompareExchange(&g_frameUs, 0, 0) * g_qpf / 1000000;
            next += frame;
            LONGLONG wait = next - Now();
            if (wait <= 0) {
                next = Now();
                continue;
            }
            LARGE_INTEGER due;
            due.QuadPart = -(wait * 10000000 / g_qpf);
            if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr,
                                          FALSE)) {
                WaitForSingleObject(timer, 100);
            } else {
                Sleep((DWORD)(wait * 1000 / g_qpf));
            }
        }
    }
    if (timer) {
        CloseHandle(timer);
    }
    return 0;
}

void Attach() {
    DWORD pid = GetCurrentProcessId();
    const UINT edge = TaskbarEdge();
    for (PCWSTR cls : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, cls, nullptr)) &&
           g_barCount < (int)ARRAYSIZE(g_bars)) {
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        if (owner != pid) {
            continue;
        }
        OwnerSearch s{};
        GetWindowRect(w, &s.bar);
        s.edge = edge;
        EnumDisplayMonitors(nullptr, nullptr, OwnerProc, (LPARAM)&s);
        if (!s.ok) {
            Log(L"%p at (%d,%d,%d,%d), edge %u: no display found for it", w,
                s.bar.left, s.bar.top, s.bar.right, s.bar.bottom, edge);
            continue;
        }
        const RECT& m = s.found;
        const RECT& b = s.bar;
        Bar& bar = g_bars[g_barCount];
        bar = Bar{};
        bar.hwnd = w;
        bar.monitor = m;
        bar.hz = s.hz;
        // Where it is shown, and how much of it is hidden right now.
        const int dx = b.left < m.left ? m.left - b.left
                       : b.right > m.right ? m.right - b.right : 0;
        const int dy = b.top < m.top ? m.top - b.top
                       : b.bottom > m.bottom ? m.bottom - b.bottom : 0;
        bar.shownX = b.left + dx;
        bar.shownY = b.top + dy;
        bar.sx = edge == ABE_RIGHT ? 1 : edge == ABE_LEFT ? -1 : 0;
        bar.sy = edge == ABE_BOTTOM ? 1 : edge == ABE_TOP ? -1 : 0;
        bar.asked = bar.cur = dx ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        // A one-pixel band just past the docked edge, along the taskbar.
        RECT past = m;
        if (bar.sy > 0) { past.top = m.bottom; past.bottom = m.bottom + 1; }
        else if (bar.sy < 0) { past.bottom = m.top; past.top = m.top - 1; }
        else if (bar.sx > 0) { past.left = m.right; past.right = m.right + 1; }
        else { past.right = m.left; past.left = m.left - 1; }
        bar.beyond = MonitorFromRect(&past, MONITOR_DEFAULTTONULL) != nullptr;
        if (!WindhawkUtils::SetWindowSubclassFromAnyThread(w, BarProc, 0)) {
            Log(L"%p: could not subclass", w);
            continue;
        }
        g_barCount++;
        PostMessageW(w, g_startMsg, 0, 0);
        Log(L"%p %s at (%d,%d,%d,%d), edge %u, on display (%d,%d,%d,%d) at "
            L"%d Hz, dpi %u, %d px hidden, display beyond the edge: %d",
            w, cls, b.left, b.top, b.right, b.bottom, edge, m.left, m.top,
            m.right, m.bottom, s.hz, GetDpiForWindow(w), bar.asked,
            bar.beyond);
    }
    }
    if (g_barCount == 0) {
        Log(L"no taskbar in this process");
    }
}

BOOL Wh_ModInit() {
    g_startMsg = RegisterWindowMessageW(L"SpikeSecondaryParkStart");
    g_stopMsg = RegisterWindowMessageW(L"SpikeSecondaryParkStop");
    g_tickMsg = RegisterWindowMessageW(L"SpikeSecondaryParkTick");
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpf = f.QuadPart;
    Log(L"---- spike 0.5 loaded");
    LoadSettings();

    HMODULE taskbar =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))",
          LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Original, TrayUI_SlideWindow_Hook, true},
    };
    if (!taskbar || !WindhawkUtils::HookSymbols(taskbar, hooks,
                                                ARRAYSIZE(hooks)) ||
        !TrayUI_SlideWindow_Original) {
        Log(L"TrayUI::SlideWindow not hooked: Windows' own slide still runs "
            L"and the mod's animation follows after it");
    } else {
        Log(L"TrayUI::SlideWindow hooked");
    }

    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_animThread = CreateThread(nullptr, 0, AnimThreadProc, nullptr, 0, nullptr);
    Attach();
    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}

void Wh_ModUninit() {
    for (int i = 0; i < g_barCount; i++) {
        if (HWND w = g_bars[i].hwnd) {
            SendMessageW(w, g_stopMsg, 0, 0);
        }
    }
    InterlockedExchange(&g_quit, 1);
    if (g_wake) {
        SetEvent(g_wake);
    }
    if (g_animThread) {
        WaitForSingleObject(g_animThread, 2000);
        CloseHandle(g_animThread);
    }
    for (int i = 0; i < g_barCount; i++) {
        HWND w = g_bars[i].hwnd;
        if (!w) {
            continue;
        }
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(w, BarProc);
        // Windows still believes the taskbar is where it last asked; the next
        // hide or reveal puts the window back there.
        SetWindowRgn(w, nullptr, TRUE);
    }
    if (g_wake) {
        CloseHandle(g_wake);
    }
    Log(L"---- spike unloaded");
}
