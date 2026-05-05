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

#include "mod_converter_common.h"

extern DivEngine e;

void saveChannelsForConvert(std::vector<std::vector<SavedChannelForConvert> >& saved, int chans) {
  saved.clear();
  saved.resize(e.song.subsong.size());
  for (size_t si=0; si<e.song.subsong.size(); si++) {
    DivSubSong* sub=e.song.subsong[si];
    saved[si].resize(chans);
    for (int ch=0; ch<chans; ch++) {
      SavedChannelForConvert& dst=saved[si][ch];
      memcpy(dst.orders,sub->orders.ord[ch],DIV_MAX_PATTERNS);
      dst.effectCols=sub->pat[ch].effectCols;
      dst.show=sub->chanShow[ch];
      dst.showOsc=sub->chanShowChanOsc[ch];
      dst.collapse=sub->chanCollapse[ch];
      dst.color=sub->chanColor[ch];
      dst.name=sub->chanName[ch];
      dst.shortName=sub->chanShortName[ch];
      for (int pat=0; pat<DIV_MAX_PATTERNS; pat++) {
        if (sub->pat[ch].data[pat]!=NULL) {
          dst.pat.data[pat]=new DivPattern;
          sub->pat[ch].data[pat]->copyOn(dst.pat.data[pat]);
        }
      }
    }
  }
}

void clearSavedChannels(std::vector<std::vector<SavedChannelForConvert> >& saved) {
  for (std::vector<SavedChannelForConvert>& sub: saved) {
    for (SavedChannelForConvert& ch: sub) {
      ch.pat.wipePatterns();
    }
  }
}

void configureNeoGeoSystemForConvert(const String& systemName) {
  e.song.systemLen=1;
  e.song.system[0]=DIV_SYSTEM_YM2610_FULL;
  e.song.systemChans[0]=MOD_CONVERT_YM2610_CHANNELS;
  e.song.systemVol[0]=1.0f;
  e.song.systemPan[0]=0;
  e.song.systemPanFR[0]=0;
  e.song.systemFlags[0].clear();
  e.song.systemFlags[0].set("clockSel",systemName.find("AES")!=String::npos?1:0);
  e.song.systemName=systemName;
  e.song.recalcChans();
}

void resetSubSongChannelsForConvert(DivSubSong* sub) {
  if (sub==NULL) return;
  for (int ch=0; ch<DIV_MAX_CHANS; ch++) {
    sub->pat[ch].wipePatterns();
    memset(sub->orders.ord[ch],0,DIV_MAX_PATTERNS);
    sub->pat[ch].effectCols=1;
    sub->chanShow[ch]=(ch<e.song.chans);
    sub->chanShowChanOsc[ch]=(ch<e.song.chans);
    sub->chanCollapse[ch]=0;
    sub->chanColor[ch]=0;
    if (ch<e.song.chans) {
      sub->chanName[ch]=e.song.chanDef[ch].name;
      sub->chanShortName[ch]=e.song.chanDef[ch].shortName;
    }
  }
}

void markSampleForADPCMAForConvert(DivSample* sample) {
  if (sample==NULL) return;
  for (int mem=0; mem<DIV_MAX_SAMPLE_TYPE; mem++) {
    for (int chip=0; chip<DIV_MAX_CHIPS; chip++) {
      sample->renderOn[mem][chip]=false;
    }
  }
  sample->renderOn[0][0]=true;
}

int addSampleForConvert(DivSample* sample) {
  if (sample==NULL) return -1;
  int index=(int)e.song.sample.size();
  e.song.sample.push_back(sample);
  e.song.sampleLen=e.song.sample.size();
  return index;
}

int addADPCMAInstrumentForConvert(const String& name, int sampleIndex) {
  if (e.song.ins.size()>=256) return -1;
  DivInstrument* ins=new DivInstrument;
  ins->type=DIV_INS_ADPCMA;
  ins->name=name;
  ins->amiga.useSample=true;
  ins->amiga.useNoteMap=false;
  ins->amiga.initSample=sampleIndex;
  int index=(int)e.song.ins.size();
  e.song.ins.push_back(ins);
  e.song.insLen=e.song.ins.size();
  return index;
}
