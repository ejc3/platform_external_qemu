// Copyright 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#pragma once

#include "aemu/base/Compiler.h"
#include "host-common/AndroidPipe.h"

#include "render-utils/RenderChannel.h"
#include "render-utils/Renderer.h"

#include <string>

namespace android {
namespace opengl {

void registerPipeService();

// Releasing the host GPU (android_gpuRelease in opengles.cpp): every GLES pipe saves
// its render channel's state in memory and drops the channel, with the guest paused
// and the renderer's threads paused for a snapshot (Renderer::beginGpuRelease()).
bool releasePipeChannels(std::string* error);
// Taking it up again: every pipe whose channel was released gets a new one, loaded
// from the saved state, on the renderer's new frame buffer. A pipe whose channel the
// renderer refuses keeps its saved state, and a later call tries it again.
bool retakePipeChannels(const gfxstream::RendererPtr& renderer, std::string* error);
// Whether a pipe still holds a released channel's state.
bool pipeChannelsReleased();

}  // namespace opengl
}  // namespace android
