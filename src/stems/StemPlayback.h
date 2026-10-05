#pragma once
#include "stems/PlaybackSnapshot.h"
#include "transport/TransportClock.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
namespace jeff::daw {
class StemPlayback {
public:
  static constexpr int blockFrames=4096;
  static constexpr int ringSlots=8;
  StemPlayback();
  // Caller must stop/quiesce the device callback before destruction. The worker joins here,
  // then releases its active snapshot and all prepared cache handles on the worker thread.
  ~StemPlayback();
  StemPlayback(const StemPlayback&)=delete;
  StemPlayback& operator=(const StemPlayback&)=delete;
  // Ownership transfers to the background worker. Replaced pending snapshots are reclaimed here.
  void publish(std::unique_ptr<PlaybackSnapshot>) noexcept;
  // Adds stereo stem playback to existing output. Callback is allocation-, I/O-, and lock-free.
  void render(float* const*,int,int,const TransportBlock&) noexcept;
  std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }
  std::uint64_t snapshotGeneration() const noexcept { return activeGeneration_.load(std::memory_order_acquire); }
  std::string workerStatus() const;
private:
  struct Slot {
    std::atomic<unsigned char> ready{0};
    std::uint64_t generation{},seekGeneration{},rateEpoch{},tempoEpoch{};
    double startBeat{},tempo{},rate{};
    std::array<float,blockFrames> left{},right{};
  };
  struct Mailbox {
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<double> beat{0},tempo{120},rate{48000};
    std::atomic<std::uint64_t> seekGeneration{0},rateEpoch{0},tempoEpoch{0};
    std::atomic<bool> playing{false};
  };
  void workerLoop() noexcept;
  bool readMailbox(double&,double&,double&,std::uint64_t&,std::uint64_t&,std::uint64_t&,bool&) const noexcept;
  void setStatus(std::string) noexcept;
  std::array<Slot,ringSlots> slots_;
  std::atomic<PlaybackSnapshot*> pending_{};
  std::atomic<bool> stopping_{};
  std::atomic<std::uint64_t> activeGeneration_{},underruns_{};
  Mailbox mailbox_;
  std::thread worker_;
  mutable std::mutex statusMutex_;
  std::string status_;
  std::size_t writeSlot_{},readSlot_{};
  int readOffset_{};
  std::uint64_t seenGeneration_{};
};
}
