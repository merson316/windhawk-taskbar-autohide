// ==WindhawkMod==
// @id              taskbar-autohide-any-edge
// @name            Taskbar auto-hide on any edge
// @description     Makes "Automatically hide the taskbar" work with the taskbar docked to the top, left or right
// @version         1.20.0
// @author          merson316
// @github          https://github.com/merson316
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -lwtsapi32 -lwinhttp -ldwmapi -lgdi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar auto-hide on any edge

Windows 11 26H2 can dock the taskbar to the top, left or right, but
"Automatically hide the taskbar" only works on the bottom edge. Elsewhere the
setting is accepted, reports success, and does nothing.

`TrayUI::GetAutoHideFlags` masks the auto-hide flags to zero unless the taskbar
is docked to the bottom. The flags are stored correctly on every edge; only the
read is masked, and everything downstream is gated on it, so no hide is ever
attempted. This mod unmasks the read, stores the same flag combination the
bottom edge uses, and supplies the pointer tracking that Windows only performs
on the bottom edge.

It handles every taskbar when "Show my taskbar on all displays" is on, each at
its own display's scaling and refresh rate.

## More than one display

A taskbar whose edge has another display beyond it has nowhere to hide into.
Windows slides it onto the neighbouring display and clips it there, and the
hidden window takes that display's scaling: with displays scaled differently,
its icons can come back the wrong size. The mod keeps such a taskbar's window
where it is and slides its content out of it, so nothing is drawn on the other
display and the scaling never changes. This applies on every edge, the bottom
included. Rest the pointer on the edge of the taskbar's own display for a
moment to bring it back.

On the bottom edge Windows still decides when a taskbar hides and the mod
carries it out, which brings "Hide completely", "Hide delay" and the reveal
settings to that edge too.

## Notification handling

Controlled by the two "Notifications" settings. **Set "keep visible for" to 0 to
turn it off entirely**; any value above 0 turns it on.

Windows refuses to auto-hide the taskbar while an app is flashing its taskbar
button, which can leave it visible indefinitely. This mod shows it once, holds
it for `notificationHoldMs` (1500 ms by default), then lets it hide and stay
hidden for the rest of the flashing.

This works on every edge the taskbar can dock to, including the bottom.

That makes Cirn09's [Better Taskbar
Autohide](https://windhawk.net/mods/taskbar-autohide-better) redundant; run one
or the other, not both, since they hook the same function.

## Nothing happens?

In the order these actually turn out to be the cause:

- **Auto-hide is not enabled in Windows.** Settings > Personalisation > Taskbar
  > Taskbar behaviours. Windows greys that option out when the taskbar is not
  at the bottom, so set it with the taskbar at the bottom and then move it.
- **The taskbar is on the bottom edge, "Hide completely" is off and no
  display lies below it**, where Windows already handles auto-hide and the
  mod leaves it to Windows.
- **"Leave edges facing another display alone" is on** and the edge faces
  another display.

If auto-hide stays off, the mod says so in a dialog. For the other two, it
writes which one applies to the Windhawk log, once per session.

## Known limitations

- Windows parks a hidden taskbar with a sliver still on screen. The mod hides
  every taskbar completely instead, since it does its own pointer tracking;
  "Hide completely" turns that off.
- Off the bottom edge a revealed taskbar can come up behind another window.
  The mod raises it before the slide starts, except while a full-screen window
  is in front. See "Keep the taskbar above other windows".
- On a Windows build whose debug symbols Microsoft has not published, a
  flashing button may bring a secondary taskbar on the bottom edge up again
  for each flash.
- Requires Windows 11 build 26200 or newer.

## If it does not work

Every way this mod can fail leaves the taskbar behaving exactly as it does with
no mod installed, which looks the same as the mod working on a machine where
nothing hides. So when it cannot run, it says so in a dialog and names the
reason instead of failing silently. The taskbar is left alone in that state.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- leaveFacingEdges: false
  $name: Leave edges facing another display alone
  $description: >-
    An edge with a display beyond it has nowhere to hide into, so the mod keeps
    such a taskbar's window where it is and slides its content out of it. Turn this on to leave such an edge alone instead, as it behaves
    without this mod. The bottom edge is not affected.

    Default: off.
- hideFully: true
  $name: Hide completely
  $description: >-
    Windows parks a hidden taskbar with a sliver still on screen so the pointer
    can find it, which reads as a line along the screen edge. This mod does
    its own pointer tracking, so the sliver earns nothing, on any edge. Turn
    this off to keep Windows' position; on the bottom edge that also hands a
    taskbar with no display below it back to Windows' own slide.

    Default: on.
- frameRate: 90
  $name: Animation frame rate
  $description: >-
    Frames per second for the hide and reveal slides. Higher is smoother and
    costs more CPU. Range 30-240.

    Disable "Taskbar auto-hide speed" if you have it: both mods pace the same
    reveal, and together they fight over it.

    Default: 90.
- hideDurationMs: 0
  $name: Hide animation duration (ms)
  $description: >-
    How long the taskbar takes to slide out of view. 0 uses 400 ms; range
    25-2000.

    The mod animates the hide itself: Windows clamps each intermediate position
    to the endpoint, which is why an unmodified vertical taskbar appears to
    vanish instantly.

    Default: 0.
- showDurationMs: 0
  $name: Reveal animation duration (ms)
  $description: >-
    0 keeps the Windows default of 200 ms. Range 25-2000.

    Default: 0.
- hideDelayMs: 0
  $name: Hide delay (ms)
  $description: >-
    How long the taskbar stays up after the last moment something was keeping
    it there: the pointer over it, Start or a flyout open, or a notification's
    hold. Helps when moving the pointer to another display or into a flyout.
    The pointer coming back starts the wait again.

    This is the wait before the hide begins, not how long the slide takes.
    It applies on every edge. Range 0-10000.

    Default: 0.
- notificationHoldMs: 1500
  $name: Notifications - keep visible for (ms)
  $description: >-
    Windows will not auto-hide the taskbar while an app is flashing its taskbar
    button, which can leave it on screen indefinitely. Instead, the taskbar
    shows once, stays for this long, then hides for the rest of the flashing.

    Set to 0 to turn notification handling off. Applies on every edge,
    including the bottom.

    Default: 1500.
- notificationBurstGapMs: 10000
  $name: Notifications - treat as one within (ms)
  $description: >-
    A flashing button asks for the taskbar once per flash, not once per
    notification. Requests closer together than this count as one, so the
    taskbar shows once instead of repeatedly.

    Raise it if the taskbar keeps reappearing during a single notification;
    lower it if two separate notifications get merged. Unused when the setting
    above is 0.

    Default: 10000.
- revealDwellMs: 0
  $name: Reveal dwell (ms)
  $description: >-
    How long the pointer must stay in the trigger strip before the taskbar
    appears. Raise it if the taskbar appears when you were only moving to the
    next screen. A pointer already over the taskbar never waits.

    Off by default, revealing as soon as the pointer reaches the strip, which
    is how Windows behaves on the bottom edge.

    Default: 0.
- revealTolerance: 8
  $name: Reveal trigger width (pixels)
  $description: >-
    How close to the screen edge the pointer must get to bring the taskbar
    back. Raise it if the taskbar is hard to summon, lower it if it appears
    when you did not mean it to.

    Default: 8.
- keepTaskbarOnTop: true
  $name: Keep the taskbar above other windows
  $description: >-
    Off the bottom edge a revealed taskbar can come up behind whatever
    sits over that edge. This puts it back in front as it appears.

    Left alone when a full-screen window is in front: Windows lowers the
    taskbar there on purpose, and overriding that would put it over
    full-screen games and video.

    Default: enabled.
*/
// ==/WindhawkModSettings==

// The mod's log says what it found and what stopped it, once each. A debug
// build also logs every request Windows makes of a taskbar and every slide,
// which is what a report of odd behaviour needs. tools/make-debug-build.py
// makes that build from this file by enabling the line below; nothing else
// differs.
// #define TASKBAR_AUTOHIDE_DEBUG

#include <windhawk_utils.h>

#include <initializer_list>

#include <dwmapi.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wtsapi32.h>

#ifdef TASKBAR_AUTOHIDE_DEBUG
#define DLog(...) Wh_Log(__VA_ARGS__)
#else
#define DLog(...) ((void)0)
#endif


// ABE_LEFT/TOP/RIGHT/BOTTOM and ABS_AUTOHIDE/ABS_ALWAYSONTOP come from
// shellapi.h.

// Member offsets within TrayUI, relative to the base used by SetAutoHideFlags,
// GetDockedRect, Unhide and SlideWindow. GetAutoHideFlags is reached on a base
// 8 bytes lower and so sees the same members 8 higher. All four are read out of
// GetAutoHideFlags at load by ScanForOffsets; nothing may use them unless
// g_offsetsVerified.
UINT g_trayUiEdgeOffset = 0;
UINT g_trayUiFlagsOffset = 0;
UINT g_getFlagsEdgeOffset = 0;
UINT g_getFlagsFlagsOffset = 0;

// WM_TIMER is only delivered when the thread's queue is empty, so hovering the
// taskbar -- which produces a stream of mouse messages -- starves it exactly
// when the reveal is needed. A worker thread posts an ordinary message instead,
// which is not deprioritised; the work still runs on the taskbar's thread.
#define WATCH_TICK_MS 40

UINT g_watchMsg = 0;
HANDLE g_watchThread = nullptr;
volatile LONG g_watchStop = 0;

// The shell gets the first to finish coming up before any warning is allowed on
// screen, and the second for the taskbar window to appear before its absence
// counts as a failure rather than as boot still being in progress.
#define WARN_SETTLE_MS 5000
#define WARN_NO_TASKBAR_MS 30000
#define WARN_AUTOHIDE_OFF_MS 15000
// ...and auto-hide must have read "off" for this long. After Explorer starts
// with the taskbar off the bottom edge the flags read off for the best part
// of a minute and then on, with nothing touched (seen: 77 s). At start
// TrayUI::_KickStartAutohide stores 0 when GetAutoHideFlags answers 0, which
// it does off the bottom edge until the mod's hook is in place; what sets the
// flags again later is outside taskbar.dll.
#define WARN_AUTOHIDE_STAYS_OFF_MS 120000

// Set when the mod cannot do its job. Kept rather than returning FALSE from
// Wh_ModInit: Windhawk unloads a mod whose init failed, taking the DLL out from
// under the thread that raises the warning. Every hook below stands down on
// this, so staying loaded is inert.
PCWSTR g_fatalReason = nullptr;

// Set when the taskbar's symbols could not be resolved. The commonest cause is
// a Windows update whose symbols Microsoft has not published yet, which the
// warning asks the symbol server about before blaming the connection.
HMODULE g_symbolsMissingFor = nullptr;

// The symbol server request in flight, so StopWatchThread can cancel it:
// closing a request handle ends a blocking WinHTTP call on another thread, and
// unloading must not wait on a slow network.
PVOID volatile g_symbolRequest = nullptr;

// When the taskbar sits on an edge this mod would act on but Windows has
// auto-hide off. That is the most common reason the mod appears to do
// nothing. The Settings toggle is not a reliable witness: it reads a second
// member (TaskbarSettings::get_HideTaskbar, the "desktop mode" setting), not
// the flags the mod and Windows' hide logic use, and Settings greys the option
// out off the bottom edge.
volatile LONG64 g_autoHideOffSince = 0;   // tick it was first seen off; 0 = on
volatile LONG g_warned = 0;

#define LEAVE_TICKS    2

// ------------------------------------------------------------------ settings

struct Settings {
    bool skipAdjacentEdges = false;
    bool hideFully = true;
    int frameRate = 90;
    int revealTolerance = 8;
    bool keepTaskbarOnTop = true;
    int revealDwellMs = 0;
    int notificationHoldMs = 1500;
    int notificationBurstGapMs = 10000;
    int hideDurationMs = 0;
    int showDurationMs = 0;
    int hideDelayMs = 0;
} g_settings;

int ClampDuration(int ms) {
    if (ms <= 0) {
        return 0;   // 0 means "use the Windows default"
    }
    if (ms < 25) {
        return 25;
    }
    if (ms > 2000) {
        return 2000;
    }
    return ms;
}

void LoadSettings() {
    // Named skipAdjacentEdges, and on by default, until 2.0. The new name
    // means a value saved under the old one does not carry over: the mod can
    // now handle such an edge, and whoever had the old default should get the
    // new behaviour without having to find the setting.
    g_settings.skipAdjacentEdges =
        Wh_GetIntSetting(L"leaveFacingEdges") != 0;
    g_settings.hideFully = Wh_GetIntSetting(L"hideFully") != 0;

    g_settings.frameRate = Wh_GetIntSetting(L"frameRate");
    if (g_settings.frameRate < 30) {
        g_settings.frameRate = 30;
    } else if (g_settings.frameRate > 240) {
        g_settings.frameRate = 240;
    }
    g_settings.keepTaskbarOnTop = Wh_GetIntSetting(L"keepTaskbarOnTop") != 0;
    g_settings.revealTolerance = Wh_GetIntSetting(L"revealTolerance");
    if (g_settings.revealTolerance < 1) {
        g_settings.revealTolerance = 1;
    } else if (g_settings.revealTolerance > 64) {
        g_settings.revealTolerance = 64;
    }

    // Clamp out-of-range values rather than discarding them: silently turning
    // an over-large value into "no override" looks identical to the setting
    // being ignored.
    g_settings.notificationHoldMs = Wh_GetIntSetting(L"notificationHoldMs");
    if (g_settings.notificationHoldMs < 0) {
        g_settings.notificationHoldMs = 0;
    } else if (g_settings.notificationHoldMs > 30000) {
        g_settings.notificationHoldMs = 30000;
    }

    g_settings.revealDwellMs = Wh_GetIntSetting(L"revealDwellMs");
    if (g_settings.revealDwellMs < 0) {
        g_settings.revealDwellMs = 0;
    } else if (g_settings.revealDwellMs > 2000) {
        g_settings.revealDwellMs = 2000;
    }

    g_settings.notificationBurstGapMs =
        Wh_GetIntSetting(L"notificationBurstGapMs");
    if (g_settings.notificationBurstGapMs < 0) {
        g_settings.notificationBurstGapMs = 0;
    } else if (g_settings.notificationBurstGapMs > 60000) {
        g_settings.notificationBurstGapMs = 60000;
    }

    g_settings.hideDurationMs = ClampDuration(Wh_GetIntSetting(L"hideDurationMs"));
    g_settings.showDurationMs = ClampDuration(Wh_GetIntSetting(L"showDurationMs"));

    g_settings.hideDelayMs = Wh_GetIntSetting(L"hideDelayMs");
    if (g_settings.hideDelayMs < 0) {
        g_settings.hideDelayMs = 0;
    } else if (g_settings.hideDelayMs > 10000) {
        g_settings.hideDelayMs = 10000;
    }
}


// --------------------------------------------------------------- pure logic

// The mod's decisions, kept apart from the Windows calls that feed them: plain
// values in, plain values out, no globals. tests/test_mod_logic.py compiles
// everything between the two markers on its own and runs it, so nothing in
// here may use more than <windows.h> types.
// ==PureLogic==

// A flashing taskbar button produces one unhide per flash. Unhides closer
// together than the gap belong to one burst and are treated as one
// notification: the taskbar shows once, hides after the hold, and stays hidden
// while the flashing continues.
//
// `request` is the kind of unhide that opened the burst. A flashing button
// repeats one kind. Any other kind arriving mid-burst is a separate event --
// the Win key, a different notification -- so it is shown, but it does not
// open a new burst: if it did, the flash after it would look new as well, and
// every stray request would bring the taskbar back twice.
struct BurstState {
    LONG64 lastUnhide;
    LONG64 holdUntil;
    int request;   // -1: no burst yet
};

enum BurstVerdict {
    BURST_BEGIN = 0,   // a new burst: show it for the hold
    BURST_PASS = 1,    // let Windows show it
    BURST_DROP = 2,    // the burst's own request repeating after its hold
};

// The one unhide request a flashing button sends (CTaskBand::UnhideTray and
// CSecondaryTaskBand::UnhideTray; no other caller uses it). Every other
// request is the user or the shell asking, and is never held back.
#define UNHIDE_REQUEST_FLASH 0x11

BurstVerdict BurstDecide(BurstState* s, LONG64 now, int request, int holdMs,
                         int gapMs, bool pointerAtEdge, bool surfaceInFront) {
    if (request != UNHIDE_REQUEST_FLASH) {
        return BURST_PASS;
    }

    const LONG64 last = s->lastUnhide;
    s->lastUnhide = now;

    if (s->request < 0 || now - last > gapMs) {
        s->request = request;
        s->holdUntil = now + holdMs;
        return BURST_BEGIN;
    }
    if (request != s->request) {
        return BURST_PASS;
    }
    if (now >= s->holdUntil && !pointerAtEdge && !surfaceInFront) {
        return BURST_DROP;
    }
    return BURST_PASS;
}

// Whether to override Windows' refusal to hide. Windows refuses while a
// button flashes, and also while a jump list or a task-list flyout is open.
// Only the first is the mod's business: a burst exists, its hold has run out,
// and the last flash was within the gap, so the flashing is still going on.
bool BurstOverridesVeto(const BurstState* s, LONG64 now, int gapMs) {
    return s->request >= 0 && now >= s->holdUntil &&
           now - s->lastUnhide <= gapMs;
}

// Whether a Quick Settings window counts as open. Its window outlives the
// panel: measured on 26100.9974, it stays where it was shown and only stops
// being visible when dismissed, by Esc, a click elsewhere or Win+A alike.
// Cloaked and off every display are the other ways a shell window is "there"
// without being on screen.
bool PanelCountsAsOpen(bool visible, bool cloaked, bool onDisplay) {
    return visible && !cloaked && onDisplay;
}

// Whether to warn that auto-hide is off in Windows: only once it has stayed
// off for `waitMs`. `offSince` is when it was first seen off, 0 while it is
// on. Moving the taskbar to another edge reports it off for a moment.
bool AutoHideOffLongEnough(LONG64 offSince, LONG64 now, int waitMs) {
    return offSince != 0 && now - offSince >= waitMs;
}

// Whether the hide delay has run out: `lastKeptUp` is the last moment
// something was keeping the taskbar up.
bool HideDelayElapsed(LONG64 now, LONG64 lastKeptUp, int delayMs) {
    return now - lastKeptUp >= delayMs;
}

// ---- taskbars held on their own display
//
// Windows hides a taskbar by moving its window past the docked edge. With
// another display past that edge, the hidden window sits mostly on that
// display and takes its DPI, and the icons come back at the wrong size. Such a
// taskbar is "held": its window does not move at all, and its content slides
// inside it instead (see PlaceBar).

// Whether `bar`, a taskbar window docked to `edge`, is `display`'s taskbar. A
// taskbar spans its display along the docked edge, and shown or hidden it
// reaches that edge: shown it ends on it, hidden it straddles it or, hidden
// completely, touches it from outside. The display
// past the edge matches neither, which is why the display the window is
// mostly on cannot be asked: hidden, that is the wrong one.
bool BarBelongsTo(const RECT& display, const RECT& bar, UINT edge) {
    const RECT& m = display;
    const RECT& b = bar;
    switch (edge) {
        case ABE_LEFT:
            return b.top == m.top && b.bottom == m.bottom &&
                   b.right <= m.right && b.right >= m.left && b.left <= m.left;
        case ABE_TOP:
            return b.left == m.left && b.right == m.right &&
                   b.bottom <= m.bottom && b.bottom >= m.top && b.top <= m.top;
        case ABE_RIGHT:
            return b.top == m.top && b.bottom == m.bottom &&
                   b.left >= m.left && b.left <= m.right && b.right >= m.right;
        default:
            return b.left == m.left && b.right == m.right && b.top >= m.top &&
                   b.top <= m.bottom && b.bottom >= m.bottom;
    }
}

// Which windows to start and stop keeping a record for: `seen` are the
// taskbar windows that exist now, `have` the ones with a record. Returns the
// number written to toAdd; *nDrop receives the number written to toDrop.
int DiffWindows(const HWND* have, int nHave, const HWND* seen, int nSeen,
                HWND* toAdd, HWND* toDrop, int* nDrop) {
    int nAdd = 0;
    *nDrop = 0;
    for (int i = 0; i < nSeen; i++) {
        bool known = false;
        for (int j = 0; j < nHave; j++) {
            known = known || have[j] == seen[i];
        }
        if (!known) {
            toAdd[nAdd++] = seen[i];
        }
    }
    for (int j = 0; j < nHave; j++) {
        bool still = false;
        for (int i = 0; i < nSeen; i++) {
            still = still || seen[i] == have[j];
        }
        if (!still) {
            toDrop[(*nDrop)++] = have[j];
        }
    }
    return nAdd;
}

// Whether another display touches the one-pixel band just past `edge` of
// `own`. An edge that only partly faces another display counts.
bool DisplayBeyond(const RECT& own, UINT edge, const RECT* others, int count) {
    RECT band = own;
    switch (edge) {
        case ABE_LEFT:   band.right = own.left;  band.left = own.left - 1;     break;
        case ABE_TOP:    band.bottom = own.top;  band.top = own.top - 1;       break;
        case ABE_RIGHT:  band.left = own.right;  band.right = own.right + 1;   break;
        default:         band.top = own.bottom;  band.bottom = own.bottom + 1; break;
    }
    for (int i = 0; i < count; i++) {
        const RECT& o = others[i];
        if (o.left < band.right && band.left < o.right && o.top < band.bottom &&
            band.top < o.bottom) {
            return true;
        }
    }
    return false;
}

// Whether another display touches the place a taskbar's window would lie in
// once slid `thickness` past `edge` of `own`, corners included. A window on
// no display is given the scaling of the nearest one, and Windows took a
// display that only touches that place for the nearest (measured: a primary
// at 125% with a taller display at 200% beside it; the primary's taskbar,
// hidden off its top edge, came back at 200%). Such a taskbar must not leave
// its display either.
bool HiddenNearOther(const RECT& own, UINT edge, int thickness,
                     const RECT* others, int count) {
    RECT place = own;
    switch (edge) {
        case ABE_LEFT:   place.right = own.left;  place.left = own.left - thickness;     break;
        case ABE_TOP:    place.bottom = own.top;  place.top = own.top - thickness;       break;
        case ABE_RIGHT:  place.left = own.right;  place.right = own.right + thickness;   break;
        default:         place.top = own.bottom;  place.bottom = own.bottom + thickness; break;
    }
    for (int i = 0; i < count; i++) {
        const RECT& o = others[i];
        if (o.left <= place.right && place.left <= o.right &&
            o.top <= place.bottom && place.top <= o.bottom) {
            return true;
        }
    }
    return false;
}

struct BarPlacement {
    int x, y;       // the window's top-left
    RECT region;    // in window coordinates; meaningful when clipped
    bool clipped;
    int contentDx, contentDy;   // where the content sits inside the window
};

// Where a taskbar's window, region and content go with `hiddenPx` of its
// thickness hidden. `shown` is the window's top-left when fully shown.
//
// Not held: the window itself slides off the desktop, as Windows moves it.
//
// Held: the window may not leave its display, so it stays where it is and its
// content, which is a child window, slides inside it toward the edge. The
// window clips the content at the display's edge, so the whole taskbar is
// seen sliding off in one piece, exactly as it does when the window moves, but
// nothing is ever on the other display and the window's DPI cannot change.
//
// Either way the region keeps only what is still showing, so the rest of the
// window's area does not take the pointer.
BarPlacement PlaceBar(POINT shown, SIZE size, UINT edge, bool held,
                      int hiddenPx) {
    const bool vertical = edge == ABE_LEFT || edge == ABE_RIGHT;
    const int thickness = vertical ? size.cx : size.cy;
    if (hiddenPx < 0) hiddenPx = 0;
    if (hiddenPx > thickness) hiddenPx = thickness;
    const int visible = thickness - hiddenPx;

    BarPlacement p{shown.x, shown.y, {0, 0, 0, 0}, hiddenPx > 0, 0, 0};
    if (!p.clipped) {
        return p;
    }
    p.region = {0, 0, size.cx, size.cy};

    if (held) {
        switch (edge) {
            case ABE_LEFT:
                p.contentDx = -hiddenPx;
                p.region.right = visible;
                break;
            case ABE_TOP:
                p.contentDy = -hiddenPx;
                p.region.bottom = visible;
                break;
            case ABE_RIGHT:
                p.contentDx = hiddenPx;
                p.region.left = hiddenPx;
                break;
            default:
                p.contentDy = hiddenPx;
                p.region.top = hiddenPx;
                break;
        }
        return p;
    }

    switch (edge) {
        case ABE_LEFT:
            p.x -= hiddenPx;
            p.region.left = hiddenPx;
            break;
        case ABE_TOP:
            p.y -= hiddenPx;
            p.region.top = hiddenPx;
            break;
        case ABE_RIGHT:
            p.x += hiddenPx;
            p.region.right = visible;
            break;
        default:
            p.y += hiddenPx;
            p.region.bottom = visible;
            break;
    }
    return p;
}

// The strip along the docked edge of the taskbar's own display, as long as the
// taskbar, where a resting pointer reveals it. `widthPx` is at 100% scaling.
RECT RevealStrip(const RECT& display, const RECT& bar, UINT edge, int widthPx,
                 UINT dpi) {
    const int depth = MulDiv(widthPx, (int)dpi, 96);
    switch (edge) {
        case ABE_LEFT:
            return {display.left, bar.top, display.left + depth, bar.bottom};
        case ABE_TOP:
            return {bar.left, display.top, bar.right, display.top + depth};
        case ABE_RIGHT:
            return {display.right - depth, bar.top, display.right, bar.bottom};
        default:
            return {bar.left, display.bottom - depth, bar.right, display.bottom};
    }
}

// On a held edge the pointer crosses the strip every time it moves between
// the two displays, so the reveal always waits a little.
int HeldDwellMs(int settingMs) {
    return settingMs > 150 ? settingMs : 150;
}

// ...and the strip is never thinner than this, at 100% scaling: the pointer
// reaches a held edge on its way to the next display, and the 8 px default
// proved too thin to come to rest in.
int HeldStripPx(int settingPx) {
    return settingPx > 12 ? settingPx : 12;
}

// How many pixels of its thickness a taskbar hides. Windows leaves a 2 px
// sliver for the pointer to find, and "Hide completely" takes that away. A
// held taskbar always hides all of itself: it is revealed from the strip on
// its display, not from a sliver.
int HiddenTarget(int thickness, bool hideFully, bool held) {
    return hideFully || held ? thickness : thickness - 2;
}

// A taskbar steps once per refresh of its own display, up to the cap.
int StepsPerSecond(int displayHz, int capFps) {
    if (displayHz <= 1) {
        displayHz = 60;   // the driver did not say
    }
    return displayHz < capFps ? displayHz : capFps;
}

// ==/PureLogic==

extern HWND g_trayWnd;   // defined with the taskbar state below

// True when another display sits immediately beyond this edge of the taskbar's
// own monitor. Such an edge has nowhere to hide into -- the hidden position is
// occupied screen space on the neighbour -- so the mod leaves it alone.
bool DisplayBeyondEdge(UINT edge) {
    if (!g_trayWnd) {
        return false;
    }

    HMONITOR own = MonitorFromWindow(g_trayWnd, MONITOR_DEFAULTTONULL);
    MONITORINFO mi{sizeof(mi)};
    if (!own || !GetMonitorInfoW(own, &mi)) {
        return false;
    }

    const RECT& m = mi.rcMonitor;
    POINT probe{};
    switch (edge) {
        case ABE_LEFT:  probe = {m.left - 1, (m.top + m.bottom) / 2}; break;
        case ABE_RIGHT: probe = {m.right, (m.top + m.bottom) / 2};    break;
        case ABE_TOP:   probe = {(m.left + m.right) / 2, m.top - 1};  break;
        default:        return false;
    }

    HMONITOR there = MonitorFromPoint(probe, MONITOR_DEFAULTTONULL);
    return there && there != own;
}

// EdgeEnabled sits in the hook path, which runs far too often to query the
// display layout on every call. A second of staleness is harmless: the answer
// changes only when monitors are rearranged.
volatile LONG g_facingEdge = -1;
volatile LONG g_facingResult = 0;
volatile LONG g_facingTick = 0;

bool EdgeFacesDisplay(UINT edge) {
    const LONG now = (LONG)GetTickCount64();
    const ULONG since =
        (ULONG)(now - InterlockedCompareExchange(&g_facingTick, 0, 0));
    if (InterlockedCompareExchange(&g_facingEdge, -1, -1) != (LONG)edge ||
        since > 1000) {
        const LONG r = DisplayBeyondEdge(edge) ? 1 : 0;
        InterlockedExchange(&g_facingResult, r);
        InterlockedExchange(&g_facingTick, now);
        InterlockedExchange(&g_facingEdge, (LONG)edge);
        return r != 0;
    }

    return InterlockedCompareExchange(&g_facingResult, 0, 0) != 0;
}

bool EdgeEnabled(UINT edge) {
    if (edge >= ABE_BOTTOM) {
        return false;   // bottom works natively
    }

    return !(g_settings.skipAdjacentEdges && EdgeFacesDisplay(edge));
}


// ---------------------------------------------------------------- build gate

typedef struct _MOD_OSVERSIONINFOW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} MOD_OSVERSIONINFOW;

ULONG g_buildNumber = 0;

bool IsSupportedBuild() {
    using RtlGetVersion_t = LONG(WINAPI*)(MOD_OSVERSIONINFOW*);

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    auto pRtlGetVersion =
        (RtlGetVersion_t)(void*)GetProcAddress(ntdll, "RtlGetVersion");
    if (!pRtlGetVersion) {
        return false;
    }

    MOD_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (pRtlGetVersion(&vi) != 0) {
        return false;
    }

    g_buildNumber = vi.dwBuildNumber;
    return vi.dwBuildNumber >= 26200;
}

PCWSTR EdgeName(UINT edge) {
    switch (edge) {
        case ABE_LEFT:   return L"left";
        case ABE_TOP:    return L"top";
        case ABE_RIGHT:  return L"right";
        case ABE_BOTTOM: return L"bottom";
        default:         return L"unknown";
    }
}

// ------------------------------------------------------------ hooked symbols

using TrayUI_GetStuckPlace_t = UINT(WINAPI*)(void* pThis);
using TrayUI_GetAutoHideFlags_t = UINT(WINAPI*)(void* pThis);
using TrayUI_SetAutoHideFlags_t = void(WINAPI*)(void* pThis, UINT flags);
using TrayUI_GetDockedRect_t = UINT(WINAPI*)(void* pThis, RECT* rect, int arg);
using TrayUI_Unhide_t = void(WINAPI*)(void* pThis, int flags, int request);
using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis, HWND hWnd,
                                           const RECT* rect, HMONITOR monitor,
                                           bool show, bool animate);

TrayUI_GetStuckPlace_t TrayUI_GetStuckPlace_Original;
TrayUI_GetAutoHideFlags_t TrayUI_GetAutoHideFlags_Original;

// Once a function is hooked, its _Original pointer is a trampoline, and the
// bytes behind it belong to the hook engine rather than taskbar.dll. The
// _Address pointers are resolved without hooking, for code that reads the
// function's own instructions.
TrayUI_GetAutoHideFlags_t TrayUI_GetAutoHideFlags_Address;
TrayUI_SetAutoHideFlags_t TrayUI_SetAutoHideFlags_Original;
TrayUI_GetDockedRect_t TrayUI_GetDockedRect_Original;
TrayUI_Unhide_t TrayUI_Unhide_Original;
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original;
TrayUI_SlideWindow_t TrayUI_SlideWindow_Address;

bool g_offsetsVerified = false;

// A TrayUI instance on the base the calls above expect, captured from
// GetDockedRect.
void* g_trayUI = nullptr;

HWND g_trayWnd = nullptr;
bool g_subclassed = false;
int g_leaveTicks = 0;
ULONGLONG g_dwellStart = 0;

// The last moment something was keeping the shown taskbar up; the hide delay
// counts from here. Taskbar thread only.
LONG64 g_lastKeptUp = 0;

// Windows unhides the taskbar itself for notifications and flashing buttons.
// Re-hiding immediately makes it flap, so hold it visible for a while after any
// unhide the mod did not issue.
volatile LONG64 g_holdVisibleUntil = 0;

// Windows 11 hosts Start, Search and the shell flyouts in their own processes,
// so the taskbar is not the foreground window while any of them is open.
bool ForegroundIsShellHost(DWORD pid) {
    // Resolving the image name every tick is wasteful and the foreground
    // process rarely changes.
    static DWORD cachedPid = 0;
    static bool cachedResult = false;
    if (pid == cachedPid) {
        return cachedResult;
    }

    bool match = false;
    if (HANDLE proc =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        WCHAR path[MAX_PATH];
        DWORD len = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(proc, 0, path, &len)) {
            PCWSTR name = wcsrchr(path, L'\\');
            name = name ? name + 1 : path;
            static PCWSTR kShellHosts[] = {
                L"StartMenuExperienceHost.exe",
                L"SearchHost.exe",
                L"ShellExperienceHost.exe",
                L"ShellHost.exe",   // hosts Quick Settings on current builds
            };
            for (PCWSTR host : kShellHosts) {
                if (lstrcmpiW(name, host) == 0) {
                    match = true;
                    break;
                }
            }
        }
        CloseHandle(proc);
    }

    cachedPid = pid;
    cachedResult = match;
    return match;
}

// A flyout only takes the foreground when the window behind it was not an
// ordinary app -- hovering a taskbar button while Terminal is focused leaves
// Terminal foreground, so no focus test can see the thumbnail peek. The pointer
// can: it is inside the flyout, which is the whole reason the taskbar must stay
// up. Same tool-window rule, asked of the window under the cursor.
bool PointerOverShellSurface() {
    POINT pt;
    if (!GetCursorPos(&pt)) {
        return false;
    }

    HWND under = WindowFromPoint(pt);
    if (!under) {
        return false;
    }

    HWND top = GetAncestor(under, GA_ROOT);
    if (!top || top == g_trayWnd) {
        return false;   // the taskbar itself is tested separately
    }

    // The desktop is a tool window in explorer.exe spanning every monitor
    // (Progman, WS_EX_TOOLWINDOW), so without this the rule below matches
    // anywhere the pointer is not over an ordinary window, and the taskbar
    // never hides. GetShellWindow names it without depending on the class.
    if (top == GetShellWindow()) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(top, &pid);
    if (pid != GetCurrentProcessId()) {
        return false;
    }

    return (GetWindowLongPtrW(top, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
}

// Whether a shell surface (Start, Search, a flyout's host) is open in front.
// Deliberately narrow: the taskbar holding focus is not a surface -- it keeps
// focus after its own flyouts close -- and neither is the desktop or a File
// Explorer window, though all three belong to this process.
bool ShellSurfaceInForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_trayWnd) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) {
        return false;
    }

    // Deliberately no test for this process. A dismissed flyout keeps the
    // foreground and still reports itself visible, so holding the taskbar up
    // for one latched until something else was clicked. The taskbar's own
    // flyouts are covered by PointerOverShellSurface, which cannot latch
    // because the pointer moves away on its own.
    if (pid == GetCurrentProcessId()) {
        return false;
    }

    return ForegroundIsShellHost(pid);
}

// Whether a Quick Settings panel is open. The taskbar must stay up for as long
// as it is: the panel is anchored to the taskbar, and when focus moves to a
// popup of another process the foreground test above no longer sees it.
bool QuickSettingsOpen() {
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"ControlCenterWindow", nullptr))) {
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (PanelCountsAsOpen(IsWindowVisible(w), cloaked,
                              MonitorFromWindow(w, MONITOR_DEFAULTTONULL) !=
                                  nullptr)) {
            return true;
        }
    }
    return false;
}

// The notification burst in progress; see BurstDecide. Only the unhide hook
// touches it, on the taskbar's thread.
BurstState g_burst{0, 0, -1};
void RevealSecondariesForFlash();

// The last docked rect seen while shown, so unloading can put the taskbar back.
RECT g_lastDockedRect{};

// Set while Windows is unhiding, and while the watcher is calling in.
thread_local bool g_inUnhide = false;
thread_local bool g_inWatcherCall = false;

UINT CurrentStuckPlace(void* pThis) {
    if (!TrayUI_GetStuckPlace_Original || !pThis) {
        return 0xFFFFFFFF;
    }
    return TrayUI_GetStuckPlace_Original(pThis);
}

// Moving the taskbar to another edge is TrayUI::_HandleTrayPrivateSettingMessage
// with setting 6 (read from taskbar.dll 9974 and 10066). Its one caller is the
// tray's window procedure, for message 0x5CA with wParam = the setting and
// lParam = the value, and every sender found uses SendMessage (Settings, its
// registry watcher, WinRT TaskbarSettings, the context menu). A second path,
// message 0x5B2 (remote settings: the edge in the low byte of wParam), does the
// same. The mod sees both in its subclass of the tray window before Windows
// does, so it brackets them there, on every build, with no symbol needed.
// The handler does, in order:
//
//   if ((GetAutoHideFlags() & 3) == 3) SetAutoHideState(0);   // show first
//   SetStuckPlace(edge);    // stores the edge, posts 0x5c3 to the tray window
//                           // (which announces the edge to the shell), cloaks
//                           // until the next render, moves the window
//   Unhide(0, 7); SizeWindows();
//   ... and _UpdateAutoHideState(true) again if it had switched it off.
//
// Without the mod GetAutoHideFlags returns 0 off the bottom edge, so the two
// auto-hide steps never run there. With the mod answering "hidden", Windows
// took that branch on an edge it was never written for, the mod's other hooks
// reshaped the unhide inside it, and the calendar, notification panel and
// Start went on opening where the previous edge called for until Explorer was
// restarted. So for the whole of that call, and a moment after it while the
// announcement it queued is delivered, the mod is transparent: every hook
// passes straight through and no taskbar is hidden or held.
#define EDGE_SETTLE_MS 2500
#define TRAY_MSG_PRIVATE_SETTING 0x5CA
#define TRAY_SETTING_STUCK_PLACE 6
#define TRAY_SETTING_AUTO_HIDE 4
#define TRAY_MSG_REMOTE_SETTINGS 0x5B2
volatile LONG g_lastEdgeSeen = -1;
volatile LONG64 g_edgeSettleUntil = 0;
volatile LONG g_inEdgeChange = 0;

bool EdgeChangeInProgress() {
    return InterlockedCompareExchange(&g_inEdgeChange, 0, 0) != 0 ||
           (LONG64)GetTickCount64() <
               InterlockedCompareExchange64(&g_edgeSettleUntil, 0, 0);
}

// Note the edge a hook has just read, and say whether the mod should stand
// back because the edge is changing or changed a moment ago. The window
// messages that change the edge are bracketed in the tray's subclass; seeing a
// new edge here covers any path that reaches Windows another way.
bool EdgeSettling(UINT edge) {
    const LONG64 now = (LONG64)GetTickCount64();
    if (edge <= ABE_BOTTOM) {
        const LONG before = InterlockedExchange(&g_lastEdgeSeen, (LONG)edge);
        if (before != -1 && before != (LONG)edge) {
            InterlockedExchange64(&g_edgeSettleUntil, now + EDGE_SETTLE_MS);
            DLog(L"taskbar moved from edge %d to edge %u: standing back for "
                   L"%d ms",
                   (int)before, edge, EDGE_SETTLE_MS);
        }
    }
    return EdgeChangeInProgress();
}

// ------------------------------------------------------------ pointer helper

RECT g_monitorRect{};
ULONGLONG g_monitorRectAt = 0;

bool TrayMonitorRect(RECT* out) {
    ULONGLONG now = GetTickCount64();
    if (g_monitorRect.right == 0 || now - g_monitorRectAt > 2000) {
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!tray) {
            return false;
        }
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(tray, MONITOR_DEFAULTTOPRIMARY),
                             &mi)) {
            return false;
        }
        g_monitorRect = mi.rcMonitor;
        g_monitorRectAt = now;
    }
    *out = g_monitorRect;
    return true;
}

// True when the pointer is within the thin strip that triggers a reveal. This
// is not the same as being over the taskbar: once revealed, the taskbar
// occupies its full width and the stay-visible test uses its docked rect.
bool CursorAtRevealStrip(UINT edge) {
    POINT pt;
    if (!GetCursorPos(&pt)) {
        return false;
    }

    RECT m{};
    if (!TrayMonitorRect(&m)) {
        return false;
    }

    if (pt.x < m.left || pt.x > m.right || pt.y < m.top || pt.y > m.bottom) {
        return false;
    }

    const LONG tol = g_settings.revealTolerance;
    switch (edge) {
        case ABE_LEFT:  return pt.x <= m.left + tol;
        case ABE_TOP:   return pt.y <= m.top + tol;
        case ABE_RIGHT: return pt.x >= m.right - tol - 1;
        case ABE_BOTTOM: return pt.y >= m.bottom - tol - 1;
        default:        return false;
    }
}

// The pointer being over whatever remains of the hidden taskbar should reveal
// it too, independently of the trigger width.
bool CursorOverTaskbar(void* pThis) {
    if (!pThis || !TrayUI_GetDockedRect_Original) {
        return false;
    }
    RECT r{};
    TrayUI_GetDockedRect_Original(pThis, &r, 0);
    // Only the part on the taskbar's own display. Hidden on an edge with a
    // display past it, the rect Windows reports lies mostly on that display,
    // and a pointer resting there, near the seam, is not over the taskbar. It
    // was taken to be, the watcher revealed the taskbar on every tick, the
    // flags hook hid it again each time, and Start opened only half the time.
    RECT m{};
    if (TrayMonitorRect(&m) && !IntersectRect(&r, &r, &m)) {
        return false;
    }
    POINT pt;
    return GetCursorPos(&pt) && PtInRect(&r, pt);
}

// Defined with the z-order handling below; the flags hook needs it too.
void RaiseTaskbarIfDemoted();

// ------------------------------------------------------------------- hooks

// Three of the four outcomes below are the mod working correctly while
// appearing to do nothing. Saying which one applies, once per session, answers
// "is it broken?" without a diagnostic build. Called from GetAutoHideFlags,
// which runs whatever the edge.
void LogStateOnce(UINT edge, UINT stored) {
    static volatile LONG logged = 0;
    if (InterlockedExchange(&logged, 1) != 0) {
        return;
    }

    Wh_Log(L"docked %s, Windows auto-hide %s -- %s", EdgeName(edge),
           (stored & ABS_AUTOHIDE) ? L"on" : L"off",
           edge >= ABE_BOTTOM ? L"bottom edge, Windows handles this itself"
           : !EdgeEnabled(edge)
               ? L"a display sits beyond this edge, so it is skipped"
           : !(stored & ABS_AUTOHIDE)
               ? L"nothing to do until auto-hide is turned on in Windows"
               : L"active");
}

void PublishAutoHideState(UINT edge, UINT stored) {
    if (edge < ABE_BOTTOM && EdgeEnabled(edge) && !(stored & ABS_AUTOHIDE)) {
        // Keep the first moment it was seen off.
        InterlockedCompareExchange64(&g_autoHideOffSince,
                                     (LONG64)GetTickCount64(), 0);
    } else {
        InterlockedExchange64(&g_autoHideOffSince, 0);
    }
}


// Returns the stored flags rather than the zero the original reports when the
// taskbar is not docked to the bottom.
// Set as the mod starts to unload. From then on Windows gets its own answer
// about auto-hide, which off the bottom edge is "off", as it will once the
// mod is gone. Without this, a secondary taskbar that Windows has just been
// made to show is hidden again by Windows half a second later, while the
// hooks are still in place, and then stays hidden. (This was taken out once,
// on the suspicion that it made Windows save auto-hide as off. That turned
// out to be Windows reporting "off" for a while after Explorer starts.)
volatile LONG g_unloading = 0;

UINT WINAPI TrayUI_GetAutoHideFlags_Hook(void* pThis) {
    UINT ret = TrayUI_GetAutoHideFlags_Original(pThis);
    if (g_fatalReason || g_unloading || EdgeChangeInProgress() || !pThis ||
        !g_offsetsVerified) {
        return ret;
    }

    // This function is reached on a base 8 bytes below the one the other calls
    // use, so it is a second source for the pointer the watcher needs. Without
    // it the watcher idles until a layout pass happens to run GetDockedRect.
    // Captured before the edge is checked; every user of the pointer checks the
    // edge for itself.
    if (!g_trayUI) {
        g_trayUI = (BYTE*)pThis + 8;
    }

    UINT edge = *(UINT*)((BYTE*)pThis + g_getFlagsEdgeOffset);
    UINT stored = *(UINT*)((BYTE*)pThis + g_getFlagsFlagsOffset);
    LogStateOnce(edge, stored);
    PublishAutoHideState(edge, stored);

    if (edge >= ABE_BOTTOM || !EdgeEnabled(edge)) {
        return ret;
    }

    return stored;
}

// TrayUI::SetAutoHideFlags and TrayUI::GetDockedRect are called, not hooked.
// The mod used to hook the first to add the hidden bit whenever Windows set
// auto-hide off the bottom edge; the watcher stores that state itself when it
// hides the taskbar, and Windows stores it nowhere else there (only _Hide and
// _KickStartAutohide write it, and _Hide stops at its edge test). The second
// was hooked only to capture the TrayUI pointer, which the GetAutoHideFlags
// hook captures too.

// Request 9 is TrayUI::InvisibleUnhide: the unhide Windows does around layout
// work (SizeWindows, VerifySize, sizing, a theme change). It follows the
// hidden state being set and would undo the hide, so drop it unless the
// pointer is actually at the edge. The request number is otherwise only
// telemetry. By caller: 2 Start (with flags 1), 4 a tray flyout, 5 auto-hide
// switched off, 7 an edge change, 8 activation, 0xf the pointer, 0x11 a
// flashing button, 0x12 a flyout reported by message 0x5d1. Nothing in
// Windows uses 1, which is what the mod passes for its own reveal.
#define UNHIDE_REQUEST_STATE_CHANGED 9

void WINAPI TrayUI_Unhide_Hook(void* pThis, int flags, int request) {
    if (g_fatalReason) {
        TrayUI_Unhide_Original(pThis, flags, request);
        return;
    }

    UINT edge = CurrentStuckPlace(pThis);
    if (EdgeSettling(edge)) {
        TrayUI_Unhide_Original(pThis, flags, request);
        return;
    }

    if (request == UNHIDE_REQUEST_STATE_CHANGED && edge < ABE_BOTTOM &&
        EdgeEnabled(edge) && !g_inWatcherCall && !CursorAtRevealStrip(edge)) {
        return;
    }

    if (!g_inWatcherCall && g_settings.notificationHoldMs > 0) {
        // A repeat is let through when the user is plainly asking: the
        // pointer is at the edge, or a shell surface is open in front. The
        // desktop or a File Explorer window having focus is not that, so this
        // asks ShellSurfaceInForeground, not ShellHasFocus. Applies on every
        // edge including the bottom, where Windows drives the hiding itself.
        const bool pointerAtEdge =
            edge > ABE_BOTTOM || CursorAtRevealStrip(edge);
        switch (BurstDecide(&g_burst, (LONG64)GetTickCount64(), request,
                            g_settings.notificationHoldMs,
                            g_settings.notificationBurstGapMs, pointerAtEdge,
                            ShellSurfaceInForeground())) {
            case BURST_BEGIN:
                InterlockedExchange64(&g_holdVisibleUntil, g_burst.holdUntil);
                RevealSecondariesForFlash();
                break;
            case BURST_DROP:
                return;
            case BURST_PASS:
                break;
        }
    }

    // Any unhide that reaches Windows means the taskbar is to become visible:
    // Windows then asks for the shown position, and the mod's animation turns
    // round from wherever it is.
    // A reveal Windows drives itself (the Windows key, a notification) never
    // passes through the watcher. Raise the taskbar now, while it is still
    // out of sight, so it does not come in behind a window and then jump in
    // front.
    if (edge < ABE_BOTTOM && EdgeEnabled(edge)) {
        RaiseTaskbarIfDemoted();
    }

    bool prev = g_inUnhide;
    g_inUnhide = true;
    TrayUI_Unhide_Original(pThis, flags, request);
    g_inUnhide = prev;
}

// ------------------------------------------------- notification auto-hide

// Windows refuses to auto-hide while an app flashes its taskbar button, and
// keeps refusing until the flashing stops, so the taskbar can sit there
// indefinitely. CTaskListWnd::PermitAutoHide is the veto, and it is on Windows'
// own hide path -- the path that runs on the bottom edge, where this mod's
// watcher is deliberately inactive.
//
// Overriding it here gives the same behaviour on every edge from one mod. The
// approach is the one used by Cirn09's "Better Taskbar Autohide"
// (https://windhawk.net/mods/taskbar-autohide-better), reimplemented against
// this mod's own notification tracking rather than a separate message hook.

using CTaskListWnd_PermitAutoHide_t = int(WINAPI*)(void* pThis);
CTaskListWnd_PermitAutoHide_t CTaskListWnd_PermitAutoHide_Original;
bool AnyBurstOverridesVeto(LONG64 now);   // with the secondary bursts, below

bool BottomHideDelayElapsed();

int WINAPI CTaskListWnd_PermitAutoHide_Hook(void* pThis) {
    // While Quick Settings is open the taskbar stays up, on every edge. On
    // the bottom edge Windows decides the hiding and, left alone, hides the
    // taskbar under an open panel once the pointer has left (measured on
    // 10.0.26100.10066 without the mod). Off the bottom edge the mod's own
    // watcher already holds it; this makes the bottom the same. Refusing here
    // is how Windows itself holds the taskbar for an open jump list.
    if (!g_fatalReason && QuickSettingsOpen()) {
        return FALSE;
    }

    // The hide delay, on the bottom edge. Off it the mod decides when a
    // taskbar hides and waits there; here Windows decides, and asks this
    // first, so the wait is a refusal until the delay has passed.
    if (!g_fatalReason && !BottomHideDelayElapsed()) {
        return FALSE;
    }

    // Otherwise permit only for a flashing button whose hold is over.
    // Windows' other reasons to refuse (an open jump list or task-list
    // flyout) stand.
    if (!g_fatalReason && g_settings.notificationHoldMs > 0 &&
        AnyBurstOverridesVeto((LONG64)GetTickCount64()) &&
        !QuickSettingsOpen()) {
        return TRUE;
    }
    return CTaskListWnd_PermitAutoHide_Original(pThis);
}

// ------------------------------------------------------------- hide animation

// The taskbar rewrites requested window positions in its WM_WINDOWPOSCHANGING
// handler, forcing every intermediate position to the endpoint the auto-hide
// flags imply. That is why the hide appeared instant: the window jumped to the
// hidden rect on the first step of the slide and never occupied the positions
// in between.
//
// Animating therefore means two things: step the window on the mod's own
// clock, not through SlideWindow, whose loop blocks the taskbar's thread; and
// put the requested position back after the taskbar's handler has replaced it,
// so the frames in between survive. Both are in "the mod's own hide and
// reveal" further down.

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// GetTickCount64 advances in ~15.6 ms steps, so sampling it once per frame at
// any useful rate returns the same value repeatedly and the slide renders
// duplicate positions. QueryPerformanceCounter is what makes a frame rate mean
// anything.
double g_qpcPeriodMs = 0.0;
HANDLE g_slideTimer = nullptr;   // taskbar thread, inside SlideWindow

double NowMs() {
    // Without this the slide would never advance and the loop would spin until
    // the mod unloaded. QueryPerformanceFrequency is documented never to fail
    // on anything this mod runs on, which is exactly why the fallback is cheap.
    if (g_qpcPeriodMs == 0.0) {
        return (double)GetTickCount64();
    }

    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * g_qpcPeriodMs;
}

void FrameTimerInit() {
    LARGE_INTEGER f;
    if (QueryPerformanceFrequency(&f) && f.QuadPart) {
        g_qpcPeriodMs = 1000.0 / (double)f.QuadPart;
    }

    // Sleep cannot resolve finer than the system timer, typically 15.6 ms, so a
    // requested 90 fps would silently cap near 64. A high-resolution timer is
    // not bound by it, and unlike timeBeginPeriod it costs the rest of the
    // process nothing.
    for (HANDLE* h : {&g_slideTimer}) {
        *h = CreateWaitableTimerExW(nullptr, nullptr,
                                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!*h) {
            *h = CreateWaitableTimerExW(nullptr, nullptr, 0,
                                        TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
    }
}

// Declared before FrameWait so the fallback can reach the unhooked Sleep:
// calling Sleep itself from inside the Sleep hook would recurse.
using Sleep_t = decltype(&Sleep);
Sleep_t Sleep_Original = nullptr;

void FrameWait(HANDLE timer, double ms) {
    if (ms <= 0.0) {
        return;
    }

    if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ms * 10000.0);   // relative, 100 ns units
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }

    const DWORD ticks = (DWORD)(ms + 0.5);
    if (Sleep_Original) {
        Sleep_Original(ticks);
    } else {
        Sleep(ticks);
    }
}

// Where the mod does not move the taskbars itself (the bottom edge with
// nothing held), the slide is Windows' own loop and there is nothing here to
// pace. What can be reached is the Sleep between its frames: record the thread
// inside SlideWindow, and replace that sleep with a frame budget while on it.
volatile LONG g_slideThreadId = 0;
double g_slideStartMs = 0.0;
double g_slideFrameStart = 0.0;

struct TaskbarRec;
TaskbarRec* RecFor(HWND hwnd);
bool BarsHandled();
void SlideHint(HWND hwnd, bool show);
bool SlideBarBefore(HWND hwnd, bool show);
void SlideBarAfter(HWND hwnd);

void WINAPI TrayUI_SlideWindow_Hook(void* pThis, HWND hWnd, const RECT* rect,
                                    HMONITOR monitor, bool show, bool animate) {
    if (g_fatalReason) {
        TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
        return;
    }

    // A taskbar the mod animates itself: Windows' slide is a loop on this
    // thread that moves the window in a few coarse steps and blocks the thread
    // meanwhile. Without it Windows asks for the end position once, and the
    // mod goes there on its own clock. Secondary taskbars slide through this
    // function too.
    if (!EdgeChangeInProgress() && BarsHandled() && RecFor(hWnd)) {
        DLog(L"taskbar %p: SlideWindow to (%d,%d,%d,%d), show %d, animate "
               L"%d -> not animated by Windows",
               hWnd, rect ? rect->left : 0, rect ? rect->top : 0,
               rect ? rect->right : 0, rect ? rect->bottom : 0, show, animate);
        const bool slid = SlideBarBefore(hWnd, show);
        TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, false);
        SlideHint(hWnd, show);
        if (slid) {
            SlideBarAfter(hWnd);
        }
        return;
    }

    g_slideStartMs = NowMs();
    g_slideFrameStart = g_slideStartMs;
    InterlockedExchange(&g_slideThreadId, (LONG)GetCurrentThreadId());

    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);

    InterlockedExchange(&g_slideThreadId, 0);
}

// SlideWindow derives each frame's position from GetTickCount, which advances
// in ~15.6 ms steps -- coarser than the frames now being asked for, so without
// this the extra frames would land on positions already drawn. Reports the same
// elapsed time measured properly, held steady for the duration of each frame so
// a frame draws at one position rather than drifting through it.
using GetTickCount_t = decltype(&GetTickCount);
GetTickCount_t GetTickCount_Original = nullptr;

DWORD WINAPI GetTickCount_Hook() {
    if ((LONG)GetCurrentThreadId() !=
        InterlockedCompareExchange(&g_slideThreadId, 0, 0)) {
        return GetTickCount_Original();
    }

    return (DWORD)(g_slideFrameStart - g_slideStartMs + 0.5);
}

void WINAPI Sleep_Hook(DWORD ms) {
    if ((LONG)GetCurrentThreadId() !=
        InterlockedCompareExchange(&g_slideThreadId, 0, 0)) {
        Sleep_Original(ms);
        return;
    }

    FrameWait(g_slideTimer,
              1000.0 / g_settings.frameRate - (NowMs() - g_slideFrameStart));
    g_slideFrameStart = NowMs();
}

// ------------------------------------------------------------------ z-order

// Windows can lower the taskbar out of the topmost band while it is hidden,
// and TrayUI::Unhide does not raise it again, so on a side edge the taskbar
// slides up underneath any window covering that edge. It keeps WS_EX_TOPMOST
// throughout; what is lost is its position in the z-order, so the style bit
// cannot be used to detect this.

// The first visible window above the taskbar in the z-order, skipping the
// invisible tooltip and IME windows that sit above it even when it is on top.
// Bounded so a long window list cannot stall the watcher.
HWND FirstRealWindowAbove(HWND wnd) {
    HWND h = wnd;
    for (int i = 0; i < 40 && h; i++) {
        h = GetWindow(h, GW_HWNDPREV);
        if (!h) {
            return nullptr;   // nothing above it: top of the z-order
        }
        if (!IsWindowVisible(h)) {
            continue;
        }
        RECT r{};
        if (!GetWindowRect(h, &r) ||
            ((r.right - r.left) <= 2 && (r.bottom - r.top) <= 2)) {
            continue;
        }
        return h;
    }
    return nullptr;
}

// A window that is not topmost sitting above the taskbar means the taskbar has
// lost its place. Seen off the bottom edge after a reveal. What causes it is
// not in taskbar.dll: there TrayUI::_ResetZorder only sends the taskbar to the
// bottom for a full-screen window, and that clears the topmost style, which
// this case keeps.
bool TaskbarDemoted() {
    HWND above = FirstRealWindowAbove(g_trayWnd);
    return above && (GetWindowLongW(above, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0;
}

// Windows lowers the taskbar deliberately while a full-screen window is in
// front. Leaving that case alone keeps the taskbar from being forced over
// full-screen games and video.
bool ForegroundFillsMonitor() {
    HWND fg = GetForegroundWindow();
    RECT r{};
    if (!fg || !GetWindowRect(fg, &r)) {
        return false;
    }
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        return false;
    }
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

// Re-asserts HWND_TOPMOST rather than changing ABS_ALWAYSONTOP: the window
// still claims the topmost style, so this only returns it to the band it
// already belongs to.
void RaiseTaskbarIfDemoted() {
    if (!g_settings.keepTaskbarOnTop || !g_trayWnd) {
        return;
    }
    if (!TaskbarDemoted() || ForegroundFillsMonitor()) {
        return;
    }

    SetWindowPos(g_trayWnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ------------------------------------------------------------ pointer watcher

// Windows' cursor-at-edge detection does not run off-bottom, and none of the
// hooked functions are called while the taskbar sits idle, so there is no
// polling site. The watch thread supplies one by posting a message that runs
// this on the taskbar's own thread.
// Defined with the taskbar records below.
bool PrimaryBarReady();
bool PrimaryBarHeld();
void RevealPrimaryBar();
void HidePrimaryBar(const RECT& docked, UINT edge);

void WatcherTick() {
    // No pointer yet: nothing has run GetDockedRect and no state change has run
    // GetAutoHideFlags. Force a recompute rather than waiting for the user to
    // interact with something. SWP_FRAMECHANGED provokes the layout that
    // WM_SETTINGCHANGE alone did not.
    if (!g_trayUI && g_trayWnd) {
        static int nudgeTicks = 0;
        if ((nudgeTicks++ % 8) == 0) {
            SetWindowPos(g_trayWnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    if (!g_trayUI || !g_offsetsVerified || g_inWatcherCall ||
        !TrayUI_Unhide_Original || !TrayUI_SetAutoHideFlags_Original ||
        !TrayUI_GetDockedRect_Original) {
        return;
    }

    UINT edge = *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);
    UINT stored = *(UINT*)((BYTE*)g_trayUI + g_trayUiFlagsOffset);
    PublishAutoHideState(edge, stored);

    if (EdgeSettling(edge)) {
        return;   // just moved to this edge: let Windows finish first
    }

    if (edge >= ABE_BOTTOM || !EdgeEnabled(edge)) {
        return;
    }

    if (!(stored & ABS_AUTOHIDE)) {
        // Switched off while the mod had the taskbar hidden: bring it back.
        RevealPrimaryBar();
        return;
    }

    if (stored & ABS_ALWAYSONTOP) {
        // Hidden. Reveal once the pointer reaches the trigger strip; Unhide
        // performs the slide itself.
        if (!CursorAtRevealStrip(edge) && !CursorOverTaskbar(g_trayUI)) {
            g_dwellStart = 0;
            g_leaveTicks = 0;
            return;
        }

        // Let the pointer settle first. At an 8 px tolerance every trip to the
        // edge for a title bar or a close button would otherwise summon the
        // taskbar. A pointer already over the taskbar has arrived deliberately
        // and waits for nothing.
        // On an edge with a display past it the pointer crosses the strip on
        // its way there, so the wait is never shorter than HeldDwellMs.
        const int dwellMs = PrimaryBarHeld()
                                ? HeldDwellMs(g_settings.revealDwellMs)
                                : g_settings.revealDwellMs;
        if (dwellMs > 0 && !CursorOverTaskbar(g_trayUI)) {
            const ULONGLONG now = GetTickCount64();
            if (g_dwellStart == 0) {
                g_dwellStart = now;
            }
            if (now - g_dwellStart < (ULONGLONG)dwellMs) {
                return;
            }
        }
        g_dwellStart = 0;

        // Raise the taskbar before the slide starts, while it is still
        // off-screen. Raised afterwards, it visibly slides up behind whatever
        // covers the edge and then jumps in front.
        RaiseTaskbarIfDemoted();

        g_inWatcherCall = true;
        TrayUI_Unhide_Original(g_trayUI, 2, 1);
        g_inWatcherCall = false;
        g_leaveTicks = 0;
        g_lastKeptUp = (LONG64)GetTickCount64();
        return;
    }

    // Shown. Stay up while the pointer is over the taskbar, or while Start,
    // Search or a shell flyout is open.
    RECT docked{};
    TrayUI_GetDockedRect_Original(g_trayUI, &docked, 0);

    POINT pt{};
    if (!GetCursorPos(&pt)) {
        return;
    }

    g_lastDockedRect = docked;   // remembered so unload can restore it

    // Covers anything that revealed the taskbar without passing through either
    // raise above.
    RaiseTaskbarIfDemoted();

    const LONG64 tick = (LONG64)GetTickCount64();

    if (PtInRect(&docked, pt) || PointerOverShellSurface() ||
        ShellSurfaceInForeground() || QuickSettingsOpen()) {
        g_leaveTicks = 0;
        g_lastKeptUp = tick;
        return;
    }

    if (tick < InterlockedCompareExchange64(&g_holdVisibleUntil, 0, 0)) {
        g_leaveTicks = 0;
        g_lastKeptUp = tick;
        return;   // Windows asked for it to be visible
    }

    if (!HideDelayElapsed(tick, g_lastKeptUp, g_settings.hideDelayMs)) {
        g_leaveTicks = 0;
        return;
    }

    if (++g_leaveTicks < LEAVE_TICKS) {
        return;
    }
    g_leaveTicks = 0;

    // Windows' Unhide asks for the shown position itself, but nothing asks for
    // the hidden one off the bottom edge. So set the state, after which
    // GetDockedRect returns the hidden rect, and start the hide here.
    g_dwellStart = 0;

    if (!PrimaryBarReady()) {
        return;   // no record of the taskbar yet; try on a later tick
    }

    g_inWatcherCall = true;
    TrayUI_SetAutoHideFlags_Original(g_trayUI, stored | ABS_ALWAYSONTOP);
    HidePrimaryBar(docked, edge);
    g_inWatcherCall = false;
}

// ------------------------------------------------------- one record per taskbar

// What the mod knows about each taskbar window: the primary and every
// secondary. Made when the window is first seen and dropped when it goes; the
// display facts are refreshed when the layout may have changed. Everything
// here runs on the taskbar's thread.
#define MAX_TASKBARS 8

struct TaskbarRec {
    HWND hwnd;
    bool primary;
    RECT display;      // the display it belongs to
    int displayHz;
    UINT edge;
    bool held;         // another display lies past its docked edge
    // The window stays on its shown position and its content slides inside
    // it. So for a held taskbar, for one whose hidden place another display
    // touches (HiddenNearOther), and for every taskbar on the bottom edge
    // while "Hide completely" is on: that is what lets the mod hide it
    // wholly there and reveal it from its own strip, where Windows would
    // need a sliver of window left on screen.
    bool inPlace;
    POINT shown;       // top-left when fully shown
    SIZE size;
    int hiddenAsked;   // pixels of thickness Windows last asked to be hidden
    int hiddenNow;     // pixels hidden right now

    // Set once the mod has put the window somewhere itself. From then on its
    // position says where the animation is, not what Windows wants.
    bool placed;
    bool ownMove;      // inside the mod's own SetWindowPos...
    int ownX, ownY;    // ...to this position
    int lastAskedOff;  // what Windows last asked for, for the log
    bool clipped;
    RECT region;       // the window region in force, when clipped
    HWND content;      // the child window that draws the taskbar
    int contentDx, contentDy;   // where the mod last put it
    bool contentMoved;

    bool animating;
    int from, to;      // hidden pixels
    double startMs;
    int durationMs;
    int frameUs;
    int steps, late;
    double lastStepMs, minGapMs, maxGapMs;
    double nextStepMs;   // when its next step is due

    // Revealing a held taskbar from the strip on its own display.
    ULONGLONG dwellStart;   // when the pointer came to rest in the strip
    ULONGLONG revealSent;   // when Windows was last asked to reveal it
    int revealStage;        // 0 idle, 1 hit test sent

    // A secondary off the bottom edge, which the mod hides and reveals itself.
    LONG64 lastKeptUp;      // last moment something was keeping it up
    int leaveTicks;

    // When no display last claimed this taskbar's window; 0 while one does.
    ULONGLONG lostSince;

    // The mod is setting this window's region (see BarPosChanging).
    bool ownRegion;
    // Put the placement on again at the next tick: Windows replaced the
    // region.
    bool reapply;
};

TaskbarRec g_recs[MAX_TASKBARS];
int g_recCount = 0;
volatile LONG g_recsStale = 1;
ULONGLONG g_recsRefreshed = 0;

TaskbarRec* RecFor(HWND hwnd) {
    for (int i = 0; i < g_recCount; i++) {
        if (g_recs[i].hwnd == hwnd) {
            return &g_recs[i];
        }
    }
    return nullptr;
}

bool AnyInPlace() {
    for (int i = 0; i < g_recCount; i++) {
        if (g_recs[i].inPlace) {
            return true;
        }
    }
    return false;
}

void DropRec(HWND hwnd) {
    for (int i = 0; i < g_recCount; i++) {
        if (g_recs[i].hwnd == hwnd) {
            DLog(L"taskbar %p: record dropped", hwnd);
            g_recs[i] = g_recs[--g_recCount];
            return;
        }
    }
}

struct DisplayList {
    RECT rect[MAX_TASKBARS];
    int hz[MAX_TASKBARS];
    int count;
};

BOOL CALLBACK CollectDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* list = (DisplayList*)data;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (list->count < MAX_TASKBARS && GetMonitorInfoW(monitor, &mi)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        list->rect[list->count] = mi.rcMonitor;
        list->hz[list->count] =
            EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)
                ? (int)dm.dmDisplayFrequency
                : 0;
        list->count++;
    }
    return TRUE;
}

// The edge the primary is docked to. The mod assumes every taskbar shares it;
// CSecondaryTray has a SetStuckPlace of its own, so Windows may not require it.
UINT DockedEdge() {
    if (g_trayUI && g_offsetsVerified) {
        return *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);
    }
    APPBARDATA abd{sizeof(abd)};
    return SHAppBarMessage(ABM_GETTASKBARPOS, &abd) ? abd.uEdge : ABE_BOTTOM;
}

void ReleaseBar(TaskbarRec* rec, bool shown = false);
void MoveBarContent(TaskbarRec* rec, int dx, int dy);
void StartBarAnimation(TaskbarRec* rec, int hiddenTarget);

void UnhideThroughWindows(HWND secondary);

// Fill in what depends on the display layout. Returns false when no display
// claims the window, which happens for a moment while the layout changes.
bool FillRecFacts(TaskbarRec* rec, const DisplayList& displays, UINT edge) {
    for (int pass = 0; pass < 2; pass++) {
        RECT bar{};
        if (!GetWindowRect(rec->hwnd, &bar)) {
            return false;
        }
        int own = -1;
        for (int i = 0; i < displays.count; i++) {
            if (BarBelongsTo(displays.rect[i], bar, edge)) {
                own = i;
                break;
            }
        }
        if (own < 0) {
            if (rec->placed) {
                // The mod is holding it somewhere no display claims: the
                // layout changed without Windows moving it first. Stop
                // holding it; Windows places it at its next move.
                DLog(L"taskbar %p at (%d,%d,%d,%d) is on no display's "
                       L"edge: letting go",
                       rec->hwnd, bar.left, bar.top, bar.right, bar.bottom);
                rec->animating = false;
                if (rec->clipped) {
                    SetWindowRgn(rec->hwnd, nullptr, TRUE);
                    rec->clipped = false;
                }
                MoveBarContent(rec, 0, 0);
                rec->placed = false;
                rec->held = false;
                rec->inPlace = false;
                rec->display = {0, 0, 0, 0};
            }
            // No display claims it, and Windows moves a taskbar only when it
            // hides or shows it, which it will not do for one it believes is
            // where it should be. Left like that it stays lost. After two
            // seconds, and every five after that, have Windows show it: that
            // puts it on its display's edge, and from there the mod finds it.
            const ULONGLONG tick = GetTickCount64();
            if (!rec->lostSince) {
                rec->lostSince = tick;
            } else if (tick - rec->lostSince >= 2000) {
                rec->lostSince = tick + 3000;
                DLog(L"taskbar %p at (%d,%d,%d,%d) has been on no display's "
                     L"edge for a while: asking Windows to show it",
                     rec->hwnd, bar.left, bar.top, bar.right, bar.bottom);
                if (rec->primary) {
                    // TrayUI answers any request with the rect it believes in.
                    SetWindowPos(rec->hwnd, nullptr, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                     SWP_NOACTIVATE);
                } else {
                    UnhideThroughWindows(rec->hwnd);
                }
            }
            return false;
        }
        rec->lostSince = 0;

        RECT others[MAX_TASKBARS];
        int nOthers = 0;
        for (int i = 0; i < displays.count; i++) {
            if (i != own) {
                others[nOthers++] = displays.rect[i];
            }
        }

        const RECT& m = displays.rect[own];
        const int hz = displays.hz[own];
        const bool held = DisplayBeyond(m, edge, others, nOthers);
        RECT now{};
        GetWindowRect(rec->hwnd, &now);
        const int thickness = (edge == ABE_LEFT || edge == ABE_RIGHT)
                                  ? now.right - now.left
                                  : now.bottom - now.top;
        const bool inPlace =
            held || (edge == ABE_BOTTOM && g_settings.hideFully) ||
            HiddenNearOther(m, edge, thickness, others, nOthers);
        const bool changed = !EqualRect(&rec->display, &m) ||
                             rec->displayHz != hz || rec->edge != edge ||
                             rec->held != held || rec->inPlace != inPlace;

        if (changed && rec->placed) {
            // The layout moved under a taskbar the mod had placed. Hand the
            // window back to where Windows wants it, then read it afresh.
            ReleaseBar(rec);
            continue;
        }

        rec->display = m;
        rec->displayHz = hz;
        rec->edge = edge;
        rec->held = held;
        rec->inPlace = inPlace;
        if (!rec->placed) {
            // How far the window is from sitting inside its display: that
            // much of its thickness is hidden, and moving it back gives the
            // shown position.
            const int dx = bar.left < m.left     ? m.left - bar.left
                           : bar.right > m.right ? m.right - bar.right
                                                 : 0;
            const int dy = bar.top < m.top         ? m.top - bar.top
                           : bar.bottom > m.bottom ? m.bottom - bar.bottom
                                                   : 0;
            rec->shown = {bar.left + dx, bar.top + dy};
            rec->size = {bar.right - bar.left, bar.bottom - bar.top};
            rec->hiddenAsked = rec->hiddenNow =
                dx ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        }

        if (changed) {
            Wh_Log(L"taskbar %p (%s) at (%d,%d,%d,%d): display (%d,%d,%d,%d) "
                   L"at %d Hz, edge %u, %s",
                   rec->hwnd, rec->primary ? L"primary" : L"secondary",
                   bar.left, bar.top, bar.right, bar.bottom, m.left, m.top,
                   m.right, m.bottom, hz, edge,
                   held ? L"held: a display lies past the edge" : L"not held");
        }
        return true;
    }
    return false;
}

// ------------------------------------------------ the mod's own hide and reveal

// Whether the mod moves the taskbars itself. On the left, top and right it
// does whenever it is active on that edge: Windows does not hide a taskbar
// there at all without it. On the bottom edge, which Windows handles, it does
// only when some taskbar is held, so that every taskbar on the desk moves
// alike; with one display that is never true and the bottom edge stays
// Windows' own.
volatile LONG g_barsReleased = 0;   // unloading: hands off every taskbar

bool BarsHandled() {
    if (g_fatalReason || g_barsReleased || g_recCount == 0 ||
        EdgeChangeInProgress()) {
        return false;
    }
    const UINT edge = g_recs[0].edge;
    return edge >= ABE_BOTTOM ? AnyInPlace() : EdgeEnabled(edge);
}

UINT g_barTickMsg = 0;
UINT g_barReleaseMsg = 0;
UINT g_barRevealMsg = 0;
HANDLE g_barWake = nullptr;
HANDLE g_barThread = nullptr;
volatile LONG g_barStop = 0;

// How much of this taskbar the mod hides. "Hide completely" is for every
// taskbar off the bottom edge: there the mod reveals each one itself, from
// the pointer resting in the strip at its display's edge, and has Windows
// unhide it with a message, so nothing needs the pointer to find a sliver of
// window. (It was once the primary only: a secondary was then revealed by a
// hit test, which needs the pointer inside the window, and a wholly hidden
// one could not be brought back.) On the bottom edge Windows reveals a
// taskbar that is not held from its sliver, so the sliver stays. A held
// taskbar hides completely on every edge.
int HiddenTargetFor(const TaskbarRec* rec) {
    const int thickness = (rec->edge == ABE_LEFT || rec->edge == ABE_RIGHT)
                              ? rec->size.cx
                              : rec->size.cy;
    return HiddenTarget(thickness,
                        g_settings.hideFully && rec->edge < ABE_BOTTOM,
                        rec->inPlace);
}

// Off the bottom edge Windows hides a secondary taskbar only after the pointer
// has been to it, and one that was simply showing when the mod became active
// never hides at all (prompting it with a hit test did nothing). So there the
// mod decides for itself when a secondary hides and reveals, as it does for
// the primary, and takes Windows' own slides for it as hints.
// CSecondaryTray::_Unhide(flags, request), read from taskbar.dll 9974. It acts
// only while Windows has the taskbar marked hidden. Flags bit 0: no animation.
// Flags bit 1: do not start the hide timer again. The request is telemetry.
// Windows' own hover path is a hit test that starts a 50 ms timer, which then
// calls this with (0, 0xf) if the real pointer is inside the window.
//
// The mod reaches it with a window message, which needs no symbol: 0x5BE with
// wParam 1 and lParam 0 runs CSecondaryTray::_HandleImmersiveInvoke, which
// calls _Unhide(2, 1): shown, and the hide timer not started (read in 9974
// and 10066). Windows then keeps the taskbar's content visible and leaves the
// hiding to the mod.
#define TRAY_MSG_IMMERSIVE_INVOKE 0x5BE

void UnhideThroughWindows(HWND secondary) {
    SendMessageW(secondary, TRAY_MSG_IMMERSIVE_INVOKE, 1, 0);
}

using CSecondaryTray_Unhide_t = void(WINAPI*)(void* pThis, int flags,
                                              int request);
CSecondaryTray_Unhide_t CSecondaryTray_Unhide_Original;

bool ModDriven(const TaskbarRec* rec) {
    return !rec->primary && rec->edge < ABE_BOTTOM;
}

void StartBarAnimation(TaskbarRec* rec, int hiddenTarget);

void RevealModBar(TaskbarRec* rec) {
    rec->lastKeptUp = (LONG64)GetTickCount64();
    rec->leaveTicks = 0;
    rec->dwellStart = 0;
    if (rec->hiddenAsked != 0 || !rec->placed) {
        rec->hiddenAsked = 0;
        rec->placed = true;
        StartBarAnimation(rec, 0);
    }
}

void HideModBar(TaskbarRec* rec) {
    const int target = HiddenTargetFor(rec);
    rec->leaveTicks = 0;
    rec->dwellStart = 0;
    if (rec->hiddenAsked != target || !rec->placed) {
        rec->hiddenAsked = target;
        rec->placed = true;
        StartBarAnimation(rec, target);
    }
}

// No cloaking. The mod once cloaked a held taskbar at rest, when hiding it
// depended on a window region that was not always drawn in time. Windows has
// one switch for cloaking a taskbar (ImmersiveTray::SetCloakWithAnimation) and
// uses it itself for a full-screen game, assigned access and the moment after
// an edge change, without keeping a record of who set it, so the two undid
// each other. With the content slid out of a window that stays put there is
// nothing left to cloak.

// The child window that draws a taskbar: its XAML content. Moving it inside the
// taskbar's window is how a held taskbar slides.
HWND BarContent(TaskbarRec* rec) {
    if (!rec->content || !IsWindow(rec->content)) {
        rec->content = FindWindowExW(
            rec->hwnd, nullptr,
            L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
        static bool warned = false;
        if (!rec->content && !warned) {
            // A build that names this window otherwise. A held taskbar then
            // still hides, behind its region, but is wiped away where it
            // stands, not slid.
            warned = true;
            Wh_Log(L"taskbar %p has no content window of the class the mod "
                   L"knows: a held taskbar will wipe, not slide",
                   rec->hwnd);
        }
    }
    return rec->content;
}

void MoveBarContent(TaskbarRec* rec, int dx, int dy) {
    if (!rec->contentMoved && dx == 0 && dy == 0) {
        return;   // never touched, and not asked to be
    }
    if (rec->contentMoved && rec->contentDx == dx && rec->contentDy == dy) {
        return;
    }
    if (HWND content = BarContent(rec)) {
        SetWindowPos(content, nullptr, dx, dy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        rec->contentDx = dx;
        rec->contentDy = dy;
        rec->contentMoved = dx != 0 || dy != 0;
    }
}

// Move a taskbar's window, marking the move as the mod's own so the handler
// puts this position back after the taskbar has replaced it. The mark names
// the position: while this SetWindowPos runs, the taskbar reacts to the move
// by asking for its own idea of the position, nested inside this call, and
// that request must not be mistaken for the mod's.
void MoveBar(TaskbarRec* rec, int x, int y) {
    rec->ownX = x;
    rec->ownY = y;
    rec->ownMove = true;
    SetWindowPos(rec->hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    rec->ownMove = false;
}

// Put the window and its region where `hiddenPx` hidden pixels call for.
void ApplyPlacement(TaskbarRec* rec, int hiddenPx) {
    const BarPlacement p =
        PlaceBar(rec->shown, rec->size, rec->edge, rec->inPlace, hiddenPx);

    // Before the move, not after: while it runs the taskbar asks for its own
    // position, and that request is answered from hiddenNow. Left at the
    // previous step's value, the last step of a hide ended one step short.
    rec->hiddenNow = hiddenPx;
    rec->placed = true;

    RECT cur{};
    GetWindowRect(rec->hwnd, &cur);
    if (cur.left != p.x || cur.top != p.y) {
        MoveBar(rec, p.x, p.y);
    }
    MoveBarContent(rec, p.contentDx, p.contentDy);

    rec->ownRegion = true;
    if (!p.clipped) {
        if (rec->clipped) {
            SetWindowRgn(rec->hwnd, nullptr, TRUE);
            rec->clipped = false;
        }
    } else if (!rec->clipped || !EqualRect(&rec->region, &p.region)) {
        // The system owns the region once SetWindowRgn has taken it.
        HRGN rgn = CreateRectRgnIndirect(&p.region);
        if (rgn && SetWindowRgn(rec->hwnd, rgn, TRUE)) {
            rec->region = p.region;
            rec->clipped = true;
        } else if (rgn) {
            DeleteObject(rgn);
        }
    }
    rec->ownRegion = false;
}

// Give a taskbar back to Windows: stop its animation, remove its region, and
// put the window where Windows last asked for it, or, with `shown`, where it
// is fully shown. Used when the layout changes under it, when the mod stops
// moving the taskbars, and when it unloads.
void ReleaseBar(TaskbarRec* rec, bool shown) {
    if (shown) {
        rec->hiddenAsked = 0;
    }
    rec->animating = false;
    rec->dwellStart = 0;
    rec->revealSent = 0;
    rec->revealStage = 0;
    MoveBarContent(rec, 0, 0);
    if (!rec->placed) {
        return;
    }

    // Not held: the position Windows itself uses for this many hidden pixels.
    const BarPlacement p = PlaceBar(rec->shown, rec->size, rec->edge, false,
                                    rec->hiddenAsked);
    if (rec->clipped) {
        SetWindowRgn(rec->hwnd, nullptr, TRUE);
        rec->clipped = false;
    }
    RECT cur{};
    GetWindowRect(rec->hwnd, &cur);
    if (cur.left != p.x || cur.top != p.y) {
        MoveBar(rec, p.x, p.y);
    }
    rec->hiddenNow = rec->hiddenAsked;
    rec->placed = false;
    DLog(L"taskbar %p released at (%d,%d), %d px hidden", rec->hwnd, p.x,
           p.y, rec->hiddenAsked);
}

// Off the bottom edge Windows hides nothing once the mod lets go, and has no
// reason to bring a hidden taskbar back, so there they are left shown.
void ReleaseAllBars() {
    for (int i = 0; i < g_recCount; i++) {
        ReleaseBar(&g_recs[i], g_recs[i].edge < ABE_BOTTOM);
    }
}

bool PrimaryBarReady() {
    TaskbarRec* rec = g_trayWnd ? RecFor(g_trayWnd) : nullptr;
    return rec && !IsRectEmpty(&rec->display) && BarsHandled();
}

void RevealPrimaryBar() {
    TaskbarRec* rec = g_trayWnd ? RecFor(g_trayWnd) : nullptr;
    if (rec && rec->placed && rec->hiddenAsked > 0 && BarsHandled()) {
        rec->hiddenAsked = 0;
        StartBarAnimation(rec, 0);
    }
}

bool PrimaryBarHeld() {
    TaskbarRec* rec = g_trayWnd ? RecFor(g_trayWnd) : nullptr;
    return rec && rec->held;
}

// Start the primary's hide off the bottom edge, from where it is docked.
void HidePrimaryBar(const RECT& docked, UINT edge) {
    TaskbarRec* rec = RecFor(g_trayWnd);
    if (!rec) {
        return;
    }
    rec->shown = {docked.left, docked.top};
    rec->size = {docked.right - docked.left, docked.bottom - docked.top};
    rec->edge = edge;
    rec->hiddenAsked = HiddenTargetFor(rec);
    rec->placed = true;
    StartBarAnimation(rec, rec->hiddenAsked);
}

void StartBarAnimation(TaskbarRec* rec, int hiddenTarget) {
    rec->from = rec->hiddenNow;
    rec->to = hiddenTarget;
    rec->startMs = NowMs();
    rec->durationMs = hiddenTarget > rec->hiddenNow
                          ? (g_settings.hideDurationMs > 0
                                 ? g_settings.hideDurationMs
                                 : 400)
                          : (g_settings.showDurationMs > 0
                                 ? g_settings.showDurationMs
                                 : 200);
    rec->frameUs =
        1000000 / StepsPerSecond(rec->displayHz, g_settings.frameRate);
    rec->steps = rec->late = 0;
    rec->lastStepMs = rec->startMs;
    rec->nextStepMs = rec->startMs;
    rec->minGapMs = 1e9;
    rec->maxGapMs = 0;
    rec->animating = true;
    if (g_barThread) {
        SetEvent(g_barWake);
    } else {
        // Nothing to pace it: go to the end in one step. Posted, because this
        // is reached from inside WM_WINDOWPOSCHANGING.
        rec->durationMs = 0;
        PostMessageW(g_trayWnd, g_barTickMsg, 0, 0);
    }
}

// One step of one taskbar's animation. Eases out both ways: quick to start,
// settling into the end position.
void TickBar(TaskbarRec* rec) {
    const double now = NowMs();
    double t = rec->durationMs > 0 ? (now - rec->startMs) / rec->durationMs
                                   : 1.0;
    if (t > 1.0) {
        t = 1.0;
    }
    const double e = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);

    const int from = rec->from, to = rec->to;
    const double pos = from + (to - from) * e;
    ApplyPlacement(rec, t >= 1.0 ? rec->to : (int)(pos + 0.5));

    const double gap = now - rec->lastStepMs;
    if (rec->steps > 0) {
        if (gap < rec->minGapMs) rec->minGapMs = gap;
        if (gap > rec->maxGapMs) rec->maxGapMs = gap;
        if (gap > rec->frameUs * 1.5 / 1000.0) rec->late++;
    }
    rec->lastStepMs = now;
    rec->steps++;

    if (t >= 1.0) {
        rec->animating = false;
        DLog(L"taskbar %p %s %d->%d px in %d ms at %d fps: %d steps, gap "
               L"%d..%d us, %d late, dpi %u",
               rec->hwnd, rec->to > rec->from ? L"hide" : L"reveal", rec->from,
               rec->to, (int)(now - rec->startMs), 1000000 / rec->frameUs,
               rec->steps, rec->steps > 1 ? (int)(rec->minGapMs * 1000) : 0,
               (int)(rec->maxGapMs * 1000), rec->late,
               GetDpiForWindow(rec->hwnd));
    }
}

// Step every animating taskbar whose next frame is due. Each keeps the pace of
// its own display: two taskbars animating together, on a 60 Hz and a 240 Hz
// display, must not both step at the faster rate. Returns how long until the
// next step is due, in microseconds, or 0 when nothing is animating.
LRESULT TickBars() {
    const double now = NowMs();
    double soonest = 0;
    bool any = false;
    for (int i = 0; i < g_recCount; i++) {
        TaskbarRec* rec = &g_recs[i];
        if (!rec->animating) {
            if (rec->reapply) {
                rec->reapply = false;
                if (rec->placed && !IsRectEmpty(&rec->display)) {
                    rec->clipped = false;
                    ApplyPlacement(rec, rec->hiddenNow);
                }
            }
            continue;
        }
        if (now >= rec->nextStepMs - 0.5) {
            TickBar(rec);
            // Aim at frame boundaries from the start, so a late step does
            // not push the later ones back; skip any already missed.
            const double frame = rec->frameUs / 1000.0;
            rec->nextStepMs += frame;
            if (rec->nextStepMs <= now) {
                rec->nextStepMs = now + frame;
            }
        }
        if (rec->animating && (!any || rec->nextStepMs < soonest)) {
            soonest = rec->nextStepMs;
            any = true;
        }
    }
    if (!any) {
        return 0;
    }
    const double wait = soonest - NowMs();
    return wait < 0.2 ? 200 : (LRESULT)(wait * 1000.0);
}

// On the bottom edge Windows hides and shows the taskbars, through
// TrayUI::SlideWindow, and right after a hide it does two things: it sets a
// region of its own on the window and it makes the taskbar's content window
// invisible. An animation the mod started from Windows' request therefore
// ran on a taskbar that already showed nothing (seen in 1 ms traces: content
// invisible for the whole of the hide, and the taskbar cut to 2 px for a
// moment before it). Windows' own slide is a loop that blocks this thread
// until the window has arrived; the mod does the same, here, before Windows
// goes on. Other taskbars that are animating keep stepping from this loop.
void SlideBarNow(TaskbarRec* rec, int hiddenTarget) {
    rec->hiddenAsked = hiddenTarget;
    if (rec->hiddenNow == hiddenTarget && !rec->animating) {
        return;
    }
    StartBarAnimation(rec, hiddenTarget);
    HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
        TIMER_ALL_ACCESS);
    const double giveUp = NowMs() + rec->durationMs + 500;
    while (rec->animating && NowMs() < giveUp) {
        const LRESULT waitUs = TickBars();
        if (!rec->animating || !waitUs) {
            break;
        }
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)waitUs * 10;
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer, 50);
        } else {
            Sleep(1);
        }
    }
    if (timer) {
        CloseHandle(timer);
    }
    if (rec->animating) {
        ApplyPlacement(rec, hiddenTarget);
        rec->animating = false;
    }
    // The pacing thread gave up on this thread while it was busy here.
    if (g_barThread) {
        SetEvent(g_barWake);
    }
}

// Before Windows' unanimated slide of a taskbar on the bottom edge: do the
// slide. Returns whether it did.
bool SlideBarBefore(HWND hwnd, bool show) {
    TaskbarRec* rec = RecFor(hwnd);
    if (!rec || rec->edge != ABE_BOTTOM || !rec->placed ||
        IsRectEmpty(&rec->display)) {
        return false;
    }
    // Windows may have put its own region on since the mod's last step.
    rec->clipped = false;
    ApplyPlacement(rec, rec->hiddenNow);
    SlideBarNow(rec, show ? 0 : HiddenTargetFor(rec));
    return true;
}

// After it: Windows may have set its region again.
void SlideBarAfter(HWND hwnd) {
    TaskbarRec* rec = RecFor(hwnd);
    if (rec && rec->placed && !IsRectEmpty(&rec->display) && !rec->animating) {
        rec->clipped = false;
        ApplyPlacement(rec, rec->hiddenNow);
    }
}

// WM_WINDOWPOSCHANGING on a taskbar the mod animates, after the taskbar's own
// handler has had it. reqX and reqY are what was asked before that handler.
void BarPosChanging(TaskbarRec* rec, WINDOWPOS* wp, int reqX, int reqY,
                    UINT reqFlags) {
    // TrayUI::_HandleWindowPosChanging has just run. It turns every request,
    // even one that only changes the z-order, into a move to the rect it
    // believes in (GetDockedRect), and when the window already sits on that
    // rect it sets SWP_NOMOVE | SWP_NOSIZE instead. So the flags as they are
    // now do not say what was asked: testing SWP_NOMOVE here used to drop the
    // mod's own moves whenever the window was on Windows' rect, which left a
    // hidden taskbar 2 px short of hidden, or parked where Windows put it.
    if (rec->ownMove && !(reqFlags & SWP_NOMOVE) && reqX == rec->ownX &&
        reqY == rec->ownY) {
        // The mod's own move: put back what it asked for.
        wp->x = reqX;
        wp->y = reqY;
        wp->flags &= ~SWP_NOMOVE;
        return;
    }

    // Windows asking. Where it wants the window is the rect it wrote, or, if
    // it left SWP_NOMOVE, the rect the window is on. From that, work out how
    // much of the taskbar it wants hidden.
    RECT cur{};
    GetWindowRect(rec->hwnd, &cur);
    const bool noMove = (wp->flags & SWP_NOMOVE) != 0;
    if (noMove && (reqFlags & SWP_NOMOVE) && rec->inPlace) {
        // Nobody asked for a position: the request arrived without a move
        // and the taskbar's handler added none (a frame change, a region, a
        // z-order change). A held taskbar's window stays on its shown rect
        // while hidden, so where the window is says nothing about what
        // Windows wants, and reading it as "shown" revealed a taskbar the
        // moment Windows hid it. (Seen on the bottom edge: the upper display's
        // taskbar asked to hide, then this 2 ms later, and never hid.)
        //
        // It is what a change of region looks like from here, and Windows
        // sets a region of its own on a hidden taskbar: put the mod's back.
        if (!rec->ownRegion && !rec->ownMove) {
            rec->reapply = true;
            PostMessageW(g_trayWnd, g_barTickMsg, 0, 0);
        }
        return;
    }
    const bool noSize = (wp->flags & SWP_NOSIZE) != 0;
    const int w = noSize ? cur.right - cur.left : wp->cx;
    const int h = noSize ? cur.bottom - cur.top : wp->cy;
    const int askedX = noMove ? cur.left : wp->x;
    const int askedY = noMove ? cur.top : wp->y;
    const RECT asked{askedX, askedY, askedX + w, askedY + h};
    const RECT& m = rec->display;

    if (!BarBelongsTo(m, asked, rec->edge)) {
        // Not a position on the display this record knows: the display
        // moved, changed size or scaling, or the taskbar changed edge. What
        // the record holds is out of date, and replacing this position from
        // it would pin the taskbar where its display used to be. Let Windows
        // have its way, forget the display, and read the facts again.
        DLog(L"taskbar %p asked to (%d,%d,%d,%d), off the display it was "
               L"on: letting go until the layout is read again",
               rec->hwnd, asked.left, asked.top, asked.right, asked.bottom);
        rec->animating = false;
        if (rec->clipped) {
            SetWindowRgn(rec->hwnd, nullptr, TRUE);
            rec->clipped = false;
        }
        MoveBarContent(rec, 0, 0);
        rec->placed = false;
        rec->held = false;
        rec->inPlace = false;
        rec->display = {0, 0, 0, 0};
        rec->dwellStart = rec->revealSent = 0;
        rec->revealStage = 0;
        InterlockedExchange(&g_recsStale, 1);
        return;
    }
    const int dx = asked.left < m.left     ? m.left - asked.left
                   : asked.right > m.right ? m.right - asked.right
                                           : 0;
    const int dy = asked.top < m.top         ? m.top - asked.top
                   : asked.bottom > m.bottom ? m.bottom - asked.bottom
                                             : 0;
    const int off = dx ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (off != rec->lastAskedOff) {
        DLog(L"taskbar %p: Windows asks (%d,%d,%d,%d), %d px hidden (was "
               L"%d), flags %#x%s",
               rec->hwnd, asked.left, asked.top, asked.right, asked.bottom,
               off, rec->lastAskedOff, wp->flags,
               rec->ownMove ? L", inside the mod's own move" : L"");
        rec->lastAskedOff = off;
    }

    rec->shown = {asked.left + dx, asked.top + dy};
    rec->size = {w, h};

    // Windows says only "shown" or "hidden"; how much to hide is the mod's
    // own answer. For a taskbar the mod drives itself, so is whether.
    const int target = ModDriven(rec) ? rec->hiddenAsked
                                      : off == 0 ? 0 : HiddenTargetFor(rec);
    if (target != rec->hiddenAsked || !rec->placed) {
        rec->hiddenAsked = target;
        rec->dwellStart = 0;
        rec->revealSent = 0;
        rec->revealStage = 0;
        StartBarAnimation(rec, target);
    }

    // Windows gets the position the animation is at, not the one it asked for.
    const BarPlacement p =
        PlaceBar(rec->shown, rec->size, rec->edge, rec->inPlace, rec->hiddenNow);
    wp->x = p.x;
    wp->y = p.y;
    if (p.x != cur.left || p.y != cur.top) {
        wp->flags &= ~SWP_NOMOVE;
    }
    rec->placed = true;
}

// Ask Windows to reveal a held taskbar. Windows reveals a hidden taskbar when
// the pointer reaches what is left of its window, and a held taskbar leaves
// only a sliver, on an edge the pointer carries straight on past. So the mod
// watches a deeper strip and then gives the window the hit test a pointer on
// the sliver would have caused. Measured on 26100.9974: that alone reveals it,
// about 60 ms later.
void RevealHeld(TaskbarRec* rec, POINT pointer) {
    RECT cur{};
    GetWindowRect(rec->hwnd, &cur);
    // The pixel of its own display nearest the docked edge, level with the
    // pointer. A sliver is there if the taskbar has one, and a held taskbar's
    // window still covers it even when its region shows nothing.
    const RECT& m = rec->display;
    POINT in = pointer;
    switch (rec->edge) {
        case ABE_LEFT:   in.x = m.left;       break;
        case ABE_TOP:    in.y = m.top;        break;
        case ABE_RIGHT:  in.x = m.right - 1;  break;
        default:         in.y = m.bottom - 1; break;
    }

    rec->revealSent = GetTickCount64();
    rec->revealStage = 1;
    SendMessageW(rec->hwnd, WM_NCHITTEST, 0, MAKELPARAM(in.x, in.y));
}

// Windows slid a taskbar the mod drives itself: take it as a hint. A reveal
// (a flashing button, the pointer on its sliver) is always followed; a hide is
// too, which keeps what Windows believes in step with what is on screen.
void SlideHint(HWND hwnd, bool show) {
    TaskbarRec* rec = RecFor(hwnd);
    // Not while the mod does not know which display the taskbar is on: the
    // displays were just rearranged, and what the record holds is the old
    // place. Animating from it carried the window back there after Windows
    // had moved it to its new display, and there it stayed, on no display's
    // edge, where the mod could not find its display again (seen after
    // moving a display from beside the primary to above it).
    if (!rec || !ModDriven(rec) || IsRectEmpty(&rec->display)) {
        return;
    }
    if (show) {
        RevealModBar(rec);
    } else {
        HideModBar(rec);
    }
}

// From the watch tick, off the bottom edge: hide each secondary taskbar when
// nothing keeps it up, and reveal it when the pointer rests in its strip.
// The per-taskbar form of what WatcherTick does for the primary.
void WatchSecondaries() {
    if (!BarsHandled() || !g_trayUI || !g_offsetsVerified) {
        return;
    }
    const bool autoHide =
        (*(UINT*)((BYTE*)g_trayUI + g_trayUiFlagsOffset) & ABS_AUTOHIDE) != 0;
    POINT pt{};
    if (!GetCursorPos(&pt)) {
        return;
    }
    const LONG64 now = (LONG64)GetTickCount64();

    for (int i = 0; i < g_recCount; i++) {
        TaskbarRec* rec = &g_recs[i];
        if (!ModDriven(rec) || IsRectEmpty(&rec->display)) {
            continue;
        }
        const RECT bar{rec->shown.x, rec->shown.y, rec->shown.x + rec->size.cx,
                       rec->shown.y + rec->size.cy};

        if (!autoHide) {
            if (rec->hiddenAsked > 0) {
                RevealModBar(rec);
            }
            continue;
        }

        if (rec->hiddenAsked > 0) {
            const RECT strip =
                RevealStrip(rec->display, bar, rec->edge,
                            rec->held ? HeldStripPx(g_settings.revealTolerance)
                                      : g_settings.revealTolerance,
                            GetDpiForWindow(rec->hwnd));
            if (!PtInRect(&strip, pt)) {
                rec->dwellStart = 0;
                continue;
            }
            if (!rec->dwellStart) {
                rec->dwellStart = (ULONGLONG)now;
            }
            const int dwellMs = rec->held
                                    ? HeldDwellMs(g_settings.revealDwellMs)
                                    : g_settings.revealDwellMs;
            if (now - (LONG64)rec->dwellStart < dwellMs) {
                continue;
            }
            // If Windows believes this taskbar hidden too, have it unhide it:
            // it keeps a hidden taskbar's content invisible. Windows does not
            // start its own hide timer again, so from then on only the mod
            // hides this taskbar and the two stay in step. It does nothing
            // when Windows already has it shown.
            UnhideThroughWindows(rec->hwnd);
            RevealModBar(rec);
            continue;
        }

        if (PtInRect(&bar, pt) || PointerOverShellSurface() ||
            ShellSurfaceInForeground() || QuickSettingsOpen() ||
            now < InterlockedCompareExchange64(&g_holdVisibleUntil, 0, 0)) {
            rec->leaveTicks = 0;
            rec->lastKeptUp = now;
            continue;
        }
        if (!HideDelayElapsed(now, rec->lastKeptUp, g_settings.hideDelayMs)) {
            rec->leaveTicks = 0;
            continue;
        }
        if (++rec->leaveTicks < LEAVE_TICKS) {
            continue;
        }
        HideModBar(rec);
    }
}

// Reveal every secondary taskbar the mod drives: a notification asked one of
// them to show, and which one is not known here.
void RevealModSecondaries() {
    if (!BarsHandled()) {
        return;
    }
    for (int i = 0; i < g_recCount; i++) {
        if (ModDriven(&g_recs[i]) && !IsRectEmpty(&g_recs[i].display)) {
            RevealModBar(&g_recs[i]);
        }
    }
}

void UnhideThroughWindows(HWND secondary);

// A flashing button has just brought the primary taskbar up. Off the bottom
// edge the mod shows and hides the secondary taskbars itself, and it hears of
// a flash on one of them only through CSecondaryTray::_Unhide. That function
// is not hooked on a build with neither symbols nor a built-in address for
// it: there, a flash left them hidden. So the primary's flash brings them up
// as well, once, and the mod hides them again as it hides any secondary
// nothing keeps up.
void RevealSecondariesForFlash() {
    if (CSecondaryTray_Unhide_Original || !BarsHandled()) {
        return;
    }
    for (int i = 0; i < g_recCount; i++) {
        TaskbarRec* rec = &g_recs[i];
        if (ModDriven(rec) && !IsRectEmpty(&rec->display)) {
            UnhideThroughWindows(rec->hwnd);
            RevealModBar(rec);
        }
    }
}

// The hide delay on the bottom edge. Windows does not say which taskbar it is
// about to hide, but it is never the one under the pointer, so the delay runs
// from the last moment something kept up a taskbar the pointer is not on now:
// the pointer over it, or Start, a flyout or Quick Settings open.
LONG64 g_bottomSurfaceUp = 0;

void WatchBottomKeepUps() {
    if (g_recCount == 0 || g_recs[0].edge != ABE_BOTTOM ||
        g_settings.hideDelayMs <= 0) {
        return;
    }
    POINT pt{};
    if (!GetCursorPos(&pt)) {
        return;
    }
    const LONG64 now = (LONG64)GetTickCount64();
    if (PointerOverShellSurface() || ShellSurfaceInForeground() ||
        QuickSettingsOpen()) {
        g_bottomSurfaceUp = now;
    }
    for (int i = 0; i < g_recCount; i++) {
        TaskbarRec* rec = &g_recs[i];
        const RECT bar{rec->shown.x, rec->shown.y, rec->shown.x + rec->size.cx,
                       rec->shown.y + rec->size.cy};
        if (!IsRectEmpty(&rec->display) && rec->hiddenAsked == 0 &&
            PtInRect(&bar, pt)) {
            rec->lastKeptUp = now;
        }
    }
}

bool BottomHideDelayElapsed() {
    if (g_recCount == 0 || g_recs[0].edge != ABE_BOTTOM ||
        g_settings.hideDelayMs <= 0) {
        return true;
    }
    POINT pt{};
    GetCursorPos(&pt);
    LONG64 last = g_bottomSurfaceUp;
    for (int i = 0; i < g_recCount; i++) {
        const TaskbarRec* rec = &g_recs[i];
        const RECT bar{rec->shown.x, rec->shown.y, rec->shown.x + rec->size.cx,
                       rec->shown.y + rec->size.cy};
        if (!PtInRect(&bar, pt) && rec->lastKeptUp > last) {
            last = rec->lastKeptUp;
        }
    }
    return HideDelayElapsed((LONG64)GetTickCount64(), last,
                            g_settings.hideDelayMs);
}

// From the watch tick: reveal any held, hidden taskbar whose strip the
// pointer has rested in.
void WatchHeldReveals() {
    if (!BarsHandled()) {
        return;
    }
    POINT pt{};
    GetCursorPos(&pt);
    const ULONGLONG now = GetTickCount64();

    for (int i = 0; i < g_recCount; i++) {
        TaskbarRec* rec = &g_recs[i];
        if (!rec->placed || rec->hiddenAsked == 0 || !rec->clipped) {
            continue;
        }
        // The bottom edge only, and only what stays in place: Windows reveals
        // the rest there. Off the bottom edge the primary is WatcherTick's
        // and the secondaries are WatchSecondaries'.
        if (rec->edge < ABE_BOTTOM || !rec->inPlace) {
            continue;
        }
        const RECT bar{rec->shown.x, rec->shown.y, rec->shown.x + rec->size.cx,
                       rec->shown.y + rec->size.cy};
        const RECT strip =
            RevealStrip(rec->display, bar, rec->edge,
                        rec->held ? HeldStripPx(g_settings.revealTolerance)
                                  : g_settings.revealTolerance,
                        GetDpiForWindow(rec->hwnd));
        if (!PtInRect(&strip, pt)) {
            rec->dwellStart = 0;
            rec->revealSent = 0;
            rec->revealStage = 0;
            continue;
        }
        if (!rec->dwellStart) {
            rec->dwellStart = now;
        }
        const int dwellMs = rec->held ? HeldDwellMs(g_settings.revealDwellMs)
                                      : g_settings.revealDwellMs;
        if (now - rec->dwellStart < (ULONGLONG)dwellMs) {
            continue;
        }
        if (rec->revealStage == 0) {
            RevealHeld(rec, pt);
        } else if (now - rec->revealSent >= 1000) {
            DLog(L"taskbar %p still hidden a second after the reveal hit "
                   L"test; trying again",
                   rec->hwnd);
            rec->revealStage = 0;
        }
    }
}

// ------------------------------------------ notifications, secondary taskbars

// A flashing button asks every taskbar to show, and a secondary taskbar is
// shown through CSecondaryTray::_Unhide, not TrayUI::Unhide. Its public
// Unhide(UnhideRequest) is a thunk that adjusts `this` and jumps here with
// flags 0, so this one function sees every request once. The same burst rule
// applies, with a burst per taskbar.
//
// Hooked from symbols, or at the address kKnownBuilds has for the build. On a
// build with neither it is left alone, and secondary taskbars keep Windows'
// behaviour.

struct SecondaryBurst {
    void* tray;
    BurstState burst;
} g_secondaryBursts[MAX_TASKBARS];

BurstState* SecondaryBurstFor(void* tray) {
    SecondaryBurst* spare = &g_secondaryBursts[0];
    for (SecondaryBurst& b : g_secondaryBursts) {
        if (b.tray == tray) {
            return &b.burst;
        }
        if (!b.tray) {
            spare = &b;
            break;
        }
    }
    // New, or the table is full of taskbars long gone: start afresh.
    spare->tray = tray;
    spare->burst = {0, 0, -1};
    return &spare->burst;
}

bool AnyBurstOverridesVeto(LONG64 now) {
    const int gap = g_settings.notificationBurstGapMs;
    if (BurstOverridesVeto(&g_burst, now, gap)) {
        return true;
    }
    for (const SecondaryBurst& b : g_secondaryBursts) {
        if (b.tray && BurstOverridesVeto(&b.burst, now, gap)) {
            return true;
        }
    }
    return false;
}

// Whether the pointer is at the docked edge of any secondary taskbar's
// display: the user reaching for one, which a repeat must not be mistaken for.
bool PointerAtSecondaryEdge() {
    POINT pt{};
    GetCursorPos(&pt);
    for (int i = 0; i < g_recCount; i++) {
        const TaskbarRec& rec = g_recs[i];
        if (rec.primary || IsRectEmpty(&rec.display)) {
            continue;
        }
        const RECT bar{rec.shown.x, rec.shown.y, rec.shown.x + rec.size.cx,
                       rec.shown.y + rec.size.cy};
        const RECT strip = RevealStrip(rec.display, bar, rec.edge,
                                       HeldStripPx(g_settings.revealTolerance),
                                       GetDpiForWindow(rec.hwnd));
        if (PtInRect(&strip, pt)) {
            return true;
        }
    }
    return false;
}

void WINAPI CSecondaryTray_Unhide_Hook(void* pThis, int flags, int request) {
    if (!g_fatalReason && g_settings.notificationHoldMs > 0) {
        BurstState* burst = SecondaryBurstFor(pThis);
        const LONG64 now = (LONG64)GetTickCount64();
        switch (BurstDecide(burst, now, request, g_settings.notificationHoldMs,
                            g_settings.notificationBurstGapMs,
                            PointerAtSecondaryEdge(),
                            ShellSurfaceInForeground())) {
            case BURST_BEGIN:
                // One hold for every taskbar: PermitAutoHide is asked by task
                // lists, and which taskbar a task list belongs to is not
                // known here. The later of the holds wins.
                if (burst->holdUntil >
                    InterlockedCompareExchange64(&g_holdVisibleUntil, 0, 0)) {
                    InterlockedExchange64(&g_holdVisibleUntil,
                                          burst->holdUntil);
                }
                break;
            case BURST_DROP:
                return;
            case BURST_PASS:
                break;
        }
    }
    // Off the bottom edge the mod shows these taskbars itself, and Windows,
    // which may believe this one is showing already, would do nothing.
    RevealModSecondaries();
    CSecondaryTray_Unhide_Original(pThis, flags, request);
}

// Paces every animation from a high-resolution timer: it asks the taskbar's
// thread, which owns the records, to step whatever is due, and sleeps for as
// long as that thread says until the next step.
DWORD WINAPI BarThreadProc(LPVOID) {
    HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
        TIMER_ALL_ACCESS);
    if (!timer) {
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }

    while (WaitForSingleObject(g_barWake, INFINITE) == WAIT_OBJECT_0 &&
           !InterlockedCompareExchange(&g_barStop, 0, 0)) {
        for (;;) {
            DWORD_PTR waitUs = 0;
            HWND tray = g_trayWnd;
            if (!tray ||
                !SendMessageTimeoutW(tray, g_barTickMsg, 0, 0,
                                     SMTO_ABORTIFHUNG, 200, &waitUs) ||
                !waitUs || InterlockedCompareExchange(&g_barStop, 0, 0)) {
                break;
            }
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)waitUs * 10;
            if (timer &&
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, 100);
            } else {
                Sleep((DWORD)(waitUs / 1000));
            }
        }
    }

    if (timer) {
        CloseHandle(timer);
    }
    return 0;
}

LRESULT CALLBACK SecondaryProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam, DWORD_PTR dwRefData) {
    if (uMsg == WM_NCDESTROY) {
        DropRec(hWnd);
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

#ifdef TASKBAR_AUTOHIDE_DEBUG
    // For some seconds after an edge change, record what is asked of a
    // secondary taskbar's window, before and after Windows has had its say.
    if (uMsg == WM_WINDOWPOSCHANGING && lParam &&
        (LONG64)GetTickCount64() <
            InterlockedCompareExchange64(&g_edgeSettleUntil, 0, 0) + 6000) {
        const WINDOWPOS before = *(const WINDOWPOS*)lParam;
        TaskbarRec* rec = RecFor(hWnd);
        const bool own = rec && rec->ownMove;
        RECT cur{};
        GetWindowRect(hWnd, &cur);
        LRESULT r;
        if (BarsHandled() && rec && !IsRectEmpty(&rec->display)) {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            BarPosChanging(rec, wp, before.x, before.y, before.flags);
        } else {
            r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }
        const WINDOWPOS* after = (const WINDOWPOS*)lParam;
        DLog(L"edge change: secondary %p at (%d,%d,%d,%d) asked (%d,%d) "
               L"%dx%d flags %#x%s -> (%d,%d) %dx%d flags %#x",
               hWnd, cur.left, cur.top, cur.right, cur.bottom, before.x,
               before.y, before.cx, before.cy, before.flags,
               own ? L" by the mod" : L"", after->x, after->y, after->cx,
               after->cy, after->flags);
        return r;
    }
#endif

    if (uMsg == WM_WINDOWPOSCHANGING && lParam && BarsHandled()) {
        TaskbarRec* rec = RecFor(hWnd);
        if (rec && !IsRectEmpty(&rec->display)) {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            const int reqX = wp->x, reqY = wp->y;
            const UINT reqFlags = wp->flags;
            LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            BarPosChanging(rec, wp, reqX, reqY, reqFlags);
            return r;
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Bring the records in line with the taskbar windows that exist, and their
// display facts in line with the layout. Cheap enough for once a second, and
// run at once when Windows says the displays or settings changed.
void RefreshRecords() {
    HWND seen[MAX_TASKBARS];
    int nSeen = 0;
    if (g_trayWnd && IsWindow(g_trayWnd)) {
        seen[nSeen++] = g_trayWnd;
    }
    const DWORD pid = GetCurrentProcessId();
    HWND w = nullptr;
    while (nSeen < MAX_TASKBARS &&
           (w = FindWindowExW(nullptr, w, L"Shell_SecondaryTrayWnd", nullptr))) {
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        if (owner == pid) {
            seen[nSeen++] = w;
        }
    }

    HWND have[MAX_TASKBARS], toAdd[MAX_TASKBARS], toDrop[MAX_TASKBARS];
    for (int i = 0; i < g_recCount; i++) {
        have[i] = g_recs[i].hwnd;
    }
    int nDrop = 0;
    const int nAdd =
        DiffWindows(have, g_recCount, seen, nSeen, toAdd, toDrop, &nDrop);
    for (int i = 0; i < nDrop; i++) {
        DropRec(toDrop[i]);
    }

    DisplayList displays{};
    EnumDisplayMonitors(nullptr, nullptr, CollectDisplay, (LPARAM)&displays);
    const UINT edge = DockedEdge();

    for (int i = 0; i < nAdd && g_recCount < MAX_TASKBARS; i++) {
        TaskbarRec rec{};
        rec.hwnd = toAdd[i];
        rec.primary = toAdd[i] == g_trayWnd;
        // The primary is already subclassed with TrayWatchProc.
        if (!rec.primary && !WindhawkUtils::SetWindowSubclassFromAnyThread(
                                rec.hwnd, SecondaryProc, 0)) {
            Wh_Log(L"taskbar %p: could not subclass", rec.hwnd);
            continue;
        }
        g_recs[g_recCount++] = rec;
    }

    for (int i = 0; i < g_recCount; i++) {
        // A window between positions is claimed by no display for a moment;
        // its facts stay as they were until the next refresh.
        FillRecFacts(&g_recs[i], displays, edge);
    }

    // Nothing is held any more (a display went, or they were rearranged):
    // every taskbar goes back to Windows.
    if (!BarsHandled()) {
        ReleaseAllBars();
    }

    // Take over a taskbar that is already hidden where Windows parked it:
    // bring it onto its own display now, not at the next reveal.
    if (BarsHandled()) {
        for (int i = 0; i < g_recCount; i++) {
            TaskbarRec* rec = &g_recs[i];
            if (rec->placed || IsRectEmpty(&rec->display)) {
                continue;
            }
            if (rec->inPlace && rec->hiddenNow > 0) {
                // Parked by Windows, it has the neighbouring display's DPI,
                // and moving it to the held position alone did not change
                // that (measured: it stayed at the neighbour's DPI until its
                // first reveal, which then drew the icons at the wrong size).
                // Passing through the shown position, wholly on its own
                // display, makes Windows settle the DPI now. An empty region
                // keeps that step off the screen.
                HRGN none = CreateRectRgn(0, 0, 0, 0);
                if (none && !SetWindowRgn(rec->hwnd, none, TRUE)) {
                    DeleteObject(none);
                }
                rec->clipped = true;
                rec->region = {0, 0, 0, 0};
                MoveBar(rec, rec->shown.x, rec->shown.y);
                DLog(L"taskbar %p taken over while hidden: dpi now %u",
                       rec->hwnd, GetDpiForWindow(rec->hwnd));
            }
            // A region may be left on the window by an instance of the mod
            // that did not get to clean up. Treat it as unknown, so the
            // placement below sets the right one or removes it.
            rec->clipped = true;
            rec->region = {-1, -1, -1, -1};
            // ...and the content may have been left slid aside.
            rec->contentMoved = true;
            rec->contentDx = rec->contentDy = -1;
            MoveBarContent(rec, 0, 0);

            // Never take one over more hidden than the mod itself would hide
            // it: a taskbar with nothing left on screen cannot be revealed.
            const int most = HiddenTargetFor(rec);
            if (rec->hiddenNow > most) {
                rec->hiddenNow = rec->hiddenAsked = most;
            }
            // ...and one hidden where Windows hides it, with its 2 px sliver,
            // goes to where the mod hides it: with "Hide completely" that
            // sliver would otherwise stay on screen until the next reveal.
            if (rec->hiddenNow > 0 && rec->hiddenNow >= most - 2) {
                rec->hiddenNow = rec->hiddenAsked = most;
            }
            ApplyPlacement(rec, rec->hiddenNow);
        }
    }
}

void MaybeRefreshRecords() {
    const ULONGLONG now = GetTickCount64();
    if (InterlockedExchange(&g_recsStale, 0) || now - g_recsRefreshed >= 1000) {
        g_recsRefreshed = now;
        RefreshRecords();
    }
}

LRESULT CALLBACK TrayWatchProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam, DWORD_PTR dwRefData) {
    if (uMsg == WM_DISPLAYCHANGE || uMsg == WM_SETTINGCHANGE) {
        InterlockedExchange(&g_recsStale, 1);
    }

    if (g_barTickMsg && uMsg == g_barTickMsg) {
        return TickBars();
    }

#ifdef TASKBAR_AUTOHIDE_DEBUG
    // While the edge is changing, and for the moment after, record what
    // reaches the tray: what Windows asks of the window and what state it
    // holds. This is how an edge change that goes wrong gets explained.
    if (g_trayUI && g_offsetsVerified &&
        (uMsg == WM_WINDOWPOSCHANGING || uMsg == WM_SIZE || uMsg == 0x553 ||
         uMsg == TRAY_MSG_PRIVATE_SETTING ||
         uMsg == TRAY_MSG_REMOTE_SETTINGS) &&
        (EdgeChangeInProgress() || uMsg == TRAY_MSG_PRIVATE_SETTING ||
         uMsg == TRAY_MSG_REMOTE_SETTINGS)) {
        RECT cur{};
        GetWindowRect(hWnd, &cur);
        const UINT edge = *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);
        const UINT stored = *(UINT*)((BYTE*)g_trayUI + g_trayUiFlagsOffset);
        if (uMsg == WM_WINDOWPOSCHANGING && lParam) {
            const WINDOWPOS* wp = (const WINDOWPOS*)lParam;
            DLog(L"edge change: Windows asks (%d,%d) %dx%d flags %#x; "
                   L"window (%d,%d,%d,%d), edge %u, flags %#x",
                   wp->x, wp->y, wp->cx, wp->cy, wp->flags, cur.left, cur.top,
                   cur.right, cur.bottom, edge, stored);
        } else {
            DLog(L"edge change: message %#x (%#llx, %#llx); window "
                   L"(%d,%d,%d,%d), edge %u, flags %#x",
                   uMsg, (unsigned long long)wParam,
                   (unsigned long long)lParam, cur.left, cur.top, cur.right,
                   cur.bottom, edge, stored);
        }
    }

#endif

    // A request for the edge the taskbar is already on changes nothing in
    // Windows (SetStuckPlace returns early), and every process with the
    // Settings handler loaded repeats the request, so one edge change arrives
    // more than once. Only the first is an edge change.
    const bool sameEdge =
        uMsg == TRAY_MSG_PRIVATE_SETTING && g_trayUI && g_offsetsVerified &&
        (UINT)lParam == *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);

    if (!g_fatalReason && !sameEdge &&
        ((uMsg == TRAY_MSG_PRIVATE_SETTING &&
          wParam == TRAY_SETTING_STUCK_PLACE) ||
         uMsg == TRAY_MSG_REMOTE_SETTINGS)) {
        // The taskbar is being moved to another edge. See
        // EdgeChangeInProgress: give every taskbar back, then answer nothing
        // for Windows until the call returns and a moment after.
        DLog(L"taskbar is changing edge (message %#x, wParam %#llx, lParam "
               L"%#llx): the mod stands back",
               uMsg, (unsigned long long)wParam, (unsigned long long)lParam);

        // Off the bottom edge "hidden" is the mod's doing: it stored that
        // state, and Windows only sees it through the mod's hook. Take it
        // back before Windows moves the taskbar. Left in place, it becomes
        // visible to Windows the moment the edge is the bottom, where
        // GetAutoHideFlags is not masked, and Windows then has a taskbar it
        // believes hidden that it never hid. (Seen: moving left to bottom,
        // the taskbar stayed on the left with its content laid out for the
        // bottom, and the edge went back to left.) TrayUI::Unhide with flags
        // 1: no animation, and the hide timer started, as after any unhide.
        // Without the timer a taskbar moved to the bottom stayed up until
        // the pointer next visited it. It must run while the mod still
        // answers GetAutoHideFlags, or it sees nothing to unhide.
        if (g_trayUI && g_offsetsVerified && TrayUI_Unhide_Original) {
            const UINT edge = *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);
            const UINT stored =
                *(UINT*)((BYTE*)g_trayUI + g_trayUiFlagsOffset);
            if (edge < ABE_BOTTOM && (stored & 3) == 3) {
                g_inWatcherCall = true;
                TrayUI_Unhide_Original(g_trayUI, 1, 7);
                g_inWatcherCall = false;
            }
        }
        ReleaseAllBars();
        for (int i = 0; i < g_recCount; i++) {
            g_recs[i].display = {0, 0, 0, 0};
            g_recs[i].held = false;
            g_recs[i].inPlace = false;
        }
        InterlockedExchange(&g_recsStale, 1);

        InterlockedExchange(&g_inEdgeChange, 1);
        const LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InterlockedExchange64(&g_edgeSettleUntil,
                              (LONG64)GetTickCount64() + EDGE_SETTLE_MS);
        InterlockedExchange(&g_inEdgeChange, 0);
        if (g_trayUI && g_offsetsVerified) {
            const UINT edge = *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);
            const UINT stored =
                *(UINT*)((BYTE*)g_trayUI + g_trayUiFlagsOffset);
            RECT now{};
            GetWindowRect(hWnd, &now);
            DLog(L"edge change handled: edge now %u, flags %#x, window at "
                   L"(%d,%d,%d,%d)",
                   edge, stored, now.left, now.top, now.right, now.bottom);
            InterlockedExchange(&g_lastEdgeSeen, (LONG)edge);
        }
        return r;
    }

    if (uMsg == TRAY_MSG_PRIVATE_SETTING &&
        wParam == TRAY_SETTING_AUTO_HIDE) {
        // Something is setting auto-hide. After Explorer starts off the
        // bottom edge the flags read off for about a minute and then on; this
        // is one of the three messages that can do that, and the log says
        // when it arrives.
        DLog(L"auto-hide set through the tray's setting message: %lld",
               (long long)lParam);
    }

    if (g_barRevealMsg && uMsg == g_barRevealMsg) {
        InterlockedExchange(&g_unloading, 1);

        // Unloading, off the bottom edge: have Windows itself reveal every
        // hidden secondary taskbar while the mod can still ask it to. Moving
        // the window back is not enough: Windows goes on believing the
        // taskbar is hidden, it stays unusable until "show my taskbar on all
        // displays" is toggled, and if the mod is loaded again its first
        // reveal has nothing to animate. Returns how many were asked.
        LRESULT asked = 0;
        for (int i = 0; i < g_recCount; i++) {
            TaskbarRec* rec = &g_recs[i];
            // Every secondary off the bottom edge, whatever the mod has it
            // doing: Windows may believe it hidden while the mod shows it.
            if (rec->primary || rec->edge >= ABE_BOTTOM ||
                IsRectEmpty(&rec->display)) {
                continue;
            }
            // Not the hit test: that only works with the pointer near, and
            // at unload it is in Windhawk's window.
            DLog(L"unload: unhiding taskbar %p through Windows", rec->hwnd);
            UnhideThroughWindows(rec->hwnd);
            asked++;
        }
        return asked;
    }

    if (g_barReleaseMsg && uMsg == g_barReleaseMsg) {
        // Unloading. Release while the windows are still subclassed, so the
        // moves are not overruled, then leave every taskbar alone.
        ReleaseAllBars();
        InterlockedExchange(&g_barsReleased, 1);

        // Off the bottom edge the primary is "hidden" only because the mod
        // said so. Have Windows unhide it itself: TrayUI::Unhide clears the
        // hidden state, moves the window, tells the content window it is
        // showing and clears the accessibility off-screen state, which
        // writing the flags and moving the window by hand does not. Flags 3:
        // no animation, and do not start the hide timer. This must run on
        // the taskbar's thread, which is where this message is handled.
        // RestoreTaskbarIfHidden remains for when this message could not be
        // delivered, and does nothing once the taskbar is no longer hidden.
        if (g_trayUI && g_offsetsVerified && TrayUI_Unhide_Original) {
            const UINT edge = *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);
            if (edge < ABE_BOTTOM) {
                g_inWatcherCall = true;
                TrayUI_Unhide_Original(g_trayUI, 3, 5);
                g_inWatcherCall = false;
            }
        }
        return 1;
    }

    if (uMsg == WM_WINDOWPOSCHANGING && lParam && BarsHandled()) {
        TaskbarRec* rec = RecFor(hWnd);
        if (rec && !IsRectEmpty(&rec->display)) {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            const int reqX = wp->x, reqY = wp->y;
            const UINT reqFlags = wp->flags;
            LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            BarPosChanging(rec, wp, reqX, reqY, reqFlags);
            return r;
        }
    }

    if (g_watchMsg && uMsg == g_watchMsg) {
        if (!g_fatalReason) {
            MaybeRefreshRecords();
            WatchHeldReveals();
            WatchSecondaries();
            WatchBottomKeepUps();
        }
        WatcherTick();
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Shell_TrayWnd does not exist yet when the mod loads at boot: Windhawk injects
// as explorer.exe starts, before the shell creates the taskbar. Looking it up
// once leaves the hooks live but nothing subclassed, so nothing drives the hide.
// Retry from the watch thread, which is off the taskbar's thread as
// SetWindowSubclassFromAnyThread requires.
void EnsureTrayWindow() {
    if (g_trayWnd && g_subclassed && IsWindow(g_trayWnd)) {
        return;
    }

    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    if (pid != GetCurrentProcessId()) {
        return;   // another explorer.exe is the shell; nothing to do here
    }

    if (tray != g_trayWnd) {
        g_trayWnd = tray;
        g_subclassed = false;
    }

    g_subclassed = WindhawkUtils::SetWindowSubclassFromAnyThread(
                       g_trayWnd, TrayWatchProc, 0) != FALSE;
}

// Windhawk injects into every explorer.exe, and folder windows can run as
// their own process. FindWindowW searches system-wide, so such a process finds
// a taskbar it cannot subclass and has no business managing.
bool ThisProcessOwnsTaskbar() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        return false;   // no taskbar anywhere yet: undecided, so stay quiet
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    return pid == GetCurrentProcessId();
}

// A failed mod leaves the taskbar behaving as it does with no mod installed,
// which looks exactly like a machine where nothing happens to hide.
void WarnOnce(PCWSTR body) {
    if (InterlockedExchange(&g_warned, 1) != 0) {
        return;
    }

    WCHAR title[] = L"Windhawk - Taskbar auto-hide on any edge";
    WCHAR text[1024];
    wsprintfW(text, L"%s\n\nDisable or remove the mod in Windhawk to stop "
                    L"this warning.", body);

    // The session owns the dialog, so nothing here waits on the user.
    // MessageBoxW would park this thread inside mod code until the button was
    // clicked, and Windhawk holds the unload until every mod thread has exited:
    // disabling the mod, which is what the warning asks for, would hang it.
    // Lengths are bytes.
    DWORD response = 0;
    WTSSendMessageW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, title,
                    (DWORD)(lstrlenW(title) * sizeof(WCHAR)), text,
                    (DWORD)(lstrlenW(text) * sizeof(WCHAR)),
                    MB_OK | MB_ICONWARNING, 0, &response, FALSE);
}

// The mod stood down, so the taskbar behaves as it does uninstalled.
void WarnNotRunning(PCWSTR reason) {
    WCHAR body[768];
    wsprintfW(body,
              L"Taskbar auto-hide on any edge is not running.\n\n%s\n\n"
              L"The taskbar will behave as it does with the mod uninstalled.",
              reason);
    WarnOnce(body);
}

// The PDB identity a module was linked with. Every build of a module has its
// own GUID, so this names one exact file.
struct CodeViewRsds {
    DWORD signature;   // "RSDS"
    GUID guid;
    DWORD age;
    char pdbPath[1];
};

const CodeViewRsds* FindCodeView(HMODULE module) {
    auto base = (const BYTE*)module;
    auto nt = (const IMAGE_NT_HEADERS*)(base +
                                        ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    auto entries = (const IMAGE_DEBUG_DIRECTORY*)(base + dir.VirtualAddress);

    for (DWORD i = 0; dir.VirtualAddress && i < dir.Size / sizeof(*entries);
         i++) {
        if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
            !entries[i].AddressOfRawData) {
            continue;
        }
        auto cv = (const CodeViewRsds*)(base + entries[i].AddressOfRawData);
        if (cv->signature == 0x53445352) {
            return cv;
        }
    }
    return nullptr;
}

// Builds the symbol server path of a module's PDB from its CodeView record:
// /download/symbols/<pdb>/<GUID><age>/<pdb>.
bool SymbolServerPath(HMODULE module, WCHAR (&path)[512]) {
    const CodeViewRsds* cv = FindCodeView(module);
    if (!cv) {
        return false;
    }

    // The record can hold the build machine's full path.
    PCSTR name = cv->pdbPath;
    for (PCSTR c = cv->pdbPath; *c; c++) {
        if (*c == '\\' || *c == '/') {
            name = c + 1;
        }
    }
    WCHAR pdb[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, name, -1, pdb, ARRAYSIZE(pdb))) {
        return false;
    }

    const GUID& g = cv->guid;
    wsprintfW(path,
              L"/download/symbols/%s/%08lX%04X%04X%02X%02X%02X%02X%02X%02X"
              L"%02X%02X%lX/%s",
              pdb, g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
              g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
              g.Data4[7], cv->age, pdb);
    return true;
}

enum class SymbolsOnServer { Unknown, Published, NotPublished };

// Asks Microsoft's symbol server whether the PDB exists, without downloading
// it. Redirects are not followed: the server answers a published PDB with a
// redirect to its storage and an unpublished one with 404.
SymbolsOnServer QuerySymbolServer(PCWSTR path) {
    SymbolsOnServer result = SymbolsOnServer::Unknown;

    HINTERNET session =
        WinHttpOpen(L"Windhawk taskbar-autohide-any-edge",
                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect =
        session ? WinHttpConnect(session, L"msdl.microsoft.com",
                                 INTERNET_DEFAULT_HTTPS_PORT, 0)
                : nullptr;
    HINTERNET request =
        connect ? WinHttpOpenRequest(connect, L"HEAD", path, nullptr,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE)
                : nullptr;

    if (request) {
        DWORD noRedirects = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirects,
                         sizeof(noRedirects));

        // StopWatchThread sets the stop flag before taking the handle, so a
        // request it missed is caught by the check here.
        InterlockedExchangePointer(&g_symbolRequest, request);
        bool answered =
            !InterlockedCompareExchange(&g_watchStop, 0, 0) &&
            WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr);

        // Whoever takes the handle back closes it.
        if (InterlockedExchangePointer(&g_symbolRequest, nullptr) == request) {
            DWORD status = 0;
            DWORD size = sizeof(status);
            if (answered &&
                WinHttpQueryHeaders(
                    request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                    WINHTTP_NO_HEADER_INDEX)) {
                if (status == 404) {
                    result = SymbolsOnServer::NotPublished;
                } else if (status == 200 || status == 302) {
                    result = SymbolsOnServer::Published;
                }
            }
            WinHttpCloseHandle(request);
        }
    }

    if (connect) {
        WinHttpCloseHandle(connect);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }
    return result;
}

// Names the taskbar build, for example " (taskbar.dll 10.0.26100.9807, dated
// 17 October 2026)", or leaves the text empty when neither can be read.
void DescribeTaskbarBuild(HMODULE taskbar, WCHAR (&text)[192]) {
    text[0] = L'\0';

    // VS_VERSIONINFO: three WORDs, the key "VS_VERSION_INFO", then the fixed
    // info aligned to 4 bytes.
    WCHAR version[48] = L"";
    if (HRSRC res = FindResourceW(taskbar, MAKEINTRESOURCEW(VS_VERSION_INFO),
                                  RT_VERSION)) {
        auto data = (const BYTE*)LockResource(LoadResource(taskbar, res));
        if (data && SizeofResource(taskbar, res) >=
                        40 + sizeof(VS_FIXEDFILEINFO)) {
            auto fixed = (const VS_FIXEDFILEINFO*)(data + 40);
            if (fixed->dwSignature == 0xFEEF04BD) {
                wsprintfW(version, L"%u.%u.%u.%u",
                          HIWORD(fixed->dwFileVersionMS),
                          LOWORD(fixed->dwFileVersionMS),
                          HIWORD(fixed->dwFileVersionLS),
                          LOWORD(fixed->dwFileVersionLS));
            }
        }
    }

    WCHAR date[64] = L"";
    WCHAR file[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    SYSTEMTIME utc, local;
    if (GetModuleFileNameW(taskbar, file, ARRAYSIZE(file)) &&
        GetFileAttributesExW(file, GetFileExInfoStandard, &attrs) &&
        FileTimeToSystemTime(&attrs.ftLastWriteTime, &utc) &&
        SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &local,
                        nullptr, date, ARRAYSIZE(date), nullptr);
    }

    if (version[0] && date[0]) {
        wsprintfW(text, L" (taskbar.dll %s, dated %s)", version, date);
    } else if (version[0] || date[0]) {
        wsprintfW(text, L" (taskbar.dll %s)", version[0] ? version : date);
    }
}

// Runs on the watch thread, which has time to spare for the network. Falls
// back to the general reason whenever the server cannot settle it.
PCWSTR ExplainMissingSymbols(HMODULE taskbar) {
    WCHAR path[512];
    if (!SymbolServerPath(taskbar, path)) {
        return g_fatalReason;
    }

    SymbolsOnServer state = QuerySymbolServer(path);
    Wh_Log(L"symbol server %s: %s", path,
           state == SymbolsOnServer::NotPublished ? L"not published"
           : state == SymbolsOnServer::Published  ? L"published"
                                                  : L"no answer");

    if (state == SymbolsOnServer::Published) {
        return L"Microsoft has published the taskbar's debug symbols, but "
               L"Windhawk could not load them. Reload the mod in Windhawk to "
               L"download them again.";
    }
    if (state != SymbolsOnServer::NotPublished) {
        return g_fatalReason;
    }

    WCHAR build[192];
    DescribeTaskbarBuild(taskbar, build);

    static WCHAR reason[512];
    wsprintfW(reason,
              L"A Windows update replaced the taskbar%s, and Microsoft has not "
              L"published the debug symbols the mod needs for this version "
              L"yet.\n\nThe mod looks for them each time Windows starts, so it "
              L"starts working again by itself once they are available.",
              build);
    return reason;
}

DWORD WINAPI WatchThreadProc(LPVOID) {
    const ULONGLONG started = GetTickCount64();

    while (!InterlockedCompareExchange(&g_watchStop, 0, 0)) {
        if (!g_fatalReason) {
            EnsureTrayWindow();
        }

        const ULONGLONG up = GetTickCount64() - started;
        if (up > WARN_SETTLE_MS &&
            !InterlockedCompareExchange(&g_warned, 0, 0)) {
            if (g_fatalReason) {
                if (ThisProcessOwnsTaskbar()) {
                    PCWSTR reason =
                        g_symbolsMissingFor
                            ? ExplainMissingSymbols(g_symbolsMissingFor)
                            : g_fatalReason;
                    if (!InterlockedCompareExchange(&g_watchStop, 0, 0)) {
                        WarnNotRunning(reason);
                    }
                }
            } else if ((!g_trayWnd || !g_subclassed) &&
                       up > WARN_NO_TASKBAR_MS && ThisProcessOwnsTaskbar()) {
                WarnNotRunning(
                    L"The mod could not attach to the taskbar window.");
            } else if (up > WARN_AUTOHIDE_OFF_MS &&
                       AutoHideOffLongEnough(
                           InterlockedCompareExchange64(&g_autoHideOffSince, 0,
                                                        0),
                           (LONG64)GetTickCount64(),
                           WARN_AUTOHIDE_STAYS_OFF_MS) &&
                       ThisProcessOwnsTaskbar()) {
                WarnOnce(L"Auto-hide is turned off in Windows, so there is "
                         L"nothing for this mod to do. Turn it on under "
                         L"Settings > Personalisation > Taskbar > Taskbar "
                         L"behaviours.");
            }
        }

        if (g_trayWnd && g_watchMsg) {
            PostMessageW(g_trayWnd, g_watchMsg, 0, 0);
        }

        // A stood-down mod stays loaded for the life of the shell with nothing
        // to do but warn once. Do not wake 25 times a second to do it.
        Sleep(g_fatalReason ? 1000 : WATCH_TICK_MS);
    }
    return 0;
}

// ------------------------------------------------------------------ lifecycle

// GetAutoHideFlags encodes both member offsets, so read them out of it rather
// than hardcoding them, and find the instructions by shape rather than at a
// fixed offset. A recompiled prologue moves the sequence and a changed struct
// moves the members; neither is a different Windows.
//
//   83 BB ?? ?? ?? ?? 03   cmp dword [rbx+edge], 3
//   74 04                  je   .return_real
//   33 C0                  xor  eax, eax
//   EB 03                  jmp  .done
//   8B 43 ??               mov  eax, [rbx+flags]
//
// 26100.9807 lays the same branch out the other way round, with the zero
// return moved out of line after the ret:
//
//   83 BB ?? ?? ?? ?? 03   cmp dword [rbx+edge], 3
//   75 ??                  jne  .return_zero
//   8B 43 ??               mov  eax, [rbx+flags]
//   ...
//   .return_zero:
//   33 C0                  xor  eax, eax
bool ScanForOffsets(const BYTE* fn) {
    // Bound the search at the int3 padding that follows the function. Scanning
    // into the next one risks a false match that would be silently wrong.
    size_t len = 0x100;
    for (size_t i = 0; i + 4 <= 0x100; i++) {
        if (fn[i] == 0xCC && fn[i + 1] == 0xCC && fn[i + 2] == 0xCC &&
            fn[i + 3] == 0xCC) {
            len = i;
            break;
        }
    }

    UINT edge = 0, flags = 0;
    int matches = 0;

    for (size_t i = 0; i + 12 <= len; i++) {
        const BYTE* p = fn + i;
        if (p[0] != 0x83 || p[1] != 0xBB || p[6] != 0x03) {
            continue;
        }

        if (i + 16 <= len && p[7] == 0x74 && p[8] == 0x04 && p[9] == 0x33 &&
            p[10] == 0xC0 && p[11] == 0xEB && p[12] == 0x03 &&
            p[13] == 0x8B && p[14] == 0x43) {
            flags = p[15];
        } else if (p[7] == 0x75 && p[9] == 0x8B && p[10] == 0x43) {
            // The jump must land on the zero return, inside the function.
            size_t zero = i + 9 + p[8];
            if (p[8] >= 0x80 || zero + 2 > len || fn[zero] != 0x33 ||
                fn[zero + 1] != 0xC0) {
                continue;
            }
            flags = p[11];
        } else {
            continue;
        }
        edge = *(const UINT*)(p + 2);
        matches++;
    }

    // Exactly one unambiguous match, and offsets that are plausible on both
    // bases -- the watcher reads the same members from a base 8 bytes lower, so
    // anything below 8 would underflow.
    if (matches != 1 || edge < 8 || edge > 0x1000 || flags < 8) {
        return false;
    }

    g_getFlagsEdgeOffset = edge;
    g_getFlagsFlagsOffset = flags;
    g_trayUiEdgeOffset = edge - 8;
    g_trayUiFlagsOffset = flags - 8;
    return true;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}

// Function addresses for taskbar builds whose symbols Microsoft has not
// published, matched by hand against the previous build's PDB. Keyed by PDB
// identity, so an entry applies to that exact file and nothing else, and only
// once symbols have failed: published symbols always win.
struct KnownBuild {
    PCWSTR version;
    GUID pdbGuid;
    DWORD pdbAge;
    DWORD getStuckPlace;
    DWORD getAutoHideFlags;
    DWORD setAutoHideFlags;
    DWORD getDockedRect;
    DWORD unhide;
    DWORD slideWindow;
    DWORD permitAutoHide;
    // Optional: CSecondaryTray::_Unhide. 0, which a row that stops before it
    // is filled with, where it was not matched with the certainty the others
    // were.
    DWORD secondaryTrayUnhide;
};

const KnownBuild kKnownBuilds[] = {
    // Matched against 10.0.26100.9549 with Ghidra. GetStuckPlace,
    // SetAutoHideFlags and GetDockedRect are instruction-identical; Unhide,
    // SlideWindow and PermitAutoHide differ only in stack layout and inlining,
    // with the same imports and member offsets.
    {L"10.0.26100.9807",
     {0x1AF71656, 0xD4C0, 0x4FBD, {0x46, 0x3B, 0xFF, 0x7E, 0x4E, 0xD1, 0x69, 0xCE}},
     1, 0x65250, 0x41F70, 0x3CC90, 0x41ED0, 0x5E670, 0x41260, 0x35C40},
    // Matched against 10.0.26100.9818 by tools/match-build.py.
    {L"10.0.26100.9974",
     {0x63C1425A, 0x7856, 0xE31E, {0x2D, 0x8C, 0x1B, 0xCE, 0xE4, 0x38, 0xEF, 0x9E}},
     1, 0x65250, 0x41F70, 0x3CC90, 0x41ED0, 0x5E670, 0x41260, 0x35C40},
    // Matched against 10.0.26100.9974 by tools/match-build.py.
    {L"10.0.26100.10066",
     {0x16BF9C30, 0x8191, 0xA000, {0x05, 0xC1, 0x90, 0x39, 0xE9, 0x6A, 0x9D, 0xF7}},
     1, 0x27F20, 0x45020, 0xB0810, 0x44F80, 0x1A2B0, 0xB08B0, 0x7F5D0, 0x15BBCC},
};

const KnownBuild* FindKnownBuild(HMODULE module) {
    const CodeViewRsds* cv = FindCodeView(module);
    if (!cv) {
        return nullptr;
    }
    for (const KnownBuild& b : kKnownBuilds) {
        if (IsEqualGUID(cv->guid, b.pdbGuid) && cv->age == b.pdbAge) {
            return &b;
        }
    }
    return nullptr;
}

// The table's addresses come from matching, not from symbols. Before patching
// any of them, check each one lands where the matcher said it would: a
// function start in the unwind table, or for GetStuckPlace, a leaf with no
// unwind data, its exact body. A wrong entry then fails with a message instead
// of patching the middle of a function and taking Explorer down with it. The
// optional address is held to the same check when the entry gives one: an
// entry with any address wrong is not trusted for the others.
bool KnownBuildLooksRight(HMODULE taskbar, const KnownBuild& b) {
    const BYTE* base = (const BYTE*)taskbar;
    auto* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage;

    static const BYTE kGetStuckPlace[] = {
        0x8B, 0x81, 0x1C, 0x01, 0x00, 0x00,  // mov eax,[rcx+11Ch]
        0x83, 0xF8, 0xFF,                    // cmp eax,-1
        0x75, 0x06,                          // jne ret
        0x8B, 0x81, 0x10, 0x01, 0x00, 0x00,  // mov eax,[rcx+110h]
        0xC3,                                // ret
    };
    if (b.getStuckPlace >= size - sizeof(kGetStuckPlace) ||
        memcmp(base + b.getStuckPlace, kGetStuckPlace,
               sizeof(kGetStuckPlace)) != 0) {
        return false;
    }

    const DWORD starts[] = {b.getAutoHideFlags, b.setAutoHideFlags,
                            b.getDockedRect,    b.unhide,
                            b.slideWindow,      b.permitAutoHide};
    auto isFunctionStart = [base, size](DWORD rva) {
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn =
            rva < size ? RtlLookupFunctionEntry((DWORD64)(base + rva),
                                                &imageBase, nullptr)
                       : nullptr;
        return fn && imageBase == (DWORD64)base && fn->BeginAddress == rva;
    };
    for (DWORD rva : starts) {
        if (!isFunctionStart(rva)) {
            return false;
        }
    }
    return !b.secondaryTrayUnhide || isFunctionStart(b.secondaryTrayUnhide);
}

// The same hooks HookSymbols would set, at the tabled addresses.
bool HookKnownBuild(HMODULE taskbar, const KnownBuild& b) {
    auto at = [taskbar](DWORD rva) { return (void*)((BYTE*)taskbar + rva); };

    TrayUI_GetStuckPlace_Original =
        (TrayUI_GetStuckPlace_t)at(b.getStuckPlace);
    TrayUI_SetAutoHideFlags_Original =
        (TrayUI_SetAutoHideFlags_t)at(b.setAutoHideFlags);
    TrayUI_GetDockedRect_Original =
        (TrayUI_GetDockedRect_t)at(b.getDockedRect);

    return WindhawkUtils::SetFunctionHook(
               (TrayUI_GetAutoHideFlags_t)at(b.getAutoHideFlags),
               TrayUI_GetAutoHideFlags_Hook,
               &TrayUI_GetAutoHideFlags_Original) &&
           WindhawkUtils::SetFunctionHook((TrayUI_Unhide_t)at(b.unhide),
                                          TrayUI_Unhide_Hook,
                                          &TrayUI_Unhide_Original) &&
           WindhawkUtils::SetFunctionHook(
               (TrayUI_SlideWindow_t)at(b.slideWindow),
               TrayUI_SlideWindow_Hook, &TrayUI_SlideWindow_Original) &&
           WindhawkUtils::SetFunctionHook(
               (CTaskListWnd_PermitAutoHide_t)at(b.permitAutoHide),
               CTaskListWnd_PermitAutoHide_Hook,
               &CTaskListWnd_PermitAutoHide_Original);
}

// Returns nullptr when the mod is ready, or a reason fit to put in front of a
// user.
PCWSTR SetUpHooks() {
    if (!IsSupportedBuild()) {
        static WCHAR reason[192];
        wsprintfW(reason,
                  L"This mod needs Windows 11 build 26200 or newer. Windows "
                  L"reports build %u.",
                  g_buildNumber);
        return reason;
    }

    HMODULE taskbar =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!taskbar) {
        return L"taskbar.dll could not be loaded.";
    }

    // Resolve the functions whose instructions are read on their own first,
    // passing no hook function so the pointers receive the symbol addresses.
    // Once a hook is registered, the pointer is a trampoline: the engine's
    // relocated copy of however many leading instructions the patch displaced,
    // followed by a jump back. How much of it matches the real function varies
    // by engine version, so reading through it gives different answers on
    // machines with identical taskbar.dll. Validating first also means an
    // unrecognised build is never patched.
    WindhawkUtils::SYMBOL_HOOK resolveOnly[] = {
        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void) )"},
         &TrayUI_GetAutoHideFlags_Address, nullptr, true},

        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))",
          LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Address, nullptr, true},
    };

    const KnownBuild* known = nullptr;
    if (!WindhawkUtils::HookSymbols(taskbar, resolveOnly,
                                    ARRAYSIZE(resolveOnly)) ||
        !TrayUI_GetAutoHideFlags_Address) {
        known = FindKnownBuild(taskbar);
        if (!known) {
            g_symbolsMissingFor = taskbar;
            return L"The taskbar's debug symbols could not be resolved. "
                   L"Windhawk downloads these from Microsoft the first time a "
                   L"mod needs them, so check the internet connection and "
                   L"reload the mod.";
        }
        if (!KnownBuildLooksRight(taskbar, *known)) {
            Wh_Log(L"built-in addresses for taskbar.dll %s failed the check",
                   known->version);
            return L"The mod's built-in addresses for this taskbar.dll did "
                   L"not check out, so it has left the taskbar alone. Please "
                   L"report this; the mod needs updating to match.";
        }
        Wh_Log(L"no symbols, using the built-in addresses for taskbar.dll %s",
               known->version);
        TrayUI_GetAutoHideFlags_Address =
            (TrayUI_GetAutoHideFlags_t)((BYTE*)taskbar + known->getAutoHideFlags);
        TrayUI_SlideWindow_Address =
            (TrayUI_SlideWindow_t)((BYTE*)taskbar + known->slideWindow);
    }

    g_offsetsVerified =
        ScanForOffsets((const BYTE*)TrayUI_GetAutoHideFlags_Address);
    if (!g_offsetsVerified) {
        return L"This build of taskbar.dll does not match what the mod was "
               L"built against, so it cannot safely read the taskbar's state. "
               L"A Windows update most likely changed it, and the mod needs "
               L"updating to match.";
    }

    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void) )"},
         &TrayUI_GetStuckPlace_Original, nullptr, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void) )"},
         &TrayUI_GetAutoHideFlags_Original, TrayUI_GetAutoHideFlags_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int))",
          LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int) )"},
         &TrayUI_SetAutoHideFlags_Original, nullptr, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int) )"},
         &TrayUI_GetDockedRect_Original, nullptr, true},

        {{LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest))",
          LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest) )"},
         &TrayUI_Unhide_Original, TrayUI_Unhide_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))",
          LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Original, TrayUI_SlideWindow_Hook, true},

        {{LR"(public: virtual int __cdecl CTaskListWnd::PermitAutoHide(void))",
          LR"(public: virtual int __cdecl CTaskListWnd::PermitAutoHide(void) )"},
         &CTaskListWnd_PermitAutoHide_Original,
         CTaskListWnd_PermitAutoHide_Hook, true},
    };

    if (known) {
        if (!HookKnownBuild(taskbar, *known)) {
            return L"The mod could not hook the taskbar's functions.";
        }
    } else if (!WindhawkUtils::HookSymbols(taskbar, hooks, ARRAYSIZE(hooks))) {
        g_symbolsMissingFor = taskbar;
        return L"The taskbar's debug symbols could not be resolved. "
               L"Windhawk downloads these from Microsoft the first time a mod "
               L"needs them, so check the internet connection and reload the "
               L"mod.";
    }

    // Optional: from symbols, or from kKnownBuilds where the entry has its
    // address (KnownBuildLooksRight has checked it with the others). Without
    // it the rest of the mod works, and a flashing button shows secondary
    // taskbars as Windows itself would.
    WindhawkUtils::SYMBOL_HOOK secondaryHooks[] = {
        {{LR"(private: void __cdecl CSecondaryTray::_Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest))",
          LR"(private: void __cdecl CSecondaryTray::_Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest) )"},
         &CSecondaryTray_Unhide_Original, CSecondaryTray_Unhide_Hook, true},
    };
    bool secondaryHooked;
    if (known) {
        secondaryHooked =
            known->secondaryTrayUnhide &&
            WindhawkUtils::SetFunctionHook(
                (CSecondaryTray_Unhide_t)((BYTE*)taskbar +
                                          known->secondaryTrayUnhide),
                CSecondaryTray_Unhide_Hook, &CSecondaryTray_Unhide_Original);
    } else {
        secondaryHooked = WindhawkUtils::HookSymbols(
            taskbar, secondaryHooks, ARRAYSIZE(secondaryHooks));
    }
    if (!secondaryHooked || !CSecondaryTray_Unhide_Original) {
        // RevealSecondariesForFlash stands in when this pointer is null.
        CSecondaryTray_Unhide_Original = nullptr;
        Wh_Log(L"secondary taskbars: notification handling unavailable on "
               L"this build%s",
               known && !known->secondaryTrayUnhide
                   ? L" (no built-in address for it)"
                   : L"");
    } else if (known) {
        Wh_Log(L"secondary taskbars: notification handling uses the built-in "
               L"address for taskbar.dll %s",
               known->version);
    }

    // Every one of these is load-bearing: running with any missing would change
    // nothing while appearing to work.
    struct {
        PCWSTR name;
        void* ptr;
    } required[] = {
        {L"GetStuckPlace", (void*)TrayUI_GetStuckPlace_Original},
        {L"GetAutoHideFlags", (void*)TrayUI_GetAutoHideFlags_Original},
        {L"SetAutoHideFlags", (void*)TrayUI_SetAutoHideFlags_Original},
        {L"GetDockedRect", (void*)TrayUI_GetDockedRect_Original},
        {L"Unhide", (void*)TrayUI_Unhide_Original},
        {L"SlideWindow", (void*)TrayUI_SlideWindow_Address},
    };

    for (const auto& r : required) {
        if (!r.ptr) {
            static WCHAR missing[256];
            wsprintfW(missing,
                      L"The taskbar function %s could not be found, so the mod "
                      L"cannot hook it. This usually means the debug symbols "
                      L"are incomplete; reloading the mod re-downloads them.",
                      r.name);
            return missing;
        }
    }

    // Pacing the reveal means pacing a loop this mod does not own. The hook is
    // inert on every thread but the one inside SlideWindow.
    if (HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll")) {
        auto pSleep = (Sleep_t)(void*)GetProcAddress(kernelBase, "Sleep");
        auto pTick =
            (GetTickCount_t)(void*)GetProcAddress(kernelBase, "GetTickCount");

        // Both or neither: pacing the frames without a clock fine enough to
        // separate them would draw the extra frames at positions already drawn.
        if (!pSleep || !pTick ||
            !WindhawkUtils::SetFunctionHook(pSleep, Sleep_Hook,
                                            &Sleep_Original) ||
            !WindhawkUtils::SetFunctionHook(pTick, GetTickCount_Hook,
                                            &GetTickCount_Original)) {
            Wh_Log(L"could not hook Sleep and GetTickCount, the reveal keeps "
                   L"Windows' frame rate");
        }
    }

    return nullptr;
}

BOOL Wh_ModInit() {
    LoadSettings();

    g_fatalReason = SetUpHooks();
    if (g_fatalReason) {
        Wh_Log(L"inactive: %s", g_fatalReason);
    } else {
        // Best effort only: at boot the taskbar does not exist yet, and the
        // watch thread picks it up as soon as it does.
        EnsureTrayWindow();
    }

    // Started even when the mod has stood down: it raises the warning.
    FrameTimerInit();

    g_watchMsg = RegisterWindowMessageW(L"WindhawkTaskbarAutoHideAnyEdgeTick");
    if (!g_watchMsg) {
        Wh_Log(L"could not register the tick message");
        return FALSE;
    }

    g_barTickMsg =
        RegisterWindowMessageW(L"WindhawkTaskbarAutoHideAnyEdgeBarTick");
    g_barReleaseMsg =
        RegisterWindowMessageW(L"WindhawkTaskbarAutoHideAnyEdgeBarRelease");
    g_barRevealMsg =
        RegisterWindowMessageW(L"WindhawkTaskbarAutoHideAnyEdgeBarReveal");
    g_barWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_barTickMsg && g_barWake) {
        g_barThread =
            CreateThread(nullptr, 0, BarThreadProc, nullptr, 0, nullptr);
    }
    if (!g_barThread) {
        Wh_Log(L"could not start the pacing thread; held taskbars will jump "
               L"instead of sliding");
    }

    g_watchThread = CreateThread(nullptr, 0, WatchThreadProc, nullptr, 0,
                                 nullptr);
    if (!g_watchThread) {
        Wh_Log(L"could not start the watch thread");
        return FALSE;
    }

    return TRUE;
}

// Unloading while the taskbar is hidden would strand it off-screen: Windows
// stops believing auto-hide is active, so nothing brings it back. Restore it
// before the hooks go away, using only plain Win32 from whichever thread this
// runs on; calling TrayUI methods from another thread crashes the shell.
void RestoreTaskbarIfHidden() {
    if (!g_trayUI || !g_offsetsVerified || !g_trayWnd) {
        return;
    }

    UINT edge = *(UINT*)((BYTE*)g_trayUI + g_trayUiEdgeOffset);
    if (edge >= ABE_BOTTOM) {
        return;
    }

    UINT* flags = (UINT*)((BYTE*)g_trayUI + g_trayUiFlagsOffset);
    if (!(*flags & ABS_AUTOHIDE) || !(*flags & ABS_ALWAYSONTOP)) {
        return;   // not hidden
    }

    *flags &= ~ABS_ALWAYSONTOP;   // no longer "hidden"

    if (g_lastDockedRect.right > g_lastDockedRect.left) {
        SetWindowPos(g_trayWnd, nullptr, g_lastDockedRect.left,
                     g_lastDockedRect.top,
                     g_lastDockedRect.right - g_lastDockedRect.left,
                     g_lastDockedRect.bottom - g_lastDockedRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    PostMessageW(g_trayWnd, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
}

void StopWatchThread() {
    InterlockedExchange(&g_watchStop, 1);

    // A symbol server check in flight would hold the thread past the wait.
    if (HINTERNET request = InterlockedExchangePointer(&g_symbolRequest, nullptr)) {
        WinHttpCloseHandle(request);
    }
    if (g_watchThread) {
        WaitForSingleObject(g_watchThread, 2000);
        CloseHandle(g_watchThread);
        g_watchThread = nullptr;
    }
}

// Stop the mod's animations and give every taskbar back to Windows. Called
// with the watch thread already stopped, and safe to call twice.
void ReleaseBarsOnUnload() {
    InterlockedExchange(&g_barStop, 1);
    if (g_barWake) {
        SetEvent(g_barWake);
    }
    if (g_barThread) {
        WaitForSingleObject(g_barThread, 2000);
        CloseHandle(g_barThread);
        g_barThread = nullptr;
    }

    // The watch and pacing threads have stopped. Put every taskbar back where
    // Windows last asked for it, whole, on the taskbar's own thread, which
    // owns the records.
    if (g_trayWnd && g_subclassed && g_barReleaseMsg &&
        !InterlockedCompareExchange(&g_barsReleased, 0, 0)) {
        DWORD_PTR asked = 0, done = 0;
        const LRESULT revealSent =
            g_barRevealMsg
                ? SendMessageTimeoutW(g_trayWnd, g_barRevealMsg, 0, 0,
                                      SMTO_ABORTIFHUNG, 2000, &asked)
                : 0;
        DLog(L"unload: reveal message %s (error %u), %d secondaries asked",
               revealSent ? L"delivered" : L"NOT delivered",
               revealSent ? 0 : GetLastError(), (int)asked);
        if (revealSent && asked) {
            // Windows answers the hit test about 60 ms later, on the
            // taskbar's thread. Give it time, but less than the half second
            // after which it would hide the taskbar again.
            Sleep(250);
        }
        const LRESULT releaseSent =
            SendMessageTimeoutW(g_trayWnd, g_barReleaseMsg, 0, 0,
                                SMTO_ABORTIFHUNG, 2000, &done);
        DLog(L"unload: release message %s (error %u)",
               releaseSent && done ? L"handled" : L"NOT handled",
               releaseSent ? 0 : GetLastError());
    } else {
        DLog(L"unload: nothing to release (window %p, subclassed %d, "
               L"released %d)",
               g_trayWnd, g_subclassed,
               (int)InterlockedCompareExchange(&g_barsReleased, 0, 0));
    }

    // Whatever happened above, no taskbar may be left with one of the mod's
    // regions: an empty one makes it invisible for good. Removing a region
    // works from any thread.
    auto contentHome = [](HWND bar) {
        if (HWND content = FindWindowExW(
                bar, nullptr,
                L"Windows.UI.Composition.DesktopWindowContentBridge",
                nullptr)) {
            SetWindowPos(content, nullptr, 0, 0, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    };
    if (g_trayWnd) {
        SetWindowRgn(g_trayWnd, nullptr, TRUE);
        contentHome(g_trayWnd);
    }
    const DWORD pid = GetCurrentProcessId();
    for (HWND w = nullptr;
         (w = FindWindowExW(nullptr, w, L"Shell_SecondaryTrayWnd", nullptr));) {
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        if (owner == pid) {
            SetWindowRgn(w, nullptr, TRUE);
            contentHome(w);
        }
    }
    InterlockedExchange(&g_barsReleased, 1);
}

void Wh_ModBeforeUninit() {
    // Stop the watcher first: left running, it can hide the taskbar again
    // between the restore below and the thread being stopped, which strands it
    // off-screen -- exactly what the restore exists to prevent.
    StopWatchThread();
    ReleaseBarsOnUnload();
    RestoreTaskbarIfHidden();
}

void Wh_ModUninit() {
    StopWatchThread();

    for (HANDLE* h : {&g_slideTimer}) {
        if (*h) {
            CloseHandle(*h);
            *h = nullptr;
        }
    }

    ReleaseBarsOnUnload();

    for (int i = 0; i < g_recCount; i++) {
        if (!g_recs[i].primary && g_recs[i].hwnd) {
            WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_recs[i].hwnd,
                                                             SecondaryProc);
        }
    }
    if (g_barWake) {
        CloseHandle(g_barWake);
        g_barWake = nullptr;
    }

    if (g_trayWnd && g_subclassed) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_trayWnd,
                                                         TrayWatchProc);
    }
}
