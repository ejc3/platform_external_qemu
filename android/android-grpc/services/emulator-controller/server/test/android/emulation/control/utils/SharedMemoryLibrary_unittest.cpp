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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <unistd.h>  // for geteuid
#endif

#include "android/base/testing/TestTempDir.h"

namespace android {
namespace emulation {
namespace control {

namespace fs = std::filesystem;
using ::testing::HasSubstr;

static std::string uri(const fs::path& path) {
#ifdef _WIN32
    return "file:///" + path.generic_u8string();
#else
    return "file://" + path.u8string();
#endif
}

TEST(SharedMemoryLibraryTest, BorrowBasic) {
    SharedMemoryLibrary lib;
    auto entry = lib.borrow("test_shm_region", 1024);
    ASSERT_TRUE(entry.ok()) << entry.status();
    ASSERT_NE(entry->get(), nullptr);
    EXPECT_TRUE((*entry)->isOpen());
    EXPECT_TRUE((*entry)->isMapped());
    EXPECT_GE((*entry)->size(), 1024u);
}

TEST(SharedMemoryLibraryTest, BorrowSizeZeroThenGrow) {
    SharedMemoryLibrary lib;
    // Initial borrow with size 0 (e.g. unscaled request)
    auto entry0 = lib.borrow("test_shm_grow", 0);
    ASSERT_TRUE(entry0.ok()) << entry0.status();
    ASSERT_NE(entry0->get(), nullptr);

    // Subsequent borrow with actual frame size
    auto entry1 = lib.borrow("test_shm_grow", 4096);
    ASSERT_TRUE(entry1.ok()) << entry1.status();
    ASSERT_NE(entry1->get(), nullptr);
    EXPECT_TRUE((*entry1)->isOpen());
    EXPECT_TRUE((*entry1)->isMapped());
    EXPECT_GE((*entry1)->size(), 4096u);
}

TEST(SharedMemoryLibraryTest, BorrowMultipleRefCounts) {
    SharedMemoryLibrary lib;
    auto entry1 = lib.borrow("test_shm_ref", 2048);
    ASSERT_TRUE(entry1.ok()) << entry1.status();
    ASSERT_NE(entry1->get(), nullptr);

    auto entry2 = lib.borrow("test_shm_ref", 2048);
    ASSERT_TRUE(entry2.ok()) << entry2.status();
    ASSERT_NE(entry2->get(), nullptr);
    EXPECT_EQ(entry1->get(), entry2->get());

    entry1->reset();
    EXPECT_TRUE((*entry2)->isOpen());
    EXPECT_TRUE((*entry2)->isMapped());
}

TEST(SharedMemoryLibraryTest, GrowKeepsBorrowedRegionMapped) {
    SharedMemoryLibrary lib;
    auto small = lib.borrow("test_shm_keep", 1024);
    ASSERT_TRUE(small.ok()) << small.status();
    ASSERT_TRUE((*small)->isMapped());
    auto* smallMemory = static_cast<char*>((*small)->get());

    auto large = lib.borrow("test_shm_keep", 4096);
    ASSERT_TRUE(large.ok()) << large.status();
    EXPECT_NE(small->get(), large->get());
    EXPECT_GE((*large)->size(), 4096u);

    // The region borrowed first must still be mapped and writable.
    EXPECT_TRUE((*small)->isMapped());
    EXPECT_EQ((*small)->get(), smallMemory);
    smallMemory[1023] = 42;
    EXPECT_EQ(smallMemory[1023], 42);

    // Releasing in any order should not trip the refcount.
    small->reset();
    EXPECT_TRUE((*large)->isMapped());
    large->reset();

    // And the handle can be borrowed again.
    auto again = lib.borrow("test_shm_keep", 1024);
    ASSERT_TRUE(again.ok()) << again.status();
}

class SharedMemoryLibraryFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        mTempDir = std::make_unique<android::base::TestTempDir>("shmlib");
        mRoot = fs::weakly_canonical(fs::u8path(mTempDir->path()));
        mAllowed = mRoot / "allowed";
        mOutside = mRoot / "outside";
        fs::create_directories(mAllowed);
        fs::create_directories(mOutside);
        mLib = std::make_unique<SharedMemoryLibrary>(
                std::vector<fs::path>{mAllowed});
    }

    void TearDown() override {
        mLib.reset();
        mTempDir.reset();
    }

    static void touch(const fs::path& path) {
        std::ofstream out(path);
        out << "do not overwrite";
    }

    static std::string contents(const fs::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    std::unique_ptr<android::base::TestTempDir> mTempDir;
    fs::path mRoot;
    fs::path mAllowed;
    fs::path mOutside;
    std::unique_ptr<SharedMemoryLibrary> mLib;
};

TEST_F(SharedMemoryLibraryFileTest, AllowedDirectoriesAreCanonical) {
    ASSERT_EQ(mLib->allowedDirectories().size(), 1u);
    EXPECT_EQ(mLib->allowedDirectories()[0], mAllowed);
}

TEST_F(SharedMemoryLibraryFileTest, AcceptsFileInsideAllowedDirectory) {
    auto file = mAllowed / "frames.rgb";
    touch(file);
    auto entry = mLib->borrow(uri(file), 4096);
    ASSERT_TRUE(entry.ok()) << entry.status();
    EXPECT_TRUE((*entry)->isOpen());
    EXPECT_TRUE((*entry)->isMapped());
}

TEST_F(SharedMemoryLibraryFileTest, AcceptsFileInSubdirectory) {
    fs::create_directories(mAllowed / "sub");
    auto file = mAllowed / "sub" / "frames.rgb";
    touch(file);
    EXPECT_TRUE(mLib->validateHandle(uri(file)).ok());
}

TEST_F(SharedMemoryLibraryFileTest, RejectsFileOutsideAllowedDirectory) {
    auto file = mOutside / "victim.txt";
    touch(file);
    auto entry = mLib->borrow(uri(file), 4096);
    ASSERT_FALSE(entry.ok());
    EXPECT_EQ(entry.status().code(), absl::StatusCode::kPermissionDenied);
    // The message should explain what was rejected and what is allowed.
    EXPECT_THAT(entry.status().message(), HasSubstr(uri(file)));
    EXPECT_THAT(entry.status().message(), HasSubstr(mAllowed.u8string()));
    // And the file should not have been touched.
    EXPECT_EQ(contents(file), "do not overwrite");
    EXPECT_EQ(fs::file_size(file), std::string("do not overwrite").size());
}

TEST_F(SharedMemoryLibraryFileTest, DoesNotCreateFileOutsideAllowedDirectory) {
    auto file = mOutside / "new_file.rgb";
    auto entry = mLib->borrow(uri(file), 4096);
    ASSERT_FALSE(entry.ok());
    EXPECT_FALSE(fs::exists(file));
}

TEST_F(SharedMemoryLibraryFileTest, RejectsPathTraversal) {
    auto file = mOutside / "victim.txt";
    touch(file);
    auto traversal = uri(mAllowed) + "/../outside/victim.txt";
    auto status = mLib->validateHandle(traversal);
    EXPECT_EQ(status.code(), absl::StatusCode::kPermissionDenied);
    EXPECT_THAT(status.message(), HasSubstr(file.u8string()));
}

TEST_F(SharedMemoryLibraryFileTest, RejectsSiblingWithSamePrefix) {
    auto sibling = mRoot / "allowed_not_really";
    fs::create_directories(sibling);
    auto status = mLib->validateHandle(uri(sibling / "frames.rgb"));
    EXPECT_EQ(status.code(), absl::StatusCode::kPermissionDenied);
}

TEST_F(SharedMemoryLibraryFileTest, RejectsAllowedDirectoryItself) {
    auto status = mLib->validateHandle(uri(mAllowed));
    EXPECT_EQ(status.code(), absl::StatusCode::kPermissionDenied);
}

TEST_F(SharedMemoryLibraryFileTest, RejectsDirectoryInsideAllowedDirectory) {
    fs::create_directories(mAllowed / "dir");
    auto status = mLib->validateHandle(uri(mAllowed / "dir"));
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(status.message(), HasSubstr("not a regular file"));
}

TEST_F(SharedMemoryLibraryFileTest, RejectsRelativePath) {
    auto status = mLib->validateHandle("file://relative/frames.rgb");
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(status.message(), HasSubstr("absolute"));
}

#ifndef _WIN32
TEST_F(SharedMemoryLibraryFileTest, RejectsSymlinkPointingOutside) {
    auto victim = mOutside / "victim.txt";
    touch(victim);
    auto link = mAllowed / "link.rgb";
    fs::create_symlink(victim, link);
    auto entry = mLib->borrow(uri(link), 4096);
    ASSERT_FALSE(entry.ok());
    EXPECT_EQ(entry.status().code(), absl::StatusCode::kPermissionDenied);
    EXPECT_THAT(entry.status().message(), HasSubstr(victim.u8string()));
    EXPECT_EQ(contents(victim), "do not overwrite");
}

TEST_F(SharedMemoryLibraryFileTest, RejectsSymlinkedDirectoryPointingOutside) {
    auto victim = mOutside / "victim.txt";
    touch(victim);
    fs::create_directory_symlink(mOutside, mAllowed / "dirlink");
    auto status =
            mLib->validateHandle(uri(mAllowed / "dirlink" / "victim.txt"));
    EXPECT_EQ(status.code(), absl::StatusCode::kPermissionDenied);
}

TEST_F(SharedMemoryLibraryFileTest, AcceptsSymlinkPointingInside) {
    auto target = mAllowed / "target.rgb";
    touch(target);
    auto link = mAllowed / "link.rgb";
    fs::create_symlink(target, link);
    EXPECT_TRUE(mLib->validateHandle(uri(link)).ok());
}

TEST_F(SharedMemoryLibraryFileTest, RejectsDanglingSymlinkPointingOutside) {
    auto victim = mOutside / "does_not_exist_yet.txt";
    auto link = mAllowed / "dangling.rgb";
    fs::create_symlink(victim, link);

    auto status = mLib->validateHandle(uri(link));
    EXPECT_EQ(status.code(), absl::StatusCode::kPermissionDenied);
    EXPECT_THAT(status.message(), HasSubstr("symbolic link"));

    auto entry = mLib->borrow(uri(link), 4096);
    ASSERT_FALSE(entry.ok());
    EXPECT_EQ(entry.status().code(), absl::StatusCode::kPermissionDenied);
    EXPECT_FALSE(fs::exists(victim));
}

TEST_F(SharedMemoryLibraryFileTest, RejectsHardLink) {
    auto victim = mOutside / "victim.txt";
    touch(victim);
    auto link = mAllowed / "hardlink.rgb";
    fs::create_hard_link(victim, link);

    auto entry = mLib->borrow(uri(link), 4096);
    ASSERT_FALSE(entry.ok());
    EXPECT_EQ(entry.status().code(), absl::StatusCode::kPermissionDenied);
    EXPECT_THAT(entry.status().message(), HasSubstr("hard links"));
    EXPECT_EQ(contents(victim), "do not overwrite");
}

TEST_F(SharedMemoryLibraryFileTest, MapsTheValidatedFile) {
    auto file = mAllowed / "frames.rgb";
    touch(file);
    {
        auto entry = mLib->borrow(uri(file), 4096);
        ASSERT_TRUE(entry.ok()) << entry.status();
        ASSERT_TRUE((*entry)->isMapped());
        auto* memory = static_cast<char*>((*entry)->get());
        memory[0] = 'X';
    }
    EXPECT_EQ(fs::file_size(file), 4096u);
    EXPECT_EQ(contents(file)[0], 'X');
    // We did not create the file, so it must not be removed.
    EXPECT_TRUE(fs::exists(file));
}

TEST_F(SharedMemoryLibraryFileTest, RemovesFileItCreated) {
    auto file = mAllowed / "created.rgb";
    {
        auto entry = mLib->borrow(uri(file), 4096);
        ASSERT_TRUE(entry.ok()) << entry.status();
        EXPECT_TRUE((*entry)->isMapped());
        EXPECT_TRUE(fs::exists(file));
    }
    EXPECT_FALSE(fs::exists(file));
}

TEST_F(SharedMemoryLibraryFileTest, CreatedFileSurvivesGrow) {
    auto file = mAllowed / "created_grow.rgb";
    auto small = mLib->borrow(uri(file), 1024);
    ASSERT_TRUE(small.ok()) << small.status();
    auto large = mLib->borrow(uri(file), 4096);
    ASSERT_TRUE(large.ok()) << large.status();

    // Releasing the replaced region must not remove the file that is still
    // in use by the new region.
    small->reset();
    EXPECT_TRUE(fs::exists(file));
    large->reset();
    EXPECT_FALSE(fs::exists(file));
}

TEST_F(SharedMemoryLibraryFileTest, MappingFailureRemovesCreatedFile) {
    // A zero sized region cannot be mapped.
    auto file = mAllowed / "unmappable.rgb";
    auto entry = mLib->borrow(uri(file), 0);
    ASSERT_FALSE(entry.ok());
    EXPECT_EQ(entry.status().code(), absl::StatusCode::kInternal);
    EXPECT_THAT(entry.status().message(), HasSubstr("Unable to map"));
    EXPECT_FALSE(fs::exists(file));
}

TEST_F(SharedMemoryLibraryFileTest, MappingFailureKeepsExistingFile) {
    auto file = mAllowed / "existing.rgb";
    touch(file);
    auto entry = mLib->borrow(uri(file), 0);
    ASSERT_FALSE(entry.ok());
    EXPECT_EQ(entry.status().code(), absl::StatusCode::kInternal);
    EXPECT_EQ(contents(file), "do not overwrite");
}

TEST_F(SharedMemoryLibraryFileTest, UnwritableFileIsPermissionDenied) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "root can open any file";
    }
    auto file = mAllowed / "readonly.rgb";
    touch(file);
    fs::permissions(file, fs::perms::owner_read);
    auto entry = mLib->borrow(uri(file), 4096);
    ASSERT_FALSE(entry.ok());
    EXPECT_EQ(entry.status().code(), absl::StatusCode::kPermissionDenied);
    EXPECT_THAT(entry.status().message(), HasSubstr(mAllowed.u8string()));
    EXPECT_EQ(contents(file), "do not overwrite");
}
#endif

TEST_F(SharedMemoryLibraryFileTest, NamedSharedMemoryIsNotRestricted) {
    EXPECT_TRUE(mLib->validateHandle("some_named_region").ok());
}

TEST(SharedMemoryLibraryDefaultsTest, DefaultsContainTempDirectory) {
    SharedMemoryLibrary lib;
    EXPECT_FALSE(lib.allowedDirectories().empty());
    std::error_code ec;
    auto tmp = fs::weakly_canonical(fs::temp_directory_path(ec));
    ASSERT_FALSE(ec);
    EXPECT_TRUE(lib.validateHandle(uri(tmp / "x.rgb")).ok());
}

}  // namespace control
}  // namespace emulation
}  // namespace android
