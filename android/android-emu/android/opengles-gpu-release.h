// Copyright 2026 The Android Open Source Project
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

#include <string>

// Releasing the host GPU from a running emulator, and taking it up again
// (the console's "gpu release" and "gpu retake"). The guest must be paused from
// the release until after the retake.
//
// android_gpuRelease() saves the renderer's state and every GLES pipe's render
// channel in memory, stops the render threads, tears down the frame buffer and
// the GL and Vulkan emulation, and unloads the host EGL and GLES libraries, so
// the process holds no file descriptor or mapping of a GPU device. On failure
// it takes the GPU up again and returns false with |error| set; |restored| says
// whether that brought back the renderer and every channel, so the guest can
// resume (otherwise it must stay paused for a 'gpu retake').
//
// android_gpuRetake() loads the host driver, initializes a frame buffer on the
// GPU present now, loads the saved state into it, recreates every pipe's render
// channel from its saved state and resumes the render threads. After a failure it
// can be called again: it retries what is still released.
//
// android_gpuReleased() is true from a release until a retake has brought back the
// renderer and every channel.
// C linkage: opengles.cpp defines its android_* functions in an extern "C" block.
extern "C" {
bool android_gpuRelease(std::string* error, bool* restored);
bool android_gpuRetake(std::string* error);
bool android_gpuReleased();
}
