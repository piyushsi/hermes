/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#ifndef HERMES_UNITTESTS_VMRUNTIME_EMPTYCELL_H
#define HERMES_UNITTESTS_VMRUNTIME_EMPTYCELL_H

#include "TestHelpers.h"
#include "hermes/Support/OSCompat.h"
#include "hermes/VM/GCCell.h"
#include "hermes/VM/VTable.h"

namespace hermes {
namespace vm {

/// An uninitialized cell that is \p Size bytes wide, for use with \c
/// DummyRuntime, in tests.  \p FixedSize = false is passed into the allocation
/// functions to simulate allocating a variabled size cell, even though this
/// cell is not a subclass of \c VariableSizeRuntimeCell and so each template
/// instantiation has a statically determined size.
template <size_t Size, bool FixedSize = false>
struct EmptyCell final : public GCCell {
  static const VTable vt;
  static constexpr size_t size() {
    return Size;
  }

  static EmptyCell *create(DummyRuntime &runtime) {
    return new (runtime.alloc<FixedSize>(size())) EmptyCell(&runtime.getHeap());
  }

  static EmptyCell *createLongLived(DummyRuntime &runtime) {
    return new (runtime.allocLongLived(size())) EmptyCell(&runtime.getHeap());
  }

  EmptyCell(GC *gc) : GCCell(gc, &vt) {}

  /// Touch bytes in the cell from the end of its header until the end of its
  /// memory region, at page sized intervals.
  ///
  /// \return The number of pages touched.
  inline size_t touch();
};

template <size_t Size, bool FixedSize>
const VTable EmptyCell<Size, FixedSize>::vt{CellKind::UninitializedKind, Size};

template <size_t Size, bool FixedSize>
size_t EmptyCell<Size, FixedSize>::touch() {
  const auto PS = hermes::oscompat::page_size();

  volatile char *begin = reinterpret_cast<char *>(this);
  volatile char *extra = begin + sizeof(EmptyCell);
  volatile char *end = begin + size();

  size_t n = 0;
  for (auto p = extra; p < end; p += PS, ++n)
    *p = 1;

  return n;
}

} // namespace vm
} // namespace hermes

#endif // HERMES_UNITTESTS_VMRUNTIME_EMPTYCELL_H
