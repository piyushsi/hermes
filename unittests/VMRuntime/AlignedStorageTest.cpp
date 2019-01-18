/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
// This test requires files that are only linked in if NCGen is used.
#ifdef HERMESVM_GC_NONCONTIG_GENERATIONAL

#include "gtest/gtest.h"

#include "Footprint.h"

#include "hermes/Support/OSCompat.h"
#include "hermes/VM/AlignedStorage.h"
#include "hermes/VM/LimitedStorageProvider.h"
#include "hermes/VM/StorageProvider.h"

#include "llvm/Support/MathExtras.h"

#include <cstdint>
#include <vector>

using namespace hermes;
using namespace hermes::vm;
using namespace hermes::vm::detail;

namespace {

static char *alignPointer(char *p, size_t align) {
  return reinterpret_cast<char *>(
      llvm::alignTo(reinterpret_cast<uintptr_t>(p), align));
}

struct AlignedStorageTest : public ::testing::Test {
  std::unique_ptr<StorageProvider> provider{StorageProvider::mmapProvider()};
};

TEST_F(AlignedStorageTest, SuccesfulAllocation) {
  AlignedStorage s{provider.get()};
  EXPECT_TRUE(static_cast<bool>(s));
}

#ifndef NDEBUG
TEST_F(AlignedStorageTest, FailedAllocation) {
  LimitedStorageProvider limitedProvider{StorageProvider::mmapProvider(), 0};
  AlignedStorage s{&limitedProvider};
  EXPECT_FALSE(static_cast<bool>(s));
}
#endif // !NDEBUG

TEST_F(AlignedStorageTest, Start) {
  AlignedStorage storage{provider.get()};

  char *lo = storage.lowLim();
  char *hi = storage.hiLim();

  EXPECT_EQ(lo, AlignedStorage::start(lo));
  EXPECT_EQ(lo, AlignedStorage::start(lo + storage.size() / 2));
  EXPECT_EQ(lo, AlignedStorage::start(hi - 1));

  // `hi` is the first address in the storage following \c storage (if
  // such a storage existed).
  EXPECT_EQ(hi, AlignedStorage::start(hi));
}

TEST_F(AlignedStorageTest, End) {
  AlignedStorage storage{provider.get()};

  char *lo = storage.lowLim();
  char *hi = storage.hiLim();

  EXPECT_EQ(hi, AlignedStorage::end(lo));
  EXPECT_EQ(hi, AlignedStorage::end(lo + storage.size() / 2));
  EXPECT_EQ(hi, AlignedStorage::end(hi - 1));

  // `hi` is the first address in the storage following \c storage (if
  // such a storage existed).
  EXPECT_EQ(hi + AlignedStorage::size(), AlignedStorage::end(hi));
}

TEST_F(AlignedStorageTest, Offset) {
  AlignedStorage storage{provider.get()};

  char *lo = storage.lowLim();
  char *hi = storage.hiLim();
  const size_t size = storage.size();

  EXPECT_EQ(0, AlignedStorage::offset(lo));
  EXPECT_EQ(size / 2, AlignedStorage::offset(lo + size / 2));
  EXPECT_EQ(size - 1, AlignedStorage::offset(hi - 1));

  // `hi` is the first address in the storage following \c storage (if
  // such a storage existed).
  EXPECT_EQ(0, AlignedStorage::offset(hi));
}

TEST_F(AlignedStorageTest, AdviseUnused) {
  const size_t FAILED = SIZE_MAX;
  const size_t PAGE_SIZE = oscompat::page_size();

  AlignedStorage storage{provider.get()};
  ASSERT_EQ(0, storage.size() % PAGE_SIZE);

  const size_t TOTAL_PAGES = storage.size() / PAGE_SIZE;
  const size_t FREED_PAGES = TOTAL_PAGES / 2;

  char *start = storage.lowLim();
  char *end = storage.hiLim();

  // On some platforms, the mapping containing [start, end) can be larger than
  // [start, end) itself, and the extra space may already contribute to the
  // footprint, so we account for this in \c initial.
  size_t initial = regionFootprint(start, end);
  ASSERT_NE(initial, FAILED);

  for (volatile char *p = start; p < end; p += PAGE_SIZE)
    *p = 1;

  size_t touched = regionFootprint(start, end);
  ASSERT_NE(touched, FAILED);

  storage.markUnused(start, start + FREED_PAGES * PAGE_SIZE);

  size_t marked = regionFootprint(start, end);
  ASSERT_NE(marked, FAILED);

  EXPECT_EQ(initial + TOTAL_PAGES, touched);
  EXPECT_EQ(touched - FREED_PAGES, marked);
}

TEST_F(AlignedStorageTest, Containment) {
  AlignedStorage storage{provider.get()};

  // Boundaries
  EXPECT_FALSE(storage.contains(storage.lowLim() - 1));
  EXPECT_TRUE(storage.contains(storage.lowLim()));
  EXPECT_TRUE(storage.contains(storage.hiLim() - 1));
  EXPECT_FALSE(storage.contains(storage.hiLim()));

  // Interior
  EXPECT_TRUE(storage.contains(storage.lowLim() + storage.size() / 2));
}

TEST_F(AlignedStorageTest, Alignment) {
  /**
   * This test alternates between allocating an AlignedStorage, and an anonymous
   * "spacer" mapping such that the i-th spacer has size:
   *
   *     AlignedStorage::size() + i MB
   *
   * In the worst case the anonymous mappings are perfectly interleaved with the
   * aligned storage, and we must be intentional about aligning the storage
   * allocations, like so:
   *
   *     ---+---+---+---+---+----+--+---+----+--+---+-----+-+---+---
   *     ...|AAA|SSS/   |AAA|SSSS|  |AAA|SSSS/  |AAA|SSSSS| |AAA|...
   *     ---+---+---+---+---+----+--+---+----+--+---+-----+-+---+---
   *
   * In the above diagram:
   *
   * - A character width correseponds to 2MB.
   * - A box's width includes its left boundary and excludes its right boundary.
   * - A / boundary indicates 1MB belongs to the previous box and 1MB to the
   *   next.
   * - Boxes labeled with `A` are AlignedStorage.
   * - Boxes labeled with `S` are spacers.
   * - Boxes with no label are unmapped.
   *
   * We cannot guarantee that we get this layout, but spacers disturb the
   * allocation pattern we (might) get from allocating in a tight loop.
   */

  std::vector<AlignedStorage> storages;
  std::vector<void *> spacers;

  const size_t MB = 1 << 20;
  const size_t SIZE = AlignedStorage::size();

  for (size_t space = SIZE + MB; space < 2 * SIZE; space += MB) {
    storages.emplace_back(provider.get());
    AlignedStorage &storage = storages.back();

    EXPECT_EQ(storage.lowLim(), alignPointer(storage.lowLim(), SIZE));

    void *spacer = oscompat::vm_allocate(space);
    ASSERT_NE(nullptr, spacer);
    spacers.push_back(spacer);
  }

  { // When \c storages goes out of scope, it will correctly destruct the \c
    // AlignedStorage instances it holds. \c spacers, on the other hand, holds
    // only raw pointers, so we must clean them up manually:
    size_t space = SIZE + MB;
    for (void *spacer : spacers) {
      oscompat::vm_free(spacer, space);
      space += MB;
    }
  }
}

} // namespace

#endif
