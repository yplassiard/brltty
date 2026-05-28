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

/* InsideVision/braille.c - InsideVision InsideONE braille display support.
 *
 * Native, direct-HID driver: it talks to the hardware itself, with no vendor
 * Core daemon. It drives two independent USB HID devices:
 *
 *  - the KGS braille module (output only) for the 32 cells, the blinking
 *    cursor and the sleep / backlight state;
 *  - the multitouch digitizer for all input (routing cursor, side sliders,
 *    power button, screen gestures, braille keyboard).
 *
 * Author: Yannick Plassiard <plassiardyannick@gmail.com>
 */

#include "prologue.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "log.h"

typedef enum {
  PARM_TOUCHVENDOR,
  PARM_TOUCHPRODUCT
} DriverParameter;
#define BRLPARMS "touchvendor", "touchproduct"

#include "brl_driver.h"
#include "io_hid.h"
#include "brldefs-iv.h"

/* Until the digitizer's identifiers are confirmed on real hardware, they are
 * supplied through the "touchvendor" / "touchproduct" driver parameters (hex).
 * Leaving them at 0 simply disables touch input; the braille output still
 * works.
 */
#define IV_TOUCH_VENDOR_DEFAULT  0
#define IV_TOUCH_PRODUCT_DEFAULT 0

struct BrailleDataStruct {
  HidDevice *brailleDevice;
  HidDevice *touchDevice;

  struct {
    unsigned char cells[IV_CELL_COUNT];
    unsigned char rewrite;
  } text;

  struct {
    unsigned char backlight;
    unsigned char sleep;
  } state;

  /* Tracking of the single primary contact, used to turn a press/release into
   * a routing tap or a directional slide. The full multi-finger gesture engine
   * (ported from the vendor's TouchLib) is still to be written.
   */
  struct {
    int active;
    IV_Zone zone;
    uint16_t startX, startY;
  } touch;
};

/* --- braille module (output) ---------------------------------------------- */

static int
writeCells (BrailleDisplay *brl) {
  unsigned char report[1 + IV_CELL_COUNT];

  report[0] = IV_RPT_CELLS;
  translateOutputCells(&report[1], brl->data->text.cells, IV_CELL_COUNT);

  /* NOTE: the device's output report length is reported by its HID descriptor;
   * confirm on hardware whether trailing padding to a larger fixed size is
   * required (e.g. 64 bytes). For now we send id + 32 cells. */
  return hidSetReport(brl->data->brailleDevice, report, sizeof(report)) != -1;
}

static int
writeSwitch (BrailleDisplay *brl) {
  unsigned char report[IV_SWITCH_SIZE];

  memset(report, 0, sizeof(report));
  report[IV_SWITCH_REPORT_ID] = IV_RPT_SWITCH;
  report[IV_SWITCH_SCREEN] = brl->data->state.backlight? 1: 0;
  report[IV_SWITCH_VEILLE] = brl->data->state.sleep? 1: 0;

  return hidSetReport(brl->data->brailleDevice, report, sizeof(report)) != -1;
}

static int
writeBlink (BrailleDisplay *brl, unsigned char cell) {
  /* cell: 1..32 to blink dots 7-8 of that cell, 0 to disable. */
  unsigned char report[] = {IV_RPT_BLINK, cell};
  return hidSetReport(brl->data->brailleDevice, report, sizeof(report)) != -1;
}

static void
logDeviceIdentity (BrailleDisplay *brl) {
  unsigned char buffer[1 + IV_GUID_SIZE];

  memset(buffer, 0, sizeof(buffer));
  buffer[0] = IV_RPT_GUID;

  ssize_t result = hidGetFeature(brl->data->brailleDevice, buffer, sizeof(buffer));
  if (result > 1) {
    char serial[IV_GUID_SIZE + 1];
    size_t length = MIN((size_t)result - 1, IV_GUID_SIZE);

    memcpy(serial, &buffer[1], length);
    serial[length] = 0;
    logMessage(LOG_INFO, "InsideONE serial number: %s", serial);
  }
}

/* --- touch digitizer (input) ---------------------------------------------- */

static HidDevice *
openTouchDevice (uint16_t vendor, uint16_t product) {
  if (!vendor && !product) return NULL;

  HidUSBFilter filter;
  hidInitializeUSBFilter(&filter);
  filter.common.vendorIdentifier = vendor;
  filter.common.productIdentifier = product;

  return hidOpenUSBDevice(&filter);
}

static IV_Zone
classifyZone (uint16_t x, uint16_t y) {
  static const IV_ZoneRect zones[] = { IV_ZONE_TABLE };

  for (unsigned int i=0; i<ARRAY_COUNT(zones); i+=1) {
    const IV_ZoneRect *r = &zones[i];

    if ((x >= r->xMin) && (x <= r->xMax) &&
        (y >= r->yMin) && (y <= r->yMax)) {
      return r->zone;
    }
  }

  return IV_ZONE_NONE;
}

/* Map an x coordinate inside the routing strip onto a cell index (0..31). */
static int
routingCell (uint16_t x) {
  static const IV_ZoneRect zones[] = { IV_ZONE_TABLE };
  const IV_ZoneRect *r = NULL;

  for (unsigned int i=0; i<ARRAY_COUNT(zones); i+=1) {
    if (zones[i].zone == IV_ZONE_ROUTING) { r = &zones[i]; break; }
  }
  if (!r || (r->xMax <= r->xMin)) return -1;
  if (x < r->xMin) x = r->xMin;
  if (x > r->xMax) x = r->xMax;

  int index = (int)(x - r->xMin) * IV_CELL_COUNT / (int)(r->xMax - r->xMin + 1);
  if (index < 0) index = 0;
  if (index >= IV_CELL_COUNT) index = IV_CELL_COUNT - 1;
  return index;
}

/* A contact is "down" when its tip switch (low bit of "reserved") is set and it
 * carries real coordinates. TODO: confirm the exact contact id / state encoding
 * against real hardware (see the vendor's TouchHelper sources). */
static int
contactIsDown (const IV_TouchPoint *p) {
  if (!p->x && !p->y && !p->z) return 0;
  return (p->reserved & 0X1) != 0;
}

/* Turn one digitizer report into at most one BRLTTY command. Only the primary
 * contact is considered, and only single-finger press/slide are recognised for
 * now. Returns a BRL_CMD_* value, or EOF when nothing actionable happened. */
static int
processTouchReport (BrailleDisplay *brl, const unsigned char *data, size_t size) {
  /* The report begins with its id; the contacts follow. */
  if (size < (1 + sizeof(IV_TouchPoint))) return EOF;
  const IV_TouchReport *report = (const IV_TouchReport *)data;

  /* Pick the primary (first present) contact. */
  const IV_TouchPoint *primary = NULL;
  for (unsigned int i=0; i<IV_TOUCH_MAX_POINTS; i+=1) {
    const IV_TouchPoint *p = &report->points[i];
    if (p->x || p->y || p->z) { primary = p; break; }
  }

  if (!primary || !contactIsDown(primary)) {
    /* Release: emit the gesture accumulated since the press. */
    if (brl->data->touch.active) {
      brl->data->touch.active = 0;
      /* A release in the routing strip routes the cursor to the touched cell.
       * Other zones currently only react to slides, handled on motion. */
    }
    return EOF;
  }

  IV_Zone zone = classifyZone(primary->x, primary->y);

  if (!brl->data->touch.active) {
    /* Press: remember where it started. */
    brl->data->touch.active = 1;
    brl->data->touch.zone = zone;
    brl->data->touch.startX = primary->x;
    brl->data->touch.startY = primary->y;

    if (zone == IV_ZONE_ROUTING) {
      int cell = routingCell(primary->x);
      if (cell >= 0) return BRL_CMD_BLK(ROUTE) + cell;
    }
    return EOF;
  }

  /* Motion since press: classify a single-finger directional slide. */
  int dx = (int)primary->x - (int)brl->data->touch.startX;
  int dy = (int)primary->y - (int)brl->data->touch.startY;
  const int threshold = 600;

  if ((abs(dx) < threshold) && (abs(dy) < threshold)) return EOF;

  /* Consume the slide so it fires once. */
  brl->data->touch.startX = primary->x;
  brl->data->touch.startY = primary->y;

  int horizontal = abs(dx) >= abs(dy);
  switch (brl->data->touch.zone) {
    case IV_ZONE_RIGHT_SLIDER:
      if (horizontal) return (dx > 0)? BRL_CMD_FWINRT: BRL_CMD_FWINLT;
      return (dy > 0)? BRL_CMD_LNDN: BRL_CMD_LNUP;

    case IV_ZONE_LEFT_SLIDER:
    case IV_ZONE_ROUTING:
      if (horizontal) return (dx > 0)? BRL_CMD_FWINRT: BRL_CMD_FWINLT;
      return EOF;

    case IV_ZONE_SCREEN:
      /* TODO: full screen gesture set (taps, multi-finger, rotor, braille
       * keyboard) — to be ported from the vendor TouchLib once validated on
       * the dev kit. */
      return EOF;

    default:
      return EOF;
  }
}

/* --- driver entry points -------------------------------------------------- */

static int
brl_construct (BrailleDisplay *brl, char **parameters, const char *device) {
  if (!(brl->data = malloc(sizeof(*brl->data)))) {
    logMallocError();
    return 0;
  }
  memset(brl->data, 0, sizeof(*brl->data));

  HidUSBFilter filter;
  hidInitializeUSBFilter(&filter);
  filter.common.vendorIdentifier = IV_BRAILLE_VENDOR;
  filter.common.productIdentifier = IV_BRAILLE_PRODUCT;

  if (!(brl->data->brailleDevice = hidOpenUSBDevice(&filter))) {
    logMessage(LOG_ERR, "InsideONE braille module not found (%04X:%04X)",
               IV_BRAILLE_VENDOR, IV_BRAILLE_PRODUCT);
    free(brl->data);
    brl->data = NULL;
    return 0;
  }

  brl->textColumns = IV_CELL_COUNT;
  brl->textRows = 1;
  makeOutputTable(dotsTable_ISO11548_1);

  logDeviceIdentity(brl);

  brl->data->state.backlight = 1;
  brl->data->state.sleep = 0;
  writeSwitch(brl);
  writeBlink(brl, 0);

  {
    uint16_t vendor = IV_TOUCH_VENDOR_DEFAULT;
    uint16_t product = IV_TOUCH_PRODUCT_DEFAULT;
    const char *vendorParm = parameters[PARM_TOUCHVENDOR];
    const char *productParm = parameters[PARM_TOUCHPRODUCT];

    if (vendorParm && *vendorParm) vendor = (uint16_t)strtol(vendorParm, NULL, 16);
    if (productParm && *productParm) product = (uint16_t)strtol(productParm, NULL, 16);

    if ((brl->data->touchDevice = openTouchDevice(vendor, product))) {
      logMessage(LOG_INFO, "InsideONE touch digitizer opened (%04X:%04X)",
                 vendor, product);
    } else {
      logMessage(LOG_WARNING,
                 "InsideONE touch digitizer unavailable; input disabled."
                 " Set the touchvendor/touchproduct parameters.");
    }
  }

  brl->data->text.rewrite = 1;
  return 1;
}

static void
brl_destruct (BrailleDisplay *brl) {
  if (brl->data) {
    if (brl->data->brailleDevice) {
      brl->data->state.sleep = 1;
      writeSwitch(brl);
      hidCloseDevice(brl->data->brailleDevice);
    }
    if (brl->data->touchDevice) hidCloseDevice(brl->data->touchDevice);
    free(brl->data);
    brl->data = NULL;
  }
}

static int
brl_writeWindow (BrailleDisplay *brl, const wchar_t *text) {
  if (cellsHaveChanged(brl->data->text.cells, brl->buffer, brl->textColumns,
                       NULL, NULL, &brl->data->text.rewrite)) {
    if (!writeCells(brl)) return 0;
  }

  return 1;
}

static int
brl_readCommand (BrailleDisplay *brl, KeyTableCommandContext context) {
  if (!brl->data->touchDevice) return EOF;

  unsigned char buffer[64];
  ssize_t size = hidReadData(brl->data->touchDevice, buffer, sizeof(buffer), 0, 0);

  if (size <= 0) return EOF;
  return processTouchReport(brl, buffer, (size_t)size);
}
