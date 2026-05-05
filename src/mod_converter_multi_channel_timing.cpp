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

#include <math.h>
#include "pch.h"
#include "mod_converter_effects.h"
#include "mod_converter_multi_channel_helpers.h"

struct ActiveNoteForConvert {
  bool active;
  int sample;
  int note;
  int order;
  int row;
  double startFrame;

  ActiveNoteForConvert():
    active(false),
    sample(-1),
    note(-1),
    order(-1),
    row(-1),
    startFrame(0.0) {
  }
};

NeoGeoMultiChannelLoopDurationsForConvert buildLoopedNoteDurationsForMultiChannelConvert(
  DivSubSong* sub,
  std::vector<SavedChannelForConvert>& saved,
  const std::vector<DivInstrument*>& sourceIns,
  const std::vector<DivSample*>& sourceSamples,
  int chans
) {
  NeoGeoMultiChannelLoopDurationsForConvert ret;
  const double adpcmARate=MOD_CONVERT_ADPCMA_RATE;
  ModConverterTimingState timing((sub->hz>0)?sub->hz:50.0);

  std::vector<ActiveNoteForConvert> active(chans);
  std::vector<int> currentIns(chans,-1);

  auto closeActive=[&](int ch, double frame, int endOrder, int endRow) {
    if (ch<0 || ch>=chans || !active[ch].active) return;
    int sample=active[ch].sample;
    if (sample>=0 && sample<(int)sourceSamples.size() && sourceSamples[sample]!=NULL && sourceSamples[sample]->loop) {
      int frames=(int)ceil(frame-active[ch].startFrame);
      if (frames>0) {
        NeoGeoMultiChannelEventKeyForConvert eventKey(ch,active[ch].order,active[ch].row);
        ret.eventFrames[eventKey]=frames;
        if (endOrder>=0 && endOrder<sub->ordersLen && endRow>=0 && endRow<sub->patLen) {
          ret.eventEndRows[eventKey]=NeoGeoMultiChannelEventKeyForConvert(ch,endOrder,endRow);
        }
        NeoGeoMultiChannelInstrumentKeyForConvert key(sample,active[ch].note,0);
        std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int>::iterator found=ret.maxFrames.find(key);
        if (found==ret.maxFrames.end() || frames>found->second) ret.maxFrames[key]=frames;
      }
    }
    active[ch].active=false;
  };

  double frame=0.0;
  std::vector<std::vector<bool> > visitedRows(sub->ordersLen,std::vector<bool>(sub->patLen,false));
  int ord=0;
  int rowStart=0;
  while (ord>=0 && ord<sub->ordersLen && rowStart>=0 && rowStart<sub->patLen && !visitedRows[ord][rowStart]) {
    visitedRows[ord][rowStart]=true;
    bool endOrder=false;
    int nextOrder=ord+1;
    int nextRow=0;
    for (int row=rowStart; row<sub->patLen; row++) {
      double rowFrames=timing.rowFrames(adpcmARate);
      for (int ch=0; ch<chans; ch++) {
        int sourcePatIndex=saved[ch].orders[ord];
        DivPattern* sourcePat=saved[ch].pat.getPattern(sourcePatIndex,false);
        if (sourcePat==NULL) continue;

        short sourceInsIndex=sourcePat->newData[row][DIV_PAT_INS];
        short sourceNote=sourcePat->newData[row][DIV_PAT_NOTE];
        if (sourceInsIndex>=0) currentIns[ch]=sourceInsIndex;

        if (sourceNote>=0 && sourceNote<DIV_NOTE_NULL_PAT) {
          closeActive(ch,frame,ord,row);
          int sourceSample=sourceSampleForMultiChannelConvert(sourceIns,currentIns[ch]);
          active[ch].active=true;
          active[ch].sample=sourceSample;
          active[ch].note=sourceNote;
          active[ch].order=ord;
          active[ch].row=row;
          active[ch].startFrame=frame;
        } else if (sourceNote==DIV_NOTE_OFF || sourceNote==DIV_NOTE_REL || sourceNote==DIV_MACRO_REL) {
          closeActive(ch,frame,ord,row);
        }

        for (int fxCol=0; fxCol<saved[ch].effectCols && fxCol<DIV_MAX_EFFECTS; fxCol++) {
          short fx=sourcePat->newData[row][DIV_PAT_FX(fxCol)];
          short fxVal=sourcePat->newData[row][DIV_PAT_FXVAL(fxCol)];
          if (fxVal<0) fxVal=0;
          if (fx==MOD_CONVERT_FX_NOTE_CUT) {
            closeActive(ch,frame+(double)MIN(timing.speed,fxVal)*adpcmARate/timing.hz,-1,-1);
          } else if (timing.applyTempoEffect(fx,fxVal)) {
            continue;
          } else {
            int jumpTarget=-1;
            int jumpRow=-1;
            if (modConvertEffectEndsOrder(fx,fxVal,ord,jumpTarget,jumpRow)) {
              endOrder=true;
              nextOrder=jumpTarget;
              nextRow=(jumpRow>=0)?CLAMP(jumpRow,0,sub->patLen-1):0;
            }
          }
        }
      }

      frame+=rowFrames;
      if (endOrder) break;
    }
    ord=nextOrder;
    rowStart=nextRow;
  }

  for (int ch=0; ch<chans; ch++) closeActive(ch,frame,-1,-1);
  return ret;
}

void deriveInitialMODTimingForNullsound(
  DivSubSong* sub,
  std::vector<SavedChannelForConvert>& saved,
  int chans,
  int& initialSpeed,
  double& initialHz
) {
  initialSpeed=6;
  initialHz=(sub->hz>0)?sub->hz:50.0;
  ModConverterTimingState timing(initialHz);

  for (int ch=0; ch<chans; ch++) {
    int sourcePatIndex=saved[ch].orders[0];
    DivPattern* sourcePat=saved[ch].pat.getPattern(sourcePatIndex,false);
    if (sourcePat==NULL) continue;

    for (int fxCol=0; fxCol<saved[ch].effectCols && fxCol<DIV_MAX_EFFECTS; fxCol++) {
      short fx=sourcePat->newData[0][DIV_PAT_FX(fxCol)];
      short fxVal=sourcePat->newData[0][DIV_PAT_FXVAL(fxCol)];
      if (fxVal<0) continue;

      timing.applyTempoEffect(fx,fxVal);
    }
  }
  initialSpeed=timing.speed;
  initialHz=timing.hz;
}
