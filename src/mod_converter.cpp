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

#include "pch.h"
#include "mod_converter.h"
#include "mod_converter_internal.h"
#include "ta-log.h"
#include "engine/engine.h"

extern DivEngine e;

static String convertTarget;

const char* modConverterTargetsHelp() {
  return "neogeo-single-channel|neogeo-multi-channel|neogeo-multi-channel-pitch";
}

TAParamResult pConvert(String val) {
  if (val!="neogeo-single-channel" && val!="neogeo-multi-channel" && val!="neogeo-multi-channel-pitch") {
    logE("invalid conversion target! valid values are: neogeo-single-channel, neogeo-multi-channel, neogeo-multi-channel-pitch.");
    return TA_PARAM_ERROR;
  }
  convertTarget=val;
  e.setAudio(DIV_AUDIO_DUMMY);
  return TA_PARAM_SUCCESS;
}

bool modConverterHasTarget() {
  return convertTarget!="";
}

bool convertLoadedModForRequestedTarget() {
  if (convertTarget=="neogeo-multi-channel") {
    return convertLoadedModToNeoGeoMultiChannel(false);
  }
  if (convertTarget=="neogeo-multi-channel-pitch") {
    return convertLoadedModToNeoGeoMultiChannel(true);
  }
  if (convertTarget=="neogeo-single-channel") {
    return convertLoadedModToNeoGeoSingleChannel();
  }
  return true;
}
