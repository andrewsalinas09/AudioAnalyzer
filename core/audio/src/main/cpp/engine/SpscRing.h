#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aa {

// Single-producer/single-consumer lock-free ring buffer of float samples.
// Producer is the audio callback (real-time thread: no locks, no allocation);
// consumer is the snapshot/analysis path. Capacity must be a power of two.
class SpscRing {
public:
    explicit SpscRing(std::size_t capacityPow2)
        : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

    // Writes up to n samples; drops the remainder if full. Returns written.
    std::size_t write(const float* data, std::size_t n) {
        const uint64_t head = head_.load(std::memory_order_relaxed);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t free = buf_.size() - static_cast<std::size_t>(head - tail);
        if (n > free) n = free;
        for (std::size_t i = 0; i < n; ++i) {
            buf_[static_cast<std::size_t>(head + i) & mask_] = data[i];
        }
        head_.store(head + n, std::memory_order_release);
        return n;
    }

    // Reads up to maxN samples into out. Returns number read.
    std::size_t read(float* out, std::size_t maxN) {
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        const uint64_t head = head_.load(std::memory_order_acquire);
        std::size_t avail = static_cast<std::size_t>(head - tail);
        if (avail > maxN) avail = maxN;
        for (std::size_t i = 0; i < avail; ++i) {
            out[i] = buf_[static_cast<std::size_t>(tail + i) & mask_];
        }
        tail_.store(tail + avail, std::memory_order_release);
        return avail;
    }

    std::size_t capacity() const { return buf_.size(); }

private:
    std::vector<float> buf_;
    std::size_t mask_;
    std::atomic<uint64_t> head_{0};  // written by producer
    std::atomic<uint64_t> tail_{0};  // written by consumer
};

}  // namespace aa
