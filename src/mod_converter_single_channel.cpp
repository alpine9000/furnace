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
#include <stdint.h>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif
#include "pch.h"
#ifdef HAVE_SNDFILE
#include "engine/sfWrapper.h"
#endif
#include "mod_converter_common.h"
#include "mod_converter_effects.h"
#include "mod_converter_internal.h"
#include "ta-log.h"
#include "fileutils.h"
#include "engine/engine.h"

#ifdef _WIN32
#define getpid _getpid
#endif

extern DivEngine e;

struct RenderedModChannelForConvert {
  std::vector<float> mono;
  int rate;
};

struct NeoGeoRenderedStreamForConvert {
  RenderedModChannelForConvert rendered;
  std::vector<int> sourceChannels;
  int dest;
  String name;

  NeoGeoRenderedStreamForConvert():
    dest(7) {
  }
};

struct NeoGeoRenderedSliceForConvert {
  int sourceOrder;
  int sourceStart;
  int sourceFrames;
  int targetFrames;
  int entryRow;
  int breakRow;
  int breakValue;
  int jumpSourceOrder;
  int jumpState;

  NeoGeoRenderedSliceForConvert():
    sourceOrder(0),
    sourceStart(0),
    sourceFrames(1),
    targetFrames(1),
    entryRow(0),
    breakRow(-1),
    breakValue(0),
    jumpSourceOrder(-1),
    jumpState(-1) {
  }
};

static String modConvertTempDir() {
  const char* envNames[]={
#ifdef _WIN32
    "TEMP",
    "TMP",
    "USERPROFILE",
#else
    "TMPDIR",
    "TEMP",
    "TMP",
#endif
    NULL
  };
  for (int i=0; envNames[i]!=NULL; i++) {
    const char* value=getenv(envNames[i]);
    if (value!=NULL && value[0]!=0 && dirExists(value)) return value;
  }
  String configPath=e.getConfigPath();
  if (!configPath.empty() && dirExists(configPath.c_str())) return configPath;
  return ".";
}

static String modConvertTempPrefix() {
  String tempDir=modConvertTempDir();
  for (int attempt=0; attempt<64; attempt++) {
    String prefix=tempDir+DIR_SEPARATOR_STR+fmt::sprintf("mod-to-neogeo-%d-%d",(int)getpid(),attempt);
    int touchResult=touchFile(prefix.c_str());
    if (touchResult==0) return prefix;
    if (touchResult!=-EEXIST) break;
  }
  return "";
}

static void cleanupRenderedChannelWavs(const String& prefix, int channels) {
  if (prefix.empty()) return;
  for (int ch=0; ch<channels; ch++) {
    deleteFile(fmt::sprintf("%s_c%02d.wav",prefix.c_str(),ch+1).c_str());
  }
  deleteFile(prefix.c_str());
}

static bool readRenderedChannelWav(const String& path, RenderedModChannelForConvert& out) {
#ifndef HAVE_SNDFILE
  logE("Furnace was not compiled with libsndfile; cannot render MOD channel slices.");
  return false;
#else
  SF_INFO si;
  memset(&si,0,sizeof(SF_INFO));
  SFWrapper wrap;
  SNDFILE* sf=wrap.doOpen(path.c_str(),SFM_READ,&si);
  if (sf==NULL) {
    logE("could not read rendered channel WAV: %s",path.c_str());
    return false;
  }
  if (si.frames<1 || si.channels<1) {
    wrap.doClose();
    logE("rendered channel WAV was empty: %s",path.c_str());
    return false;
  }

  std::vector<float> interleaved;
  interleaved.resize((size_t)si.frames*si.channels);
  sf_count_t gotFrames=sf_readf_float(sf,interleaved.data(),si.frames);
  wrap.doClose();
  if (gotFrames<1) {
    logE("could not read samples from rendered channel WAV: %s",path.c_str());
    return false;
  }

  out.rate=si.samplerate;
  out.mono.clear();
  out.mono.resize(gotFrames);
  for (sf_count_t i=0; i<gotFrames; i++) {
    float selected=0.0f;
    for (int ch=0; ch<si.channels; ch++) {
      float next=interleaved[(size_t)i*si.channels+ch];
      if (fabs(next)>fabs(selected)) selected=next;
    }
    out.mono[i]=selected;
  }
  return true;
#endif
}

static bool renderModChannelsForConvert(int channels, std::vector<RenderedModChannelForConvert>& rendered) {
#ifndef HAVE_SNDFILE
  logE("Furnace was not compiled with libsndfile; cannot render MOD channel slices.");
  return false;
#else
  String prefix=modConvertTempPrefix();
  if (prefix.empty()) {
    logE("could not create temporary render prefix for MOD channel slices.");
    return false;
  }
  DivAudioExportOptions opts;
  opts.mode=DIV_EXPORT_MODE_MANY_CHAN;
  opts.format=DIV_EXPORT_FORMAT_WAV;
  opts.wavFormat=DIV_EXPORT_WAV_F32;
  opts.sampleRate=44100;
  opts.chans=2;
  opts.loops=0;
  opts.fadeOut=0.0;
  for (int i=0; i<DIV_MAX_CHANS; i++) {
    opts.channelMask[i]=(i<channels);
  }

  if (!e.saveAudio(prefix.c_str(),opts)) {
    logE("could not begin per-channel MOD render.");
    cleanupRenderedChannelWavs(prefix,channels);
    return false;
  }
  e.waitAudioFile();
  e.finishAudioFile();

  rendered.clear();
  rendered.resize(channels);
  for (int ch=0; ch<channels; ch++) {
    String path=fmt::sprintf("%s_c%02d.wav",prefix.c_str(),ch+1);
    if (!readRenderedChannelWav(path,rendered[ch])) {
      cleanupRenderedChannelWavs(prefix,channels);
      return false;
    }
  }
  cleanupRenderedChannelWavs(prefix,channels);
  return true;
#endif
}

static RenderedModChannelForConvert mixRenderedChannelsForConvert(const std::vector<RenderedModChannelForConvert>& rendered, const std::vector<int>& sources) {
  RenderedModChannelForConvert ret;
  if (sources.empty()) return ret;
  ret.rate=rendered[sources[0]].rate;
  size_t maxFrames=0;
  for (int source: sources) {
    if (source>=0 && source<(int)rendered.size()) {
      maxFrames=MAX(maxFrames,rendered[source].mono.size());
    }
  }
  ret.mono.assign(maxFrames,0.0f);
  for (int source: sources) {
    if (source<0 || source>=(int)rendered.size()) continue;
    for (size_t i=0; i<rendered[source].mono.size(); i++) {
      ret.mono[i]+=rendered[source].mono[i];
    }
  }
  float peak=0.0f;
  for (float& sample: ret.mono) {
    peak=MAX(peak,(float)fabs(sample));
  }
  if (peak>0.0f) {
    const float targetPeak=24576.0f/32768.0f;
    float gain=targetPeak/peak;
    for (float& sample: ret.mono) {
      sample*=gain;
    }
  }
  return ret;
}

static std::vector<short> buildRenderedSlicePCM(const RenderedModChannelForConvert& rendered, int startFrame, int sourceFrameCount, int targetFrameCount, int targetRate) {
  if (sourceFrameCount<1) sourceFrameCount=1;
  if (targetFrameCount<1) targetFrameCount=1;
  int frameCount=MAX(1,(int)round((double)targetFrameCount*(double)targetRate/(double)rendered.rate));
  if (frameCount&1) frameCount++;
  std::vector<short> pcm(frameCount);
  for (int i=0; i<frameCount; i++) {
    double srcPos=(double)startFrame+((double)i*(double)sourceFrameCount/(double)frameCount);
    int pos=(int)floor(srcPos);
    double frac=srcPos-pos;
    float s1=0.0f;
    float s2=0.0f;
    if (pos>=0 && pos<(int)rendered.mono.size()) s1=rendered.mono[pos];
    if (pos+1>=0 && pos+1<(int)rendered.mono.size()) s2=rendered.mono[pos+1];
    float next=(float)(s1+(s2-s1)*frac);
    pcm[i]=(short)MAX(-32768,MIN(32767,(int)round(next*32767.0f)));
  }
  return pcm;
}

static String renderedSliceKey(const std::vector<short>& pcm, int targetRate, int centerRate) {
  uint64_t hash=1469598103934665603ULL;
  for (short sample: pcm) {
    unsigned short word=(unsigned short)sample;
    hash^=(unsigned char)(word&0xff);
    hash*=1099511628211ULL;
    hash^=(unsigned char)((word>>8)&0xff);
    hash*=1099511628211ULL;
  }
  return fmt::sprintf("%d:%d:%d:%016llx",targetRate,centerRate,(int)pcm.size(),(unsigned long long)hash);
}

static int addRenderedSliceSampleFromPCM(const std::vector<short>& pcm, int centerRate, const String& name) {
  DivSample* sample=new DivSample;
  sample->name=name;
  sample->depth=DIV_SAMPLE_DEPTH_16BIT;
  sample->centerRate=centerRate;
  sample->loop=false;
  sample->loopStart=-1;
  sample->loopEnd=-1;
  markSampleForADPCMAForConvert(sample);
  if (!sample->init((int)pcm.size())) {
    delete sample;
    return -1;
  }
  for (size_t i=0; i<pcm.size(); i++) {
    sample->data16[i]=pcm[i];
  }
  sample->render(1U<<DIV_SAMPLE_DEPTH_ADPCM_A);
  sample->depth=DIV_SAMPLE_DEPTH_ADPCM_A;
  return addSampleForConvert(sample);
}

static double quantizeNSSPlaybackHz(double hz) {
  int tempoByte=(int)round(256.0-(4000000.0/(1152.0*hz)));
  if (tempoByte<0) tempoByte=0;
  if (tempoByte>255) tempoByte=255;
  int divider=256-tempoByte;
  if (divider<1) divider=1;
  return 4000000.0/(1152.0*(double)divider);
}

bool convertLoadedModToNeoGeoSingleChannel() {
  if (e.song.version!=DIV_VERSION_MOD) {
    logE("Neo Geo single-channel conversion currently expects an imported MOD file.");
    return false;
  }

  const int oldChans=e.song.chans;
  if (oldChans<1) {
    logE("cannot convert a song with no channels.");
    return false;
  }

  const int renderChans=MIN(oldChans,4);
  const double adpcmAFixedRate=MOD_CONVERT_ADPCMA_RATE;

  std::vector<std::vector<SavedChannelForConvert> > saved;
  saveChannelsForConvert(saved,oldChans);

  std::vector<RenderedModChannelForConvert> rendered;
  if (!renderModChannelsForConvert(renderChans,rendered)) {
    clearSavedChannels(saved);
    return false;
  }

  DivSubSong* sourceSub=e.song.subsong[0];
  std::vector<NeoGeoRenderedSliceForConvert> slices;
  std::map<String,int> sliceStateBySourceEntry;
  std::vector<std::vector<bool> > visitedRows(sourceSub->ordersLen,std::vector<bool>(sourceSub->patLen,false));
  ModConverterTimingState timing((sourceSub->hz>0)?sourceSub->hz:50.0);
  int playbackSpeed=timing.speed;
  double playbackHz=quantizeNSSPlaybackHz(timing.hz);
  double sourceFramePos=0.0;
  double targetFramePos=0.0;
  bool firstRenderedRow=true;
  int ord=0;
  int rowStart=0;
  while (ord>=0 && ord<sourceSub->ordersLen && rowStart>=0 && rowStart<sourceSub->patLen && !visitedRows[ord][rowStart]) {
    if (slices.size()>=DIV_MAX_PATTERNS) {
      clearSavedChannels(saved);
      logE("single-channel MOD conversion exceeded the maximum of %d generated order states.",DIV_MAX_PATTERNS);
      return false;
    }
    int sliceIndex=(int)slices.size();
    slices.push_back(NeoGeoRenderedSliceForConvert());
    NeoGeoRenderedSliceForConvert& slice=slices.back();
    slice.sourceOrder=ord;
    slice.entryRow=rowStart;
    sliceStateBySourceEntry[fmt::sprintf("%d:%d",ord,rowStart)]=sliceIndex;

    double sourceOrderStart=sourceFramePos;
    double targetOrderStart=targetFramePos;
    slice.sourceStart=(int)round(sourceOrderStart);
    visitedRows[ord][rowStart]=true;
    int nextOrderOverride=-1;
    int nextRowOverride=0;
    for (int row=rowStart; row<sourceSub->patLen; row++) {
      bool endOrderAfterRow=false;

      for (int ch=0; ch<oldChans; ch++) {
        int patIndex=sourceSub->orders.ord[ch][ord];
        DivPattern* pat=sourceSub->pat[ch].getPattern(patIndex,false);
        if (pat==NULL) continue;
        for (int fx=0; fx<sourceSub->pat[ch].effectCols; fx++) {
          short fxTyp=pat->newData[row][DIV_PAT_FX(fx)];
          short fxVal=pat->newData[row][DIV_PAT_FXVAL(fx)];
          if (timing.applyTempoEffect(fxTyp,fxVal)) {
            continue;
          }
          int jumpRow=-1;
          if (modConvertEffectEndsOrder(fxTyp,fxVal,ord,nextOrderOverride,jumpRow)) {
            endOrderAfterRow=true;
            if (jumpRow>=0) nextRowOverride=CLAMP(jumpRow,0,sourceSub->patLen-1);
          }
        }
      }

      if (firstRenderedRow) {
        playbackSpeed=timing.speed;
        playbackHz=quantizeNSSPlaybackHz(timing.hz);
        firstRenderedRow=false;
      }
      double effectiveHz=quantizeNSSPlaybackHz(timing.hz);
      double sourceRowFrames=timing.rowFrames(44100.0);
      double targetRowFrames=(double)timing.speed*44100.0/effectiveHz;
      sourceFramePos+=sourceRowFrames;
      targetFramePos+=targetRowFrames;
      if (endOrderAfterRow) {
        slice.breakRow=row;
        slice.breakValue=nextRowOverride;
        slice.jumpSourceOrder=nextOrderOverride;
        break;
      }
    }
    slice.sourceFrames=MAX(1,(int)round(sourceFramePos)-(int)round(sourceOrderStart));
    slice.targetFrames=MAX(1,(int)round(targetFramePos)-(int)round(targetOrderStart));
    logD("source order %d entry %d pat %d frames %d target %d break %d jump %d",
      ord,
      slice.entryRow,
      sourceSub->orders.ord[0][ord],
      slice.sourceFrames,
      slice.targetFrames,
      slice.breakRow,
      slice.jumpSourceOrder
    );
    if (slice.jumpSourceOrder>=0) {
      rowStart=slice.breakValue;
      ord=slice.jumpSourceOrder;
    } else {
      rowStart=0;
      ord++;
    }
  }
  for (NeoGeoRenderedSliceForConvert& slice: slices) {
    if (slice.jumpSourceOrder<0) continue;
    std::map<String,int>::iterator foundState=sliceStateBySourceEntry.find(fmt::sprintf("%d:%d",slice.jumpSourceOrder,slice.breakValue));
    if (foundState!=sliceStateBySourceEntry.end()) {
      slice.jumpState=foundState->second;
    } else {
      logW("source order %d jumps to unreachable source state %d:%d; generated pattern will stop following that jump.",slice.sourceOrder,slice.jumpSourceOrder,slice.breakValue);
    }
  }
  for (int fallbackOrd=0; fallbackOrd<sourceSub->ordersLen; fallbackOrd++) {
    bool reached=false;
    for (int row=0; row<sourceSub->patLen; row++) {
      if (visitedRows[fallbackOrd][row]) {
        reached=true;
        break;
      }
    }
    if (!reached) {
      logW("order %d was not reached during MOD playback traversal; it was omitted from the single-channel render order list.",fallbackOrd);
    }
  }
  const int sliceCount=(int)slices.size();
  if (sliceCount<1) {
    clearSavedChannels(saved);
    logE("single-channel MOD conversion did not find any reachable playback states.");
    return false;
  }
  for (DivInstrument* ins: e.song.ins) delete ins;
  e.song.ins.clear();
  e.song.insLen=0;
  for (DivSample* sample: e.song.sample) delete sample;
  e.song.sample.clear();
  e.song.sampleLen=0;

  configureNeoGeoSystemForConvert("Neo Geo MVS");

  std::vector<int> allSources;
  for (int src=0; src<renderChans; src++) allSources.push_back(src);

  std::vector<NeoGeoRenderedStreamForConvert> streams;
  RenderedModChannelForConvert fullMix=mixRenderedChannelsForConvert(rendered,allSources);
  NeoGeoRenderedStreamForConvert aStream;
  aStream.rendered=fullMix;
  aStream.sourceChannels=allSources;
  aStream.dest=MOD_CONVERT_ADPCMA_CHANNEL_BASE;
  aStream.name="MOD fullmix A0";
  streams.push_back(aStream);

  std::vector<std::vector<int> > sliceSamples(streams.size());
  std::vector<std::vector<int> > sliceInstruments(streams.size());
  std::map<String,std::pair<int,int> > dedupedSlices;
  int uniqueSlices=0;
  int reusedSlices=0;
  for (size_t streamIndex=0; streamIndex<streams.size(); streamIndex++) {
    const NeoGeoRenderedStreamForConvert& stream=streams[streamIndex];
    int targetRate=(int)round(adpcmAFixedRate);
    int centerRate=targetRate;
    sliceSamples[streamIndex].assign(sliceCount,-1);
    sliceInstruments[streamIndex].assign(sliceCount,-1);
    for (int sliceIndex=0; sliceIndex<sliceCount; sliceIndex++) {
      const NeoGeoRenderedSliceForConvert& slice=slices[sliceIndex];
      std::vector<short> pcm=buildRenderedSlicePCM(
        stream.rendered,
        slice.sourceStart,
        slice.sourceFrames,
        slice.targetFrames,
        targetRate
      );
      String pcmKey=renderedSliceKey(pcm,targetRate,centerRate);
      std::map<String,std::pair<int,int> >::iterator found=dedupedSlices.find(pcmKey);
      if (found!=dedupedSlices.end()) {
        sliceSamples[streamIndex][sliceIndex]=found->second.first;
        sliceInstruments[streamIndex][sliceIndex]=found->second.second;
        reusedSlices++;
        logD("reusing PCM slice for stream %d state %d",streamIndex,sliceIndex);
        continue;
      }

      String sliceName=slice.entryRow>0?
        fmt::sprintf("%s order%d row%d",stream.name.c_str(),slice.sourceOrder,slice.entryRow):
        fmt::sprintf("%s order%d",stream.name.c_str(),slice.sourceOrder);
      int sampleIndex=addRenderedSliceSampleFromPCM(
        pcm,
        centerRate,
        sliceName
      );
      if (sampleIndex<0) {
        clearSavedChannels(saved);
        return false;
      }
      sliceSamples[streamIndex][sliceIndex]=sampleIndex;
      int insIndex=addADPCMAInstrumentForConvert(sliceName,sampleIndex);
      if (insIndex<0) {
        logE("cannot add slice instrument; instrument limit exceeded.");
        clearSavedChannels(saved);
        return false;
      }
      sliceInstruments[streamIndex][sliceIndex]=insIndex;
      dedupedSlices[pcmKey]=std::pair<int,int>(sampleIndex,insIndex);
      uniqueSlices++;
    }
  }

  DivSubSong* sub=e.song.subsong[0];
  sub->hz=playbackHz;
  sub->speeds.len=1;
  sub->speeds.val[0]=playbackSpeed;
  resetSubSongChannelsForConvert(sub);
  sub->ordersLen=sliceCount;

  for (size_t streamIndex=0; streamIndex<streams.size(); streamIndex++) {
    const NeoGeoRenderedStreamForConvert& stream=streams[streamIndex];
    int dest=stream.dest;
    sub->pat[dest].effectCols=2;
    std::map<String,int> reusedPatterns;
    for (int sliceIndex=0; sliceIndex<sub->ordersLen; sliceIndex++) {
      const NeoGeoRenderedSliceForConvert& slice=slices[sliceIndex];
      int flowFx0=MOD_CONVERT_FX_NONE;
      int flowFxVal0=MOD_CONVERT_FX_NONE;
      int flowFx1=MOD_CONVERT_FX_NONE;
      int flowFxVal1=MOD_CONVERT_FX_NONE;
      if (slice.breakRow>=0 && slice.jumpState>=0) {
        if (slice.jumpState!=(sliceIndex+1)) {
          flowFx0=MOD_CONVERT_FX_ORDER_JUMP;
          flowFxVal0=slice.jumpState;
          if (slice.breakValue>0) {
            flowFx1=MOD_CONVERT_FX_PATTERN_BREAK;
            flowFxVal1=slice.breakValue;
          }
        } else {
          flowFx0=MOD_CONVERT_FX_PATTERN_BREAK;
          flowFxVal0=slice.breakValue;
        }
      }
      String patternKey=fmt::sprintf(
        "%d:%d:%d:%d:%d:%d:%d:%d:%d",
        dest,
        sliceSamples[streamIndex][sliceIndex],
        sliceInstruments[streamIndex][sliceIndex],
        slice.entryRow,
        slice.breakRow,
        flowFx0,
        flowFxVal0,
        flowFx1,
        flowFxVal1
      );
      std::map<String,int>::iterator reusedPattern=reusedPatterns.find(patternKey);
      if (reusedPattern!=reusedPatterns.end()) {
        sub->orders.ord[dest][sliceIndex]=reusedPattern->second;
        continue;
      }
      reusedPatterns[patternKey]=sliceIndex;
      sub->orders.ord[dest][sliceIndex]=sliceIndex;
      DivPattern* pat=sub->pat[dest].getPattern(sliceIndex,true);
      for (int row=0; row<sub->patLen; row++) {
        if (row==slice.entryRow && sliceSamples[streamIndex][sliceIndex]>=0) {
          pat->newData[row][DIV_PAT_NOTE]=108;
          pat->newData[row][DIV_PAT_INS]=sliceInstruments[streamIndex][sliceIndex];
          pat->newData[row][DIV_PAT_VOL]=31;
        }
        if (row==slice.breakRow) {
          if (flowFx0!=MOD_CONVERT_FX_NONE) {
            pat->newData[row][DIV_PAT_FX(0)]=flowFx0;
            pat->newData[row][DIV_PAT_FXVAL(0)]=flowFxVal0;
          }
          if (flowFx1!=MOD_CONVERT_FX_NONE) {
            pat->newData[row][DIV_PAT_FX(1)]=flowFx1;
            pat->newData[row][DIV_PAT_FXVAL(1)]=flowFxVal1;
          }
        }
      }
    }
  }

  size_t removedSubSongs=0;
  while (e.song.subsong.size()>1) {
    DivSubSong* removed=e.song.subsong.back();
    removed->clearData();
    delete removed;
    e.song.subsong.pop_back();
    removedSubSongs++;
  }
  if (removedSubSongs>0) {
    logW("removed %d additional subsong(s); the single-channel converter renders only the first MOD subsong.",(int)removedSubSongs);
  }

  clearSavedChannels(saved);

  logW("converted MOD to Neo Geo using a single ADPCM-A full-mix stream.");
  logW("created %d unique rendered phrase sample(s) across %d rendered output stream(s); reused %d duplicate phrase reference(s).",uniqueSlices,(int)streams.size(),reusedSlices);
  if (oldChans>renderChans) {
    logW("channels after MOD channel %d were not converted by the single-channel converter.",renderChans);
  }
  return true;
}
