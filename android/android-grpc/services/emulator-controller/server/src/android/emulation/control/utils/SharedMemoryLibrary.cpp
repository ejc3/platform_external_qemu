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
#include "android/emulation/control/utils/SharedMemoryLibrary.h"

#include <algorithm>  // for find
#include <cassert>    // for assert
#include <cctype>     // for tolower
#include <cerrno>     // for errno
#include <cstring>    // for strerror
#include <optional>   // for optional
#include <string_view>
#include <system_error>  // for error_code
#include <utility>       // for move

#ifndef _WIN32
#include <fcntl.h>     // for open, openat, O_*
#include <sys/stat.h>  // for fstat
#include <unistd.h>    // for close, unlinkat
#ifdef __APPLE__
#include <sys/param.h>  // for MAXPATHLEN
#endif
#endif

#include "aemu/base/Log.h"

#include "aemu/base/files/ScopedFd.h"       // for ScopedFd
#include "aemu/base/memory/ScopedPtr.h"     // for makeCustomScopedPtr
#include "aemu/base/memory/SharedMemory.h"  // for SharedMemory, std::string_view
#include "android/base/system/System.h"     // for System
#include "android/emulation/ConfigDirs.h"   // for ConfigDirs

#define assertm(exp, msg) assert(((void)msg, exp))

namespace android {
namespace emulation {
namespace control {

using android::base::SharedMemory;
namespace fs = std::filesystem;

namespace {

// Resolves symlinks, "." and ".." for the existing part of |path| and
// lexically normalizes the remainder. Strips a trailing separator.
fs::path resolvePath(const fs::path& path, std::error_code& ec) {
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec) {
        return {};
    }
    if (!resolved.has_filename() && resolved != resolved.root_path()) {
        resolved = resolved.parent_path();
    }
    return resolved;
}

bool isLocalAbsolutePath(const fs::path& path) {
    if (path.empty() || !path.is_absolute()) {
        return false;
    }
    // On Windows, PathCreateFromUrlW turns "file://relative/frames.rgb" into
    // the UNC path "\\relative\frames.rgb", which std::filesystem considers
    // absolute. Reject UNC and device roots before calling weakly_canonical so
    // non-local or relative file:// URLs fail with INVALID_ARGUMENT and never
    // trigger outbound SMB probes.
    const auto rootName = path.root_name().u8string();
    if (rootName.size() >= 2 && ((rootName[0] == '\\' && rootName[1] == '\\') ||
                                 (rootName[0] == '/' && rootName[1] == '/'))) {
        return false;
    }
    return true;
}

bool sameComponent(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    // Windows file systems are case insensitive.
    auto lhs = a.u8string();
    auto rhs = b.u8string();
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char l, char r) {
               return std::tolower(static_cast<unsigned char>(l)) ==
                      std::tolower(static_cast<unsigned char>(r));
           });
#else
    return a == b;
#endif
}

// True if |child| is located inside |dir|, or is |dir| itself when
// |allowEqual| is set. Both paths are expected to be resolved.
bool isInside(const fs::path& child, const fs::path& dir, bool allowEqual) {
    auto childIt = child.begin();
    for (const auto& component : dir) {
        if (childIt == child.end() || !sameComponent(*childIt, component)) {
            return false;
        }
        ++childIt;
    }
    // There must be at least one more non empty component.
    for (; childIt != child.end(); ++childIt) {
        if (!childIt->empty()) {
            return true;
        }
    }
    return allowEqual;
}

std::string toFileUri(const fs::path& path) {
#ifdef _WIN32
    return "file:///" + path.generic_u8string();
#else
    return "file://" + path.u8string();
#endif
}

#ifndef _WIN32
// Returns the path of the file or directory that |fd| refers to.
std::optional<fs::path> pathOfFd(int fd) {
#ifdef __APPLE__
    char buf[MAXPATHLEN];
    if (fcntl(fd, F_GETPATH, buf) == -1) {
        return std::nullopt;
    }
    return fs::path(buf);
#else
    std::error_code ec;
    auto path = fs::read_symlink("/proc/self/fd/" + std::to_string(fd), ec);
    if (ec) {
        return std::nullopt;
    }
    return path;
#endif
}

// A path that re-opens the file that |fd| refers to, without resolving its
// original path again.
std::string fdUri(int fd) {
#ifdef __APPLE__
    return "file:///dev/fd/" + std::to_string(fd);
#else
    return "file:///proc/self/fd/" + std::to_string(fd);
#endif
}

using android::base::ScopedFd;
#endif

}  // namespace

SharedMemoryLibrary::Region::~Region() {
    // Unmap and close the region before removing the file.
    shm.reset();
#ifndef _WIN32
    if (dirFd >= 0) {
        if (created) {
            // Remove the file we created, relative to the verified directory
            // so we never follow a swapped path.
            unlinkat(dirFd, fileName.c_str(), 0);
        }
        ::close(dirFd);
    }
#endif
}

SharedMemoryLibrary::SharedMemoryLibrary()
    : SharedMemoryLibrary(defaultAllowedDirectories()) {}

SharedMemoryLibrary::SharedMemoryLibrary(
        std::vector<fs::path> allowedDirectories) {
    for (const auto& dir : allowedDirectories) {
        if (!isLocalAbsolutePath(dir)) {
            continue;
        }
        std::error_code ec;
        auto resolved = resolvePath(dir, ec);
        if (ec || resolved.empty()) {
            continue;
        }
        if (std::find(mAllowedDirectories.begin(), mAllowedDirectories.end(),
                      resolved) == mAllowedDirectories.end()) {
            mAllowedDirectories.push_back(std::move(resolved));
        }
    }
}

// static
std::vector<fs::path> SharedMemoryLibrary::defaultAllowedDirectories() {
    std::vector<fs::path> dirs;
    auto emulatorTmp = android::base::System::get()->getTempDir();
    if (!emulatorTmp.empty()) {
        dirs.push_back(fs::u8path(emulatorTmp));
    }

    std::error_code ec;
    auto systemTmp = fs::temp_directory_path(ec);
    if (!ec && !systemTmp.empty()) {
        dirs.push_back(systemTmp);
    }
#ifndef _WIN32
    dirs.push_back("/tmp");
#endif
#ifdef __linux__
    dirs.push_back("/dev/shm");
#endif

    auto discovery = android::ConfigDirs::getDiscoveryDirectory();
    if (!discovery.empty()) {
        dirs.push_back(fs::u8path(discovery));
    }
    return dirs;
}

std::string SharedMemoryLibrary::allowedDirectoriesAsString() const {
    std::string result;
    for (const auto& dir : mAllowedDirectories) {
        if (!result.empty()) {
            result += ", ";
        }
        result += dir.u8string();
    }
    return result.empty() ? "<none>" : result;
}

std::string SharedMemoryLibrary::howToFix() const {
    const fs::path example =
            mAllowedDirectories.empty()
                    ? fs::path("/tmp/frames.rgb")
                    : mAllowedDirectories.front() / "frames.rgb";
    return " File backed (MMAP) image transport only accepts regular files "
           "inside one of the following directories: [" +
           allowedDirectoriesAsString() +
           "]. Create the file inside one of these directories and pass it "
           "as an absolute file:/// URL, for example " +
           toFileUri(example) + ".";
}

bool SharedMemoryLibrary::isInsideOrEqualAllowedDirectory(
        const fs::path& p) const {
    return std::any_of(
            mAllowedDirectories.begin(), mAllowedDirectories.end(),
            [&](const fs::path& dir) { return isInside(p, dir, true); });
}

absl::StatusOr<fs::path> SharedMemoryLibrary::resolveFileHandle(
        const std::string& handle,
        const SharedMemory& probe) const {
    fs::path requested = fs::u8path(probe.name());
    if (!isLocalAbsolutePath(requested)) {
        return absl::InvalidArgumentError(
                "Rejected shared memory handle '" + handle +
                "': it does not refer to a local absolute path." + howToFix());
    }

    std::error_code ec;
    fs::path resolved = resolvePath(requested, ec);
    if (ec || resolved.empty()) {
        return absl::InvalidArgumentError(
                "Rejected shared memory handle '" + handle +
                "': unable to resolve path '" + requested.u8string() + "' (" +
                ec.message() + ")." + howToFix());
    }

    bool inside =
            std::any_of(mAllowedDirectories.begin(), mAllowedDirectories.end(),
                        [&](const fs::path& dir) {
                            return isInside(resolved, dir, false);
                        });
    if (!inside) {
        return absl::PermissionDeniedError(
                "Rejected shared memory handle '" + handle +
                "': it resolves to '" + resolved.u8string() +
                "', which is not inside a directory that the emulator allows "
                "for shared memory regions." +
                howToFix());
    }

    // weakly_canonical resolves every symlink whose target exists, so a
    // symlink that is left over is dangling. Do not follow it, it could be
    // used to create a file outside of the allowed directories.
    auto status = fs::symlink_status(resolved, ec);
    if (!ec && fs::is_symlink(status)) {
        return absl::PermissionDeniedError(
                "Rejected shared memory handle '" + handle +
                "': it resolves to '" + resolved.u8string() +
                "', which is a symbolic link whose target does not exist." +
                howToFix());
    }
    if (!ec && fs::exists(status) && !fs::is_regular_file(status)) {
        return absl::InvalidArgumentError(
                "Rejected shared memory handle '" + handle +
                "': it resolves to '" + resolved.u8string() +
                "', which exists but is not a regular file." + howToFix());
    }

    return resolved;
}

absl::Status SharedMemoryLibrary::validateHandle(
        const std::string& handle) const {
    // Let SharedMemory parse the handle, so we validate exactly the path that
    // would be opened.
    SharedMemory probe(handle, 0);
    if (probe.type() != SharedMemory::ShareType::FILE_BACKED) {
        // Named shared memory, does not touch the file system.
        return absl::OkStatus();
    }
    return resolveFileHandle(handle, probe).status();
}

absl::StatusOr<std::shared_ptr<SharedMemoryLibrary::Region>>
SharedMemoryLibrary::openFileBacked(const std::string& handle,
                                    const fs::path& resolved,
                                    size_t size) const {
    auto region = std::make_shared<Region>();
#ifdef _WIN32
    // Temporary directories on Windows are private to the user, so another
    // (unprivileged) user cannot swap files in them.
    region->shm = std::make_unique<SharedMemory>(handle, size);
    if (region->shm->open(SharedMemory::AccessMode::READ_WRITE) != 0 ||
        !region->shm->isOpen() || !region->shm->isMapped()) {
        region->shm->create(0600);
    }
    return region;
#else
    // Open the containing directory and verify where it actually is. From
    // here on everything is relative to this descriptor and no symlinks are
    // followed, so swapping paths after validation has no effect.
    const auto dirPath = resolved.parent_path();
    const auto fileName = resolved.filename().string();
    ScopedFd dir(::open(dirPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!dir.valid()) {
        return absl::InvalidArgumentError(
                "Rejected shared memory handle '" + handle +
                "': unable to open directory '" + dirPath.u8string() + "' (" +
                std::strerror(errno) + ")." + howToFix());
    }
    auto actualDir = pathOfFd(dir.get());
    if (!actualDir || !isInsideOrEqualAllowedDirectory(*actualDir)) {
        return absl::PermissionDeniedError(
                "Rejected shared memory handle '" + handle +
                "': the directory '" + dirPath.u8string() + "' resolved to '" +
                (actualDir ? actualDir->u8string() : std::string("<unknown>")) +
                "' when it was opened, which is not inside a directory that "
                "the emulator allows for shared memory regions." +
                howToFix());
    }

    bool created = false;
    ScopedFd file(openat(dir.get(), fileName.c_str(),
                         O_RDWR | O_NOFOLLOW | O_CLOEXEC));
    if (!file.valid() && errno == ENOENT) {
        file = ScopedFd(openat(
                dir.get(), fileName.c_str(),
                O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        created = file.valid();
    }
    if (!file.valid()) {
        int err = errno;
        if (err == ELOOP) {
            return absl::PermissionDeniedError(
                    "Rejected shared memory handle '" + handle + "': '" +
                    resolved.u8string() +
                    "' is a symbolic link, which is not allowed." + howToFix());
        }
        if (err == EACCES || err == EPERM) {
            return absl::PermissionDeniedError(
                    "Rejected shared memory handle '" + handle +
                    "': the emulator is not allowed to open '" +
                    resolved.u8string() + "' for reading and writing (" +
                    std::strerror(err) +
                    "). Make sure the file is owned by the user running the "
                    "emulator." +
                    howToFix());
        }
        return absl::InvalidArgumentError(
                "Rejected shared memory handle '" + handle +
                "': unable to open '" + resolved.u8string() + "' (" +
                std::strerror(err) + ")." + howToFix());
    }

    // Remove the file if we created it but bail out below.
    auto cleanup = [&]() {
        file.close();
        if (created) {
            unlinkat(dir.get(), fileName.c_str(), 0);
        }
    };

    struct stat st;
    if (fstat(file.get(), &st) != 0 || !S_ISREG(st.st_mode)) {
        cleanup();
        return absl::InvalidArgumentError(
                "Rejected shared memory handle '" + handle + "': '" +
                resolved.u8string() + "' is not a regular file." + howToFix());
    }
    if (!created && st.st_nlink != 1) {
        cleanup();
        return absl::PermissionDeniedError(
                "Rejected shared memory handle '" + handle + "': '" +
                resolved.u8string() +
                "' has multiple hard links, which is not allowed." +
                howToFix());
    }

    // Map the file we just verified, by re-opening the descriptor instead of
    // the original path.
    region->shm = std::make_unique<SharedMemory>(fdUri(file.get()), size);
    int err = region->shm->open(SharedMemory::AccessMode::READ_WRITE);
    if (err != 0 || !region->shm->isOpen() || !region->shm->isMapped()) {
        region->shm.reset();
        cleanup();
        return absl::InternalError(
                "Unable to map shared memory region '" + handle + "' (" +
                resolved.u8string() + ") with a size of " +
                std::to_string(size) + " bytes" +
                (err < 0 ? std::string(": ") + std::strerror(-err)
                         : std::string()) +
                ".");
    }
    file.close();

    region->dirFd = dir.release();
    region->fileName = fileName;
    region->created = created;
    region->dev = st.st_dev;
    region->ino = st.st_ino;
    return region;
#endif
}

absl::StatusOr<SharedMemoryLibrary::LibraryEntry> SharedMemoryLibrary::borrow(
        std::string handle,
        size_t size) {
    const std::lock_guard<std::mutex> lock(mAccess);

    auto it = mMemMap.find(handle);
    bool needsCreate = it == mMemMap.end() || !it->second->shm->isOpen() ||
                       !it->second->shm->isMapped() ||
                       (size > 0 && it->second->shm->size() < size);

    if (needsCreate) {
        std::shared_ptr<Region> region;
        // Only validate when we are about to (re)open the region, handles
        // that are already in use have been validated before.
        SharedMemory probe(handle, 0);
        if (probe.type() == SharedMemory::ShareType::FILE_BACKED) {
            auto resolved = resolveFileHandle(handle, probe);
            absl::StatusOr<std::shared_ptr<Region>> opened =
                    resolved.ok() ? openFileBacked(handle, *resolved, size)
                                  : resolved.status();
            if (!opened.ok()) {
                LOG(ERROR) << std::string(opened.status().message());
                return opened.status();
            }
            region = std::move(opened).value();
#ifndef _WIN32
            // If we are replacing a region for a file that we created, the
            // new region becomes responsible for removing it, otherwise the
            // file would be removed while it is still in use.
            if (it != mMemMap.end() && it->second->created &&
                it->second->dev == region->dev &&
                it->second->ino == region->ino) {
                it->second->created = false;
                region->created = true;
            }
#endif
        } else {
            region = std::make_shared<Region>();
            region->shm = std::make_unique<SharedMemory>(handle, size);
            if (region->shm->open(SharedMemory::AccessMode::READ_WRITE) != 0 ||
                !region->shm->isOpen() || !region->shm->isMapped()) {
                region->shm->create(0600);
            }
        }
        // Outstanding entries keep a reference to the region they borrowed,
        // so replacing it does not unmap memory that is still in use.
        mMemMap[handle] = std::move(region);
    }

    mHandlesCnt[handle]++;
    std::shared_ptr<Region> region = mMemMap[handle];
    SharedMemory* shm = region->shm.get();
    return LibraryEntry(
            shm, android::base::FuncDelete<deleter>(
                         deleter([this, handle, region](SharedMemory*) mutable {
                             release(handle);
                             // Drop our reference right away, the deleter
                             // itself lives as long as the LibraryEntry.
                             region.reset();
                         })));
}

void SharedMemoryLibrary::release(std::string handle) {
    const std::lock_guard<std::mutex> lock(mAccess);
    if (mHandlesCnt.count(handle) == 0) {
        // What? We are releasing a released handle?
        assert(false);
        return;
    }
    mHandlesCnt[handle]--;
    if (mHandlesCnt[handle] == 0) {
        mHandlesCnt.erase(handle);
        mMemMap.erase(handle);
    }
}

}  // namespace control
}  // namespace emulation
}  // namespace android
