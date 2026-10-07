#include "transport/TransportClock.h"
#include <algorithm>
#include <cmath>
namespace jeff::daw {
void TransportClock::prepare(double r) noexcept {
  rate_ = r > 0 ? r : 44100;
  ++rateEpoch_;
  nextClickBeat_ = std::ceil(positionBeats_ - 1.0e-12);
}
void TransportClock::setTempo(double b) noexcept {
  requestedBpm_.store(std::clamp(b,20.0,300.0),std::memory_order_release);
}
void TransportClock::seekBeats(double beats) noexcept {
  if(!std::isfinite(beats)) return;
  requestedSeekBeats_.store(std::max(0.0,beats),std::memory_order_relaxed);
  seekRequest_.fetch_add(1,std::memory_order_release);
}
void TransportClock::reset() noexcept {
  seekBeats(0);
  nextClickBeat_=0;
}
std::int64_t TransportClock::positionSamples() const noexcept {
  return static_cast<std::int64_t>(positionBeats_*60.0/bpm_*rate_);
}
TransportBlock TransportClock::process(int n) noexcept {
  const auto seek=seekRequest_.load(std::memory_order_acquire);
  if(seek!=appliedSeekRequest_) {
    positionBeats_=requestedSeekBeats_.load(std::memory_order_relaxed);
    appliedSeekRequest_=seek;
    ++seekGeneration_;
    nextClickBeat_=std::ceil(positionBeats_-1.0e-12);
  }
  const auto newTempo=requestedBpm_.load(std::memory_order_acquire);
  if(newTempo!=bpm_) { bpm_=newTempo; ++tempoEpoch_; }
  playing_=requestedPlaying_.load(std::memory_order_acquire);
  metro_=requestedMetro_.load(std::memory_order_acquire);
  TransportBlock b;
  b.startBeat=positionBeats_; b.endBeat=positionBeats_; b.tempoBpm=bpm_;
  b.isPlaying=playing_; b.seekGeneration=seekGeneration_; b.rateEpoch=rateEpoch_;
  b.tempoEpoch=tempoEpoch_; b.sampleRate=rate_;
  b.startSample=positionSamples(); b.endSample=b.startSample;
  if(!playing_ || n<=0) return b;
  const double beatStep=bpm_/(60.0*rate_);
  const double end=positionBeats_+n*beatStep;
  if(metro_) {
    while(nextClickBeat_<end) {
      const double offset=(nextClickBeat_-positionBeats_)/beatStep;
      if(offset>=-1.0e-9 && b.clickCount<b.clickOffsets.size())
        b.clickOffsets[b.clickCount++]=std::max(0,static_cast<int>(std::llround(offset)));
      nextClickBeat_+=1.0;
    }
  }
  positionBeats_=end;
  b.endBeat=end;
  b.endSample=positionSamples();
  return b;
}
}
