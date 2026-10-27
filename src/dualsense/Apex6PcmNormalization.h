#pragma once

#include "dualsense/Apex6PcmGain.h"

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
            if (!state_.valid || state_.effectiveGainRatio == 1.0) {
                return static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale);
            }
            const double originalOut = static_cast<double>(sample) * 127.0 / static_cast<double>(fullScale);
            const double scaledOut = originalOut * state_.effectiveGainRatio;
            const auto rounded = static_cast<std::int64_t>(std::lround(scaledOut));
            if (rounded > 127) {
                clipped = true;
                return 127;
            }
            if (rounded < -127) {
                clipped = true;
                return -127;
            }
            return static_cast<std::int8_t>(rounded);
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
