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

static const int MOD_CONVERT_YM2610_CHANNELS=14;
static const int MOD_CONVERT_ADPCMA_CHANNEL_BASE=7;
static const double MOD_CONVERT_ADPCMA_RATE=18518.0;

struct SavedChannelForConvert {
  DivChannelData pat;
  unsigned char orders[DIV_MAX_PATTERNS];
  unsigned char effectCols;
  bool show;
  bool showOsc;
  unsigned char collapse;
  unsigned int color;
  String name;
  String shortName;

  SavedChannelForConvert():
    effectCols(1),
    show(true),
    showOsc(true),
    collapse(0),
    color(0) {
    memset(orders,0,DIV_MAX_PATTERNS);
  }
};

void saveChannelsForConvert(std::vector<std::vector<SavedChannelForConvert> >& saved, int chans);
void clearSavedChannels(std::vector<std::vector<SavedChannelForConvert> >& saved);
void configureNeoGeoSystemForConvert(const String& systemName);
void resetSubSongChannelsForConvert(DivSubSong* sub);
void markSampleForADPCMAForConvert(DivSample* sample);
int addSampleForConvert(DivSample* sample);
int addADPCMAInstrumentForConvert(const String& name, int sampleIndex);
