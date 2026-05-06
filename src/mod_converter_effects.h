/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2026 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include "pch.h"
#include "engine/engine.h"

static const short MOD_CONVERT_FX_NONE=-1;
static const short MOD_CONVERT_FX_ARPEGGIO=0x00;
static const short MOD_CONVERT_FX_PORTAMENTO_UP=0x01;
static const short MOD_CONVERT_FX_PORTAMENTO_DOWN=0x02;
static const short MOD_CONVERT_FX_TONE_PORTAMENTO=0x03;
static const short MOD_CONVERT_FX_VIBRATO=0x04;
static const short MOD_CONVERT_FX_VIBRATO_VOL_SLIDE=0x06;
static const short MOD_CONVERT_FX_TREMOLO=0x07;
static const short MOD_CONVERT_FX_SAMPLE_OFFSET=0x09;
static const short MOD_CONVERT_FX_VOLUME_SLIDE=0x0a;
static const short MOD_CONVERT_FX_ORDER_JUMP=0x0b;
static const short MOD_CONVERT_FX_RETRIGGER=0x0c;
static const short MOD_CONVERT_FX_PATTERN_BREAK=0x0d;
static const short MOD_CONVERT_FX_SPEED=0x0f;
static const short MOD_CONVERT_FX_SAMPLE_OFFSET_LOW=0x90;
static const short MOD_CONVERT_FX_SAMPLE_OFFSET_MID=0x91;
static const short MOD_CONVERT_FX_SAMPLE_OFFSET_HIGH=0x92;
static const short MOD_CONVERT_FX_ARPEGGIO_SPEED=0xe0;
static const short MOD_CONVERT_FX_VIBRATO_SHAPE=0xe3;
static const short MOD_CONVERT_FX_VIBRATO_RANGE=0xe4;
static const short MOD_CONVERT_FX_FURNACE_BPM=0xf0;
static const short MOD_CONVERT_FX_SINGLE_PITCH_SLIDE_UP=0xf1;
static const short MOD_CONVERT_FX_SINGLE_PITCH_SLIDE_DOWN=0xf2;
static const short MOD_CONVERT_FX_FINE_VOLUME_SLIDE_UP=0xf3;
static const short MOD_CONVERT_FX_FINE_VOLUME_SLIDE_DOWN=0xf4;
static const short MOD_CONVERT_FX_SINGLE_VOLUME_SLIDE_UP=0xf8;
static const short MOD_CONVERT_FX_SINGLE_VOLUME_SLIDE_DOWN=0xf9;
static const short MOD_CONVERT_FX_FAST_VOLUME_SLIDE=0xfa;
static const short MOD_CONVERT_FX_NOTE_CUT=0xec;
static const short MOD_CONVERT_FX_NOTE_DELAY=0xed;

static const short MOD_CONVERT_PROTRACKER_MAX_SPEED=0x20;

struct ModConverterEffectStats {
  int unsupportedEffects;
  int unsupportedTempoEffects;
  int bakedSingleVolumeSlides;
  std::map<short,int> unsupportedByFx;
  std::map<int,int> unsupportedByFxVal;

  ModConverterEffectStats():
    unsupportedEffects(0),
    unsupportedTempoEffects(0),
    bakedSingleVolumeSlides(0) {
  }
};

struct ModConvertEffectSupport {
  short sourceFx;
  short destFx;
  bool supportedByNullsound;
  bool affectsPitch;
  bool affectsTiming;
  bool bakedIntoSample;
  const char* name;
};

struct ModConverterTimingState {
  int speed;
  double hz;

  ModConverterTimingState(double initialHz=50.0);
  bool applyTempoEffect(short fx, short fxVal);
  double rowFrames(double sampleRate) const;
};

const ModConvertEffectSupport* multiChannelConvertEffectSupportFor(short fx);
const ModConvertEffectSupport* multiChannelConvertEffectSupportTable(int& count);
bool modConvertEffectEndsOrder(short fx, short fxVal, int currentOrder, int& jumpTarget, int& jumpRow);
bool isNoOpMultiChannelConvertEffect(short fx, short fxVal);
bool isUnsupportedEffectResetForMultiChannelConvert(short fx, short fxVal);
bool isRedundantFixedTempoEffectForMultiChannelConvert(short fx, short fxVal, double fixedHz);
bool isMultiChannelConvertSampleOffsetEffect(short fx, short fxVal, int& offset);
int sampleOffsetForMultiChannelConvertNote(DivPattern* sourcePat, int row, int effectCols);
int arpeggioForMultiChannelConvertNote(DivPattern* sourcePat, int row, int effectCols);
bool applySingleTickVolumeSlideForMultiChannelConvert(short fx, short fxVal, int& volume);
bool copySupportedMultiChannelConvertEffect(short fx, short fxVal, short& outFx, short& outFxVal);
void recordUnsupportedMultiChannelConvertEffect(short fx, short fxVal, ModConverterEffectStats& stats);
