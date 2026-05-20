/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2026 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#include "prologue.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <stdint.h>

#include "log.h"
#include "scr_driver.h"
#include "async_handle.h"
#include "async_io.h"
#include "async_alarm.h"
#include "clipboard.h"
#include "report.h"
#include "brlapi_param.h"
#include "ax_bridge.h"

// Hard upper bounds to keep buffer sizes sane on rogue input.
#define SCREEN_MAX_COLUMNS 512
#define SCREEN_MAX_ROWS    256

// Minimum dimensions reported even when we have nothing to display, so
// brltty has a non-empty screen to draw to.
#define SCREEN_MIN_COLUMNS 40
#define SCREEN_MIN_ROWS    1

static wchar_t *screenBuffer = NULL;
static int screenRows = SCREEN_MIN_ROWS;
static int screenCols = SCREEN_MIN_COLUMNS;
static int screenCapacity = 0;

static int cursorRow = 0;
static int cursorCol = 0;

/* Per-row "where did the rendered text actually end on this row"
 * tracker, updated by renderTextIntoGrid. Cells beyond rowEndCol[r]
 * are the fillScreenWithSpaces() padding, not real text — they must
 * not be confused with "trailing whitespace the user typed".
 * Sized to SCREEN_MAX_ROWS so we never index out of bounds. */
static int rowEndCol[SCREEN_MAX_ROWS] = {0};

static char lastFingerprint[2048] = {0};
static AsyncHandle wakeMonitor = NULL;
static int wakeFd = -1;

/* ---- System clipboard ↔ brltty clipboard bridge --------------------------
 *
 * Mirrors what the Linux AtSpi2 driver does with X selection: keep
 * brltty's internal clipboard in sync with the host's general
 * pasteboard so a Cmd+C from any app lands in brltty, and a brltty
 * BRL_CMD_COPY shows up on the system pasteboard for Cmd+V to consume.
 *
 * macOS has no push notification for NSPasteboard, so we poll the
 * changeCount on a 500 ms alarm. The changeCount we receive from our
 * own writes is recorded in clipboardLastSeenChangeCount; anything
 * strictly greater is treated as an external change. The
 * suppressOwnClipboardReport flag breaks the inverse loop: when our
 * poll pushes external content into brltty via setMainClipboardContent,
 * the resulting REPORT_API_PARAMETER_UPDATED would otherwise echo us
 * right back to NSPasteboard.
 */
static AsyncHandle clipboardPollAlarm = NULL;
static ReportListenerInstance *clipboardReportListener = NULL;
static long clipboardLastSeenChangeCount = 0;
static int suppressOwnClipboardReport = 0;

#define CLIPBOARD_POLL_INTERVAL_MS 500
#define CLIPBOARD_MAX_BYTES        (1024 * 1024)  // 1 MB cap on payload size

static int
handleWakeFromObserver(const AsyncMonitorCallbackParameters *parameters) {
  ax_observer_drain(wakeFd);
  mainScreenUpdated();
  return 1; // keep monitoring
}

/* Pull a fresh string from NSPasteboard and shove it into brltty's
 * clipboard. The suppress flag tells our REPORT_LISTENER below to
 * ignore the param-updated event we just triggered (so we don't loop
 * the same content back to NSPasteboard).
 */
static void
pullSystemClipboardIntoBrltty(void) {
  static char buf[CLIPBOARD_MAX_BYTES];
  size_t n = ax_pasteboard_get_string(buf, sizeof(buf));
  if (n == 0) return;  // empty pasteboard — don't blow away brltty's own
  suppressOwnClipboardReport = 1;
  setMainClipboardContent(buf);
  suppressOwnClipboardReport = 0;
}

/* Periodic alarm: cheap changeCount check, only fetch + push on
 * detected change. The 500 ms cadence is far below human copy-paste
 * latency and the polling itself is microseconds. */
static void
clipboardPollAlarmCallback(const AsyncAlarmCallbackParameters *parameters) {
  long now = ax_pasteboard_change_count();
  if (now > clipboardLastSeenChangeCount) {
    clipboardLastSeenChangeCount = now;
    pullSystemClipboardIntoBrltty();
  }
  /* Re-arm. asyncResetAlarmIn keeps the same handle alive. */
  asyncResetAlarmIn(clipboardPollAlarm, CLIPBOARD_POLL_INTERVAL_MS);
}

/* brltty core fires this when its internal clipboard mutates (typically
 * after BRL_CMD_COPY / BRL_CMD_APND). Push the content to NSPasteboard
 * so Cmd+V outside brltty picks it up. */
static
REPORT_LISTENER(moClipboardReportListener) {
  if (parameters->reportIdentifier != REPORT_API_PARAMETER_UPDATED) return;
  const ApiParameterUpdatedReport *report = parameters->reportData;
  if (!report || report->parameter != BRLAPI_PARAM_CLIPBOARD_CONTENT) return;
  if (suppressOwnClipboardReport) return;

  char *content = getMainClipboardContent();
  if (!content) return;
  if (*content) {
    clipboardLastSeenChangeCount = ax_pasteboard_set(content);
  }
  free(content);
}

static void
ensureBufferCapacity(int rows, int cols) {
  int need = rows * cols;
  if (need <= screenCapacity) return;

  wchar_t *newBuffer = realloc(screenBuffer, need * sizeof(wchar_t));
  if (!newBuffer) {
    logMessage(LOG_WARNING, "mo: failed to grow screen buffer to %d cells", need);
    return;
  }
  screenBuffer = newBuffer;
  screenCapacity = need;
}

static void
fillScreenWithSpaces(void) {
  for (int i = 0; i < screenRows * screenCols; i += 1) {
    screenBuffer[i] = L' ';
  }
}

// Decode one UTF-8 sequence starting at *p (must point inside a buffer of at
// least `remaining` bytes). Returns the number of bytes consumed and writes
// the codepoint into *out. Invalid sequences yield U+FFFD and consume one
// byte so we always make progress.
static int
decodeUtf8(const unsigned char *p, size_t remaining, uint32_t *out) {
  if (remaining == 0) { *out = 0; return 0; }
  unsigned char c = p[0];
  if (c < 0x80) { *out = c; return 1; }
  if ((c & 0xE0) == 0xC0 && remaining >= 2
      && (p[1] & 0xC0) == 0x80) {
    *out = ((c & 0x1F) << 6) | (p[1] & 0x3F);
    return 2;
  }
  if ((c & 0xF0) == 0xE0 && remaining >= 3
      && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
    *out = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return 3;
  }
  if ((c & 0xF8) == 0xF0 && remaining >= 4
      && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80
      && (p[3] & 0xC0) == 0x80) {
    *out = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12)
         | ((p[2] & 0x3F) << 6) |  (p[3] & 0x3F);
    return 4;
  }
  *out = 0xFFFD;
  return 1;
}

// First pass: scan the source text (UTF-8) to discover the actual grid
// geometry it needs (longest line in codepoints, line count). Returns rows
// and cols clamped to maxima.
static void
measureText(const char *text, int *outRows, int *outCols) {
  int rows = 1;
  int cols = 0;
  int curCol = 0;
  if (!text) { *outRows = SCREEN_MIN_ROWS; *outCols = SCREEN_MIN_COLUMNS; return; }

  size_t len = strlen(text);
  const unsigned char *p = (const unsigned char *)text;
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    int n = decodeUtf8(p + i, len - i, &cp);
    if (n <= 0) break;
    i += n;
    if (cp == '\n' || cp == '\r') {
      if (curCol > cols) cols = curCol;
      rows += 1;
      curCol = 0;
    } else {
      curCol += 1;
    }
  }
  if (curCol > cols) cols = curCol;

  if (cols < SCREEN_MIN_COLUMNS) cols = SCREEN_MIN_COLUMNS;
  if (rows < SCREEN_MIN_ROWS) rows = SCREEN_MIN_ROWS;
  if (cols > SCREEN_MAX_COLUMNS) cols = SCREEN_MAX_COLUMNS;
  if (rows > SCREEN_MAX_ROWS) rows = SCREEN_MAX_ROWS;

  *outRows = rows;
  *outCols = cols;
}

// Second pass: paint the source text (UTF-8) into the (already sized)
// buffer as Unicode codepoints, so brltty's text table can translate
// accented characters correctly.
static void
renderTextIntoGrid(const char *text) {
  fillScreenWithSpaces();
  for (int r = 0; r < SCREEN_MAX_ROWS; r += 1) rowEndCol[r] = 0;
  if (!text || !*text) return;

  int row = 0;
  int col = 0;
  size_t len = strlen(text);
  const unsigned char *p = (const unsigned char *)text;
  size_t i = 0;
  while (i < len && row < screenRows) {
    uint32_t cp;
    int n = decodeUtf8(p + i, len - i, &cp);
    if (n <= 0) break;
    i += n;

    if (cp == '\n' || cp == '\r') {
      row += 1;
      col = 0;
      continue;
    }
    if (cp == '\t') cp = ' ';
    // Replace non-printable ASCII controls; keep all other codepoints (they
    // map to printable Unicode that the text table handles).
    if (cp < 0x20 || cp == 0x7f) cp = '?';

    if (col >= screenCols) {
      row += 1;
      col = 0;
      if (row >= screenRows) break;
    }
    screenBuffer[row * screenCols + col] = (wchar_t)cp;
    col += 1;
    if (row < SCREEN_MAX_ROWS && col > rowEndCol[row]) rowEndCol[row] = col;
  }
}

static int
construct_MacOSAccessibilityScreen(void) {
  logMessage(LOG_DEBUG, "mo: construct: entered");
  int trusted = ax_request_trust();
  logMessage(LOG_DEBUG, "mo: construct: ax_request_trust=%d", trusted);
  if (!trusted) {
    logMessage(LOG_WARNING,
      "macOS Accessibility permission not granted. "
      "Approve brltty in System Settings -> Privacy & Security -> Accessibility, "
      "then restart brltty.");
  }
  cursorRow = 0;
  cursorCol = 0;
  lastFingerprint[0] = '\0';

  screenRows = SCREEN_MIN_ROWS;
  screenCols = SCREEN_MIN_COLUMNS;
  ensureBufferCapacity(screenRows, screenCols);
  if (screenBuffer) fillScreenWithSpaces();
  logMessage(LOG_DEBUG, "mo: construct: buffer ready");

  const char *initial = trusted
    ? "brltty: macOS Accessibility ready"
    : "brltty: waiting for Accessibility permission";
  renderTextIntoGrid(initial);
  logMessage(LOG_DEBUG, "mo: construct: rendered initial");

  wakeFd = ax_observer_start();
  logMessage(LOG_DEBUG, "mo: construct: ax_observer_start=%d", wakeFd);
  if (wakeFd >= 0) {
    if (!asyncMonitorFileInput(&wakeMonitor, wakeFd, handleWakeFromObserver, NULL)) {
      logMessage(LOG_WARNING, "mo: failed to register wake monitor");
      wakeMonitor = NULL;
    }
  }

  /* Clipboard bridge: prime the changeCount so we don't mistake the
   * initial pasteboard state for an external change, then arm the
   * periodic poll + the brltty-side listener. */
  clipboardLastSeenChangeCount = ax_pasteboard_change_count();
  if (!asyncNewRelativeAlarm(&clipboardPollAlarm,
                             CLIPBOARD_POLL_INTERVAL_MS,
                             clipboardPollAlarmCallback, NULL)) {
    logMessage(LOG_WARNING, "mo: failed to arm clipboard poll alarm");
    clipboardPollAlarm = NULL;
  }
  clipboardReportListener = registerReportListener(
      REPORT_API_PARAMETER_UPDATED, moClipboardReportListener, NULL);
  if (!clipboardReportListener) {
    logMessage(LOG_WARNING, "mo: failed to register clipboard listener");
  } else {
    logMessage(LOG_DEBUG,
      "mo: construct: clipboard bridge armed (poll=%d ms, initial cc=%ld)",
      CLIPBOARD_POLL_INTERVAL_MS, clipboardLastSeenChangeCount);
  }

  logMessage(LOG_DEBUG, "mo: construct: returning success");
  return 1;
}

static void
destruct_MacOSAccessibilityScreen(void) {
  if (clipboardReportListener) {
    unregisterReportListener(clipboardReportListener);
    clipboardReportListener = NULL;
  }
  if (clipboardPollAlarm) {
    asyncCancelRequest(clipboardPollAlarm);
    clipboardPollAlarm = NULL;
  }
  if (wakeMonitor) {
    asyncCancelRequest(wakeMonitor);
    wakeMonitor = NULL;
  }
  ax_observer_stop();
  wakeFd = -1;
  free(screenBuffer);
  screenBuffer = NULL;
  screenCapacity = 0;
}

/* macOS AX delivers per-attribute notifications independently:
 * AXSelectedTextChanged (caret moved) and AXValueChanged (text content
 * changed) can fire in either order for a single keystroke. When the
 * caret notification arrives first we snapshot a transient state where
 *   - kAXSelectedTextRangeAttribute reports the *new* caret offset, but
 *   - AXStringForRange(visibleRange) still returns the *old* text.
 * The cursor then lands at a column where there's no rendered text
 * yet — visible on the braille line as "caret at right position but
 * the surrounding text is missing".
 *
 * We can't synchronise those two reads on macOS, so instead we detect
 * the inconsistency after rendering and arm a few short retries. The
 * follow-up AXValueChanged / AXLayoutChanged fires within tens of ms
 * and the next refresh sees a consistent state. */
static int pendingResettleRefreshes = 0;
#define AX_MAX_RESETTLE_RETRIES 3

/* Returns 1 when the cursor sits past where the actual rendered text
 * ended on its row — i.e. somewhere inside the fillScreenWithSpaces
 * padding rather than against real content. That gap is the
 * signature of the AX caret/value race: AX reports a caret offset
 * that no longer matches the (possibly-shrunk) text we just
 * rendered.
 *
 * rowEndCol[cursorRow] is the col one past the last rendered
 * character of the row's true text (including any trailing spaces
 * the user actually typed). Cursor at that exact column is the
 * legitimate "ready for next character" position. Anything strictly
 * greater means the caret outran the text — clamp.
 *
 * The caller gets back the row's rendered length via *outRowEnd so
 * it can compare across cycles for stability tracking. */
static int
isCursorBeyondRowContent(int *outRowEnd) {
  int rowEnd = (cursorRow >= 0 && cursorRow < SCREEN_MAX_ROWS) ? rowEndCol[cursorRow] : 0;
  if (outRowEnd) *outRowEnd = rowEnd;
  if (!screenBuffer || cursorRow < 0 || cursorRow >= screenRows) return 0;
  if (cursorCol <= 0) return 0;
  return cursorCol > rowEnd;
}

/* Remember the last suspect state we armed retries for. If the next
 * detection comes back with exactly the same (row, col, rowEnd)
 * after a full retry cycle, the state is stable — not the transient
 * AX race we were trying to ride out. Stop re-arming so we don't
 * thrash the poll loop forever. */
static int lastResettleArmRow = -1;
static int lastResettleArmCol = -1;
static int lastResettleArmRowEnd = INT32_MIN;

static int
poll_MacOSAccessibilityScreen(void) {
  // The AXObserver thread sets a dirty flag whenever macOS pushes us a
  // notification. We trust that signal first because brltty's own update
  // cadence is too slow for typing.
  if (ax_consume_dirty()) {
    mo_log("poll: dirty (from AX observer)");
    pendingResettleRefreshes = 0;
    return 1;
  }

  if (pendingResettleRefreshes > 0) {
    pendingResettleRefreshes -= 1;
    mo_log("poll: resettle retry (remaining=%d)", pendingResettleRefreshes);
    return 1;
  }

  // Belt-and-braces: also recompute the fingerprint here in case the
  // observer missed a change.
  char fp[2048];
  ax_fingerprint(fp, sizeof(fp));
  if (strcmp(fp, lastFingerprint) != 0) {
    mo_log("poll: fp changed");
    strncpy(lastFingerprint, fp, sizeof(lastFingerprint) - 1);
    lastFingerprint[sizeof(lastFingerprint) - 1] = '\0';
    return 1;
  }
  return 0;
}

static int
refresh_MacOSAccessibilityScreen(void) {
  static char buf[131072];  // 128 KB; AX visible ranges fit here easily.
  int row = 0;
  int col = 0;
  ax_snapshot_lines(buf, sizeof(buf), &row, &col);

  int newRows, newCols;
  measureText(buf, &newRows, &newCols);

  screenRows = newRows;
  screenCols = newCols;
  ensureBufferCapacity(newRows, newCols);
  if (!screenBuffer) return 0;

  renderTextIntoGrid(buf);

  if (row < 0) row = 0;
  if (row >= screenRows) row = screenRows - 1;
  if (col < 0) col = 0;
  if (col >= screenCols) col = screenCols - 1;
  cursorRow = row;
  cursorCol = col;

  /* If the cursor landed past the visible text on its row, the app
   * probably hasn't finished publishing the new content yet (the
   * AXSelectedTextChanged/AXValueChanged race — see the comment near
   * pendingResettleRefreshes).
   *
   * For the typing-forward shape of that race the next AX event tends
   * to fire within tens of ms and the resettle catches it. For the
   * backspace shape, though, Terminal.app frequently keeps the AX
   * caret offset stuck at the pre-backspace position even after the
   * value has shrunk — so col reads e.g. 4 when the rendered last
   * line stops at col 3. resettle alone is useless because AX never
   * updates. Clamp the visible cursor column to (lastNonSpace + 1)
   * so the user actually sees the caret retreat, then ALSO arm the
   * resettle so a slower AX catch-up still wins if it comes. */
  int rowEnd = 0;
  if (isCursorBeyondRowContent(&rowEnd)) {
    int clampedCol = rowEnd;
    if (clampedCol < 0) clampedCol = 0;
    if (clampedCol >= screenCols) clampedCol = screenCols - 1;

    int sameAsLastArm = (cursorRow == lastResettleArmRow
                         && cursorCol == lastResettleArmCol
                         && rowEnd == lastResettleArmRowEnd);
    if (pendingResettleRefreshes == 0 && !sameAsLastArm) {
      pendingResettleRefreshes = AX_MAX_RESETTLE_RETRIES;
      lastResettleArmRow = cursorRow;
      lastResettleArmCol = cursorCol;
      lastResettleArmRowEnd = rowEnd;
      mo_log("cursor beyond row content — clamp col %d->%d, arming resettle (row=%d rowEnd=%d)",
             cursorCol, clampedCol, cursorRow, rowEnd);
    }
    cursorCol = clampedCol;
  } else {
    /* State no longer suspect — clear the markers so a future transient
     * race on the same row/col can be armed again. */
    lastResettleArmRow = -1;
    lastResettleArmCol = -1;
    lastResettleArmRowEnd = INT32_MIN;
  }
  return 1;
}

// Bundle ids we can read meaningfully through our AX heuristics
// (mostly text-area focus + AXVisibleCharacterRange). Anything not
// in this list falls through to the "screen not in a terminal app"
// message rather than getting a stale snapshot or a garbled GUI
// rendering. Add to the list when a new terminal emulator is
// verified to expose the AX shape we already handle in ax_bridge.
static const char *const SUPPORTED_BUNDLE_IDS[] = {
    "com.apple.Terminal",          // Apple Terminal.app
    "com.googlecode.iterm2",       // iTerm2
    "co.zeit.hyper",               // Hyper
    "net.kovidgoyal.kitty",        // Kitty
    "io.alacritty",                // Alacritty
    "com.github.wez.wezterm",      // WezTerm
};

static int
isSupportedBundle(const char *bundleId) {
  if (!bundleId || !*bundleId) return 0;
  for (size_t i = 0; i < sizeof SUPPORTED_BUNDLE_IDS / sizeof *SUPPORTED_BUNDLE_IDS; i++) {
    if (strcmp(bundleId, SUPPORTED_BUNDLE_IDS[i]) == 0) return 1;
  }
  return 0;
}

// Displayed on the braille line whenever the frontmost macOS app is
// not one we know how to read via AX. Padded to a fixed 40-cell width
// so we present a real, deterministic 1x40 screen to brltty rather
// than going through the desc->unreadable mechanism — the latter
// didn't reliably repaint over the previous (much larger) terminal
// screen and the user kept seeing stale Terminal content.
#define UNREADABLE_NOT_TERMINAL "screen not in a terminal app"
#define UNREADABLE_SCREEN_COLS  40
#define UNREADABLE_SCREEN_ROWS  1

// Forward decl so describe() can call into the brlapi scope hash
// without reordering the whole file. The definition stays alongside
// switchVirtualTerminal further down.
static int currentVirtualTerminal_MacOSAccessibilityScreen(void);

static void
describe_MacOSAccessibilityScreen(ScreenDescription *desc) {
  char bundle[256];
  size_t bn = ax_frontmost_bundle_id(bundle, sizeof bundle);

  // Log bundle transitions so we can see exactly when describe() flips
  // between terminal and unreadable modes — useful for chasing
  // "stuck on stale state" symptoms after Cmd+Tab.
  static char lastBundleSeen[256] = {0};
  if (strncmp(bundle, lastBundleSeen, sizeof(lastBundleSeen)) != 0) {
    mo_log("describe: bundle=%s (len=%zu, supported=%d)",
           bn > 0 ? bundle : "(none)", bn,
           bn > 0 ? isSupportedBundle(bundle) : 0);
    strncpy(lastBundleSeen, bundle, sizeof(lastBundleSeen) - 1);
    lastBundleSeen[sizeof(lastBundleSeen) - 1] = '\0';
  }

  // Keep currentVirtualTerminal in sync (it also memoises lastReportedScope
  // for switchVirtualTerminal's prev/next detection) even though we
  // expose the tab index to brltty as desc->number for human display.
  (void)currentVirtualTerminal_MacOSAccessibilityScreen();

  int axIndex = 0, axCount = 0;
  desc->number = ax_get_active_tab(&axIndex, &axCount) ? axIndex : 1;
  desc->hasSelection = 0;

  if (bn == 0 || !isSupportedBundle(bundle)) {
    // Present a deterministic 1x40 screen containing the message,
    // padded with spaces. We don't go through desc->unreadable because
    // brltty's core seems to retain the previous frame's geometry when
    // unreadable is set — the user kept reading stale terminal content
    // on Cmd+Tab. By advertising a real, smaller screen with explicit
    // dimensions, we force the braille line to redraw against our
    // buffer.
    desc->unreadable = NULL;
    desc->cols = UNREADABLE_SCREEN_COLS;
    desc->rows = UNREADABLE_SCREEN_ROWS;
    desc->posx = 0;
    desc->posy = 0;
    desc->hasCursor = 0;
    desc->quality = SCQ_POOR;
    return;
  }

  desc->cols = screenCols;
  desc->rows = screenRows;
  desc->posx = cursorCol;
  desc->posy = cursorRow;
  desc->hasCursor = 1;
  desc->quality = SCQ_FAIR;
}

static int
readCharacters_MacOSAccessibilityScreen(const ScreenBox *box, ScreenCharacter *buffer) {
  char bundle[256];
  size_t bn = ax_frontmost_bundle_id(bundle, sizeof bundle);

  if (bn == 0 || !isSupportedBundle(bundle)) {
    // describe() advertised a 1x40 screen with the unreadable message.
    // Fill the requested box from a 40-cell buffer containing the
    // message left-justified and space-padded. validateScreenBox bounds
    // box against the dimensions we promised in describe().
    if (!validateScreenBox(box, UNREADABLE_SCREEN_COLS, UNREADABLE_SCREEN_ROWS)) return 0;
    const char *msg = UNREADABLE_NOT_TERMINAL;
    size_t mlen = strlen(msg);
    for (int row = 0; row < box->height; row += 1) {
      for (int col = 0; col < box->width; col += 1) {
        int srcCol = box->left + col;
        ScreenCharacter *target = &buffer[(row * box->width) + col];
        target->text = (srcCol < (int)mlen) ? (wchar_t)(unsigned char)msg[srcCol] : L' ';
        target->color.vgaAttributes = 0x07;
        target->color.foreground = (RGBColor){255, 255, 255};
        target->color.background = (RGBColor){0, 0, 0};
      }
    }
    return 1;
  }

  if (!validateScreenBox(box, screenCols, screenRows)) return 0;
  if (!screenBuffer) return 0;
  for (int row = 0; row < box->height; row += 1) {
    for (int col = 0; col < box->width; col += 1) {
      ScreenCharacter *target = &buffer[(row * box->width) + col];
      target->text = screenBuffer[(box->top + row) * screenCols + (box->left + col)];
      target->color.vgaAttributes = 0x07;
      target->color.foreground = (RGBColor){255, 255, 255};
      target->color.background = (RGBColor){0, 0, 0};
    }
  }
  return 1;
}

/* macOS has no real virtual terminals; brltty's VT concept is split
 * into two distinct roles here:
 *
 *   currentVirtualTerminal() -> a per-(app, window) BrlAPI scope, with
 *                               *no* tab index folded in. Stable as long
 *                               as the user stays in the same window;
 *                               changes only when the frontmost app or
 *                               window changes. Computed by ax_bridge
 *                               from public information so BrlAPI
 *                               clients can independently reproduce it.
 *
 *   switchVirtualTerminal(vt) -> drives tab navigation. The driver
 *                               recognises three intents from the input:
 *                                 vt == lastReportedScope + 1 -> NEXT tab
 *                                 vt == lastReportedScope - 1 -> PREV tab
 *                                 1..9                        -> tab N
 *                               AX is the source of truth for the
 *                               current tab and the total count, so
 *                               next/prev correctly clamp at the edges
 *                               and absolute jumps reject out-of-range.
 *
 *   desc->number -> the human-meaningful tab index (1..N from AX), set
 *                  in describe(). Decoupled from the BrlAPI scope so
 *                  brltty's "current vt" announcements show "tab 3"
 *                  rather than a 31-bit hash.
 */

/* Last scope value handed to brltty's core. brltty's relative-motion
 * dispatch computes `currentVirtualTerminal() + 1` (and -1) and feeds
 * the result back to switchVirtualTerminal — we compare against this
 * to recognise NEXT/PREV intent without needing the scope int to
 * encode the tab number. */
static int lastReportedScope = 0;

static int
currentVirtualTerminal_MacOSAccessibilityScreen(void) {
  uint32_t scope = 0;
  if (!ax_get_frontmost_scope(&scope)) {
    // No frontmost app / window — let brlapi broadcast to every client.
    lastReportedScope = SCR_NO_VT;
    return SCR_NO_VT;
  }
  // High bit is guaranteed 0 by the encoding (see ax_bridge.h), so the
  // int cast can't collide with SCR_NO_VT = -1.
  lastReportedScope = (int)scope;
  return lastReportedScope;
}

static int
switchVirtualTerminal_MacOSAccessibilityScreen(int vt) {
  int axCurrent = 0, axCount = 0;
  int haveAx = ax_get_active_tab(&axCurrent, &axCount);

  // SWITCHVT N (absolute, small positive int): the user explicitly
  // asked for a numbered tab. Checked first because it's the most
  // specific intent — VTPREV/VTNEXT will never produce a value in
  // 1..9 under normal scope encoding (scopes are 31-bit hashes,
  // collision with this range is ~5e-9).
  if (vt >= 1 && vt <= 9) {
    if (haveAx && vt > axCount) return 0;
    return ax_post_shortcut_tab_index(vt);
  }

  // VTNEXT: brltty asked for one past whatever we last reported.
  if (vt == lastReportedScope + 1) {
    if (haveAx && axCurrent >= axCount) return 0;  // already on last tab
    return ax_post_shortcut_tab_next();
  }

  // VTPREV: brltty asked for one before whatever we last reported.
  if (vt == lastReportedScope - 1) {
    if (haveAx && axCurrent <= 1) return 0;        // already on first tab
    return ax_post_shortcut_tab_prev();
  }

  return 0;
}

static int
insertKey_MacOSAccessibilityScreen(ScreenKey key) {
  // brltty's ScreenKey is a uint32 the bridge already knows how to decode.
  if (!ax_post_key((uint32_t)key)) {
    logMessage(LOG_WARNING, "mo: failed to inject key 0x%08X", (unsigned)key);
    return 0;
  }
  return 1;
}

static void
scr_initialize(MainScreen *main) {
  initializeRealScreen(main);
  main->base.poll = poll_MacOSAccessibilityScreen;
  main->base.refresh = refresh_MacOSAccessibilityScreen;
  main->base.describe = describe_MacOSAccessibilityScreen;
  main->base.readCharacters = readCharacters_MacOSAccessibilityScreen;
  main->base.insertKey = insertKey_MacOSAccessibilityScreen;
  main->base.currentVirtualTerminal = currentVirtualTerminal_MacOSAccessibilityScreen;
  main->base.switchVirtualTerminal = switchVirtualTerminal_MacOSAccessibilityScreen;
  main->construct = construct_MacOSAccessibilityScreen;
  main->destruct = destruct_MacOSAccessibilityScreen;
}
