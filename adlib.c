/*
 * BRLTTY - A background process providing access to the Linux console (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2001 by The BRLTTY Team. All rights reserved.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

/*
 * Miscellaneous FM chip soundcard routines for BRLTTY.
 * Implemented by Dave Mielke <dave@mielke.cc>.
 * Method gleaned from sccw, a morse code program written
 * by Steven J. Merrifield <sjm@ee.latrobe.edu.au> (VK3ESM).
 * Must compile with -O2.
 * Must link with -lm.
 * May compile with -DDEBUG_ADLIB.
 */

#include <stdlib.h>
#include <unistd.h>  
#include <sys/io.h>
#include <math.h>

#include "misc.h"
#include "adlib.h"

unsigned char AL_channelOffsets[] = {
   /* 1     2     3     4     5     6     7     8     9 */
   0X00, 0X01, 0X02, 0X08, 0X09, 0X0A, 0X10, 0X11, 0X12
};
const unsigned char AL_channelCount = sizeof(AL_channelOffsets);

static int AL_setPorts (state) {
   if (ioperm(ALP_REGISTER, 1, state) != -1) {
      if (ioperm(ALP_DATA, 1, state) != -1) {
         return 1;
      }
   }
   return 0;
}

int AL_enablePorts (void) {
   return AL_setPorts(1);
}

void AL_disablePorts (void) {
   AL_setPorts(0);
}

unsigned char AL_readStatus (void) {
   return inb(ALP_STATUS);
}

static void AL_writeDelay (int delay) {
   while (delay-- > 0) {
      AL_readStatus();
   }
}

void AL_writeRegister (int number, unsigned char data) {
   LogPrint(LOG_DEBUG, "AL_writeRegister: %2.2X=%2.2X", number, data);
   outb(number, ALP_REGISTER);
   AL_writeDelay(6);
   outb(data, ALP_DATA);
   AL_writeDelay(35);
}

void AL_resetCard (void) {
   int number;
   for (number=ALR_FIRST; number<=ALR_LAST; ++number) {
      AL_writeRegister(number, 0);
   }
}

static void AL_resetTimers (void) {
   AL_writeRegister(ALR_TCTL, AL_TCTL_T1MASK|AL_TCTL_T2MASK);
   AL_writeRegister(ALR_TCTL, AL_TCTL_RESET);
}

int AL_testCard (void) {
   unsigned char before, after;
   const unsigned char mask = AL_STAT_EXP | AL_STAT_EXP1 | AL_STAT_EXP2;
   AL_resetTimers();
   before = AL_readStatus();
   AL_writeRegister(ALR_T1DATA, 0xFF);
   AL_writeRegister(ALR_TCTL, AL_TCTL_T1START|AL_TCTL_T2MASK);
   usleep(80);
   after = AL_readStatus();
   AL_resetTimers(); 
   return ((before & mask) == 0)
       && ((after & mask) == (AL_STAT_EXP | AL_STAT_EXP1)); 
}

void AL_evaluatePitch (int pitch, int *exponent, int *mantissa) {
   /* Would you belive it took three days to work out how to do this ? */
   *exponent = -1;
   do {
      *mantissa = (int)(((float)pitch / 50000.0) / (1/(pow(2,abs(++*exponent-20))))); 
   } while (*mantissa > 0X3FF);
}

void AL_initiateTone (int channel, int exponent, int mantissa) {
   LogPrint(LOG_DEBUG, "AL_initiateTone: %1.1X[%3.3X]", exponent, mantissa);
   AL_writeRegister(ALR_FREQUENCY_LSB(channel),
                    (mantissa & 0XFF));
   AL_writeRegister(ALR_FREQUENCY_MSB(channel),
                    (((mantissa >> 8) & 0X3) |
		     ((exponent & 0X7) << AL_OCTAVE_SHIFT) |
		     AL_FREQ_ON));
}

void AL_startTone (int channel, int pitch) {
   int exponent;
   int mantissa;
   AL_evaluatePitch(pitch, &exponent, &mantissa);
   LogPrint(LOG_DEBUG, "AL_startTone: %d", pitch);
   AL_initiateTone(channel, exponent, mantissa);
}

void AL_stopTone (int channel) {
   AL_writeRegister(ALR_FREQUENCY_MSB(channel), 0);
}

void AL_playTone (int channel, int pitch, unsigned long int duration, int volume) {

   /* Play tone at fundamental frequency. */
   AL_writeRegister(ALR_MODULATOR(ALG_EFFECT, channel),
                    (AL_HARMONIC_1 << AL_HARMONIC_SHIFT));

   /* Set the carrier to the fundamental frequency. */
   AL_writeRegister(ALR_CARRIER(ALG_EFFECT, channel),
                    (AL_HARMONIC_1 << AL_HARMONIC_SHIFT));

   /* Set the volume (passed in as 0-100) */
   AL_writeRegister(ALR_CARRIER(ALG_LEVEL, channel),
                    ((AL_VOLUME_SOFT - ((AL_VOLUME_SOFT * volume) / 100)) << AL_VOLUME_SHIFT));

   /* Set fast attack and slow decay. */
   AL_writeRegister(ALR_CARRIER(ALG_ATTDEC, channel),
                    ((AL_ATTACK_FAST << AL_ATTACK_SHIFT) |
		     (AL_DECAY_SLOW << AL_DECAY_SHIFT)));

   /* Set soft sustain and fast release. */
   AL_writeRegister(ALR_CARRIER(ALG_SUSREL, channel),
                    ((AL_SUSTAIN_SOFT << AL_SUSTAIN_SHIFT) |
		     (AL_RELEASE_FAST << AL_RELEASE_SHIFT)));
      
   AL_startTone(channel, pitch);
   usleep(duration);
   AL_stopTone(channel);
}
