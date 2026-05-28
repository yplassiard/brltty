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

/* InsideVision InsideONE - device-specific definitions.
 *
 * The InsideONE exposes two independent USB HID devices:
 *
 *  1. The KGS braille module (Silicon Labs HID, VID 0x10C4 / PID 0x82CD,
 *     C8051F381 MCU). Output + state only; it reports no keys. Protocol
 *     reverse-engineered from the vendor's KGSDLL.dll.
 *
 *  2. A multitouch digitizer that carries every user input: the routing
 *     cursor, the two side sliders, the power button, screen gestures and
 *     the braille keyboard. Its raw report layout is known; its VID/PID is
 *     supplied at runtime via driver parameters until confirmed on hardware.
 */

#ifndef BRLTTY_INCLUDED_IV_BRLDEFS
#define BRLTTY_INCLUDED_IV_BRLDEFS

#include <stdint.h>

/* --- Braille module (KGS over USB HID) ------------------------------------ */

#define IV_BRAILLE_VENDOR  0X10C4
#define IV_BRAILLE_PRODUCT 0X82CD

#define IV_CELL_COUNT 32
#define IV_GUID_SIZE  36

/* Output/feature report identifiers (report[0]). */
typedef enum {
  IV_RPT_CELLS  = 0X01, /* [0x01][cell0..cell31]                              */
  IV_RPT_SWITCH = 0X02, /* [0x02][screen,veille,sens,tempo,pot,vib1..vib4]    */
  IV_RPT_BLINK  = 0X03, /* [0x03][cursorCellPosition 1..32, 0=off]            */
  IV_RPT_GUID   = 0X05, /* feature in: 36-byte serial/GUID                    */
} IV_ReportIdentifier;

/* Byte offsets inside the SWITCH (0x02) output report, report id included. */
typedef enum {
  IV_SWITCH_REPORT_ID = 0,
  IV_SWITCH_SCREEN,     /* backlight on/off (1/0)        */
  IV_SWITCH_VEILLE,     /* sleep: all dots down (1/0)    */
  IV_SWITCH_SENS,       /* orientation                   */
  IV_SWITCH_TEMPO,
  IV_SWITCH_POT10K,
  IV_SWITCH_VIBREUR1,
  IV_SWITCH_VIBREUR2,
  IV_SWITCH_VIBREUR3,
  IV_SWITCH_VIBREUR4,
  IV_SWITCH_SIZE        /* payload length                */
} IV_SwitchOffset;

/* --- Touch digitizer ------------------------------------------------------ */

#define IV_TOUCH_MAX_POINTS 5

/* Logical coordinate span of the digitizer (from TouchLibSettings.xml). */
#define IV_TOUCH_X_MAX 11263
#define IV_TOUCH_Y_MAX 7167

/* One contact, as delivered by the digitizer. Byte-packed: 9 bytes.
 * On Windows the vendor filter driver stuffs the zone into the high nibble
 * of "flag"; on Linux there is no such driver, so the zone is computed from
 * (x, y) using the rectangles below. "reserved" carries contact id + state.
 */
#pragma pack(1)
typedef struct {
  uint8_t  flag;
  uint16_t x;
  uint16_t y;
  uint16_t z;
  uint16_t reserved;
} IV_TouchPoint;

typedef struct {
  uint8_t reportId;
  IV_TouchPoint points[IV_TOUCH_MAX_POINTS];
} IV_TouchReport;
#pragma pack()

/* Touch zones, classified from raw (x, y). The numbering mirrors the vendor's
 * TouchLib (1..5); 0 means "outside any known zone".
 */
typedef enum {
  IV_ZONE_NONE = 0,
  IV_ZONE_SCREEN,        /* main reading/gesture surface */
  IV_ZONE_LEFT_SLIDER,
  IV_ZONE_RIGHT_SLIDER,
  IV_ZONE_ROUTING,       /* bottom strip: routing cursor + braille keyboard */
  IV_ZONE_POWER,         /* power button */
} IV_Zone;

/* Inclusive bounding box of a zone, in raw digitizer coordinates. */
typedef struct {
  IV_Zone zone;
  uint16_t xMin, yMin, xMax, yMax;
} IV_ZoneRect;

/* Geometry from TouchLibSettings.xml / TouchHelper.h. */
#define IV_ZONE_TABLE \
  { IV_ZONE_SCREEN,       940, 230, 10350, 6210 }, \
  { IV_ZONE_LEFT_SLIDER,    0, 2670,  420, 6240 }, \
  { IV_ZONE_RIGHT_SLIDER, 10800, 2670, 11263, 6240 }, \
  { IV_ZONE_ROUTING,     1200, 6400, 10045, 7167 }, \
  { IV_ZONE_POWER,          0,  100,  650, 1020 }

/* Gestures the driver can recognise. Kept aligned with the vendor protocol so
 * the full gesture engine (ported from the TouchLib sources) can be filled in
 * incrementally. Only the single-finger subset is wired up so far.
 */
typedef enum {
  IVG_NONE = 0,
  IVG_Tap,
  IVG_DoubleTap,
  IVG_TripleTap,
  IVG_FingerUp,
  IVG_FingerDown,
  IVG_FingerLeft,
  IVG_FingerRight,
  IVG_Stay,
} IV_Gesture;

#endif /* BRLTTY_INCLUDED_IV_BRLDEFS */
