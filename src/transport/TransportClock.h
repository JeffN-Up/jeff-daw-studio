#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace jeff::daw {
struct TransportBlock {
  std::int64_t startSample{}, endSample{};
  bool isPlaying{};
  std::array<int,16> clickOffsets{};
  std::uint8_t clickCount{};
  double startBeat{}, endBeat{}, tempoBpm{};
  std::uint64_t seekGeneration{}, rateEpoch{}, tempoEpoch{};
  double sampleRate{};
};
class TransportClock {
public:
  void prepare(double) noexcept;
  void setTempo(double) noexcept;
  double tempo() const noexcept { return requestedBpm_.load(std::memory_order_acquire); }
  void setPlaying(bool p) noexcept { requestedPlaying_.store(p, std::memory_order_release); }
  void setMetronomeEnabled(bool e) noexcept { requestedMetro_.store(e, std::memory_order_release); }
  // The request is applied at the beginning of the next audio block, including while paused.
  void seekBeats(double) noexcept;
  TransportBlock process(int) noexcept;
  void reset() noexcept;
  std::int64_t positionSamples() const noexcept;
  double positionBeats() const noexcept { return positionBeats_; }
private:
  double rate_{44100}, bpm_{120}, positionBeats_{}, nextClickBeat_{};
  std::atomic<double> requestedBpm_{120}, requestedSeekBeats_{0};
  std::atomic<std::uint64_t> seekRequest_{0};
  std::atomic<bool> requestedPlaying_{false}, requestedMetro_{false};
  std::uint64_t appliedSeekRequest_{}, seekGeneration_{}, rateEpoch_{}, tempoEpoch_{};
  bool playing_{}, metro_{};
};
}
