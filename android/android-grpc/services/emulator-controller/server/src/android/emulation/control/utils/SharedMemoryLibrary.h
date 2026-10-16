// Copyright (C) 2020 The Android Open Source Project
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

#include <stddef.h>  // for size_t
#ifndef _WIN32
#include <sys/types.h>  // for dev_t, ino_t
#endif

#include <filesystem>     // for path
#include <functional>     // for function
#include <memory>         // for unique_ptr
#include <mutex>          // for mutex
#include <string>         // for string, hash, operator==
#include <unordered_map>  // for unordered_map
#include <vector>         // for vector

#include "absl/status/status.h"             // for Status
#include "absl/status/statusor.h"           // for StatusOr
#include "aemu/base/memory/ScopedPtr.h"     // for makeCustomScopedPtr
#include "aemu/base/memory/SharedMemory.h"  // for SharedMemory

namespace android {
namespace emulation {
namespace control {

using android::base::SharedMemory;

// A SharedMemoryLibrary can be used to borrow shared memory objects that are
// ref counted. You can borrow a shared memory handle for a given region, and
// return it when you are done with it. This makes sure that a memory mapped
// region only has to be initialized once if it is going to be used frequently.
//
// The borrowed objects will be opened with read+write access, and only the
// initial size specification will be used.
//
// Handles are supplied by (untrusted) gRPC clients. File backed handles
// (file:///...) are only accepted if they resolve to a regular file inside one
// of the allowed directories, see defaultAllowedDirectories(). On POSIX hosts
// the file is opened relative to a verified directory descriptor, without
// following symlinks, so the path cannot be swapped between validation and
// use.
//
// For example:
//
// SharedMemoryLibrary lib;
// {
// auto mem = lib.borrow("foo", 124);
// ... do things with *mem ...
// auto mem2 = lib.borrow("foo", 124);
// ... do things with *mem2 (points to same memory region)
// }
class SharedMemoryLibrary {
public:
    using deleter = std::function<void(SharedMemory*)>;
    using LibraryEntry =
            std::unique_ptr<SharedMemory, android::base::FuncDelete<deleter>>;

    // Creates a library that accepts file backed handles in the
    // defaultAllowedDirectories().
    SharedMemoryLibrary();

    // Creates a library that only accepts file backed handles that resolve
    // to a file inside one of the given directories.
    explicit SharedMemoryLibrary(
            std::vector<std::filesystem::path> allowedDirectories);

    // Borrow the given shared memory region, opening it if it has not been
    // opened yet. The LibraryEntry will automatically return the shared memory
    // handle when it goes out of scope.
    //
    // Returns an error status (and logs an error) if the handle is rejected,
    // for example because it is a file backed handle that does not resolve to
    // a file inside one of the allowed directories. The status message
    // explains why the handle was rejected and which directories are allowed.
    absl::StatusOr<LibraryEntry> borrow(std::string handle, size_t size);

    // Checks whether the given handle is acceptable without opening it.
    // Non file backed (named shared memory) handles are always accepted.
    absl::Status validateHandle(const std::string& handle) const;

    // The canonical directories in which file backed handles are accepted.
    const std::vector<std::filesystem::path>& allowedDirectories() const {
        return mAllowedDirectories;
    }

    // The directories in which file backed handles are accepted by default:
    //
    // - The emulator temporary directory (ANDROID_TMP, /tmp/android-$USER or
    //   %TEMP%\AndroidEmulator)
    // - The system temporary directory ($TMPDIR, /tmp, or %TEMP%)
    // - /dev/shm (Linux only)
    // - The emulator discovery directory (where the emulator publishes its
    //   gRPC endpoint, e.g. $XDG_RUNTIME_DIR/avd/running)
    static std::vector<std::filesystem::path> defaultAllowedDirectories();

private:
    // A mapped region, kept alive for as long as there are outstanding
    // LibraryEntry objects that refer to it. This makes sure that replacing
    // a region with a larger one does not unmap memory that is still in use.
    struct Region {
        ~Region();

        std::unique_ptr<SharedMemory> shm;
#ifndef _WIN32
        // Directory that contains a file backed region, and the name of the
        // file inside it. Used to remove a file we created without following
        // (possibly swapped) paths.
        int dirFd = -1;
        std::string fileName;
        bool created = false;
        // Identity of the mapped file.
        dev_t dev = 0;
        ino_t ino = 0;
#endif
    };

    // Resolves a file backed handle and checks that it refers to a (possibly
    // not yet existing) regular file inside one of the allowed directories.
    absl::StatusOr<std::filesystem::path> resolveFileHandle(
            const std::string& handle,
            const SharedMemory& probe) const;

    // Opens (or creates) the resolved file backed region.
    absl::StatusOr<std::shared_ptr<Region>> openFileBacked(
            const std::string& handle,
            const std::filesystem::path& resolved,
            size_t size) const;

    bool isInsideOrEqualAllowedDirectory(const std::filesystem::path& p) const;

    // Returns the given handle, releasing it completely if the refcount is 0.
    void release(std::string handle);

    std::string allowedDirectoriesAsString() const;
    std::string howToFix() const;

    std::vector<std::filesystem::path> mAllowedDirectories;

    std::mutex mAccess;
    std::unordered_map<std::string, int> mHandlesCnt;
    std::unordered_map<std::string, std::shared_ptr<Region>> mMemMap;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
