// Copyright 2016 The Android Open Source Project
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

#include "host-common/DmaMap.h"
#include "android/emulation/testing/TestDmaMap.h"
#include "android/emulation/testing/TestVmLock.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using android::base::Optional;
using android::base::kNullopt;

namespace android {

namespace {

class ThreadSafeTestVmLock : public VmLock {
public:
    ThreadSafeTestVmLock() : mOldVmLock(VmLock::set(this)) {}
    ~ThreadSafeTestVmLock() override { VmLock::set(mOldVmLock); }

    void lock() override {
        mLock.lock();
        mOwner.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }

    void unlock() override {
        mOwner.store(std::thread::id(), std::memory_order_relaxed);
        mLock.unlock();
    }

    bool isLockedBySelf() const override {
        return mOwner.load(std::memory_order_relaxed) ==
               std::this_thread::get_id();
    }

private:
    base::Lock mLock;
    std::atomic<std::thread::id> mOwner{};
    VmLock* mOldVmLock = nullptr;
};

class LockCheckingDmaMap : public TestDmaMap {
public:
    void* doMap(uint64_t addr, uint64_t buffer) override {
        EXPECT_TRUE(VmLock::get()->isLockedBySelf());
        mMapCount.fetch_add(1, std::memory_order_relaxed);
        return reinterpret_cast<void*>(static_cast<uintptr_t>(addr + 0x1000));
    }

    void doUnmap(void* mapped, uint64_t buffer) override {
        EXPECT_TRUE(VmLock::get()->isLockedBySelf());
        mUnmapCount.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int> mMapCount{0};
    std::atomic<int> mUnmapCount{0};
};

}  // namespace


TEST(DmaMap, Default) {
    // No default impl of DmaMap should exist on startup.
    DmaMap* dmaMap = DmaMap::get();
    ASSERT_FALSE(dmaMap);
}

TEST(DmaMap, CustomDmaMap) {
    TestDmaMap myMap;
    const DmaMapState& currState = myMap.dumpState();
    EXPECT_EQ(currState.size(), 0U);
}

TEST(DmaMap, SingleMapAdd) {
    TestDmaMap myMap;
    myMap.addBuffer(nullptr, 1, 2);
    const DmaMapState& currState = myMap.dumpState();
    const auto& it = currState.find(1);
    EXPECT_TRUE(it != currState.end());
    const DmaBufferInfo& info = it->second;
    EXPECT_EQ(info.guestAddr, 1U);
    EXPECT_EQ(info.bufferSize, 2U);
    EXPECT_TRUE(info.currHostAddr);
}

TEST(DmaMap, SingleMapAddRemove) {
    TestDmaMap myMap;
    const DmaMapState& currState = myMap.dumpState();
    myMap.addBuffer(nullptr, 1, 2);
    const auto& it = currState.find(1);
    EXPECT_TRUE(it != currState.end());
    myMap.removeBuffer(1);
    const auto& it2 = currState.find(1);
    EXPECT_TRUE(it2 == currState.end());
}

TEST(DmaMap, SingleMapAddMulti) {
    TestDmaMap myMap;
    myMap.addBuffer(nullptr, 1, 2);
    myMap.addBuffer(nullptr, 1, 3);
    const DmaMapState& currState = myMap.dumpState();
    const DmaBufferInfo& info = currState.find(1)->second;
    EXPECT_EQ(info.guestAddr, 1U);
    EXPECT_EQ(info.bufferSize, 3U);
}

TEST(DmaMap, MultiMap) {
    TestDmaMap myMap;
    myMap.addBuffer(nullptr, 1, 2);
    myMap.addBuffer(nullptr, 3, 4);
    const DmaMapState& currState = myMap.dumpState();
    const auto& it1 = currState.find(1);
    const auto& it3 = currState.find(3);
    EXPECT_TRUE(it1 != currState.end());
    EXPECT_TRUE(it3 != currState.end());
    const DmaBufferInfo& info1 = it1->second;
    const DmaBufferInfo& info3 = it3->second;
    EXPECT_EQ(info1.guestAddr, 1U);
    EXPECT_EQ(info1.bufferSize, 2U);
    EXPECT_TRUE(info1.currHostAddr);
    EXPECT_EQ(info3.guestAddr, 3U);
    EXPECT_EQ(info3.bufferSize, 4U);
    EXPECT_TRUE(info3.currHostAddr);
    myMap.removeBuffer(1);
    myMap.removeBuffer(3);
    const auto& it1_notfound = currState.find(1);
    const auto& it3_notfound = currState.find(3);
    EXPECT_TRUE(it1_notfound == currState.end());
    EXPECT_TRUE(it3_notfound == currState.end());
}

TEST(DmaMap, SingleMapHostRead) {
    TestDmaMap myMap;
    myMap.addBuffer(nullptr, 1, 2);
    myMap.getHostAddr(1);

    const DmaMapState& currState = myMap.dumpState();
    const auto& it = currState.find(1);
    const DmaBufferInfo& info = it->second;
    EXPECT_EQ(info.guestAddr, 1U);
    EXPECT_EQ(info.bufferSize, 2U);
    EXPECT_TRUE(info.currHostAddr);

    myMap.getHostAddr(1);
    EXPECT_TRUE(currState.find(1)->second.currHostAddr);
    myMap.getHostAddr(1);
    EXPECT_TRUE(currState.find(1)->second.currHostAddr);
}

TEST(DmaMap, SingleMapHostReadInvalidate) {
    TestDmaMap myMap;
    myMap.addBuffer(nullptr, 1, 2);
    myMap.getHostAddr(1);
    myMap.invalidateHostMappings();
    const DmaMapState& currState = myMap.dumpState();
    const auto& it = currState.find(1);
    const DmaBufferInfo& info = it->second;
    EXPECT_EQ(info.guestAddr, 1U);
    EXPECT_EQ(info.bufferSize, 2U);
    EXPECT_FALSE(info.currHostAddr);
}

TEST(DmaMap, MultiMapHostReadInvalidate) {
    TestDmaMap myMap;
    const DmaMapState& currState = myMap.dumpState();
    myMap.addBuffer(nullptr, 1, 2);
    myMap.addBuffer(nullptr, 3, 4);
    myMap.addBuffer(nullptr, 5, 6);
    myMap.getHostAddr(1);
    myMap.getHostAddr(3);
    EXPECT_TRUE(currState.find(1)->second.currHostAddr);
    EXPECT_TRUE(currState.find(3)->second.currHostAddr);
    EXPECT_TRUE(currState.find(5)->second.currHostAddr);
    myMap.invalidateHostMappings();
    EXPECT_FALSE(currState.find(1)->second.currHostAddr);
    EXPECT_FALSE(currState.find(3)->second.currHostAddr);
    EXPECT_FALSE(currState.find(5)->second.currHostAddr);
}

TEST(DmaMap, MultiMapHostReadInvalidateRemap) {
    TestDmaMap myMap;
    const DmaMapState& currState = myMap.dumpState();
    myMap.addBuffer(nullptr, 1, 2);
    myMap.addBuffer(nullptr, 3, 4);
    myMap.addBuffer(nullptr, 5, 6);
    myMap.getHostAddr(1);
    myMap.getHostAddr(3);
    EXPECT_TRUE(currState.find(1)->second.currHostAddr);
    EXPECT_TRUE(currState.find(3)->second.currHostAddr);
    EXPECT_TRUE(currState.find(5)->second.currHostAddr);
    myMap.invalidateHostMappings();
    myMap.getHostAddr(3);
    myMap.getHostAddr(5);
    EXPECT_FALSE(currState.find(1)->second.currHostAddr);
    EXPECT_TRUE(currState.find(3)->second.currHostAddr);
    EXPECT_TRUE(currState.find(5)->second.currHostAddr);
}

TEST(DmaMap, ErrorCaseMapNonExistent) {
    TestDmaMap myMap;
    myMap.getHostAddr(1);
}

TEST(DmaMap, ErrorCaseRemoveNonExistent) {
    TestDmaMap myMap;
    myMap.removeBuffer(1);
}

TEST(DmaMap, AcquiresVmLockOnMapAndUnmap) {
    TestVmLock vmLock;
    LockCheckingDmaMap myMap;

    myMap.addBuffer(nullptr, 0x1000, 64);
    EXPECT_EQ(1, vmLock.mLockCount);
    EXPECT_EQ(1, vmLock.mUnlockCount);
    EXPECT_EQ(1, myMap.mMapCount.load());
    EXPECT_EQ(0, myMap.mUnmapCount.load());

    // Fast path (already mapped) should not need to acquire VmLock.
    EXPECT_NE(nullptr, myMap.getHostAddr(0x1000));
    EXPECT_EQ(1, vmLock.mLockCount);
    EXPECT_EQ(1, vmLock.mUnlockCount);

    // Invalidating host mappings must hold VmLock while unmapping.
    myMap.invalidateHostMappings();
    EXPECT_EQ(2, vmLock.mLockCount);
    EXPECT_EQ(2, vmLock.mUnlockCount);
    EXPECT_EQ(1, myMap.mUnmapCount.load());

    // Slow path (remapping after invalidation) must acquire VmLock.
    EXPECT_NE(nullptr, myMap.getHostAddr(0x1000));
    EXPECT_EQ(3, vmLock.mLockCount);
    EXPECT_EQ(3, vmLock.mUnlockCount);
    EXPECT_EQ(2, myMap.mMapCount.load());

    // Overwriting an existing buffer unmaps the old mapping under VmLock.
    myMap.addBuffer(nullptr, 0x1000, 128);
    EXPECT_EQ(4, vmLock.mLockCount);
    EXPECT_EQ(4, vmLock.mUnlockCount);
    EXPECT_EQ(3, myMap.mMapCount.load());
    EXPECT_EQ(2, myMap.mUnmapCount.load());

    // Removing the buffer unmaps it under VmLock.
    myMap.removeBuffer(0x1000);
    EXPECT_EQ(5, vmLock.mLockCount);
    EXPECT_EQ(5, vmLock.mUnlockCount);
    EXPECT_EQ(3, myMap.mUnmapCount.load());
}

TEST(DmaMap, ConcurrentRemapAndInvalidateWithVmLock) {
    ThreadSafeTestVmLock vmLock;
    LockCheckingDmaMap myMap;

    constexpr uint64_t kNumBuffers = 8;
    for (uint64_t i = 1; i <= kNumBuffers; ++i) {
        ScopedVmLock lock;
        myMap.addBuffer(nullptr, i * 0x1000, 64);
    }

    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                for (uint64_t i = 1; i <= kNumBuffers; ++i) {
                    myMap.getHostAddr(i * 0x1000);
                }
            }
        });
    }

    // Simulate vCPU/snapshot threads holding VmLock first before mutating
    // DmaMap while render threads call getHostAddr without holding VmLock.
    std::thread mutator([&]() {
        for (int iter = 0; iter < 200; ++iter) {
            {
                ScopedVmLock lock;
                myMap.invalidateHostMappings();
            }
            {
                ScopedVmLock lock;
                myMap.removeBuffer(kNumBuffers * 0x1000);
                myMap.addBuffer(nullptr, kNumBuffers * 0x1000, 64);
            }
        }
        stop.store(true, std::memory_order_relaxed);
    });

    mutator.join();
    for (auto& reader : readers) {
        reader.join();
    }

    ScopedVmLock lock;
    myMap.resetHostMappings();
    EXPECT_EQ(myMap.mMapCount.load(), myMap.mUnmapCount.load());
}

} // namespace android
