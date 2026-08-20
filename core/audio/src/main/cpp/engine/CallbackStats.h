#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace aa {

// Callback-interval statistics, safe to update from the real-time audio
// thread (atomics only, no locks). Intervals are histogrammed in 100 µs
// buckets up to 51.2 ms; anything longer lands in the overflow bucket.
class CallbackStats {
public:
    static constexpr int kBucketUs = 100;
    static constexpr int kNumBuckets = 512;  // + 1 overflow

    // Producer side (audio callback).
    void recordCallback(int64_t nowNanos) {
        if (lastNanos_ != 0) {
            const int64_t dtUs = (nowNanos - lastNanos_) / 1000;
            int idx = static_cast<int>(dtUs / kBucketUs);
            if (idx < 0) idx = 0;
            if (idx > kNumBuckets) idx = kNumBuckets;
            hist_[static_cast<std::size_t>(idx)].fetch_add(1, std::memory_order_relaxed);
            sumUs_.fetch_add(dtUs, std::memory_order_relaxed);
            intervalCount_.fetch_add(1, std::memory_order_relaxed);
            atomicMin(minUs_, dtUs);
            atomicMax(maxUs_, dtUs);
        }
        lastNanos_ = nowNanos;
        callbackCount_.fetch_add(1, std::memory_order_relaxed);
    }

    // Consumer side.
    struct Summary {
        int64_t callbackCount = 0;
        double meanMs = 0, minMs = 0, maxMs = 0, p99Ms = 0;
    };

    Summary summarize() const {
        Summary s;
        s.callbackCount = callbackCount_.load(std::memory_order_relaxed);
        const int64_t n = intervalCount_.load(std::memory_order_relaxed);
        if (n <= 0) return s;
        s.meanMs = static_cast<double>(sumUs_.load(std::memory_order_relaxed)) / n / 1000.0;
        s.minMs = static_cast<double>(minUs_.load(std::memory_order_relaxed)) / 1000.0;
        s.maxMs = static_cast<double>(maxUs_.load(std::memory_order_relaxed)) / 1000.0;
        // p99 from the histogram.
        const int64_t target = n - n / 100;
        int64_t cum = 0;
        for (int i = 0; i <= kNumBuckets; ++i) {
            cum += hist_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
            if (cum >= target) {
                s.p99Ms = (i >= kNumBuckets) ? s.maxMs
                                             : (i + 1) * kBucketUs / 1000.0;
                break;
            }
        }
        return s;
    }

    void reset() {
        for (auto& b : hist_) b.store(0, std::memory_order_relaxed);
        sumUs_.store(0);
        intervalCount_.store(0);
        callbackCount_.store(0);
        minUs_.store(INT64_MAX);
        maxUs_.store(0);
        lastNanos_ = 0;
    }

private:
    static void atomicMin(std::atomic<int64_t>& target, int64_t v) {
        int64_t cur = target.load(std::memory_order_relaxed);
        while (v < cur && !target.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    static void atomicMax(std::atomic<int64_t>& target, int64_t v) {
        int64_t cur = target.load(std::memory_order_relaxed);
        while (v > cur && !target.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }

    std::array<std::atomic<uint32_t>, kNumBuckets + 1> hist_{};
    std::atomic<int64_t> sumUs_{0};
    std::atomic<int64_t> intervalCount_{0};
    std::atomic<int64_t> callbackCount_{0};
    std::atomic<int64_t> minUs_{INT64_MAX};
    std::atomic<int64_t> maxUs_{0};
    int64_t lastNanos_ = 0;  // touched only by the audio thread
};

}  // namespace aa
