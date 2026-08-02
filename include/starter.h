#pragma once
#include <stdint.h>

namespace starter {
    int32_t add(int32_t a, int32_t b);
    bool isLedOn();

    // Linearly map x from [inMin, inMax] into [outMin, outMax], clamped to the
    // output range. Pure logic - safe to unit test on the host.
    int32_t mapClamp(int32_t x, int32_t inMin, int32_t inMax,
                     int32_t outMin, int32_t outMax);
}