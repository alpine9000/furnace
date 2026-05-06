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
#include <set>
#include "pch.h"
#include "mod_converter_common.h"
#include "mod_converter_effects.h"
#include "mod_converter_internal.h"
#include "mod_converter_multi_channel_helpers.h"
#include "ta-log.h"
#include "engine/engine.h"

extern DivEngine e;

static void collectPitchEventsForMultiChannelNote(
  DivSubSong* sub,
  std::vector<SavedChannelForConvert>& saved,
  int srcCh,
  int startOrd,
  int startRow,
  int speed,
  double hz,
  std::vector<NeoGeoMultiChannelPitchEventForConvert>& pitchEvents,
  std::vector<NeoGeoMultiChannelEventKeyForConvert>& consumedRows
) {
  if (sub==NULL || srcCh<0 || srcCh>=(int)saved.size()) return;
  if (speed<1) speed=6;
  if (hz<=0.0) hz=50.0;
  double rowFrames=(double)speed*MOD_CONVERT_ADPCMA_RATE/hz;
  int rowsElapsed=0;
  short lastVibrato=-1;
  short lastSlideFx=-1;
  short lastSlideVal=-1;

  for (int ord=startOrd; ord<sub->ordersLen; ord++) {
    int sourcePatIndex=saved[srcCh].orders[ord];
    DivPattern* sourcePat=saved[srcCh].pat.getPattern(sourcePatIndex,false);
    if (sourcePat==NULL) return;
    int firstRow=(ord==startOrd)?startRow:0;
    for (int row=firstRow; row<sub->patLen; row++) {
      short sourceNote=sourcePat->newData[row][DIV_PAT_NOTE];
      if (!(ord==startOrd && row==startRow) && (
        (sourceNote>=0 && sourceNote<DIV_NOTE_NULL_PAT) ||
        sourceNote==DIV_NOTE_OFF ||
        sourceNote==DIV_NOTE_REL ||
        sourceNote==DIV_MACRO_REL
      )) {
        return;
      }

      for (int fxCol=0; fxCol<saved[srcCh].effectCols && fxCol<DIV_MAX_EFFECTS; fxCol++) {
        short fx=sourcePat->newData[row][DIV_PAT_FX(fxCol)];
        short fxVal=sourcePat->newData[row][DIV_PAT_FXVAL(fxCol)];
        if (fx==MOD_CONVERT_FX_VIBRATO && fxVal>0) {
          if (fxVal!=lastVibrato) {
            pitchEvents.push_back(NeoGeoMultiChannelPitchEventForConvert((int)round((double)rowsElapsed*rowFrames),fx,fxVal));
            lastVibrato=fxVal;
          }
          consumedRows.push_back(NeoGeoMultiChannelEventKeyForConvert(srcCh,ord,row));
        } else if ((fx==MOD_CONVERT_FX_PORTAMENTO_UP || fx==MOD_CONVERT_FX_PORTAMENTO_DOWN) && fxVal>0) {
          if (fx!=lastSlideFx || fxVal!=lastSlideVal) {
            pitchEvents.push_back(NeoGeoMultiChannelPitchEventForConvert((int)round((double)rowsElapsed*rowFrames),fx,fxVal));
            lastSlideFx=fx;
            lastSlideVal=fxVal;
          }
          consumedRows.push_back(NeoGeoMultiChannelEventKeyForConvert(srcCh,ord,row));
        }
      }
      rowsElapsed++;
    }
  }
}

bool convertLoadedModToNeoGeoMultiChannel(bool bakePitchModulation) {
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
  int bakedArpeggios=0;
  int bakedArpeggioVariants=0;
  int bakedPitchEffects=0;
  int bakedPitchVariants=0;
  int attenuatedSamples=0;
  int loopFittedSamples=0;
  int syntheticLoopStops=0;
  std::set<NeoGeoMultiChannelEventKeyForConvert> bakedPitchRows;

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
      int currentVolume=64;
      for (int ord=0; ord<sub->ordersLen; ord++) {
        sub->orders.ord[destCh][ord]=ord;
        int sourcePatIndex=saved[si][srcCh].orders[ord];
        DivPattern* sourcePat=saved[si][srcCh].pat.getPattern(sourcePatIndex,false);
        if (sourcePat==NULL) continue;
        DivPattern* destPat=sub->pat[destCh].getPattern(ord,true);

        for (int row=0; row<sub->patLen; row++) {
          short sourceInsIndex=sourcePat->newData[row][DIV_PAT_INS];
          short sourceNote=sourcePat->newData[row][DIV_PAT_NOTE];
          short sourceVol=sourcePat->newData[row][DIV_PAT_VOL];
          if (sourceInsIndex>=0) currentIns=sourceInsIndex;
          if (sourceVol>=0) currentVolume=CLAMP((int)sourceVol,0,64);

          if (sourceNote>=0 && sourceNote<DIV_NOTE_NULL_PAT) {
            int sourceSample=sourceSampleForMultiChannelConvert(sourceIns,currentIns);
            int sourceOffset=sampleOffsetForMultiChannelConvertNote(sourcePat,row,saved[si][srcCh].effectCols);
            int arpeggio=arpeggioForMultiChannelConvertNote(sourcePat,row,saved[si][srcCh].effectCols);
            std::vector<NeoGeoMultiChannelPitchEventForConvert> pitchEvents;
            std::vector<NeoGeoMultiChannelEventKeyForConvert> consumedPitchRows;
            if (bakePitchModulation) {
              collectPitchEventsForMultiChannelNote(sub,saved[si],srcCh,ord,row,initialSpeed,initialHz,pitchEvents,consumedPitchRows);
            }
            int targetADPCMFrames=0;
            int eventADPCMFrames=0;
            NeoGeoMultiChannelEventKeyForConvert eventKey(srcCh,ord,row);
            std::map<NeoGeoMultiChannelEventKeyForConvert,int>::iterator eventDuration=loopedDurations.eventFrames.find(eventKey);
            if (eventDuration!=loopedDurations.eventFrames.end()) eventADPCMFrames=eventDuration->second;
            std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int>::iterator maxDuration=loopedDurations.maxFrames.find(NeoGeoMultiChannelInstrumentKeyForConvert(sourceSample,sourceNote,0));
            if (maxDuration!=loopedDurations.maxFrames.end()) targetADPCMFrames=maxDuration->second;
            bool newArpeggioVariant=arpeggio>0 && cachedIns.find(NeoGeoMultiChannelInstrumentKeyForConvert(sourceSample,sourceNote,targetADPCMFrames,sourceOffset,arpeggio))==cachedIns.end();
            bool newPitchVariant=!pitchEvents.empty() && cachedIns.find(NeoGeoMultiChannelInstrumentKeyForConvert(sourceSample,sourceNote,targetADPCMFrames,sourceOffset,arpeggio,pitchEvents))==cachedIns.end();
            int destIns=addMultiChannelADPCMANoteInstrument(sourceSamples,sourceSample,sourceNote,targetADPCMFrames,sourceOffset,arpeggio,initialSpeed,initialHz,pitchEvents,cachedIns,attenuatedSamples,loopFittedSamples);
            if (destIns>=0) {
              destPat->newData[row][DIV_PAT_NOTE]=108;
              destPat->newData[row][DIV_PAT_INS]=destIns;
              convertedNotes++;
              if (sourceOffset>0) {
                bakedSampleOffsets++;
              }
              if (arpeggio>0) {
                bakedArpeggios++;
                if (newArpeggioVariant) bakedArpeggioVariants++;
              }
              if (!pitchEvents.empty()) {
                bakedPitchEffects+=(int)consumedPitchRows.size();
                if (newPitchVariant) bakedPitchVariants++;
                for (std::vector<NeoGeoMultiChannelEventKeyForConvert>::const_iterator it=consumedPitchRows.begin(); it!=consumedPitchRows.end(); ++it) {
                  bakedPitchRows.insert(*it);
                }
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

          int rowVolume=currentVolume;
          for (int fxCol=0; fxCol<saved[si][srcCh].effectCols && fxCol<DIV_MAX_EFFECTS; fxCol++) {
            if (applySingleTickVolumeSlideForMultiChannelConvert(
              sourcePat->newData[row][DIV_PAT_FX(fxCol)],
              sourcePat->newData[row][DIV_PAT_FXVAL(fxCol)],
              rowVolume
            )) {
              effectStats.bakedSingleVolumeSlides++;
            }
          }
          if (rowVolume!=currentVolume || sourceVol>=0) {
            currentVolume=rowVolume;
            destPat->newData[row][DIV_PAT_VOL]=scaleMODVolumeToADPCMA((short)currentVolume);
          }

          int destFxCol=0;
          for (int fxCol=0; fxCol<saved[si][srcCh].effectCols && fxCol<DIV_MAX_EFFECTS; fxCol++) {
            short outFx=-1;
            short outFxVal=-1;
            short sourceFx=sourcePat->newData[row][DIV_PAT_FX(fxCol)];
            short sourceFxVal=sourcePat->newData[row][DIV_PAT_FXVAL(fxCol)];
            if (isNoOpMultiChannelConvertEffect(sourceFx,sourceFxVal)) {
              continue;
            }
            if (isUnsupportedEffectResetForMultiChannelConvert(sourceFx,sourceFxVal)) {
              continue;
            }
            if (isRedundantFixedTempoEffectForMultiChannelConvert(sourceFx,sourceFxVal,initialHz)) {
              continue;
            }
            int ignoredSampleOffset=0;
            if (isMultiChannelConvertSampleOffsetEffect(sourceFx,sourceFxVal,ignoredSampleOffset) && sourceNote>=0 && sourceNote<DIV_NOTE_NULL_PAT) {
              continue;
            }
            if (sourceFx==MOD_CONVERT_FX_ARPEGGIO && sourceFxVal>0 && sourceNote>=0 && sourceNote<DIV_NOTE_NULL_PAT) {
              continue;
            }
            if (
              (sourceFx==MOD_CONVERT_FX_VIBRATO || sourceFx==MOD_CONVERT_FX_PORTAMENTO_UP || sourceFx==MOD_CONVERT_FX_PORTAMENTO_DOWN) &&
              bakedPitchRows.find(NeoGeoMultiChannelEventKeyForConvert(srcCh,ord,row))!=bakedPitchRows.end()
            ) {
              continue;
            }
            int ignoredVolume=currentVolume;
            if (applySingleTickVolumeSlideForMultiChannelConvert(sourceFx,sourceFxVal,ignoredVolume)) {
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
              recordUnsupportedMultiChannelConvertEffect(sourceFx,sourceFxVal,effectStats);
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
  if (bakePitchModulation) {
    logW("ADPCM-A pitch modulation baking is enabled; this may create many sample variants.");
  } else {
    logW("ADPCM-A pitch modulation baking is disabled; use -convert neogeo-multi-channel-pitch to trade ROM size for closer pitch effects.");
  }
  logW("mapped %d note event(s) to %d ADPCM-A sample/instrument variant(s).",convertedNotes,(int)cachedIns.size());
  if (droppedNotes>0) {
    logW("dropped %d note event(s) because ADPCM-A sample/instrument variants could not be created.",droppedNotes);
  }
  if (bakedSampleOffsets>0) {
    logW("baked %d MOD sample offset effect(s) into ADPCM-A sample variants.",bakedSampleOffsets);
  }
  if (bakedArpeggios>0) {
    logW("baked %d MOD arpeggio effect(s) into %d ADPCM-A sample variant(s).",bakedArpeggios,bakedArpeggioVariants);
  }
  if (bakedPitchEffects>0) {
    logW("baked %d MOD pitch modulation effect(s) into %d ADPCM-A sample variant(s).",bakedPitchEffects,bakedPitchVariants);
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
  if (effectStats.bakedSingleVolumeSlides>0) {
    logW("baked %d MOD single-tick volume slide effect(s) into ADPCM-A volume columns.",effectStats.bakedSingleVolumeSlides);
  }
  if (effectStats.unsupportedTempoEffects>0) {
    logW("dropped %d F0xx BPM effect(s); nullsound does not support per-row BPM changes.",effectStats.unsupportedTempoEffects);
  }
  if (effectStats.unsupportedEffects>0) {
    logW("dropped %d unsupported MOD effect column value(s); remaining unsupported pitch/volume/time effects are not implemented in the multi-channel converter yet.",effectStats.unsupportedEffects);
    int detailCount=0;
    for (std::map<short,int>::const_iterator it=effectStats.unsupportedByFx.begin(); it!=effectStats.unsupportedByFx.end(); ++it) {
      const ModConvertEffectSupport* support=multiChannelConvertEffectSupportFor(it->first);
      const char* name=(support!=NULL && support->name!=NULL)?support->name:"unknown";
      logW("  unsupported effect %02Xxx (%s): %d",it->first&0xff,name,it->second);
      detailCount++;
      if (detailCount>=8 && (int)effectStats.unsupportedByFx.size()>detailCount) {
        logW("  plus %d other unsupported effect type(s).",(int)effectStats.unsupportedByFx.size()-detailCount);
        break;
      }
    }
    std::vector<std::pair<int,int> > unsupportedFxVals(effectStats.unsupportedByFxVal.begin(),effectStats.unsupportedByFxVal.end());
    std::sort(unsupportedFxVals.begin(),unsupportedFxVals.end(),[](const std::pair<int,int>& a, const std::pair<int,int>& b) {
      if (a.second!=b.second) return a.second>b.second;
      return a.first<b.first;
    });
    detailCount=0;
    for (std::vector<std::pair<int,int> >::const_iterator it=unsupportedFxVals.begin(); it!=unsupportedFxVals.end(); ++it) {
      short fx=(short)((it->first>>16)&0xffff);
      short fxVal=(short)(it->first&0xffff);
      logW("  unsupported effect value %02X%02X: %d",fx&0xff,fxVal&0xff,it->second);
      detailCount++;
      if (detailCount>=8 && (int)unsupportedFxVals.size()>detailCount) {
        logW("  plus %d other unsupported effect value(s).",(int)unsupportedFxVals.size()-detailCount);
        break;
      }
    }
  }
  return true;
}
