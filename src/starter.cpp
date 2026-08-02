// Tiny pure-logic helper reused by both firmware and unit tests.
// Keep hardware deps out of here so the `native` test env can compile it.
#include "starter.h"

namespace starter {
    int32_t add(int32_t a, int32_t b) {
        return a + b;
    }

    bool isLedOn() {
        return false; // overridden in firmware; tests just sanity check signature
    }

    int32_t mapClamp(int32_t x, int32_t inMin, int32_t inMax,
                     int32_t outMin, int32_t outMax) {
        if (inMin == inMax) return outMin; // avoid divide-by-zero
        int32_t inSpan = inMax - inMin;
        int32_t outSpan = outMax - outMin;
        int32_t mapped = outMin + (x - inMin) * outSpan / inSpan;
        int32_t lo = outMin < outMax ? outMin : outMax;
        int32_t hi = outMin < outMax ? outMax : outMin;
        if (mapped < lo) return lo;
        if (mapped > hi) return hi;
        return mapped;
    }
}