// Lock-free primitives for passing data between the real-time core and the
// communications core. Neither blocks, allocates, or disables interrupts.
#pragma once
#include <atomic>
#include <stdint.h>
#include <string.h>

namespace sd {

// Single writer, any number of readers. The writer never waits, so a reader on
// the other core can never delay the control loop. T must be trivially copyable.
template <typename T>
class SeqLock {
 public:
  void write(const T& v) {
    uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    memcpy(&data_, &v, sizeof(T));
    std::atomic_thread_fence(std::memory_order_release);
    seq_.store(s + 2, std::memory_order_release);
  }
  // Retries only while a write is in flight; a write is a short memcpy.
  bool read(T& out) const {
    for (int tries = 0; tries < 64; tries++) {
      uint32_t a = seq_.load(std::memory_order_acquire);
      if (a & 1) continue;
      memcpy(&out, &data_, sizeof(T));
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) == a) return a != 0;
    }
    return false;
  }

 private:
  std::atomic<uint32_t> seq_{0};
  T data_{};
};

// Bounded single-producer single-consumer ring. N must be a power of two.
template <typename T, uint32_t N>
class SpscRing {
  static_assert((N & (N - 1)) == 0, "N must be a power of two");

 public:
  bool push(const T& v) {
    uint32_t h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) == N) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    buf_[h & (N - 1)] = v;
    head_.store(h + 1, std::memory_order_release);
    return true;
  }
  bool pop(T& out) {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;
    out = buf_[t & (N - 1)];
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }
  uint32_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  T buf_[N];
  std::atomic<uint32_t> head_{0}, tail_{0}, dropped_{0};
};

}  // namespace sd
