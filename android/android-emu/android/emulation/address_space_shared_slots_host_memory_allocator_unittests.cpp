// Copyright 2020 The Android Open Source Project
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

#include "host-common/address_space_shared_slots_host_memory_allocator.h"
#include <cstdint>
#include <gtest/gtest.h>

namespace android {
namespace emulation {
namespace {
typedef AddressSpaceSharedSlotsHostMemoryAllocatorContext ASSSHMAC;
typedef ASSSHMAC::MemBlock MemBlock;
typedef MemBlock::FreeSubblocks_t FreeSubblocks_t;

int add_memory_mapping(uint64_t gpa, void *ptr, uint64_t size) {
    return 1;
}

int remove_memory_mapping(uint64_t gpa, void *ptr, uint64_t size) { return 1; }

struct address_space_device_control_ops create_address_space_device_control_ops() {
    struct address_space_device_control_ops ops = {};

    ops.add_memory_mapping = &add_memory_mapping;
    ops.remove_memory_mapping = &remove_memory_mapping;

    return ops;
}

uint64_t getPhysAddrStartLocked(void) {
    return 2020;
}

int allocSharedHostRegionLocked(uint64_t page_aligned_size, uint64_t* offset) {
    *offset = page_aligned_size * 10;
    return 0;
}

int freeSharedHostRegionLocked(uint64_t offset) {
    return 0;
}

AddressSpaceHwFuncs create_AddressSpaceHwFuncs() {
    AddressSpaceHwFuncs hw = {};

    hw.allocSharedHostRegionLocked = &allocSharedHostRegionLocked;
    hw.freeSharedHostRegionLocked = &freeSharedHostRegionLocked;
    hw.getPhysAddrStartLocked = &getPhysAddrStartLocked;

    return hw;
}

uint32_t getGuestPageSize() {
    return 4096;
}

uint64_t g_nextSharedRegionOffset = 4096;

int allocUniqueSharedHostRegionLocked(uint64_t page_aligned_size,
                                      uint64_t* offset) {
    *offset = g_nextSharedRegionOffset;
    g_nextSharedRegionOffset += page_aligned_size;
    return 0;
}

AddressSpaceDevicePingInfo createAllocateRequest(uint64_t size) {
    AddressSpaceDevicePingInfo req = {};
    req.metadata = static_cast<uint64_t>(
        ASSSHMAC::HostMemoryAllocatorCommand::Allocate);
    req.size = size;
    return req;
}

AddressSpaceDevicePingInfo createUnallocateRequest(uint64_t phys_addr) {
    AddressSpaceDevicePingInfo req = {};
    req.metadata = static_cast<uint64_t>(
        ASSSHMAC::HostMemoryAllocatorCommand::Unallocate);
    req.phys_addr = phys_addr;
    return req;
}
}

TEST(MemBlock_findFreeSubblock, Simple) {
    FreeSubblocks_t fsb;
    EXPECT_TRUE(MemBlock::findFreeSubblock(&fsb, 11) == fsb.end());

    fsb[100] = 10;
    EXPECT_TRUE(MemBlock::findFreeSubblock(&fsb, 11) == fsb.end());

    FreeSubblocks_t::const_iterator i;

    i = MemBlock::findFreeSubblock(&fsb, 7);
    ASSERT_TRUE(i != fsb.end());
    EXPECT_EQ(i->first, 100);
    EXPECT_EQ(i->second, 10);

    fsb[200] = 6;
    i = MemBlock::findFreeSubblock(&fsb, 7);
    ASSERT_TRUE(i != fsb.end());
    EXPECT_EQ(i->first, 100);
    EXPECT_EQ(i->second, 10);

    fsb[300] = 8;
    i = MemBlock::findFreeSubblock(&fsb, 7);
    ASSERT_TRUE(i != fsb.end());
    EXPECT_EQ(i->first, 300);
    EXPECT_EQ(i->second, 8);
}

TEST(MemBlock_tryMergeSubblocks, NoMerge) {
    FreeSubblocks_t fsb;

    auto i = fsb.insert({10, 5}).first;
    auto j = fsb.insert({20, 5}).first;

    auto r = MemBlock::tryMergeSubblocks(&fsb, i, i, j);

    EXPECT_EQ(fsb.size(), 2);
    EXPECT_EQ(fsb[10], 5);
    EXPECT_EQ(fsb[20], 5);
    EXPECT_TRUE(r == i);
}

TEST(MemBlock_tryMergeSubblocks, Merge) {
    FreeSubblocks_t fsb;

    auto i = fsb.insert({10, 10}).first;
    auto j = fsb.insert({20, 5}).first;

    auto r = MemBlock::tryMergeSubblocks(&fsb, i, i, j);

    EXPECT_EQ(fsb.size(), 1);
    EXPECT_EQ(fsb[10], 15);
    ASSERT_TRUE(r != fsb.end());
    EXPECT_EQ(r->first, 10);
    EXPECT_EQ(r->second, 15);
}

TEST(MemBlock, allocate) {
    const struct address_space_device_control_ops ops =
        create_address_space_device_control_ops();

    const AddressSpaceHwFuncs hw = create_AddressSpaceHwFuncs();

    MemBlock block(&ops, &hw, 100);
    EXPECT_TRUE(block.isAllFree());
    EXPECT_EQ(block.physBase, 2020 + 100 * 10);

    EXPECT_EQ(block.allocate(110), 0);  // too large

    uint32_t off;

    off = block.allocate(50);
    EXPECT_GE(off, 2020 + 100 * 10);

    off = block.allocate(47);
    EXPECT_GE(off, 2020 + 100 * 10);

    off = block.allocate(2);
    EXPECT_GE(off, 2020 + 100 * 10);

    off = block.allocate(2);
    EXPECT_EQ(off, 0);

    off = block.allocate(1);
    EXPECT_GE(off, 2020 + 100 * 10);

    off = block.allocate(1);
    EXPECT_EQ(off, 0);
}

TEST(MemBlock, unallocate) {
    const struct address_space_device_control_ops ops =
        create_address_space_device_control_ops();

    const AddressSpaceHwFuncs hw = create_AddressSpaceHwFuncs();

    MemBlock block(&ops, &hw, 100);
    EXPECT_TRUE(block.isAllFree());
    EXPECT_EQ(block.physBase, 2020 + 100 * 10);

    uint32_t off60 = block.allocate(60);
    EXPECT_GE(off60, 2020 + 100 * 10);

    uint32_t off20 = block.allocate(20);
    EXPECT_GE(off20, 2020 + 100 * 10);

    uint32_t off30 = block.allocate(30);
    EXPECT_EQ(off30, 0);

    block.unallocate(off20, 20);

    off30 = block.allocate(30);
    EXPECT_GE(off30, 2020 + 100 * 10);

    block.unallocate(off60, 60);
    block.unallocate(off30, 30);

    EXPECT_TRUE(block.isAllFree());
}

TEST(MemBlock, allocateZeroSize) {
    const struct address_space_device_control_ops ops =
        create_address_space_device_control_ops();

    const AddressSpaceHwFuncs hw = create_AddressSpaceHwFuncs();

    MemBlock block(&ops, &hw, 100);
    EXPECT_TRUE(block.isAllFree());

    EXPECT_EQ(block.allocate(0), 0);
    EXPECT_TRUE(block.isAllFree());

    uint64_t off50 = block.allocate(50);
    EXPECT_GE(off50, 2020 + 100 * 10);
    EXPECT_FALSE(block.isAllFree());

    EXPECT_EQ(block.allocate(0), 0);

    block.unallocate(off50, 50);
    EXPECT_TRUE(block.isAllFree());
}

class AddressSpaceSharedSlotsHostMemoryAllocatorContextTest
    : public ::testing::Test {
protected:
    void SetUp() override {
        g_nextSharedRegionOffset = 4096;
        m_ops = create_address_space_device_control_ops();
        m_hw = {};
        m_hw.allocSharedHostRegionLocked = &allocUniqueSharedHostRegionLocked;
        m_hw.freeSharedHostRegionLocked = &freeSharedHostRegionLocked;
        m_hw.getPhysAddrStartLocked = &getPhysAddrStartLocked;
        m_hw.getGuestPageSize = &getGuestPageSize;

        ASSSHMAC::globalStateClear();
        m_prevHw = address_space_set_hw_funcs(&m_hw);
    }

    void TearDown() override {
        ASSSHMAC::globalStateClear();
        address_space_set_hw_funcs(m_prevHw);
    }

    struct address_space_device_control_ops m_ops = {};
    AddressSpaceHwFuncs m_hw = {};
    const AddressSpaceHwFuncs* m_prevHw = nullptr;
};

TEST_F(AddressSpaceSharedSlotsHostMemoryAllocatorContextTest,
       RejectZeroSizeAllocate) {
    ASSSHMAC ctx(&m_ops, &m_hw);

    AddressSpaceDevicePingInfo req1 = createAllocateRequest(0);
    ctx.perform(&req1);
    EXPECT_NE(req1.metadata, 0);

    AddressSpaceDevicePingInfo req2 = createAllocateRequest(0);
    ctx.perform(&req2);
    EXPECT_NE(req2.metadata, 0);
}

TEST_F(AddressSpaceSharedSlotsHostMemoryAllocatorContextTest,
       Reject64BitTruncatedZeroAndOverflowSizes) {
    ASSSHMAC ctx(&m_ops, &m_hw);

    // 4 GiB has low 32 bits == 0; previously truncated by align(int, int) to 0.
    AddressSpaceDevicePingInfo req4g = createAllocateRequest(0x100000000ULL);
    ctx.perform(&req4g);
    EXPECT_NE(req4g.metadata, 0);

    // Sizes above UINT32_MAX - (pageSize - 1) page-align past UINT32_MAX.
    AddressSpaceDevicePingInfo reqBoundary = createAllocateRequest(0xFFFFF001ULL);
    ctx.perform(&reqBoundary);
    EXPECT_NE(reqBoundary.metadata, 0);

    // Near UINT32_MAX cannot page-align within uint32_t bookkeeping.
    AddressSpaceDevicePingInfo reqUintMax = createAllocateRequest(UINT32_MAX);
    ctx.perform(&reqUintMax);
    EXPECT_NE(reqUintMax.metadata, 0);
}

TEST_F(AddressSpaceSharedSlotsHostMemoryAllocatorContextTest,
       ZeroSizeDoesNotCauseGcUseAfterFree) {
    ASSSHMAC ctx1(&m_ops, &m_hw);
    ASSSHMAC ctx2(&m_ops, &m_hw);

    // Step 1: Zero-size request is rejected and leaves no MemBlock* in ctx1.
    AddressSpaceDevicePingInfo zeroReq = createAllocateRequest(0);
    ctx1.perform(&zeroReq);
    EXPECT_NE(zeroReq.metadata, 0);

    // Step 2: Allocate one page in ctx1 (creates 64 MiB block A).
    AddressSpaceDevicePingInfo req1 = createAllocateRequest(4096);
    ctx1.perform(&req1);
    ASSERT_EQ(req1.metadata, 0);
    const uint64_t gpa1 = m_hw.getPhysAddrStartLocked() + req1.phys_addr;

    // Step 3: Unallocate gpa1 (block A becomes empty, kept as allowedEmpty=1).
    AddressSpaceDevicePingInfo unreq1 = createUnallocateRequest(gpa1);
    ctx1.perform(&unreq1);
    ASSERT_EQ(unreq1.metadata, 0);

    // Step 4: Fill block A completely from ctx2 (64 MiB).
    AddressSpaceDevicePingInfo req2a = createAllocateRequest(64u << 20);
    ctx2.perform(&req2a);
    ASSERT_EQ(req2a.metadata, 0);
    const uint64_t gpa2a = m_hw.getPhysAddrStartLocked() + req2a.phys_addr;

    // Step 5: Allocate one more page in ctx2 (creates 64 MiB block B).
    AddressSpaceDevicePingInfo req2b = createAllocateRequest(4096);
    ctx2.perform(&req2b);
    ASSERT_EQ(req2b.metadata, 0);
    const uint64_t gpa2b = m_hw.getPhysAddrStartLocked() + req2b.phys_addr;

    // Step 6: Free both ctx2 allocations, triggering gcEmptyBlocks(1) to erase
    // one of the empty MemBlocks from g_blocks.
    AddressSpaceDevicePingInfo unreq2a = createUnallocateRequest(gpa2a);
    ctx2.perform(&unreq2a);
    ASSERT_EQ(unreq2a.metadata, 0);

    AddressSpaceDevicePingInfo unreq2b = createUnallocateRequest(gpa2b);
    ctx2.perform(&unreq2b);
    ASSERT_EQ(unreq2b.metadata, 0);
}

}  // namespace emulation
} // namespace android
