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

#include "mod_converter_effects.h"

static const ModConvertEffectSupport multiChannelConvertEffectSupport[]={
  {MOD_CONVERT_FX_ARPEGGIO,MOD_CONVERT_FX_ARPEGGIO,false,true,false,false,"arpeggio"},
  {MOD_CONVERT_FX_PORTAMENTO_UP,MOD_CONVERT_FX_PORTAMENTO_UP,false,true,false,false,"portamento up"},
  {MOD_CONVERT_FX_PORTAMENTO_DOWN,MOD_CONVERT_FX_PORTAMENTO_DOWN,false,true,false,false,"portamento down"},
  {MOD_CONVERT_FX_TONE_PORTAMENTO,MOD_CONVERT_FX_TONE_PORTAMENTO,false,true,false,false,"tone portamento"},
  {MOD_CONVERT_FX_VIBRATO,MOD_CONVERT_FX_VIBRATO,false,true,false,false,"vibrato"},
  {MOD_CONVERT_FX_VIBRATO_VOL_SLIDE,MOD_CONVERT_FX_VIBRATO_VOL_SLIDE,false,true,false,false,"vibrato volume slide"},
  {MOD_CONVERT_FX_SAMPLE_OFFSET,MOD_CONVERT_FX_NONE,true,false,false,true,"sample offset"},
  {MOD_CONVERT_FX_VOLUME_SLIDE,MOD_CONVERT_FX_VOLUME_SLIDE,true,false,false,false,"volume slide"},
  {MOD_CONVERT_FX_ORDER_JUMP,MOD_CONVERT_FX_ORDER_JUMP,true,false,true,false,"order jump"},
  {MOD_CONVERT_FX_RETRIGGER,MOD_CONVERT_FX_RETRIGGER,true,false,true,false,"retrigger"},
  {MOD_CONVERT_FX_PATTERN_BREAK,MOD_CONVERT_FX_PATTERN_BREAK,true,false,true,false,"pattern break"},
  {MOD_CONVERT_FX_SPEED,MOD_CONVERT_FX_SPEED,true,false,true,false,"speed"},
  {MOD_CONVERT_FX_SAMPLE_OFFSET_LOW,MOD_CONVERT_FX_NONE,true,false,false,true,"sample offset low"},
  {MOD_CONVERT_FX_SAMPLE_OFFSET_MID,MOD_CONVERT_FX_NONE,true,false,false,true,"sample offset mid"},
  {MOD_CONVERT_FX_SAMPLE_OFFSET_HIGH,MOD_CONVERT_FX_NONE,true,false,false,true,"sample offset high"},
  {MOD_CONVERT_FX_FURNACE_BPM,MOD_CONVERT_FX_FURNACE_BPM,false,false,true,false,"BPM"},
  {MOD_CONVERT_FX_FAST_VOLUME_SLIDE,MOD_CONVERT_FX_VOLUME_SLIDE,true,false,false,false,"fast volume slide"},
  {MOD_CONVERT_FX_NOTE_CUT,MOD_CONVERT_FX_NOTE_CUT,true,false,true,false,"note cut"},
  {MOD_CONVERT_FX_NOTE_DELAY,MOD_CONVERT_FX_NOTE_DELAY,true,false,true,false,"note delay"}
};

const ModConvertEffectSupport* multiChannelConvertEffectSupportFor(short fx) {
  for (size_t i=0; i<sizeof(multiChannelConvertEffectSupport)/sizeof(multiChannelConvertEffectSupport[0]); i++) {
    if (multiChannelConvertEffectSupport[i].sourceFx==fx) return &multiChannelConvertEffectSupport[i];
  }
  return NULL;
}

const ModConvertEffectSupport* multiChannelConvertEffectSupportTable(int& count) {
  count=sizeof(multiChannelConvertEffectSupport)/sizeof(multiChannelConvertEffectSupport[0]);
  return multiChannelConvertEffectSupport;
}

ModConverterTimingState::ModConverterTimingState(double initialHz):
  speed(6),
  hz((initialHz>0.0)?initialHz:50.0) {
}

bool ModConverterTimingState::applyTempoEffect(short fx, short fxVal) {
  if (fxVal<=0) return false;
  if (fx==MOD_CONVERT_FX_SPEED) {
    if (fxVal<=MOD_CONVERT_PROTRACKER_MAX_SPEED) {
      speed=fxVal;
    } else {
      hz=(double)fxVal*2.0/5.0;
      if (hz<1.0) hz=1.0;
    }
    return true;
  }
  if (fx==MOD_CONVERT_FX_FURNACE_BPM) {
    hz=(double)fxVal*2.0/5.0;
    if (hz<1.0) hz=1.0;
    return true;
  }
  return false;
}

double ModConverterTimingState::rowFrames(double sampleRate) const {
  return (double)speed*sampleRate/hz;
}

bool modConvertEffectEndsOrder(short fx, short fxVal, int currentOrder, int& jumpTarget, int& jumpRow) {
  if (fx==MOD_CONVERT_FX_PATTERN_BREAK) {
    if (jumpTarget<0) jumpTarget=currentOrder+1;
    jumpRow=MAX(0,(int)fxVal);
    return true;
  }
  if (fx==MOD_CONVERT_FX_ORDER_JUMP) {
    jumpTarget=fxVal;
    if (jumpRow<0) jumpRow=0;
    return true;
  }
  return false;
}

bool isMultiChannelConvertSampleOffsetEffect(short fx, short fxVal, int& offset) {
  if (fxVal<0) fxVal=0;
  if (fx==MOD_CONVERT_FX_SAMPLE_OFFSET && fxVal>0) {
    offset=(int)fxVal*256;
    return true;
  }
  if (fx==MOD_CONVERT_FX_SAMPLE_OFFSET_LOW || fx==MOD_CONVERT_FX_SAMPLE_OFFSET_MID || fx==MOD_CONVERT_FX_SAMPLE_OFFSET_HIGH) {
    offset=(int)fxVal<<(8*(fx-MOD_CONVERT_FX_SAMPLE_OFFSET_LOW));
    return offset>0;
  }
  return false;
}

int sampleOffsetForMultiChannelConvertNote(DivPattern* sourcePat, int row, int effectCols) {
  if (sourcePat==NULL || row<0) return 0;
  for (int fxCol=0; fxCol<effectCols && fxCol<DIV_MAX_EFFECTS; fxCol++) {
    int offset=0;
    if (isMultiChannelConvertSampleOffsetEffect(
      sourcePat->newData[row][DIV_PAT_FX(fxCol)],
      sourcePat->newData[row][DIV_PAT_FXVAL(fxCol)],
      offset
    )) {
      return offset;
    }
  }
  return 0;
}

bool copySupportedMultiChannelConvertEffect(short fx, short fxVal, short& outFx, short& outFxVal) {
  const ModConvertEffectSupport* support=multiChannelConvertEffectSupportFor(fx);
  if (support==NULL || !support->supportedByNullsound || support->bakedIntoSample) return false;
  outFx=support->destFx;
  outFxVal=fxVal;
  return true;
}

void recordUnsupportedMultiChannelConvertEffect(short fx, ModConverterEffectStats& stats) {
  if (fx==MOD_CONVERT_FX_NONE) return;
  if (fx==MOD_CONVERT_FX_FURNACE_BPM) {
    stats.unsupportedTempoEffects++;
  }
  stats.unsupportedEffects++;
}
