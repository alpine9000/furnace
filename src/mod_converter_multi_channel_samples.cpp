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

int addMultiChannelADPCMANoteInstrument(
  const std::vector<DivSample*>& sourceSamples,
  int sourceSample,
  int note,
  int targetADPCMFrames,
  int sourceOffset,
  std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int>& cachedIns,
  int& attenuatedSamples,
  int& loopFittedSamples
) {
  NeoGeoMultiChannelInstrumentKeyForConvert key(sourceSample,note,targetADPCMFrames,sourceOffset);
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
  dst->depth=DIV_SAMPLE_DEPTH_16BIT;
  dst->centerRate=src->centerRate;
  dst->loop=false;
  dst->loopStart=-1;
  dst->loopEnd=-1;
  if (!dst->init(src->samples-sourceOffset)) {
    delete dst;
    return -1;
  }

  src->render(1U<<DIV_SAMPLE_DEPTH_16BIT);
  for (unsigned int i=0; i<dst->samples; i++) {
    dst->data16[i]=(src->data16!=NULL)?src->data16[sourceOffset+i]:0;
  }

  double sourceRate=(src->centerRate>0)?src->centerRate:8363.0;
  // Imported MOD notes use Furnace note 108 as the ProTracker/native sample pitch.
  sourceRate*=pow(2.0,(double)(note-108)/12.0);
  if (sourceRate<1.0) sourceRate=1.0;
  if (targetADPCMFrames>0) {
    int targetSourceSamples=(int)ceil((double)targetADPCMFrames*sourceRate/adpcmARate);
    if (fitLoopedSampleForADPCMANote(dst,src,targetSourceSamples,sourceOffset)) loopFittedSamples++;
  }

  dst->centerRate=(int)round(sourceRate);
  if (!dst->resample(sourceRate,adpcmARate,DIV_RESAMPLE_LINEAR)) {
    delete dst;
    return -1;
  }
  if (applyADPCMAEncodeHeadroom(dst)) attenuatedSamples++;
  dst->centerRate=(int)round(adpcmARate);
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
