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
#include "mod_converter_common.h"
#include "engine/engine.h"

struct NeoGeoMultiChannelInstrumentKeyForConvert {
  int sample;
  int note;
  int frames;
  int offset;

  NeoGeoMultiChannelInstrumentKeyForConvert(int s=0, int n=0, int f=0, int o=0):
    sample(s),
    note(n),
    frames(f),
    offset(o) {
  }

  bool operator<(const NeoGeoMultiChannelInstrumentKeyForConvert& other) const {
    if (sample!=other.sample) return sample<other.sample;
    if (note!=other.note) return note<other.note;
    if (frames!=other.frames) return frames<other.frames;
    return offset<other.offset;
  }
};

struct NeoGeoMultiChannelEventKeyForConvert {
  int ch;
  int order;
  int row;

  NeoGeoMultiChannelEventKeyForConvert(int c=0, int o=0, int r=0):
    ch(c),
    order(o),
    row(r) {
  }

  bool operator<(const NeoGeoMultiChannelEventKeyForConvert& other) const {
    if (ch!=other.ch) return ch<other.ch;
    if (order!=other.order) return order<other.order;
    return row<other.row;
  }
};

struct NeoGeoMultiChannelLoopDurationsForConvert {
  std::map<NeoGeoMultiChannelEventKeyForConvert,int> eventFrames;
  std::map<NeoGeoMultiChannelEventKeyForConvert,NeoGeoMultiChannelEventKeyForConvert> eventEndRows;
  std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int> maxFrames;
};

int sourceSampleForMultiChannelConvert(const std::vector<DivInstrument*>& sourceIns, int insIndex);
int addMultiChannelADPCMANoteInstrument(
  const std::vector<DivSample*>& sourceSamples,
  int sourceSample,
  int note,
  int targetADPCMFrames,
  int sourceOffset,
  std::map<NeoGeoMultiChannelInstrumentKeyForConvert,int>& cachedIns,
  int& attenuatedSamples,
  int& loopFittedSamples
);
NeoGeoMultiChannelLoopDurationsForConvert buildLoopedNoteDurationsForMultiChannelConvert(
  DivSubSong* sub,
  std::vector<SavedChannelForConvert>& saved,
  const std::vector<DivInstrument*>& sourceIns,
  const std::vector<DivSample*>& sourceSamples,
  int chans
);
short scaleMODVolumeToADPCMA(short vol);
void deriveInitialMODTimingForNullsound(
  DivSubSong* sub,
  std::vector<SavedChannelForConvert>& saved,
  int chans,
  int& initialSpeed,
  double& initialHz
);
