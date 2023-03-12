//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <cstdlib>

#include <glog/logging.h>

/**
 * Common macros used by this project
 */

#define GET_MACRO_2(_1, _2, NAME, ...) NAME
#define GET_MACRO_3(_1, _2, _3, NAME, ...) NAME
#define GET_MACRO_4(_1, _2, _3, _4, NAME, ...) NAME

#define ASSERT_1(condition)             \
  do {                                  \
    if (!(condition)) {                 \
      LOG(ERROR) << "Assertion failed"; \
      std::abort();                     \
    }                                   \
  } while (0)

#define ASSERT_2(condition, msg)                 \
  do {                                           \
    if (!(condition)) {                          \
      LOG(ERROR) << "Assertion failed: " << msg; \
      std::abort();                              \
    }                                            \
  } while (0)

#define ASSERT(...) GET_MACRO_2(__VA_ARGS__, ASSERT_2, ASSERT_1)(__VA_ARGS__)

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &) = delete;     \
  void operator=(const TypeName &) = delete

#if defined(__GNUC__) && __GNUC__ >= 4
#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif
