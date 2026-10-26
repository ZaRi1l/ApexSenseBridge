#include "dualsense/Apex6PcmNormalization.h"
#include "platform/Apex6PcmLiveMetrics.h"
#include "TestCheck.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
class Client {
public:
    explicit Client(const std::string& token) {
        const auto name = "Local\\ApexSenseBridge.Session." + token + ".Apex6Pcm";
        mutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + ".Mutex").c_str());
        mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        CHECK(mutex && mapping);
        view = static_cast<asb::platform::Apex6PcmLiveBlock*>(MapViewOfFile(mapping,
            FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*view)));
        CHECK(view);
    }
    ~Client() { UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(mutex); }
    void start(unsigned generation) {
        asb::platform::Apex6PcmLock lock(mutex); CHECK(lock);
        view->measurementGeneration = generation;
        view->measurementActive = 1;
    }
    asb::platform::Apex6PcmLiveBlock snapshot(bool stop = false) {
        asb::platform::Apex6PcmLock lock(mutex); CHECK(lock);
        if (stop) view->measurementActive = 0;
        return *view;
    }
private:
    HANDLE mutex{}, mapping{};
    asb::platform::Apex6PcmLiveBlock* view{};
};

// NOTE: testPcmLiveConcurrency is an IPC shared-memory stress test for
// Apex6PcmLiveServer::updateLive() ensuring non-blocking execution, race-free
// snapshot capture, and zero deadlocks across 10,000+ render updates.
// It is NOT a verification of the hardware Apex6HapticBridge::renderBlock()
// audio rendering or physical motor behavior.
void testPcmLiveConcurrency() {
    const auto token = std::to_string(GetCurrentProcessId()) + "-concurrency";
    asb::platform::Apex6PcmLiveServer server(token);
    Client client(token);
    std::atomic_bool running{true};
    std::atomic_uint64_t frames{0};
    std::thread render([&] {
        std::uint64_t count = 0;
        while (running.load() || count < 10000) {
            ++count;
            server.updateLive(1000, 500, 2000, 1000, 64, 32, 4, true,
                count / 50, count * 16, count * 2);
            frames.store(count);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (unsigned generation = 1; generation <= 500; ++generation) {
        client.start(generation);
        asb::platform::Apex6PcmLiveBlock current{};
        do {
            CHECK(std::chrono::steady_clock::now() < deadline);
            current = client.snapshot();
            std::this_thread::yield();
        } while (current.sessionGenerationEcho != generation || current.measuredActiveBlocks < 20);
        // Under non-blocking try-lock, processed samples count cumulative audio samples
        // while active blocks count updates where lock acquisition succeeded.
        CHECK(current.measuredProcessedSamples >= current.measuredActiveBlocks * 8);
        CHECK(current.measuredProcessedSamples >= current.measuredClippedSamples);
        CHECK(current.measuredMaxPeak16 == 1000);
        const auto stopped = client.snapshot(true);
        const auto frameAtStop = frames.load();
        while (frames.load() < frameAtStop + 20) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        const auto saved = client.snapshot();
        CHECK(saved.measuredActiveBlocks == stopped.measuredActiveBlocks);
        CHECK(saved.measuredProcessedSamples == stopped.measuredProcessedSamples);
        CHECK(saved.sessionGenerationEcho == generation);
    }
    running.store(false);
    render.join();
    CHECK(frames.load() >= 10000);
    std::printf("Render updates: %llu; start/stop cycles: 500\n",
        static_cast<unsigned long long>(frames.load()));
}

void testDeterministicMutexContention() {
    const auto token = std::to_string(GetCurrentProcessId()) + "-contention";
    asb::platform::Apex6PcmLiveServer server(token);
    Client client(token);

    // 1. Client starts generation 1
    client.start(1);
    server.updateLive(1000, 500, 2000, 1000, 64, 32, 4, true, 0, 16, 0);
    auto initial = client.snapshot();
    CHECK(initial.measuredMaxPeak16 == 1000 && initial.measuredProcessedSamples == 16);

    // 2. Client deliberately acquires and holds the mutex
    const auto name = "Local\\ApexSenseBridge.Session." + token + ".Apex6Pcm.Mutex";
    HANDLE mutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name.c_str());
    CHECK(mutex);
    CHECK(WaitForSingleObject(mutex, INFINITE) == WAIT_OBJECT_0);

    // 3. Audio render thread calls updateLive with a high peak (28000)
    // Server must return IMMEDIATELY via Try-Lock (0ms) without stalling or blocking!
    const auto startTick = std::chrono::steady_clock::now();
    server.updateLive(28000, 14000, 30000, 15000, 114, 57, 4, true, 1, 32, 2);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTick).count();
    CHECK(elapsedMs < 100); // Proves non-blocking execution!

    // 4. Release mutex so server can fold in the deferred peak
    ReleaseMutex(mutex);
    CloseHandle(mutex);

    // 5. Next update occurs uncontented with a lower peak (500)
    server.updateLive(500, 250, 500, 250, 10, 5, 4, true, 1, 48, 2);
    auto afterContention = client.snapshot(true);

    // Verify deferred peak 28000 was preserved and folded into the measurement!
    CHECK(afterContention.measuredMaxPeak16 == 28000);
    CHECK(afterContention.rawMeasuredHapticPeak16 == 30000);
    CHECK(afterContention.measuredProcessedSamples == 48);
    CHECK(afterContention.measuredMissedUpdates >= 1);

    // 6. Test generation boundary: start generation 2
    client.start(2);
    // Ensure deferred peaks from generation 1 NEVER leak into generation 2
    server.updateLive(300, 150, 400, 200, 8, 4, 4, true, 1, 64, 2);
    auto gen2 = client.snapshot(true);
    CHECK(gen2.measuredMaxPeak16 == 300); // Must be 300, NOT 28000!
    CHECK(gen2.rawMeasuredHapticPeak16 == 400);
}

void testIntervalReset() {
    const auto token = std::to_string(GetCurrentProcessId()) + "-reset";
    asb::platform::Apex6PcmLiveServer server(token);
    Client client(token);
    server.updateLive(0, 0, 0, 0, 0, 0, 4, true, 5, 160, 2);
    client.start(1);
    server.updateLive(9830, 1000, 20000, 1000, 114, 12, 4, true, 6, 176, 3);
    auto first = client.snapshot(true);
    CHECK(first.version == 2 && first.measuredProcessedSamples == 16);
    CHECK(first.measuredClippedSamples == 1 && first.measuredDroppedBlocks == 1);
    server.updateLive(32767, 0, 32767, 0, 127, 0, 4, true, 7, 184, 4);
    auto stopped = client.snapshot();
    CHECK(stopped.measuredMaxPeak16 == 9830 && stopped.rawMeasuredHapticPeak16 == 20000);
    client.start(2);
    // No render between start/stop: old generation must not be saved as a new result.
    CHECK(client.snapshot(true).sessionGenerationEcho != 2);
    client.start(3);
    server.updateLive(1000, 0, 1000, 0, 12, 0, 4, true, 7, 192, 4);
    auto next = client.snapshot(true);
    CHECK(next.measuredMaxPeak16 == 1000 && next.rawMeasuredHapticPeak16 == 1000);
    CHECK(next.measuredProcessedSamples == 8 && next.measuredDroppedBlocks == 0);
}
#endif

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--check-failure") == 0) CHECK(false);
    using namespace asb::dualsense;
    Apex6PcmConfig config{};
    config.mode = Apex6PcmMode::Normalize;
    config.referencePeakAbs16 = 10028;
    Apex6PcmScaler precise(config);
    CHECK(std::abs(precise.state().effectiveGainRatio - 32767.0 * .9 / 10028) < 1e-10);
    bool clipped = false;
    CHECK(precise.scaleSample(10028, clipped) == 114 && !clipped);
    config.mode = Apex6PcmMode::Companding;
    config.compandingGainPercent = 175;
    Apex6PcmScaler curve(config);
    for (int sample = -32768; sample <= 32767; ++sample)
        CHECK(curve.scaleSample(static_cast<std::int16_t>(sample), clipped) ==
            scaleApex6Pcm(static_cast<std::int16_t>(sample), 175));
#ifdef _WIN32
    testIntervalReset();
    testDeterministicMutexContention();
    testPcmLiveConcurrency();
#endif
    return 0;
}
