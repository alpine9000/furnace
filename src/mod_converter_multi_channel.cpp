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

#include <stdio.h>
#include "pch.h"
#include "mod_converter_common.h"
#include "mod_converter_effects.h"
#include "mod_converter_internal.h"
#include "mod_converter_multi_channel_helpers.h"
#include "ta-log.h"
#include "engine/engine.h"

extern DivEngine e;

bool convertLoadedModToNeoGeoMultiChannel() {
  if (e.song.version!=DIV_VERSION_MOD) {
    logE("Neo Geo multi-channel conversion currently expects an imported MOD file.");
    return false;
  }

  const int oldChans=e.song.chans;
  if (oldChans<1) {
    logE("cannot convert a song with no channels.");
    return false;
  }

  const int convertChans=MIN(oldChans,6);
  const int adpcmAOff=MOD_CONVERT_ADPCMA_CHANNEL_BASE;
  if (oldChans>6) {
    logW("Neo Geo MVS has six ADPCM-A channels; only the first six MOD channels will be converted.");
  }

  std::vector<DivSample*> sourceSamples=e.song.sample;
  std::vector<DivInstrument*> sourceIns=e.song.ins;
  e.song.sample.clear();
  e.song.sampleLen=0;
  e.song.ins.clear();
  e.song.insLen=0;

  std::vector<std::vector<SavedChannelForConvert> > saved;
  saveChannelsForConvert(saved,oldChans);

  configureNeoGeoSystemForConvert("Neo Geo MVS");

  std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int> cachedIns;
  ModConverterEffectStats effectStats;
  int convertedNotes=0;
  int droppedNotes=0;
  int bakedSampleOffsets=0;
  int attenuatedSamples=0;
  int loopFittedSamples=0;
  int syntheticLoopStops=0;

  for (size_t si=0; si<e.song.subsong.size(); si++) {
    DivSubSong* sub=e.song.subsong[si];
    int initialSpeed=6;
    double initialHz=50.0;
    deriveInitialMODTimingForNullsound(sub,saved[si],oldChans,initialSpeed,initialHz);
    NeoGeoMultiChannelLoopDurationsForConvert loopedDurations=buildLoopedNoteDurationsForMultiChannelConvert(
      sub,
      saved[si],
      sourceIns,
      sourceSamples,
      convertChans
    );
    sub->hz=initialHz;
    sub->speeds.len=1;
    sub->speeds.val[0]=initialSpeed;

    resetSubSongChannelsForConvert(sub);

    for (int srcCh=0; srcCh<convertChans; srcCh++) {
      int destCh=adpcmAOff+srcCh;
      sub->pat[destCh].effectCols=MIN(DIV_MAX_EFFECTS,MAX(1,(int)saved[si][srcCh].effectCols));

      int currentIns=-1;
      for (int ord=0; ord<sub->ordersLen; ord++) {
        sub->orders.ord[destCh][ord]=ord;
        int sourcePatIndex=saved[si][srcCh].orders[ord];
        DivPattern* sourcePat=saved[si][srcCh].pat.getPattern(sourcePatIndex,false);
        if (sourcePat==NULL) continue;
        DivPattern* destPat=sub->pat[destCh].getPattern(ord,true);

        for (int row=0; row<sub->patLen; row++) {
          short sourceInsIndex=sourcePat->newData[row][DIV_PAT_INS];
          short sourceNote=sourcePat->newData[row][DIV_PAT_NOTE];
          if (sourceInsIndex>=0) currentIns=sourceInsIndex;

          if (sourceNote>=0 && sourceNote<DIV_NOTE_NULL_PAT) {
            int sourceSample=sourceSampleForMultiChannelConvert(sourceIns,currentIns);
            int sourceOffset=sampleOffsetForMultiChannelConvertNote(sourcePat,row,saved[si][srcCh].effectCols);
            int targetADPCMFrames=0;
            int eventADPCMFrames=0;
            NeoGeoMultiChannelEventKeyForConvert eventKey(srcCh,ord,row);
            std::map<NeoGeoMultiChannelEventKeyForConvert,int>::iterator eventDuration=loopedDurations.eventFrames.find(eventKey);
            if (eventDuration!=loopedDurations.eventFrames.end()) eventADPCMFrames=eventDuration->second;
            std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int>::iterator maxDuration=loopedDurations.maxFrames.find(NeoGeoMultiChannelInstrumentKeyForConvert(sourceSample,sourceNote,0));
            if (maxDuration!=loopedDurations.maxFrames.end()) targetADPCMFrames=maxDuration->second;
            int destIns=addMultiChannelADPCMANoteInstrument(sourceSamples,sourceSample,sourceNote,targetADPCMFrames,sourceOffset,cachedIns,attenuatedSamples,loopFittedSamples);
            if (destIns>=0) {
              destPat->newData[row][DIV_PAT_NOTE]=108;
              destPat->newData[row][DIV_PAT_INS]=destIns;
              convertedNotes++;
              if (sourceOffset>0) {
                bakedSampleOffsets++;
              }
            } else {
              droppedNotes++;
            }
            if (eventADPCMFrames>0 && targetADPCMFrames>eventADPCMFrames) {
              std::map<NeoGeoMultiChannelEventKeyForConvert,NeoGeoMultiChannelEventKeyForConvert>::iterator endAt=loopedDurations.eventEndRows.find(eventKey);
              if (endAt!=loopedDurations.eventEndRows.end()) {
                int endOrd=endAt->second.order;
                int endRow=endAt->second.row;
                if (endOrd>=0 && endOrd<sub->ordersLen && endRow>=0 && endRow<sub->patLen) {
                  DivPattern* endPat=sub->pat[destCh].getPattern(endOrd,true);
                  if (endPat!=NULL && endPat->newData[endRow][DIV_PAT_NOTE]==-1) {
                    endPat->newData[endRow][DIV_PAT_NOTE]=DIV_NOTE_OFF;
                    syntheticLoopStops++;
                  }
                }
              }
            }
          } else if (sourceNote==DIV_NOTE_OFF || sourceNote==DIV_NOTE_REL || sourceNote==DIV_MACRO_REL) {
            destPat->newData[row][DIV_PAT_NOTE]=sourceNote;
          }

          destPat->newData[row][DIV_PAT_VOL]=scaleMODVolumeToADPCMA(sourcePat->newData[row][DIV_PAT_VOL]);

          int destFxCol=0;
          for (int fxCol=0; fxCol<saved[si][srcCh].effectCols && fxCol<DIV_MAX_EFFECTS; fxCol++) {
            short outFx=-1;
            short outFxVal=-1;
            short sourceFx=sourcePat->newData[row][DIV_PAT_FX(fxCol)];
            short sourceFxVal=sourcePat->newData[row][DIV_PAT_FXVAL(fxCol)];
            int ignoredSampleOffset=0;
            if (isMultiChannelConvertSampleOffsetEffect(sourceFx,sourceFxVal,ignoredSampleOffset) && sourceNote>=0 && sourceNote<DIV_NOTE_NULL_PAT) {
              continue;
            }
            bool copied=copySupportedMultiChannelConvertEffect(
              sourceFx,
              sourceFxVal,
              outFx,
              outFxVal
            );
            if (copied) {
              destPat->newData[row][DIV_PAT_FX(destFxCol)]=outFx;
              destPat->newData[row][DIV_PAT_FXVAL(destFxCol)]=outFxVal;
              destFxCol++;
              if (destFxCol>=DIV_MAX_EFFECTS) break;
            } else {
              recordUnsupportedMultiChannelConvertEffect(sourceFx,effectStats);
            }
          }
        }
      }
    }
  }

  clearSavedChannels(saved);
  for (DivSample* sample: sourceSamples) delete sample;
  for (DivInstrument* ins: sourceIns) delete ins;

  logW("converted MOD to Neo Geo using multiple ADPCM-A tracker channels.");
  logW("mapped %d note event(s) to %d ADPCM-A sample/instrument variant(s).",convertedNotes,(int)cachedIns.size());
  if (droppedNotes>0) {
    logW("dropped %d note event(s) because ADPCM-A sample/instrument variants could not be created.",droppedNotes);
  }
  if (bakedSampleOffsets>0) {
    logW("baked %d MOD sample offset effect(s) into ADPCM-A sample variants.",bakedSampleOffsets);
  }
  if (attenuatedSamples>0) {
    logW("attenuated %d hot ADPCM-A sample variant(s) before encoding to avoid clipping.",attenuatedSamples);
  }
  if (loopFittedSamples>0) {
    logW("fit %d looped MOD sample variant(s) to their longest observed sample/note duration.",loopFittedSamples);
  }
  if (syntheticLoopStops>0) {
    logW("inserted %d synthetic note-off event(s) to stop shared looped sample variants at phrase boundaries.",syntheticLoopStops);
  }
  if (effectStats.unsupportedTempoEffects>0) {
    logW("dropped %d F0xx BPM effect(s); nullsound does not support per-row BPM changes.",effectStats.unsupportedTempoEffects);
  }
  if (effectStats.unsupportedEffects>0) {
    logW("dropped %d unsupported MOD effect column value(s); remaining unsupported pitch/volume/time effects are not implemented in the multi-channel converter yet.",effectStats.unsupportedEffects);
  }
  return true;
}
