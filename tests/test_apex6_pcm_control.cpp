#ifdef NDEBUG
#undef NDEBUG
#endif
#include "platform/Apex6PcmControl.h"
#include "cli/BridgeOptions.h"
#include <cassert>
#include <cstring>
#include <thread>
#include <atomic>
#include <iostream>

using namespace asb::platform;
using namespace asb::dualsense;

int main() {
    static_assert(sizeof(Apex6PcmControlBlock) == 308);
    asb::cli::BridgeCommandOptions options;
    std::string error;
    char a0[] = "asb", a1[] = "bridge", a2[] = "--apex6-pcm-game-key", a3[] = "game:123";
    char* args[]{a0, a1, a2, a3};
    assert(asb::cli::parseBridgeOptions(4, args, options, error));
    assert(options.apex6PcmGameKey == "game:123");
    char hexOption[] = "--apex6-pcm-game-key-hex";
    char hexValue[] = "67616d653ae6b8b8";
    char* hexArgs[]{a0, a1, hexOption, hexValue};
    asb::cli::BridgeCommandOptions hexOptions;
    assert(asb::cli::parseBridgeOptions(4, hexArgs, hexOptions, error));
    assert(hexOptions.apex6PcmGameKey == "game:\xE6\xB8\xB8");
    for (std::string invalid : {std::string("0"), std::string("zz"), std::string("00"), std::string(512, 'a')}) {
        hexArgs[3] = invalid.data();
        asb::cli::BridgeCommandOptions invalidOptions;
        assert(!asb::cli::parseBridgeOptions(4, hexArgs, invalidOptions, error));
    }
    char* duplicateArgs[]{a0, a1, a2, a3, hexOption, hexValue};
    asb::cli::BridgeCommandOptions duplicateOptions;
    assert(!asb::cli::parseBridgeOptions(6, duplicateArgs, duplicateOptions, error));
    std::string tooLong(256, 'a');
    args[3] = tooLong.data();
    asb::cli::BridgeCommandOptions tooLongOptions;
    assert(!asb::cli::parseBridgeOptions(4, args, tooLongOptions, error));
    char empty[] = ""; args[3] = empty;
    asb::cli::BridgeCommandOptions emptyOptions;
    assert(!asb::cli::parseBridgeOptions(4, args, emptyOptions, error));
#ifdef _WIN32
    char invalidUtf8[]{static_cast<char>(0xFF), 0}; args[3] = invalidUtf8;
    asb::cli::BridgeCommandOptions utf8Options;
    assert(!asb::cli::parseBridgeOptions(4, args, utf8Options, error));
    char invalidHexUtf8[] = "ff"; hexArgs[3] = invalidHexUtf8;
    asb::cli::BridgeCommandOptions hexUtf8Options;
    assert(!asb::cli::parseBridgeOptions(4, hexArgs, hexUtf8Options, error));
#endif
#ifdef _WIN32
    const auto token = std::to_string(GetCurrentProcessId()) + "_control_test";
    const auto name = "Local\\ApexSenseBridge.Session." + token + ".Apex6PcmControl";
    HANDLE mapping = nullptr, mutex = nullptr;
    Apex6PcmControlBlock* view = nullptr;
    {
        Apex6PcmControlServer server(token, "game:123");
        mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        mutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + ".Mutex").c_str());
        assert(mapping && mutex);
        view = static_cast<Apex6PcmControlBlock*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 308));
        assert(view);
        Apex6PcmConfig applied;
        unsigned applications = 0;
        auto apply = [&](const Apex6PcmConfig& config) noexcept {
            applied = config; ++applications; return true;
        };
        auto submit = [&](std::uint32_t id) {
            Apex6PcmLock lock(mutex);
            *view = Apex6PcmControlBlock{};
            std::memcpy(view->gameKey, "game:123", 9);
            view->requestId = id;
            view->mode = 2;
            view->linearGainPercent = 200;
        };
        submit(1);
        assert(view->ackId == 0 && applications == 0);
        server.applyPending([](const auto&) noexcept { return false; });
        assert(view->ackId == 0);
        server.applyPending(apply);
        assert(view->ackId == 1 && view->status == 1);
        assert(applied.linearGainPercent == 200 && applications == 1);
        server.applyPending(apply);
        assert(applications == 1);
        submit(2); view->version = 99;
        server.applyPending(apply);
        assert(view->status == 2 && view->ackId == 0 && applications == 1);
        submit(3); view->gameKey[0] = 'X';
        server.applyPending(apply);
        assert(view->status == 3 && applications == 1);
        submit(4); view->linearGainPercent = 1001;
        server.applyPending(apply);
        assert(view->status == 4 && applications == 1);
        submit(5); view->mode = 3;
        server.applyPending(apply);
        assert(view->status == 4 && applications == 1);
        submit(6);
        std::atomic_bool held{false}, release{false};
        std::thread holder([&] {
            Apex6PcmLock lock(mutex); assert(lock);
            held.store(true);
            while (!release.load()) std::this_thread::yield();
        });
        while (!held.load()) std::this_thread::yield();
        const auto start = std::chrono::steady_clock::now();
        server.applyPending(apply);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        release.store(true); holder.join();
        assert(elapsed < std::chrono::milliseconds(50));
        assert(applications == 1 && view->ackId == 0);
        server.applyPending(apply);
        assert(view->ackId == 6 && applications == 2);
        submit(2); server.applyPending(apply);
        assert(view->status == 5 && applications == 2);
        // A second engine cannot reset an existing session's endpoint.
        Apex6PcmControlServer duplicate(token, "game:123");
        submit(7); duplicate.applyPending(apply);
        assert(applications == 2);
        server.applyPending(apply);
        assert(view->ackId == 7 && applications == 3);
    }
    assert(view->status == 6);
    // A retained client handle must not allow a new engine to revive the old session.
    Apex6PcmControlServer stale(token, "game:123");
    assert(view->status == 6);
    UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(mutex);
#endif
    std::cout << "APEX6 PCM control tests passed\n";
}
