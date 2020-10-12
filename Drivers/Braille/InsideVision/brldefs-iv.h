/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2020 by The BRLTTY Developers.
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

/** @brief Device-specific communication / definitions.
 */

#ifndef BRLTTY_INCLUDED_IV_BRLDEFS
#define BRLTTY_INCLUDED_IV_BRLDEFS

/** @brief Packet offset definitions
 */

#define IV_PKT_LEN (0)
#define IV_PKT_SRC (1)
#define IV_PKT_DST (2)
#define IV_PKT_CMD  (3)
#define IV_PKT_DATA (4)

/** @brief Communication type (source and destination)
 */

typedef enum e_iv_component
  {
   IVD_NONE = 0,
   IVD_BROADCAST,
   IVD_INSIDE_SOFTWARE,
   IVD_INSIDE_HARDWARE,
   IVD_BUS_BRAILLE,
   IVD_SCREEN_READER,
   IVD_INSIDE_SERVER,
   IVD_TIERS
  } t_iv_component;

/** @brief protocol packet types */

typedef enum e_iv_protocol
  {
   ICC_DEBUG = 0,
   ICC_CONFIG,
   ICC_INIT,
   ICC_EXPORT_BUS,
   ICC_GESTURE,
   ICC_DISCONNECT,
   ICC_LOG,
   ICC_DISABLE_TOUCH_HANDLER,
   ICC_ENABLE_TOUCH_HANDLER,
   ICC_DISABLE_SPEECH,
   ICC_ENABLE_SPEECH,
  } t_iv_protocol;

/** @brief gestures supported by the device.
 */

typedef enum
  {
   UNKNOWN = 0,
   IVG_Tap,
   IVG_DoubleTap,
   IVG_TripleTap,

   /** up gestures */

   IVG_OneFingerUp,
   IVG_TwoFingersUp,
   IVG_ThreeFingersUp,

   /** down gestures */

   IVG_OneFingerDown,
   IVG_TwoFingersDown,
   IVG_ThreeFingersDown,

   /** left gestures */

   IVG_OneFingerLeft,
   IVG_TwoFingersLeft,
   IVG_ThreeFingersLeft,

   /** right gestures */

   IVG_OneFingerRight,
   IVG_TwoFingersRight,
   IVG_ThreeFingersRight,

   /** stay gestures */

   IVG_Stay,
   IVG_StayTwoFingers,

   /** multi part gestures */
   IVG_UpRight,
   IVG_TwoFingersUpRight,
   IVG_ThreeFingersUpRight,
   IVG_DownRight,
   IVG_TwoFingersDownRight,
   IVG_ThreeFingersDownRight,
   IVG_DownLeft,
   IVG_TwoFingersDownLeft,
   IVG_ThreeFingersDownLeft,
   IVG_UpLeft,
   IVG_TwoFingersUpLeft,
   IVG_ThreeFingersUpLeft,
   IVG_RightUp,
   IVG_TwoFingersRightUp,
   IVG_ThreeFingersRightUp,
   IVG_RightDown,
   IVG_TwoFingersRightDown,
   IVG_ThreeFingersRightDown,
   IVG_LeftDown,
   IVG_TwoFingersLeftDown,
   IVG_ThreeFingersLeftDown,
   IVG_LeftUp,
   IVG_TwoFingersLeftUp,
   IVG_ThreeFingersLeftUp,

   /** rotate fingers */
   IVG_RoundTripLeft,
   IVG_TwoFingersRoundTripLeft,
   IVG_ThreeFingersRoundTripLeft,
   IVG_RoundTripRight,
   IVG_TwoFingersRoundTripRight,
   IVG_ThreeFingersRoundTripRight,
   IVG_RoundTripTop,
   IVG_TwoFingersRoundTripTop,
   IVG_ThreeFingersRoundTripTop,
   IVG_RoundTripBottom,
   IVG_TwoFingersRoundTripBottom,
   IVG_ThreeFingersRoundTripBottom,

   /** multi-fingers tap(s) gestures */
   IVG_TwoFingersTap,
   IVG_ThreeFingersTap,
   IVG_TwoFingersDoubleTap,
   IVG_ThreeFingersDoubleTap,
   IVG_TwoFingersTripleTap,
   IVG_ThreeFingersTripleTap,
   IVG_TwoFingersTapLong,
   IVG_OneFingerPinchOutToIn,
   IVG_TwoFingersPinchOutToIn,
   IVG_OneFingerPinchInToOut,
   IVG_TwoFingersPinchInToOut,
  } IV_Gesture;



typedef enum
  {
   IV_KEYGRP_POWER_ZONE = 0,
   IV_KEYGRP_LEFT_SLIDER_ZONE,
   IV_KEYGRP_RIGHT_SLIDER_ZONE,
   IV_KEYGRP_ROUTING_SLIDER_ZONE,
   IV_KEYGRP_SCREEN_ZONE
} IV_KeyGroup;

#endif /* BRLTTY_INCLUDED_IV_BRLDEFS */ 
