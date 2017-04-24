/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2017 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version. Please see the file LICENSE-GPL for details.
 *
 * Web Page: http://brltty.com/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#include "prologue.h"

#include "log.h"
#include "ctb_translate.h"

#include <liblouis.h>

static void
initialize (void) {
  static unsigned char initialized = 0;

  if (!initialized) {
    int logLevel = LOG_INFO;

    logMessage(logLevel, "LibLouis version: %s", lou_version());
    logMessage(logLevel, "LibLouis Data Directory: %s", lou_getDataPath());
    logMessage(logLevel, "LibLouis Character Size: %d", lou_charSize());

    initialized = 1;
  }
}

static int
contractText_louis (BrailleContractionData *bcd) {
  initialize();

  int inputLength = getInputCount(bcd);
  widechar inputBuffer[inputLength];

  {
    const wchar_t *source = bcd->input.begin;
    widechar *target = inputBuffer;

    while (source < bcd->input.end) {
      *target++ = *source++;
    }
  }

  int outputLength = getOutputCount(bcd);
  widechar outputBuffer[outputLength];

  char *typeForm = NULL;
  char *spacing = NULL;
  int *outputOffsets = NULL;
  int *inputOffsets = NULL;
  int *cursor = NULL;
  int mode = dotsIO | ucBrl;

  int translated = lou_translate(
    bcd->table->data.louis.tableList,
    inputBuffer, &inputLength, outputBuffer, &outputLength,
    typeForm, spacing, outputOffsets, inputOffsets, cursor, mode
  );

  if (translated) {
    bcd->input.current = bcd->input.begin + inputLength;
    bcd->output.current = bcd->output.begin + outputLength;

    {
      const widechar *source = outputBuffer;
      BYTE *target = bcd->output.begin;

      while (target < bcd->output.current) {
        *target++ = *source++ & 0XFF;
      }
    }
  }

  return translated;
}

static void
finishCharacterEntry_louis (BrailleContractionData *bcd, CharacterEntry *entry) {
}

static const ContractionTableTranslationMethods louisTranslationMethods = {
  .contractText = contractText_louis,
  .finishCharacterEntry = finishCharacterEntry_louis
};

const ContractionTableTranslationMethods *
getContractionTableTranslationMethods_louis (void) {
  return &louisTranslationMethods;
}
