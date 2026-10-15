// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

// Bounds-checked lookup and write helpers for Goldfish Address Space host
// memory mappings managed by AddressSpaceDeviceState.

#include <stddef.h>
#include <stdint.h>

namespace android {
namespace emulation {

// Returns the host pointer for |gpa| if mapped, and optionally writes the
// number of bytes remaining in that mapping starting at |gpa| to
// |outRemainingSize|.
//
// Note: The returned pointer is only valid while the mapping remains
// registered; concurrent unallocation by the guest can invalidate it after
// this call returns. Callers writing to guest mappings should use
// goldfish_address_space_write_memory_mapping() instead.
void* goldfish_address_space_get_host_ptr(uint64_t gpa,
                                          uint64_t* outRemainingSize = nullptr);

// Advisory pre-check that returns true if [gpa, gpa + size) is currently
// completely contained within a single registered memory mapping. Because a
// mapping may be concurrently unmapped after this check returns, any actual
// write must still be performed via
// goldfish_address_space_write_memory_mapping().
bool goldfish_address_space_is_memory_mapping_valid(uint64_t gpa,
                                                    uint64_t size);

// Atomically validates that [gpa, gpa + size) is completely contained within a
// single registered memory mapping and copies |size| bytes from |src| into the
// mapping while holding the memory mappings lock. Returns false if the range
// is unmapped or smaller than |size|.
bool goldfish_address_space_write_memory_mapping(uint64_t gpa,
                                                 const void* src,
                                                 size_t size);

}  // namespace emulation
}  // namespace android
