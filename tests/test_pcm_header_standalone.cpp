#include "dualsense/Apex6PcmNormalization.h"
#include "platform/Apex6PcmLiveMetrics.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

void testPcmLiveConcurrency() {
#ifdef _WIN32
    const std::string token = "aabbccddeeff00112233445566778899";
    asb::platform::Apex6PcmLiveServer server(token);
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
        ("Local\\ApexSenseBridge.Session." + token + ".Apex6Pcm").c_str());
    assert(mapping);
    auto* view = static_cast<asb::platform::Apex6PcmLiveBlock*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(asb::platform::Apex6PcmLiveBlock)));
    assert(view && view->magic == 0x50365041);

    std::atomic_bool running{true};
    std::atomic_uint64_t audioFrames{0};

    // Thread 1: Audio Render Thread (updating 1kHz blocks)
    std::thread audioThread([&] {
        std::uint64_t drops = 0;
        std::uint64_t processed = 0;
        std::uint64_t clips = 0;
        std::uint16_t amp = 1000;
        while (running.load(std::memory_order_relaxed)) {
            processed += 16;
            if (amp > 30000) clips += 2;
            if ((processed % 800) == 0) drops++;
            amp = (amp + 7919) % 32768;
            server.updateLive(amp, amp / 2, amp, amp / 2, 64, 32, 4, true, drops, processed, clips);
            audioFrames.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Thread 2: UI Client Thread (frequent measurement starts, reads, stops)
    std::thread uiThread([&] {
        for (std::uint32_t gen = 1; gen <= 500; ++gen) {
            view->measurementGeneration = gen;
            view->measurementActive = 1;

            // Wait a few cycles
            for (int poll = 0; poll < 10; ++poll) {
                const auto maxPeak = view->measuredMaxPeak16;
                const auto proc = view->measuredProcessedSamples;
                const auto clp = view->measuredClippedSamples;
                const auto drp = view->measuredDroppedBlocks;
                assert(maxPeak <= 32768);
                assert(proc >= clp);
                assert(drp < 1000000); // verify no unsigned underflow
                std::this_thread::yield();
            }

            view->measurementActive = 0;
            std::this_thread::yield();
        }
    });

    uiThread.join();
    running.store(false, std::memory_order_relaxed);
    audioThread.join();

    UnmapViewOfFile(view);
    CloseHandle(mapping);
#endif
}

int main() {
    using namespace asb::dualsense;
    Apex6PcmConfig config{};
    config.mode = Apex6PcmMode::Normalize;
    config.referencePeakAbs16 = 10028;
    config.targetPercent = 90;
    Apex6PcmScaler precise(config);
    assert(std::abs(precise.state().effectiveGainRatio - 32767.0 * .9 / 10028) < 1e-10);
    bool clipped = false;
    assert(precise.scaleSample(10028, clipped) == 114 && !clipped);
    config.mode = Apex6PcmMode::Companding;
    config.compandingGainPercent = 175;
    Apex6PcmScaler curve(config);
    for (int sample = -32768; sample <= 32767; ++sample) {
        assert(curve.scaleSample(static_cast<std::int16_t>(sample), clipped) ==
            scaleApex6Pcm(static_cast<std::int16_t>(sample), 175));
    }
#ifdef _WIN32
    const std::string token = "00112233445566778899aabbccddeeff";
    asb::platform::Apex6PcmLiveServer server(token);
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
        ("Local\\ApexSenseBridge.Session." + token + ".Apex6Pcm").c_str());
    assert(mapping);
    auto* view = static_cast<asb::platform::Apex6PcmLiveBlock*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(asb::platform::Apex6PcmLiveBlock)));
    assert(view && view->magic == 0x50365041);
    server.updateLive(0, 0, 0, 0, 0, 0, 4, true, 5, 160, 2);
    view->measurementGeneration = 1;
    view->measurementActive = 1;
    server.updateLive(9830, 1000, 9830, 1000, 114, 12, 4, true, 6, 176, 3);
    assert(view->measuredDroppedBlocks == 1);
    assert(view->measuredProcessedSamples == 16 && view->measuredClippedSamples == 1);
    assert(view->rawMeasuredHapticPeak16 == 9830);
    server.updateLive(1000, 1000, 1000, 1000, 12, 12, 4, true, 6, 192, 3);
    assert(view->measuredDroppedBlocks == 1);
    view->measurementActive = 0;
    server.updateLive(32767, 0, 32767, 0, 127, 0, 4, true, 7, 200, 4);
    assert(view->measuredMaxPeak16 == 9830);
    view->measurementGeneration = 2;
    view->measurementActive = 1;
    server.updateLive(1000, 0, 1000, 0, 12, 0, 4, true, 7, 208, 4);
    assert(view->measuredMaxPeak16 == 1000 && view->measuredProcessedSamples == 8);
    assert(view->measuredDroppedBlocks == 0);
    UnmapViewOfFile(view);
    CloseHandle(mapping);

    testPcmLiveConcurrency();
#endif
    return 0;
}
