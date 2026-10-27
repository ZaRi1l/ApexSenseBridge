#pragma once

// APEX 6 PCM extension: a small, optional-to-use processing and measurement layer.
// The unmodified game's PCM path is recovered by selecting Original (gain 100%).
// No second HID writer or process monitor is created by this extension.
#include "dualsense/Apex6PcmNormalization.h"
#include "platform/Apex6PcmControl.h"
#include "platform/Apex6PcmLiveMetrics.h"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <utility>

namespace asb::extensions {

class Apex6PcmExtension final {
public:
    struct PolicySnapshot {
        dualsense::Apex6PcmConfig config;
        dualsense::Apex6PcmScalingState state;
        unsigned legacyGainPercent = 100;
    };

    explicit Apex6PcmExtension(dualsense::Apex6PcmConfig config = {},
                                std::string_view token = {},
                                std::string_view gameKey = {})
        : scaler_(config), live_(token), control_(token, gameKey),
          statsConfig_(scaler_.config()), statsState_(scaler_.state()),
          legacyGainPercent_(legacyGain(config)) {}

    // Run on the existing bridge render thread. The control service retains the
    // request when the stats mutex is busy, so audio never blocks on UI reads.
    void applyPending() noexcept {
        control_.applyPending([this](const dualsense::Apex6PcmConfig& config) noexcept {
            std::unique_lock lock(statsMutex_, std::try_to_lock);
            if (!lock.owns_lock()) return false;
            scaler_ = dualsense::Apex6PcmScaler(config);
            statsConfig_ = scaler_.config();
            statsState_ = scaler_.state();
            legacyGainPercent_ = legacyGain(config);
            return true;
        });
    }

    [[nodiscard]] std::int8_t scale(std::int16_t sample, bool& clipped) noexcept {
        return scaler_.scaleSample(sample, clipped);
    }

    void publish(std::uint16_t left, std::uint16_t right,
                 std::uint16_t rawLeft, std::uint16_t rawRight,
                 std::uint8_t outputLeft, std::uint8_t outputRight,
                 std::uint8_t audioChannels, bool verified,
                 std::uint64_t dropped, std::uint64_t processed,
                 std::uint64_t clipped) noexcept {
        live_.updateLive(left, right, rawLeft, rawRight, outputLeft, outputRight,
                         audioChannels, verified, dropped, processed, clipped);
    }

    [[nodiscard]] PolicySnapshot snapshot() const noexcept {
        std::lock_guard lock(statsMutex_);
        return {statsConfig_, statsState_, legacyGainPercent_};
    }

private:
    static unsigned legacyGain(const dualsense::Apex6PcmConfig& config) noexcept {
        return config.mode == dualsense::Apex6PcmMode::Companding
                   ? std::clamp(config.legacyGainPercent, 100U, 200U) : 100U;
    }

    dualsense::Apex6PcmScaler scaler_{};
    platform::Apex6PcmLiveServer live_{};
    platform::Apex6PcmControlServer control_;
    mutable std::mutex statsMutex_;
    dualsense::Apex6PcmConfig statsConfig_{};
    dualsense::Apex6PcmScalingState statsState_{};
    unsigned legacyGainPercent_ = 100;
};

} // namespace asb::extensions
