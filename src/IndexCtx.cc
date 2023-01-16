#include "IndexCtx.h"

namespace pview {
IndexCtx *IndexCtx::get_instance() {
  static IndexCtx ctx_;
  return &ctx_;
}
}  // namespace pview
