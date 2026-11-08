#pragma once

#include "dualsense/Apex6PcmGain.h"
#include "dualsense/Apex6PcmLinear.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace asb::dualsense {

enum class Apex6PcmMode : std::uint8_t {
    Original = 0,
    Companding = 1,
    Linear = 2
};

[[nodiscard]] constexpr std::string_view apex6PcmModeName(Apex6PcmMode mode) noexcept {
    switch (mode) {
    case Apex6PcmMode::Original: return "original";
    case Apex6PcmMode::Companding: return "companding";
    case Apex6PcmMode::Linear: return "linear";
    }
    return "original";
}

[[nodiscard]] inline std::optional<Apex6PcmMode> parseApex6PcmMode(std::string_view text) noexcept {
    if (text == "original") return Apex6PcmMode::Original;
    if (text == "companding") return Apex6PcmMode::Companding;
    if (text == "linear") return Apex6PcmMode::Linear;
    return std::nullopt;
}

struct Apex6PcmConfig {
    Apex6PcmMode mode = Apex6PcmMode::Original;
    unsigned linearGainPercent = 100;
    unsigned referencePercent = 0;
    unsigned referencePeakAbs16 = 0;
    unsigned targetPercent = 90;
    unsigned maxGainPercent = 400;
    unsigned legacyGainPercent = 100;
    unsigned compandingGainPercent = 100;
    // Defaults off to preserve the original global vibration-strength behavior.
    bool ignoreVibrationStrength = false;
};

struct Apex6PcmScalingState {
    double requestedGainRatio = 1.0;
    double effectiveGainRatio = 1.0;
    bool gainLimited = false;
    bool valid = true;
    std::string_view status = "valid";
};

class Apex6PcmScaler {
public:
    explicit Apex6PcmScaler(const Apex6PcmConfig& config = {}) noexcept
        : config_(config) {
        init();
    }

    [[nodiscard]] std::int8_t scaleSample(std::int16_t sample, bool& clipped) const noexcept {
        clipped = false;
        const std::int64_t fullScale = sample >= 0 ? 32767 : 32768;

        switch (config_.mode) {
        case Apex6PcmMode::Original:
            return static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale);

        case Apex6PcmMode::Companding: {
            const unsigned gainVal = config_.compandingGainPercent != 100 ? config_.compandingGainPercent : config_.legacyGainPercent;
            return scaleApex6Pcm(sample, gainVal);
        }

        case Apex6PcmMode::Linear: {
            // Reuse the Nov 6 upstream-dev integer scaler so native CLI and
            // Extended WPF per-game gain use the exact same PCM transform.
            const unsigned gain = std::clamp(config_.linearGainPercent, 0U, 1000U);
            if (!state_.valid) return scaleApex6PcmLinear(sample, 100);
            const auto result = scaleApex6PcmLinear(sample, gain);
            // The digital output saturates at +/-127. Count actual saturation
            // without changing the rounding or the 100% upstream baseline.
            const std::int64_t magnitude = sample >= 0 ? sample : -std::int64_t{sample};
            const std::int64_t denominator = fullScale * 100;
            const std::int64_t rounded = (magnitude * 127 * gain + denominator / 2) / denominator;
            clipped = rounded > 127;
            return result;
        }
        }
        return static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale);
    }

    [[nodiscard]] const Apex6PcmScalingState& state() const noexcept { return state_; }
    [[nodiscard]] const Apex6PcmConfig& config() const noexcept { return config_; }

private:
    void init() noexcept {
        state_ = {};
        if (config_.mode == Apex6PcmMode::Original) {
            state_.requestedGainRatio = 1.0;
            state_.effectiveGainRatio = 1.0;
            state_.gainLimited = false;
            state_.valid = true;
            state_.status = "valid";
            return;
        }

        if (config_.mode == Apex6PcmMode::Companding) {
            const unsigned gainVal = config_.compandingGainPercent != 100 ? config_.compandingGainPercent : config_.legacyGainPercent;
            const auto gain = std::clamp(gainVal, 100U, 200U);
            state_.requestedGainRatio = static_cast<double>(gain) / 100.0;
            state_.effectiveGainRatio = state_.requestedGainRatio;
            state_.gainLimited = false;
            state_.valid = true;
            state_.status = "valid";
            return;
        }

        if (config_.mode == Apex6PcmMode::Linear) {
            const auto gain = std::clamp(config_.linearGainPercent, 0U, 1000U);
            state_.requestedGainRatio = static_cast<double>(gain) / 100.0;
            state_.effectiveGainRatio = state_.requestedGainRatio;
            state_.gainLimited = false;
            state_.valid = true;
            state_.status = "valid";
            return;
        }


    }

    Apex6PcmConfig config_{};
    Apex6PcmScalingState state_{};
};

} // namespace asb::dualsense
