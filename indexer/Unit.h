#pragma once

constexpr size_t operator""_KB(unsigned long long v) { return 1024u * v; }

constexpr size_t operator""_MB(unsigned long long v) {
  return 1024u * 1024u * v;
}

constexpr size_t operator""_GB(unsigned long long v) {
  return 1024u * 1024u * 1024u * v;
}
