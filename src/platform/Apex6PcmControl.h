#pragma once
#include "dualsense/Apex6PcmNormalization.h"
#include "platform/Apex6PcmLiveMetrics.h"
#include <cstring>
namespace asb::platform {
#pragma pack(push, 1)
struct Apex6PcmControlBlock {
    std::uint32_t magic = 0x43365041;
    std::uint32_t version = 2;
    std::uint32_t requestId = 0;
    std::uint32_t ackId = 0;
    std::uint32_t status = 0;
    char gameKey[256]{};
    std::uint32_t mode = 0;
    std::uint32_t linearGainPercent = 100;
    std::uint32_t referencePercent = 0;
    std::uint32_t referencePeakAbs16 = 0;
    std::uint32_t targetPercent = 90;
    std::uint32_t maxGainPercent = 400;
    std::uint32_t legacyGainPercent = 100;
    std::uint32_t compandingGainPercent = 100;
    std::uint32_t ignoreVibrationStrength = 0; // 0: apply, 1: bypass for PCM only
};
#pragma pack(pop)
static_assert(sizeof(Apex6PcmControlBlock) == 312);
class Apex6PcmControlServer {
public:
    Apex6PcmControlServer(std::string_view token, std::string_view gameKey) {
#ifdef _WIN32
        if (token.empty() || !validGameKey(gameKey)) return;
        std::memcpy(gameKey_, gameKey.data(), gameKey.size());
        const auto name = "Local\\ApexSenseBridge.Session." + std::string(token) + ".Apex6PcmControl";
        mutex_ = CreateMutexA(nullptr, FALSE, (name + ".Mutex").c_str());
        if (!mutex_) return;
        mapping_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, sizeof(Apex6PcmControlBlock), name.c_str());
        const bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
        if (!mapping_ || existing) return; // Never revive a retained stale client mapping.
        view_ = static_cast<Apex6PcmControlBlock*>(MapViewOfFile(
            mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Apex6PcmControlBlock)));
        Apex6PcmLock lock(mutex_, 100);
        if (!lock || !view_) { if (view_) UnmapViewOfFile(view_); view_ = nullptr; return; }
        *view_ = Apex6PcmControlBlock{};
        std::memcpy(view_->gameKey, gameKey_, sizeof(gameKey_));
#else
        (void)token; (void)gameKey;
#endif
    }
    ~Apex6PcmControlServer() {
#ifdef _WIN32
        if (view_) {
            { Apex6PcmLock lock(mutex_, 100); if (lock) view_->status = 6; }
            UnmapViewOfFile(view_);
        }
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
#endif
    }
    Apex6PcmControlServer(const Apex6PcmControlServer&) = delete;
    Apex6PcmControlServer& operator=(const Apex6PcmControlServer&) = delete;

    static bool validGameKey(std::string_view key) noexcept {
        if (key.empty() || key.size() >= 256 || key.find('\0') != std::string_view::npos) return false;
#ifdef _WIN32
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key.data(),
                                   static_cast<int>(key.size()), nullptr, 0) > 0;
#else
        return true;
#endif
    }

    // Invoked once at a render boundary. Apply must be non-blocking and allocation-free.
    // A false result means retry later (e.g. statistics reader owns the snapshot lock).
    template<class Apply> void applyPending(Apply&& apply) noexcept {
#ifdef _WIN32
        if (!view_) return;
        Apex6PcmLock lock(mutex_, 0);
        if (!lock || view_->requestId == 0) return;
        if (view_->magic != 0x43365041 || view_->version != 2) { view_->status = 2; return; }
        if (std::memcmp(view_->gameKey, gameKey_, sizeof(gameKey_)) != 0) { view_->status = 3; return; }
        if (view_->requestId == appliedId_) { view_->ackId = appliedId_; view_->status = 1; return; }
        if (view_->requestId < appliedId_) { view_->status = 5; return; }
        if (view_->mode > 2 || view_->linearGainPercent > 1000 ||
            view_->referencePercent > 100 || view_->referencePeakAbs16 > 32768 ||
            view_->targetPercent < 1 || view_->targetPercent > 100 ||
            view_->maxGainPercent < 100 || view_->maxGainPercent > 400 ||
            view_->legacyGainPercent < 100 || view_->legacyGainPercent > 200 ||
            view_->compandingGainPercent < 100 || view_->compandingGainPercent > 200 ||
            view_->ignoreVibrationStrength > 1) {
            view_->status = 4; return;
        }
        const dualsense::Apex6PcmConfig config{
            static_cast<dualsense::Apex6PcmMode>(view_->mode), view_->linearGainPercent,
            view_->referencePercent, view_->referencePeakAbs16, view_->targetPercent,
            view_->maxGainPercent, view_->legacyGainPercent, view_->compandingGainPercent,
            view_->ignoreVibrationStrength != 0};
        if (!apply(config)) return;
        appliedId_ = view_->requestId;
        view_->ackId = appliedId_;
        view_->status = 1;
#else
        (void)apply;
#endif
    }
private:
#ifdef _WIN32
    char gameKey_[256]{};
    HANDLE mutex_ = nullptr;
    HANDLE mapping_ = nullptr;
    Apex6PcmControlBlock* view_ = nullptr;
    std::uint32_t appliedId_ = 0;
#endif
};
}
