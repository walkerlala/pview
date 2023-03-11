//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include "IndexCtx.h"

namespace pview {
IndexCtx *IndexCtx::get_instance() {
  static IndexCtx ctx_;
  return &ctx_;
}
}  // namespace pview
