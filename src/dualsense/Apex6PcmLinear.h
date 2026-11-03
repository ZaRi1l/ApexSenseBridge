#pragma once

#include "dualsense/Apex6PcmGain.h"

#include <algorithm>
#include <cstdint>

namespace asb::dualsense {

// Optional APEX 6 Pro native PCM-only linear gain. Does not touch trigger,
// conventional rumble, envelopes or any other model. 100% is bit-identical
// to the upstream quantizer; higher values clamp to the signed 8-bit range.
// Integer arithmetic prevents overflow and keeps per-sample latency bounded.
[[nodiscard]] constexpr std::int8_t scaleApex6PcmLinear(
    std::int16_t sample, unsigned gainPercent) noexcept {
    const auto gain = std::clamp(gainPercent, 0U, 1000U);
    if (gain == 100) return scaleApex6Pcm(sample, 100);
    if (gain == 0 || sample == 0) return 0;
    const std::int64_t fullScale = sample >= 0 ? 32767 : 32768;
    const std::int64_t magnitude = sample >= 0 ? sample : -std::int64_t{sample};
    const std::int64_t denominator = fullScale * 100;
    const std::int64_t rounded = (magnitude * 127 * gain + denominator / 2) / denominator;
    const auto limited = (std::min)(rounded, std::int64_t{127});
    return static_cast<std::int8_t>(sample < 0 ? -limited : limited);
}

} // namespace asb::dualsense
