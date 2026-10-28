#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/Apex6PcmGain.h"
#include "dualsense/Apex6PcmNormalization.h"
#include "dualsense/Apex6TriggerEffect.h"
#include "flydigi/Apex6Protocol.h"
#include "platform/Apex6PcmControl.h"
#include <cstring>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

int peak(const std::array<std::int8_t, 8>& samples) {
    int result = 0;
    for (const auto sample : samples) result = (std::max)(result, std::abs(static_cast<int>(sample)));
    return result;
}

void testNativeTriggerEffects() {
    using namespace asb::dualsense;
    std::array<std::uint8_t, 11> bytes{};
    bytes[0] = 0x21;
    bytes[1] = static_cast<std::uint8_t>((1U << 2U) | (1U << 3U));
    const auto packed = (1U << 6U) | (7U << 9U);
    bytes[3] = static_cast<std::uint8_t>(packed);
    bytes[4] = static_cast<std::uint8_t>(packed >> 8U);
    const auto feedback = decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0);
    assert(feedback && feedback->type == Apex6TriggerType::Feedback);
    assert(feedback->zoneStrengths[2] == 2 && feedback->zoneStrengths[3] == 8);
    Apex6TriggerRenderState weakState{};
    Apex6TriggerRenderState strongState{};
    assert(peak(renderApex6TriggerEffect(*feedback, 60, weakState)) <= 16);
    assert(peak(renderApex6TriggerEffect(*feedback, 90, strongState)) > 40);

    bytes[0] = 0x26;
    bytes[1] = 1U << 3U;
    bytes[3] = bytes[4] = 0;
    bytes[9] = 180;
    bytes[10] = 7;
    const auto vibration = decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0);
    assert(vibration && vibration->frequency == 180);
    Apex6TriggerRenderState vibrationState{};
    // A high requested frequency must never become high motor amplitude.
    assert(peak(renderApex6TriggerEffect(*vibration, 90, vibrationState)) <= 10);
    bytes[9] = 0;
    const auto silentVibration = decodeApex6TriggerEffect(
        asb::TriggerSide::Right, bytes, 0);
    assert(silentVibration);
    Apex6TriggerRenderState silentState{};
    assert(peak(renderApex6TriggerEffect(*silentVibration, 90, silentState)) == 0);

    const std::array<std::uint8_t, 11> referenceVibration{
        0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 150, 0};
    const auto reference = decodeApex6TriggerEffect(
        asb::TriggerSide::Left, referenceVibration, 0);
    assert(reference && reference->frequency == 150);
    assert(reference->zoneStrengths[0] == 8 && reference->zoneStrengths[9] == 8);
    Apex6TriggerRenderState referenceState{};
    assert(peak(renderApex6TriggerEffect(*reference, 255, referenceState)) > 60);
    for (const auto type : {0x21, 0x25, 0x26}) {
        std::array<std::uint8_t, 11> empty{};
        empty[0] = static_cast<std::uint8_t>(type);
        empty[9] = 150;
        Apex6TriggerDecodeError error{};
        const auto decoded = decodeApex6TriggerEffect(
            asb::TriggerSide::Left, empty, 0, &error);
        assert(decoded && decoded->type == Apex6TriggerType::Off);
        assert(error == Apex6TriggerDecodeError::None);
    }

    bytes = {};
    bytes[0] = 0x25;
    bytes[1] = static_cast<std::uint8_t>((1U << 2U) | (1U << 7U));
    bytes[3] = 7;
    const auto weapon = decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0);
    assert(weapon && weapon->type == Apex6TriggerType::Weapon);
    assert(weapon->startZone == 2 && weapon->endZone == 7 && weapon->strength == 8);
    Apex6TriggerRenderState weaponState{};
    assert(peak(renderApex6TriggerEffect(*weapon, 0, weaponState)) == 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 100, weaponState)) > 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) > 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) > 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) == 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 0, weaponState)) == 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) > 0);

    bytes[0] = 0x05;
    const auto off = decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0);
    assert(off && off->type == Apex6TriggerType::Off);
    bytes[0] = 0;
    assert(decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0)->type ==
           Apex6TriggerType::Off);
    bytes[0] = 0x25;
    bytes[2] = 0x80;
    Apex6TriggerDecodeError error{};
    assert(!decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0, &error));
    assert(error == Apex6TriggerDecodeError::InvalidParameters);
    bytes[0] = 0x7F;
    assert(!decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0, &error));
    assert(error == Apex6TriggerDecodeError::UnsupportedType);
}

void testBowEffect() {
    using namespace asb::dualsense;
    std::array<std::uint8_t, 11> bytes{0x22, 1, 1, 55}; // Endfield: zones 0/8, force 8, snap 7.
    Apex6TriggerDecodeError error{};
    const auto bow = decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0, &error);
    assert(bow && bow->type == Apex6TriggerType::Bow);
    assert(error == Apex6TriggerDecodeError::None);
    assert(bow->startZone == 0 && bow->endZone == 8);
    assert(bow->strength == 8 && bow->snapStrength == 7);
    Apex6TriggerRenderState state{};
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        assert(peak(renderApex6TriggerEffect(*bow, 0, state)) == 0);
        assert(peak(renderApex6TriggerEffect(*bow, 100, state)) > 0);
        assert(peak(renderApex6TriggerEffect(*bow, 220, state)) > 0);
        assert(peak(renderApex6TriggerEffect(*bow, 220, state)) > 0);
        assert(peak(renderApex6TriggerEffect(*bow, 220, state)) == 0);
        assert(peak(renderApex6TriggerEffect(*bow, 255, state)) == 0);
    }
    // Receiving Bow while already held must not invent a crossing/snap.
    state.reset();
    assert(peak(renderApex6TriggerEffect(*bow, 255, state)) == 0);
    for (const auto mask : {0x001U, 0x103U, 0x201U, 0x801U}) {
        bytes[1] = static_cast<std::uint8_t>(mask);
        bytes[2] = static_cast<std::uint8_t>(mask >> 8U);
        assert(!decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0, &error));
        assert(error == Apex6TriggerDecodeError::InvalidParameters);
    }
    bytes = {0x22, 1, 1, 0x40};
    assert(!decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0, &error));
    bytes = {0x22};
    assert(decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0)->type == Apex6TriggerType::Off);
}

void testContinuousCarrierClock() {
    using namespace asb::dualsense;
    for (const auto type : {Apex6TriggerType::Feedback, Apex6TriggerType::Vibration}) {
        for (const auto frequency : {1, 30, 85, 150, 180, 255}) {
            Apex6TriggerEffect effect{};
            effect.type = type;
            effect.frequency = static_cast<std::uint8_t>(frequency);
            effect.zoneStrengths[9] = 8;
            Apex6TriggerRenderState left{}, right{};
            for (std::uint64_t block = 0; block < 250; ++block) {
                // Different press/release timing and independently installed effects.
                if (block % 19 == 0) right.reset();
                (void)renderApex6TriggerEffect(effect, 255, left, block * 8);
                (void)renderApex6TriggerEffect(effect, block % 3 == 0 ? 0 : 255,
                                               right, block * 8);
                const auto lt = renderApex6TriggerEffect(effect, 255, left, block * 8 + 8);
                const auto rt = renderApex6TriggerEffect(effect, 255, right, block * 8 + 8);
                assert(lt == rt); // Identical effects remain eligible for Both, at full duty.
            }
        }
    }
}

class FakeApex6Transport final : public asb::platform::HidTransport {
public:
    FakeApex6Transport() {
        info_.vendorId = asb::flydigi::apex6::kVendorId;
        info_.productId = asb::flydigi::apex6::kProductId;
        info_.usagePage = asb::flydigi::apex6::kUsagePage;
        info_.inputReportLength = 33;
        info_.outputReportLength = 33;
    }

    bool isOpen() const noexcept override { return true; }
    const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report,
                           std::string&) override {
        writes.emplace_back(report.begin(), report.end());
        if (report.size() == 33 &&
            report[3] == asb::flydigi::apex6::kCmdGetInfo) {
            std::vector<std::uint8_t> reply(33, 0);
            reply[1] = asb::flydigi::apex6::kMagic0;
            reply[2] = asb::flydigi::apex6::kMagic1;
            reply[3] = asb::flydigi::apex6::kCmdGetInfo;
            reply[4] = 1;
            reply[6] = asb::flydigi::apex6::kDeviceType;
            reply[30] = 0x9F;
            reply[32] = asb::flydigi::apex6::checksum(
                std::span<const std::uint8_t>(reply).subspan(1));
            replies.push_back(std::move(reply));
        }
        return true;
    }

    asb::platform::HidReadStatus readInputReport(
        std::span<std::uint8_t> report,
        std::chrono::milliseconds,
        std::size_t& bytesRead,
        std::string&) override {
        bytesRead = 0;
        if (replies.empty()) return asb::platform::HidReadStatus::Timeout;
        const auto reply = std::move(replies.front());
        replies.pop_front();
        std::copy(reply.begin(), reply.end(), report.begin());
        bytesRead = reply.size();
        return asb::platform::HidReadStatus::Data;
    }

    asb::HidDeviceInfo info_{};
    std::deque<std::vector<std::uint8_t>> replies;
    std::vector<std::vector<std::uint8_t>> writes;
};

} // namespace

void testTriggerDiagnostics() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    bridge.updateTriggerPositions(200, 200);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x08;
    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0, 0, 0, 0, 0, 0, 90, 0};
    bridge.handle(feedback);
    feedback.leftTriggerEffect[2] = 0x80;
    bridge.handle(feedback);
    bridge.handle(feedback);
    feedback.leftTriggerEffect[0] = 0x7F;
    bridge.handle(feedback);
    auto stats = bridge.stats();
    assert(stats.triggerUnsupported == 3 && stats.triggerMalformed == 2);
    assert(stats.triggerRejectedStops == 1 && stats.triggerStops == 0);
    assert(stats.triggerTraceCount == 3);
    assert(stats.lastActiveLeft && stats.lastActiveLeft->bytes[9] == 90);
    assert(stats.lastRejectedLeft && stats.lastRejectedLeft->bytes[0] == 0x7F);
    assert(stats.triggerTrace[1].status == Apex6TriggerTraceStatus::Malformed);
    assert(stats.triggerTrace[1].position == 200);

    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0, 0, 0, 0, 0, 0, 1, 0};
    for (unsigned frequency = 1; frequency <= 70; ++frequency) {
        feedback.leftTriggerEffect[9] = static_cast<std::uint8_t>(frequency);
        bridge.handle(feedback);
    }
    feedback.leftTriggerEffect = {0x26};
    bridge.handle(feedback);
    stats = bridge.stats();
    assert(stats.triggerTraceCount == 64 && stats.triggerTraceOverwritten == 10);
    assert(stats.triggerTrace[63].status == Apex6TriggerTraceStatus::Off);
    assert(stats.lastActiveLeft->bytes[9] == 70);
    assert(stats.lastRejectedLeft->bytes[0] == 0x7F);
    assert(stats.triggerUnsupported == 3 && stats.triggerStops == 1);

    DualSenseFeedback audio{};
    audio.kind = FeedbackKind::AudioHapticWaveform;
    audio.audioSequence = 1;
    audio.leftHapticSamples.fill(4000);
    audio.rightHapticSamples.fill(-8000);
    bridge.handle(audio);
    audio.audioSequence = 2;
    audio.leftHapticSamples.fill(0);
    audio.rightHapticSamples.fill(0);
    bridge.handle(audio);
    stats = bridge.stats();
    assert(stats.waveformSilentBlocks == 1);
    assert(stats.waveformLeftActiveRms == 4000.0);
    assert(stats.waveformRightActiveRms == 8000.0);
    assert(stats.waveformLeftThresholded == 0 && stats.waveformRightThresholded == 0);
    assert(stats.rawAudioMeasuredBlocks == 0 && stats.rawAudioFrames == 0);
    audio.audioSequence = 3;
    audio.hasRawAudioMeasurements = true;
    audio.rawAudioFrames = 384;
    audio.rawAudioPeaks = {1234, 5678, 10000, 32768};
    audio.rawHapticLeftSumSquares = 384ULL * 10000 * 10000;
    audio.rawHapticRightSumSquares = 384ULL * 32768 * 32768;
    bridge.handle(audio);
    stats = bridge.stats();
    assert(stats.rawAudioMeasuredBlocks == 1 && stats.rawAudioFrames == 384);
    assert(stats.rawAudioPeaks[0] == 1234 && stats.rawAudioPeaks[3] == 32768);
    assert(stats.rawHapticLeftRms == 10000.0 && stats.rawHapticRightRms == 32768.0);
    assert(stats.waveformSilentBlocks == 2);
}

void testPcmGainCurve() {
    using asb::dualsense::scaleApex6Pcm;
    for (int sample = -32768; sample <= 32767; ++sample) {
        const auto pcm = static_cast<std::int16_t>(sample);
        const auto baseline = sample * 127 / (sample >= 0 ? 32767 : 32768);
        assert(scaleApex6Pcm(pcm) == baseline);
        for (const auto gain : {100U, 125U, 150U, 200U}) {
            const auto value = int(scaleApex6Pcm(pcm, gain));
            assert(value >= -127 && value <= 127);
            assert(std::abs(value) >= std::abs(baseline));
            if (sample > -32768) {
                assert(value >= scaleApex6Pcm(static_cast<std::int16_t>(sample - 1), gain));
            }
            assert(sample >= 0 ? value >= 0 : value <= 0);
        }
    }
    assert(scaleApex6Pcm(0, 200) == 0);
    assert(scaleApex6Pcm(32767, 200) == 127);
    assert(scaleApex6Pcm(-32768, 200) == -127);
    assert(scaleApex6Pcm(1000, 200) > scaleApex6Pcm(1000));
    assert(scaleApex6Pcm(10028, 200) == 59); // ZZZ capture peak, not a linear 2x/clipping boost.
    assert(scaleApex6Pcm(1000, 999) == scaleApex6Pcm(1000, 200));
    assert(scaleApex6Pcm(1000, 0) == scaleApex6Pcm(1000));
}

void testApex6PcmNormalization() {
    using namespace asb::dualsense;

    // Original and legacy companding remain bit-identical.
    Apex6PcmScaler original(Apex6PcmConfig{Apex6PcmMode::Original});
    Apex6PcmConfig legacy{};
    legacy.mode = Apex6PcmMode::Companding;
    legacy.compandingGainPercent = 150;
    legacy.legacyGainPercent = 150;
    Apex6PcmScaler companding(legacy);
    for (int sample = -32768; sample <= 32767; ++sample) {
        bool clipped = false;
        const auto pcm = static_cast<std::int16_t>(sample);
        assert(original.scaleSample(pcm, clipped) == scaleApex6Pcm(pcm, 100));
        assert(!clipped);
        assert(companding.scaleSample(pcm, clipped) == scaleApex6Pcm(pcm, 150));
        assert(!clipped);
    }

    // Linear scales signed samples uniformly with a safe 8-bit saturation.
    Apex6PcmConfig linear{};
    linear.mode = Apex6PcmMode::Linear;
    for (const unsigned gain : {100U, 400U, 1000U, 1200U}) {
        linear.linearGainPercent = gain;
        Apex6PcmScaler scaler(linear);
        const auto effective = gain > 1000 ? 10.0 : gain / 100.0;
        assert(std::abs(scaler.state().effectiveGainRatio - effective) < 1e-9);
        bool clipped = false;
        assert(scaler.scaleSample(0, clipped) == 0 && !clipped);
        const auto mid = scaler.scaleSample(1000, clipped);
        assert(mid > 0 && !clipped);
        assert(scaler.scaleSample(-1000, clipped) == -mid && !clipped);
        assert(scaler.scaleSample(32767, clipped) == 127);
        if (effective > 1.0) assert(clipped);
        assert(scaler.scaleSample(-32768, clipped) == -127);
        if (effective > 1.0) assert(clipped);
    }
    linear.linearGainPercent = 1000;
    Apex6PcmScaler maxGain(linear);
    bool clipped = false;
    assert(maxGain.scaleSample(5000, clipped) == 127 && clipped);
    assert(maxGain.scaleSample(-5000, clipped) == -127 && clipped);
    assert(maxGain.scaleSample(100, clipped) > 0 && !clipped);
}

void testApex6EnvelopeThresholdPolicy() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    asb::haptics::HapticConfig config{};
    config.activationThreshold = 0.95; // Shared setting may be high for APEX 4/5.
    Apex6HapticBridge bridge(device, config, true);
    DualSenseFeedback feedback{};
    feedback.kind = FeedbackKind::AudioHaptics;
    feedback.leftPeak = 5000; // Suppressed by the default APEX 4/5 processor.
    bridge.handle(feedback);
    assert(bridge.stats().audioEnvelopeActive == 1);
    // The caller's config and conventional-motor behavior remain unchanged.
    assert(config.activationThreshold == 0.95);
    assert(asb::haptics::HapticProcessor(config).process(feedback).lowFrequency == 0);
}

void testStrength(unsigned strength, double threshold, int pcmAmplitude = 30000,
                  unsigned gain = 100, bool leftOnly = false) {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    asb::haptics::HapticConfig config{};
    config.activationThreshold = threshold;
    Apex6HapticBridge bridge(device, config, true, strength, strength, gain);
    bridge.updateTriggerPositions(200, 200);
    assert(bridge.start(error));
    DualSenseFeedback trigger{};
    trigger.enableBits1 = 0x0C;
    trigger.leftTriggerEffect = {1, 0, 100};
    trigger.rightTriggerEffect = trigger.leftTriggerEffect;
    bridge.handle(trigger);
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.leftHapticSamples.fill(static_cast<std::int16_t>(pcmAmplitude));
    waveform.rightHapticSamples.fill(leftOnly ? 0 : static_cast<std::int16_t>(-pcmAmplitude));
    for (unsigned sequence = 1; sequence <= 4; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(bridge.stop(error));
    int maximumGrip = 0;
    int maximumTrigger = 0;
    for (const auto& report : transport->writes) {
        if (report[3] != apex6::kCmdRealtimeMotor) continue;
        for (std::size_t sample = 0; sample < 8; ++sample) {
            maximumGrip = (std::max)(maximumGrip, std::abs(int(report[7 + sample * 3]) - 128));
            maximumGrip = (std::max)(maximumGrip, std::abs(int(report[8 + sample * 3]) - 128));
            if (leftOnly) assert(report[8 + sample * 3] == 128);
            maximumTrigger = (std::max)(maximumTrigger, std::abs(int(report[6 + sample * 3]) - 128));
        }
    }
    if (strength == 0) assert(maximumGrip == 0);
    else assert(maximumGrip == int(scaleApex6Pcm(static_cast<std::int16_t>(pcmAmplitude), gain)) * int(strength) / 100);
    if (strength == 0) assert(maximumTrigger == 0);
    else assert(maximumTrigger > 0 && maximumTrigger <= int(strength));
    const auto stats = bridge.stats();
    if (strength == 0) assert(stats.waveformActiveRendered == 0);
    else assert(stats.waveformActiveRendered > 0);
    assert(stats.waveformLeftThresholded == 0 && stats.waveformRightThresholded == 0);
    assert(stats.pcmGainPercent == gain);
    assert(stats.pcmOutputLeftPeak == static_cast<unsigned>(maximumGrip));
    const auto expectedRight = leftOnly ? 0 :
        std::abs(int(scaleApex6Pcm(static_cast<std::int16_t>(-pcmAmplitude), gain))) * int(strength) / 100;
    assert(stats.pcmOutputRightPeak == static_cast<unsigned>(expectedRight));
}

void testSubQuantizationPcmDoesNotCountRumbleAsRenderedPcm() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    asb::haptics::HapticConfig config{};
    config.activationThreshold = 0;
    Apex6HapticBridge bridge(device, config, true);
    assert(bridge.start(error));
    DualSenseFeedback rumble{};
    rumble.enableBits1 = 0x01;
    rumble.rumbleRight = 100;
    bridge.handle(rumble);
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.leftHapticSamples.fill(1); // Nonzero, but below 8-bit output resolution.
    for (unsigned sequence = 1; sequence <= 4; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(bridge.stop(error));
    const auto stats = bridge.stats();
    assert(stats.waveformLeftActiveBlocks == 4 && stats.waveformActiveRendered == 0);
    assert(stats.gripRumbleFrames != 0);
    assert(stats.pcmOutputLeftPeak == 0 && stats.pcmOutputRightPeak == 0);
}

void testIdleSkipsFramesAndWakesImmediately() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;

    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const auto idle = bridge.stats();
    assert(idle.framesWritten < 40); // 100 ticks without idle suppression
    assert(idle.idleFramesSkipped > 30);
    assert(idle.idleKeepalives > 0);

    DualSenseFeedback silent{};
    silent.kind = FeedbackKind::AudioHapticWaveform;
    silent.audioSequence = 1;
    bridge.handle(silent);
    // A keepalive may briefly clear idle_ while the audio worker runs. Whether
    // this silent input was skipped immediately or consumed by that worker is
    // timing-dependent; both are valid as long as it creates no motor output.
    assert(bridge.stats().waveformSilentBlocks == 1);
    assert(bridge.stats().pcmOutputLeftPeak == 0 &&
           bridge.stats().pcmOutputRightPeak == 0);

    const auto framesBefore = bridge.stats().framesWritten;
    DualSenseFeedback active = silent;
    active.audioSequence = 2;
    active.leftHapticSamples.fill(12000);
    bridge.handle(active);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(bridge.stats().framesWritten > framesBefore);
    assert(bridge.stats().waveformActiveRendered > 0);

    DualSenseFeedback rumble{};
    rumble.enableBits1 = 0x01;
    rumble.rumbleLeft = 200;
    bridge.handle(rumble);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(bridge.stats().gripRumbleFrames > 0);
    assert(bridge.stop(error));
    assert(!bridge.failed());
}

void testPerGamePcmStrengthBypass() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    // Fake HID transport: measure the same native waveform with strength=50%.
    // The bypass may affect PCM only, not the existing ordinary rumble path.
    const auto run = [](bool bypass) {
        auto* transport = new FakeApex6Transport();
        Apex5Device device{TransportPtr(transport)};
        std::string error;
        assert(device.verifyIdentity(error));
        Apex6PcmConfig config{};
        config.mode = Apex6PcmMode::Linear;
        config.linearGainPercent = 100;
        config.ignoreVibrationStrength = bypass;
        Apex6HapticBridge bridge(device, {}, true, 100, 50, config);
        assert(bridge.start(error));
        DualSenseFeedback wave{};
        wave.kind = FeedbackKind::AudioHapticWaveform;
        wave.leftHapticSamples.fill(12000);
        wave.rightHapticSamples.fill(0);
        for (unsigned sequence = 1; sequence <= 4; ++sequence) {
            wave.audioSequence = sequence;
            bridge.handle(wave);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto stats = bridge.stats();
        assert(stats.pcmOutputLeftPeak > 0);
        DualSenseFeedback rumble{};
        rumble.enableBits1 = 0x01;
        rumble.rumbleRight = 200;
        bridge.handle(rumble);
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        assert(bridge.stop(error));
        unsigned maxRightRumble = 0;
        for (const auto& report : transport->writes) {
            if (report.size() != 33 || report[3] != apex6::kCmdRealtimeMotor) continue;
            for (unsigned i = 0; i < 8; ++i)
                maxRightRumble = (std::max)(maxRightRumble,
                    static_cast<unsigned>(std::abs(int(report[8 + i * 3]) - 128)));
        }
        return std::array<unsigned, 2>{static_cast<unsigned>(stats.pcmOutputLeftPeak), maxRightRumble};
    };
    const auto legacy = run(false);
    const auto independent = run(true);
    assert(independent[0] > legacy[0]);
    assert(legacy[1] > 0 && legacy[1] == independent[1]);
}

// Integration scenario: one running game, PCM measurement active, and multiple
// live UI-like global-strength toggles. Verify actual fake-HID report order,
// cumulative metering, acknowledgements and no device/session restart.
void testPcmLiveStrengthHotSwitchAndMeter() {
#ifdef _WIN32
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    using namespace asb::platform;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6PcmConfig config{};
    config.mode = Apex6PcmMode::Linear;
    config.linearGainPercent = 200;
    const auto token = std::to_string(GetCurrentProcessId()) + "_pcm_hot_switch";
    Apex6HapticBridge bridge(device, {}, true, 100, 50, config, token, "game:hot");

    const auto controlName = "Local\\ApexSenseBridge.Session." + token + ".Apex6PcmControl";
    HANDLE controlMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, controlName.c_str());
    HANDLE controlMutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (controlName + ".Mutex").c_str());
    assert(controlMap && controlMutex);
    auto* control = static_cast<Apex6PcmControlBlock*>(
        MapViewOfFile(controlMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Apex6PcmControlBlock)));
    assert(control);

    const auto liveName = "Local\\ApexSenseBridge.Session." + token + ".Apex6Pcm";
    HANDLE liveMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, liveName.c_str());
    HANDLE liveMutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (liveName + ".Mutex").c_str());
    assert(liveMap && liveMutex);
    auto* live = static_cast<Apex6PcmLiveBlock*>(
        MapViewOfFile(liveMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Apex6PcmLiveBlock)));
    assert(live);
    {
        Apex6PcmLock lock(liveMutex, 100);
        assert(lock && live->version == 3);
        live->measurementGeneration = 1;
        live->measurementActive = 1;
    }

    assert(bridge.start(error));
    unsigned sequence = 0;
    const auto sendWaveform = [&] {
        DualSenseFeedback feedback{};
        feedback.kind = FeedbackKind::AudioHapticWaveform;
        feedback.leftHapticSamples.fill(5000);
        feedback.rightHapticSamples.fill(0);
        for (int i = 0; i < 6; ++i) {
            feedback.audioSequence = ++sequence;
            bridge.handle(feedback);
            std::this_thread::sleep_for(std::chrono::milliseconds(11));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    };
    const auto updatePolicy = [&](unsigned request, bool bypass) {
        {
            Apex6PcmLock lock(controlMutex, 100);
            assert(lock);
            *control = Apex6PcmControlBlock{};
            std::memcpy(control->gameKey, "game:hot", 8);
            control->requestId = request;
            control->mode = 2;
            control->linearGainPercent = 200;
            control->ignoreVibrationStrength = bypass ? 1 : 0;
        }
        bool applied = false;
        for (int retry = 0; retry < 200; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            Apex6PcmLock lock(controlMutex, 100);
            assert(lock);
            if (control->ackId == request && control->status == 1) {
                applied = true; break;
            }
        }
        assert(applied && !bridge.failed());
    };

    // 200% Linear at global 50%: output 19; bypass: output 39;
    // re-enable global scaling: output 19 again, in the same session.
    sendWaveform();
    {
        Apex6PcmLock lock(liveMutex, 100);
        assert(lock && live->sessionGenerationEcho == 1);
        assert(live->measuredMotorOutputLeftPeak8 == 19);
    }
    updatePolicy(1, true);
    sendWaveform();
    {
        Apex6PcmLock lock(liveMutex, 100);
        assert(lock && live->measuredMotorOutputLeftPeak8 == 39);
        assert(live->measuredMaxPeak16 == 5000);
        assert(live->measuredActiveBlocks > 0);
    }
    updatePolicy(2, false);
    sendWaveform();
    assert(bridge.stats().framesWritten > 0);
    assert(!bridge.failed());
    assert(bridge.stop(error));

    // Validate wire-level signed PCM amplitude sequence, not only policy ACKs.
    bool firstReduced = false, boosted = false, reducedAgain = false;
    for (const auto& report : transport->writes) {
        if (report.size() != 33 || report[3] != apex6::kCmdRealtimeMotor) continue;
        const auto left = report[7];
        if (!firstReduced && left == 128 + 19) firstReduced = true;
        else if (firstReduced && !boosted && left == 128 + 39) boosted = true;
        else if (boosted && left == 128 + 19) reducedAgain = true;
    }
    assert(firstReduced && boosted && reducedAgain);
    UnmapViewOfFile(live); CloseHandle(liveMap); CloseHandle(liveMutex);
    UnmapViewOfFile(control); CloseHandle(controlMap); CloseHandle(controlMutex);
#endif
}

void testLivePcmPolicyAndConcurrentStats() {
#ifdef _WIN32
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    using namespace asb::platform;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    const auto token = std::to_string(GetCurrentProcessId()) + "_bridge_control";
    Apex6HapticBridge bridge(device, {}, true, 100, 100, Apex6PcmConfig{}, token, "game:123");
    const auto name = "Local\\ApexSenseBridge.Session." + token + ".Apex6PcmControl";
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    assert(mapping);
    HANDLE mutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + ".Mutex").c_str());
    assert(mutex);
    auto* view = static_cast<Apex6PcmControlBlock*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 312));
    assert(view);
    assert(bridge.start(error));
    std::atomic_bool reading{true};
    std::thread reader([&] {
        while (reading.load()) {
            const auto stats = bridge.stats();
            assert(stats.pcmEffectiveGainRatio >= 0 && stats.pcmEffectiveGainRatio <= 10);
            assert(stats.pcmMeasurementStatus == "valid" || stats.pcmMeasurementStatus == "limited");
            std::this_thread::yield();
        }
    });
    unsigned sequence = 0;
    for (unsigned revision = 1; revision <= 5; ++revision) {
        {
            Apex6PcmLock lock(mutex);
            *view = Apex6PcmControlBlock{};
            std::memcpy(view->gameKey, "game:123", 9);
            view->requestId = revision;
            view->mode = revision == 1 ? 0 : revision == 5 ? 1 : 2;
            view->compandingGainPercent = 200;
            view->linearGainPercent = revision == 2 ? 0 : revision == 4 ? 1000 : 200;
            view->referencePercent = 10;
        }
        bool acknowledged = false;
        for (unsigned retry = 0; retry < 100; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            Apex6PcmLock lock(mutex);
            if (view->ackId == revision) { acknowledged = true; break; }
        }
        assert(acknowledged);
        const auto before = bridge.stats().waveformBlocksRendered;
        DualSenseFeedback feedback{};
        feedback.kind = FeedbackKind::AudioHapticWaveform;
        feedback.audioSequence = ++sequence;
        feedback.leftHapticSamples.fill(5000);
        bridge.handle(feedback);
        for (unsigned retry = 0; retry < 100 && bridge.stats().waveformBlocksRendered == before; ++retry)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        // Stats publication precedes rendered-block completion; wait one tick.
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
        const auto stats = bridge.stats();
        const double expected = revision == 1 ? 1 : revision == 2 ? 0 : revision == 4 ? 10 : 2;
        assert(stats.pcmEffectiveGainRatio == expected);
        const auto companded = static_cast<unsigned>(scaleApex6Pcm(5000, 200));
        const auto expectedPeak = revision < 3 ? 19U : revision == 3 ? 39U : 127U;
        assert(stats.pcmOutputLeftPeak == expectedPeak);
    }
    reading.store(false); reader.join();
    assert(bridge.stop(error));
    bool foundScaled = false;
    bool foundCompanded = false;
    for (const auto& report : transport->writes) {
        if (report.size() == 33 && report[3] == asb::flydigi::apex6::kCmdRealtimeMotor) {
            if (report[7] == 255) foundScaled = true;
            if (report[7] == 128 + scaleApex6Pcm(5000, 200)) foundCompanded = true;
        }
    }
    assert(foundScaled && foundCompanded);
    UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(mutex);
#endif
}

int main() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;

    testLivePcmPolicyAndConcurrentStats();
    testPcmLiveStrengthHotSwitchAndMeter();
    testPerGamePcmStrengthBypass();
    testIdleSkipsFramesAndWakesImmediately();
    testNativeTriggerEffects();
    testPcmGainCurve();
    testApex6PcmNormalization();
    testApex6EnvelopeThresholdPolicy();
    testBowEffect();
    testContinuousCarrierClock();
    testTriggerDiagnostics();
    testSubQuantizationPcmDoesNotCountRumbleAsRenderedPcm();
    testStrength(100, 0.0);
    testStrength(50, 0.0);
    testStrength(0, 0.0);
    testStrength(100, 0.95);
    testStrength(100, 0.12, 1000); // Quiet native texture must survive the default threshold.
    testStrength(100, 0.95, 1000);
    testStrength(100, 0.12, 10028, 200);
    testStrength(50, 0.12, 1000, 150, true);
    testStrength(0, 0.12, 10028, 200);
    testStrength(100, 0.12, 32767, 200);

    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    assert(device.identity() && device.identity()->isApex6());

    Apex6HapticBridge bridge(device, {}, true);
    bridge.updateTriggerPositions(200, 0);

    DualSenseFeedback trigger{};
    trigger.enableBits1 = 0x08;
    trigger.leftTriggerEffect[0] = 0x21;
    trigger.leftTriggerEffect[1] = 0xFC; // active from zone 2 to the end of travel
    trigger.leftTriggerEffect[2] = 0x03;
    trigger.leftTriggerEffect[3] = 60;
    bridge.handle(trigger);

    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.audioSequence = 1;
    waveform.leftHapticSamples.fill(12000);
    waveform.rightHapticSamples.fill(-12000);
    assert(bridge.start(error));
    bridge.handle(waveform);
    bridge.handle(waveform); // duplicate sequence 1
    waveform.audioSequence = 0;
    bridge.handle(waveform); // older than sequence 1
    waveform.audioSequence = 3; // sequence 2 was lost upstream
    bridge.handle(waveform);
    waveform.audioSequence = 4;
    bridge.handle(waveform);
    waveform.audioSequence = 5; // bounded queue evicts sequence 1
    bridge.handle(waveform);

    std::this_thread::sleep_for(std::chrono::milliseconds(42));
    DualSenseFeedback rumble{};
    rumble.enableBits1 = 0x01;
    rumble.rumbleLeft = 120;
    bridge.handle(rumble);
    waveform.audioSequence = 6;
    waveform.leftHapticSamples.fill(0);
    waveform.rightHapticSamples.fill(0);
    bridge.handle(waveform);
    std::this_thread::sleep_for(std::chrono::milliseconds(24));
    trigger.leftTriggerEffect[0] = 0x05;
    bridge.handle(trigger);
    bridge.updateTriggerPositions(200, 200);
    DualSenseFeedback both{};
    both.enableBits1 = 0x0C;
    both.leftTriggerEffect[0] = both.rightTriggerEffect[0] = 0x26;
    both.leftTriggerEffect[1] = both.rightTriggerEffect[1] = 0xFC;
    both.leftTriggerEffect[2] = both.rightTriggerEffect[2] = 0x03;
    both.leftTriggerEffect[9] = both.rightTriggerEffect[9] = 90;
    bridge.handle(both);
    std::this_thread::sleep_for(std::chrono::milliseconds(24));
    both.enableBits1 = 0x04;
    both.rightTriggerEffect[9] = 140;
    bridge.handle(both);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(bridge.stop(error));
    assert(!bridge.failed());
    waveform.audioSequence = 7;
    waveform.leftHapticSamples.fill(12000);
    bridge.handle(waveform); // stale by the time the next enable delay completes
    const auto restartWriteStart = transport->writes.size();
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    assert(bridge.stop(error));
    for (std::size_t index = restartWriteStart; index < transport->writes.size(); ++index) {
        const auto& report = transport->writes[index];
        if (report[3] == apex6::kCmdRealtimeMotor) {
            assert((report[5] & 0x04U) == 0); // no previous trigger effect survives restart
        }
    }

    bool foundRealtime = false;
    bool foundWaveform = false;
    bool foundLeftTrigger = false;
    bool foundBipolarTrigger = false;
    for (const auto& report : transport->writes) {
        assert(report.size() == apex6::kReportSize);
        assert(report[0] == apex6::kReportId);
        assert(report[3] != kCmdSetRumble);
        assert(report[3] != kCmdSetForceTrigger);
        if (report[3] != apex6::kCmdRealtimeMotor) continue;
        foundRealtime = true;
        if (report[7] != 0x80 || report[8] != 0x80) foundWaveform = true;
        if ((report[5] & 0x07) == 0x04) {
            bool positive = false;
            bool negative = false;
            for (std::size_t sample = 0; sample < 8; ++sample) {
                const auto encoded = report[6 + sample * 3];
                positive = positive || encoded > 0x80;
                negative = negative || encoded < 0x80;
            }
            foundLeftTrigger = foundLeftTrigger || positive || negative;
            foundBipolarTrigger = foundBipolarTrigger || (positive && negative);
        }
    }
    assert(foundRealtime);
    assert(foundWaveform);
    assert(foundLeftTrigger);
    assert(foundBipolarTrigger);
    const auto stats = bridge.stats();
    assert(stats.framesWritten >= 2);
    assert(stats.waveformBlocks == 8);
    assert(stats.waveformBlocksRendered == 4);
    assert(stats.waveformBlocksDropped == 4);
    assert(stats.waveformQueueMaxDepth == 3);
    assert(stats.waveformSequenceGaps == 1);
    assert(stats.waveformDuplicates == 1);
    assert(stats.waveformOutOfOrder == 1);
    assert(stats.waveformOverflowDrops == 1);
    assert(stats.waveformStaleDrops == 1);
    assert(stats.hidReports == 5);
    assert(stats.triggerLeftUpdates == 3);
    assert(stats.triggerRightUpdates == 2);
    assert(stats.triggerActiveUpdates == 4);
    assert(stats.triggerStops == 1);
    assert(stats.lastLeftTriggerType == 0x26);
    assert(stats.lastRightTriggerType == 0x26);
    assert(stats.bothTriggerFrames > 0);
    assert(stats.rightTriggerFrames > 0);
    assert(stats.waveformLeftActiveBlocks == 7);
    assert(stats.waveformRightActiveBlocks == 6);
    assert(stats.waveformLeftPeak == 12000);
    assert(stats.waveformRightPeak == 12000);
    assert(stats.waveformActiveRendered == 3);
    assert(stats.gripRumbleFrames > 0); // silent PCM must not mask HID rumble
    assert(stats.hapticEnables == 2 && stats.hapticDisables == 2);
    assert(stats.writeFailures == 0);
    return 0;
}
