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
#include "ta-log.h"

extern DivEngine e;

int sourceSampleForMultiChannelConvert(const std::vector<DivInstrument*>& sourceIns, int insIndex) {
  if (insIndex<0 || insIndex>=(int)sourceIns.size() || sourceIns[insIndex]==NULL) return insIndex;
  DivInstrument* ins=sourceIns[insIndex];
  if (ins->amiga.useSample || ins->type==DIV_INS_AMIGA) return ins->amiga.initSample;
  return insIndex;
}

static bool applyADPCMAEncodeHeadroom(DivSample* sample) {
  if (sample==NULL || sample->data16==NULL || sample->samples<1) return false;
  int peak=0;
  for (unsigned int i=0; i<sample->samples; i++) {
    int v=sample->data16[i];
    int absV=(v==-32768)?32768:abs(v);
    if (absV>peak) peak=absV;
  }

  const int targetPeak=24576;
  if (peak<=targetPeak) return false;

  double gain=(double)targetPeak/(double)peak;
  for (unsigned int i=0; i<sample->samples; i++) {
    sample->data16[i]=(short)round((double)sample->data16[i]*gain);
  }
  return true;
}

static bool fitLoopedSampleForADPCMANote(DivSample* dst, DivSample* src, int targetSamples, int sourceOffset) {
  if (dst==NULL || src==NULL || dst->data16==NULL || src->data16==NULL) return false;
  if (!src->loop || src->loopStart<0 || src->loopEnd<=src->loopStart) return false;
  if (targetSamples<1 || targetSamples==(int)dst->samples) return false;

  int loopStart=MIN((int)src->samples,MAX(0,src->loopStart));
  int loopEnd=MIN((int)src->samples,MAX(loopStart+1,src->loopEnd));
  int loopLen=loopEnd-loopStart;
  if (loopLen<1) return false;

  unsigned int oldSamples=dst->samples;
  if (!dst->resize(targetSamples)) return false;
  for (unsigned int i=oldSamples; i<dst->samples; i++) {
    int sourcePos=sourceOffset+(int)i;
    int loopOffset=(sourcePos-loopStart)%loopLen;
    if (loopOffset<0) loopOffset+=loopLen;
    dst->data16[i]=src->data16[loopStart+loopOffset];
  }
  return true;
}

static short readSampleLinear16(DivSample* src, double pos, int sourceOffset) {
  if (src==NULL || src->data16==NULL || src->samples<1) return 0;
  double sourcePos=(double)sourceOffset+pos;
  int sampleCount=(int)src->samples;
  if (src->loop && src->loopStart<src->loopEnd) {
    int loopStart=MIN(sampleCount,MAX(0,src->loopStart));
    int loopEnd=MIN(sampleCount,MAX(loopStart+1,src->loopEnd));
    int loopLen=loopEnd-loopStart;
    if (sourcePos>=(double)loopEnd && loopLen>0) {
      double loopPos=fmod(sourcePos-(double)loopStart,(double)loopLen);
      if (loopPos<0.0) loopPos+=(double)loopLen;
      sourcePos=(double)loopStart+loopPos;
    }
  }
  if (sourcePos<0.0 || sourcePos>=(double)(sampleCount-1)) {
    int nearest=(int)floor(sourcePos);
    if (nearest>=0 && nearest<sampleCount) return src->data16[nearest];
    return 0;
  }
  int posInt=(int)floor(sourcePos);
  double frac=sourcePos-(double)posInt;
  double a=(double)src->data16[posInt];
  double b=(double)src->data16[posInt+1];
  return (short)round(a+(b-a)*frac);
}

static bool renderArpeggiatedADPCMANote(
  DivSample* dst,
  DivSample* src,
  int note,
  int targetADPCMFrames,
  int sourceOffset,
  int arpeggio,
  int speed,
  double hz
) {
  if (dst==NULL || src==NULL || src->data16==NULL || src->samples<1 || arpeggio<=0) return false;
  const double adpcmARate=MOD_CONVERT_ADPCMA_RATE;
  double sourceRate=(src->centerRate>0)?src->centerRate:8363.0;
  sourceRate*=pow(2.0,(double)(note-108)/12.0);
  if (sourceRate<1.0) sourceRate=1.0;
  if (hz<=0.0) hz=50.0;
  if (speed<1) speed=6;

  int outFrames=targetADPCMFrames;
  if (outFrames<1) {
    outFrames=(int)ceil((double)MAX(1,(int)src->samples-sourceOffset)*adpcmARate/sourceRate);
  }
  if (!dst->resize(outFrames)) return false;

  const int arpNotes[3]={0,(arpeggio>>4)&15,arpeggio&15};
  const double tickFrames=adpcmARate/hz;
  double sourcePos=0.0;
  for (unsigned int i=0; i<dst->samples; i++) {
    int tick=(tickFrames>0.0)?(int)floor((double)i/tickFrames):0;
    int arpNote=arpNotes[tick%3];
    double step=(sourceRate*pow(2.0,(double)arpNote/12.0))/adpcmARate;
    dst->data16[i]=readSampleLinear16(src,sourcePos,sourceOffset);
    sourcePos+=step;
  }
  dst->centerRate=(int)round(adpcmARate);
  return true;
}

static bool renderPitchEventADPCMANote(
  DivSample* dst,
  DivSample* src,
  int note,
  int targetADPCMFrames,
  int sourceOffset,
  int arpeggio,
  int speed,
  double hz,
  const std::vector<NeoGeoMultiChannelPitchEventForConvert>& pitchEvents
) {
  if (dst==NULL || src==NULL || src->data16==NULL || src->samples<1) return false;
  const double adpcmARate=MOD_CONVERT_ADPCMA_RATE;
  double sourceRate=(src->centerRate>0)?src->centerRate:8363.0;
  sourceRate*=pow(2.0,(double)(note-108)/12.0);
  if (sourceRate<1.0) sourceRate=1.0;
  if (hz<=0.0) hz=50.0;
  if (speed<1) speed=6;

  int outFrames=targetADPCMFrames;
  if (outFrames<1) {
    outFrames=(int)ceil((double)MAX(1,(int)src->samples-sourceOffset)*adpcmARate/sourceRate);
  }
  if (!dst->resize(outFrames)) return false;

  const int arpNotes[3]={0,(arpeggio>>4)&15,arpeggio&15};
  const double tickFrames=adpcmARate/hz;
  double sourcePos=0.0;
  short vibrato=0;
  double vibratoPhase=0.0;
  double slideSemitoneOffset=0.0;
  double slideSemitonesPerFrame=0.0;
  size_t eventIndex=0;
  for (unsigned int i=0; i<dst->samples; i++) {
    while (eventIndex<pitchEvents.size() && pitchEvents[eventIndex].frame<=(int)i) {
      if (pitchEvents[eventIndex].fx==MOD_CONVERT_FX_VIBRATO) {
        vibrato=pitchEvents[eventIndex].fxVal;
      } else if (pitchEvents[eventIndex].fx==MOD_CONVERT_FX_PORTAMENTO_UP) {
        slideSemitonesPerFrame=(double)MAX(0,(int)pitchEvents[eventIndex].fxVal)/(64.0*tickFrames);
      } else if (pitchEvents[eventIndex].fx==MOD_CONVERT_FX_PORTAMENTO_DOWN) {
        slideSemitonesPerFrame=-(double)MAX(0,(int)pitchEvents[eventIndex].fxVal)/(64.0*tickFrames);
      }
      eventIndex++;
    }

    double semitoneOffset=0.0;
    if (arpeggio>0) {
      int tick=(tickFrames>0.0)?(int)floor((double)i/tickFrames):0;
      semitoneOffset+=(double)arpNotes[tick%3];
    }
    if (vibrato>0) {
      int vibSpeed=(vibrato>>4)&15;
      int vibDepth=vibrato&15;
      if (vibSpeed<1) vibSpeed=1;
      // MOD vibrato depth is in pitch-slide units. This approximation keeps it musical without over-bending ADPCM samples.
      semitoneOffset+=sin(vibratoPhase)*(double)vibDepth/32.0;
      vibratoPhase+=((double)vibSpeed*2.0*3.14159265358979323846)/(64.0*tickFrames);
    }
    semitoneOffset+=slideSemitoneOffset;

    double step=(sourceRate*pow(2.0,semitoneOffset/12.0))/adpcmARate;
    dst->data16[i]=readSampleLinear16(src,sourcePos,sourceOffset);
    sourcePos+=step;
    slideSemitoneOffset+=slideSemitonesPerFrame;
  }
  dst->centerRate=(int)round(adpcmARate);
  return true;
}

int addMultiChannelADPCMANoteInstrument(
  const std::vector<DivSample*>& sourceSamples,
  int sourceSample,
  int note,
  int targetADPCMFrames,
  int sourceOffset,
  int arpeggio,
  int speed,
  double hz,
  const std::vector<NeoGeoMultiChannelPitchEventForConvert>& pitchEvents,
  std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int>& cachedIns,
  int& attenuatedSamples,
  int& loopFittedSamples
) {
  NeoGeoMultiChannelInstrumentKeyForConvert key(sourceSample,note,targetADPCMFrames,sourceOffset,arpeggio,pitchEvents);
  std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int>::iterator found=cachedIns.find(key);
  if (found!=cachedIns.end()) return found->second;
  if (sourceSample<0 || sourceSample>=(int)sourceSamples.size() || sourceSamples[sourceSample]==NULL) return -1;
  if (e.song.ins.size()>=256) return -1;

  DivSample* src=sourceSamples[sourceSample];
  if (src->samples<1) return -1;
  sourceOffset=MIN((int)src->samples-1,MAX(0,sourceOffset));

  const double adpcmARate=MOD_CONVERT_ADPCMA_RATE;
  DivSample* dst=new DivSample;
  dst->name=(sourceOffset>0)?fmt::sprintf("%s n%d o%d",src->name.c_str(),note,sourceOffset):fmt::sprintf("%s n%d",src->name.c_str(),note);
  if (arpeggio>0) dst->name+=fmt::sprintf(" a%02X",arpeggio);
  if (!pitchEvents.empty()) dst->name+=fmt::sprintf(" v%d",(int)pitchEvents.size());
  dst->depth=DIV_SAMPLE_DEPTH_16BIT;
  dst->centerRate=src->centerRate;
  dst->loop=false;
  dst->loopStart=-1;
  dst->loopEnd=-1;
  if (!dst->init(MAX(1,(int)src->samples-sourceOffset))) {
    delete dst;
    return -1;
  }

  src->render(1U<<DIV_SAMPLE_DEPTH_16BIT);
  double sourceRate=(src->centerRate>0)?src->centerRate:8363.0;
  // Imported MOD notes use Furnace note 108 as the ProTracker/native sample pitch.
  sourceRate*=pow(2.0,(double)(note-108)/12.0);
  if (sourceRate<1.0) sourceRate=1.0;
  if (!pitchEvents.empty()) {
    if (!renderPitchEventADPCMANote(dst,src,note,targetADPCMFrames,sourceOffset,arpeggio,speed,hz,pitchEvents)) {
      delete dst;
      return -1;
    }
  } else if (arpeggio>0) {
    if (!renderArpeggiatedADPCMANote(dst,src,note,targetADPCMFrames,sourceOffset,arpeggio,speed,hz)) {
      delete dst;
      return -1;
    }
  } else {
    for (unsigned int i=0; i<dst->samples; i++) {
      dst->data16[i]=(src->data16!=NULL)?src->data16[sourceOffset+i]:0;
    }
    if (targetADPCMFrames>0) {
    int targetSourceSamples=(int)ceil((double)targetADPCMFrames*sourceRate/adpcmARate);
      if (fitLoopedSampleForADPCMANote(dst,src,targetSourceSamples,sourceOffset)) loopFittedSamples++;
    }

    dst->centerRate=(int)round(sourceRate);
    if (!dst->resample(sourceRate,adpcmARate,DIV_RESAMPLE_LINEAR)) {
      delete dst;
      return -1;
    }
    dst->centerRate=(int)round(adpcmARate);
  }
  if (applyADPCMAEncodeHeadroom(dst)) attenuatedSamples++;
  dst->convert(DIV_SAMPLE_DEPTH_ADPCM_A,1U<<DIV_SAMPLE_DEPTH_ADPCM_A);
  markSampleForADPCMAForConvert(dst);

  int sampleIndex=addSampleForConvert(dst);
  int insIndex=addADPCMAInstrumentForConvert(dst->name,sampleIndex);

  cachedIns[key]=insIndex;
  return insIndex;
}

short scaleMODVolumeToADPCMA(short vol) {
  if (vol<0) return -1;
  return (short)MIN(31,MAX(0,(int)round((double)vol*31.0/64.0)));
}
