#pragma once
#include "audio/OutputRecorder.h"
#include "midi/PerformanceEvent.h"
#include "stems/StemPlayback.h"
#include "transport/TransportClock.h"
#include <atomic>
namespace jeff::daw {
class AudioEngine {
public:
  void prepare(double, int, int) noexcept;
  void setAuditionNote(int, float) noexcept;
  void stopAuditionNote() noexcept { audition_.store(0, std::memory_order_release); }
  void handlePerformanceEvent(const PerformanceEvent &, bool virtualPad) noexcept;
  void setPlaying(bool v) noexcept { clock.setPlaying(v); }
  void setMetronomeEnabled(bool v) noexcept { clock.setMetronomeEnabled(v); }
  void setTempo(double v) noexcept { clock.setTempo(v); }
  void seekBeats(double v) noexcept { clock.seekBeats(v); }
  void publishPlayback(std::unique_ptr<PlaybackSnapshot> p) noexcept {
    stems.publish(std::move(p));
  }
  StemPlayback &stemPlayback() noexcept { return stems; }
  OutputRecorder &recorder() noexcept { return recorder_; }
  double playheadBeats() const noexcept { return playhead_.load(); }
  int sampleRate() const noexcept { return sampleRate_.load(); }
  void processBlock(float *const *, int, int) noexcept;
  float peak() const noexcept { return outPeak.load(); }
  TransportClock &transportClock() noexcept { return clock; }

private:
  TransportClock clock;
  StemPlayback stems;
  OutputRecorder recorder_;
  std::atomic<int> sampleRate_{48000};
  std::atomic<std::uint64_t> audition_{};
  std::atomic<double> playhead_{};
  double rate{48000}, phase{};
  std::atomic<float> outPeak{};
};
} // namespace jeff::daw
