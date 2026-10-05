#include "stems/StemPlayback.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
namespace jeff::daw {
namespace {
// Bounds the worker's planar source window to about 16 MiB at 64 channels.
constexpr double maxPreparedFramesPerDeviceFrame=16.0;
constexpr int maxSourceWindowFrames=65540;
}
StemPlayback::StemPlayback():worker_([this]{workerLoop();}){}
StemPlayback::~StemPlayback(){
  stopping_.store(true,std::memory_order_release);
  if(worker_.joinable()) worker_.join();
  delete pending_.exchange(nullptr,std::memory_order_acq_rel);
}
void StemPlayback::publish(std::unique_ptr<PlaybackSnapshot> s) noexcept {
  auto* incoming=s.release();
  auto* replaced=pending_.exchange(incoming,std::memory_order_acq_rel);
  delete replaced;
}
std::string StemPlayback::workerStatus() const {
  std::lock_guard lock(statusMutex_); return status_;
}
void StemPlayback::setStatus(std::string s) noexcept {
  try { std::lock_guard lock(statusMutex_); status_=std::move(s); } catch(...) {}
}
bool StemPlayback::readMailbox(double& beat,double& tempo,double& rate,std::uint64_t& seek,
  std::uint64_t& rateEpoch,std::uint64_t& tempoEpoch,bool& playing) const noexcept {
  for(int tries=0;tries<4;++tries) {
    const auto a=mailbox_.sequence.load(std::memory_order_acquire);
    if(a&1) continue;
    beat=mailbox_.beat.load(std::memory_order_relaxed); tempo=mailbox_.tempo.load(std::memory_order_relaxed);
    rate=mailbox_.rate.load(std::memory_order_relaxed); seek=mailbox_.seekGeneration.load(std::memory_order_relaxed);
    rateEpoch=mailbox_.rateEpoch.load(std::memory_order_relaxed); tempoEpoch=mailbox_.tempoEpoch.load(std::memory_order_relaxed);
    playing=mailbox_.playing.load(std::memory_order_relaxed);
    if(a==mailbox_.sequence.load(std::memory_order_acquire)) return true;
  }
  return false;
}
void StemPlayback::render(float* const* out,int channels,int frames,const TransportBlock& b) noexcept {
  auto seq=mailbox_.sequence.fetch_add(1,std::memory_order_acq_rel)+1;
  (void)seq;
  mailbox_.beat.store(b.endBeat,std::memory_order_relaxed); mailbox_.tempo.store(b.tempoBpm,std::memory_order_relaxed);
  mailbox_.rate.store(b.sampleRate,std::memory_order_relaxed); mailbox_.seekGeneration.store(b.seekGeneration,std::memory_order_relaxed);
  mailbox_.rateEpoch.store(b.rateEpoch,std::memory_order_relaxed); mailbox_.tempoEpoch.store(b.tempoEpoch,std::memory_order_relaxed);
  mailbox_.playing.store(b.isPlaying,std::memory_order_relaxed);
  mailbox_.sequence.fetch_add(1,std::memory_order_release);
  if(!out || channels<1 || frames<1) return;
  double expected=b.startBeat;
  const double step=(b.tempoBpm>0&&b.sampleRate>0)?b.tempoBpm/(60.0*b.sampleRate):0;
  auto discard=[&]() noexcept { slots_[readSlot_].ready.store(0,std::memory_order_release);readSlot_=(readSlot_+1)%ringSlots;readOffset_=0; };
  bool missed=false;
  for(int i=0;i<frames;++i) {
    if(!b.isPlaying || step<=0) { expected+=step; continue; }
    unsigned attempts=0;
    while(attempts++<ringSlots) {
      auto& s=slots_[readSlot_];
      if(!s.ready.load(std::memory_order_acquire)) break;
      const double slotBeat=s.startBeat+readOffset_*s.tempo/(60.0*s.rate);
      const bool valid=s.generation==activeGeneration_.load(std::memory_order_acquire) &&
        s.seekGeneration==b.seekGeneration && s.rateEpoch==b.rateEpoch && s.tempoEpoch==b.tempoEpoch;
      if(!valid || std::abs(slotBeat-expected)>std::max(1.0e-7,step*.55)) { discard(); continue; }
      const float l=s.left[readOffset_],r=s.right[readOffset_];
      for(int c=0;c<channels;++c) if(out[c]) {
        const float v=channels==1?(l+r)*0.5f:((c&1)?r:l);
        out[c][i]+=v;
      }
      if(++readOffset_==blockFrames) discard();
      goto rendered;
    }
    missed=true;
    rendered: expected+=step;
  }
  if(missed) underruns_.fetch_add(1,std::memory_order_relaxed);
}
void StemPlayback::workerLoop() noexcept {
  std::unique_ptr<PlaybackSnapshot> active;
  std::uint64_t generation=0,seenSeek=~std::uint64_t{},seenRateEpoch=~std::uint64_t{},seenTempoEpoch=~std::uint64_t{};
  double cursor=0,tempo=120,rate=48000;
  bool playing=false,wasPlaying=false;
  while(!stopping_.load(std::memory_order_acquire)) {
    if(auto* p=pending_.exchange(nullptr,std::memory_order_acq_rel)) {
      active.reset(p); ++generation; activeGeneration_.store(generation,std::memory_order_release);
      setStatus(active?"Prepared playback ready.":"No prepared playback.");
      seenSeek=~std::uint64_t{};
    }
    double requestedBeat,requestedTempo,requestedRate;std::uint64_t seek,re,te;bool run;
    if(!readMailbox(requestedBeat,requestedTempo,requestedRate,seek,re,te,run)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;
    }
    if(seek!=seenSeek || re!=seenRateEpoch || te!=seenTempoEpoch) {
      cursor=requestedBeat;tempo=requestedTempo;rate=requestedRate;
      seenSeek=seek;seenRateEpoch=re;seenTempoEpoch=te;
    } else {
      if(run&&!wasPlaying) cursor=requestedBeat;
      tempo=requestedTempo;rate=requestedRate;
    }
    playing=run;
    wasPlaying=run;
    if(!active || !playing || tempo<=0 || rate<=0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;
    }
    auto& slot=slots_[writeSlot_];
    if(slot.ready.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;
    }
    slot.left.fill(0);slot.right.fill(0);
    try {
      const auto& tracks=active->tracks();
      const bool anySolo=std::any_of(tracks.begin(),tracks.end(),[](const auto& t){return t.solo;});
      CancellationToken token;
      for(const auto& t:tracks) {
        if(t.mute || (anySolo&&!t.solo)) continue;
        const auto& a=t.audio;
        if(a.frameCount<=0 || a.sampleRate<=0) continue;
        const double origin=t.placementBeats+a.originBeats;
        const double sourceStep=(tempo/active->tempoBpm())*a.sampleRate/rate;
        const double startPos=(cursor-origin)*60.0/active->tempoBpm()*a.sampleRate;
        if(!std::isfinite(sourceStep)||sourceStep<=0||sourceStep>maxPreparedFramesPerDeviceFrame) {
          setStatus("Prepared playback rate ratio exceeds the bounded 16:1 source window.");
          continue;
        }
        const double endPos=startPos+(blockFrames-1)*sourceStep;
        if(!std::isfinite(startPos)||!std::isfinite(endPos)||endPos<0||startPos>=a.frameCount) continue;
        const auto first=std::max<Frame>(0,static_cast<Frame>(std::floor(startPos)));
        const auto last=std::min<Frame>(a.frameCount-1,static_cast<Frame>(std::floor(endPos))+1);
        if(last<first) continue;
        const int count=static_cast<int>(last-first+1);
        if(count>maxSourceWindowFrames) {
          setStatus("Prepared playback source window exceeds its bounded read size.");
          continue;
        }
        std::vector<std::vector<float>> planes(static_cast<std::size_t>(a.channels),std::vector<float>(static_cast<std::size_t>(count)));
        std::vector<float*> ptrs;ptrs.reserve(a.channels);
        for(auto& p:planes)ptrs.push_back(p.data());
        auto result=a.read(first,count,std::span<float* const>(ptrs.data(),ptrs.size()),token);
        if(!result) {setStatus("Prepared playback read failed: "+result.error().message);continue;}
        std::array<const float*,64> samplePtrs{};
        for(int i=0;i<blockFrames;++i) {
          const double position=startPos+i*sourceStep;
          if(position<0 || position>=a.frameCount) continue;
          const auto low=std::clamp(static_cast<int>(std::floor(position))-static_cast<int>(first),0,count-1);
          const auto high=std::min(low+1,count-1);const float f=static_cast<float>(position-std::floor(position));
          std::array<float,64> values{};
          for(int c=0;c<t.channels;++c) {
            const auto cacheChannel=t.firstChannel+c;
            values[c]=planes[cacheChannel][low]+(planes[cacheChannel][high]-planes[cacheChannel][low])*f;
            samplePtrs[c]=&values[c];
          }
          PlaybackSnapshot::mixTrack(samplePtrs.data(),t.channels,t.gain,t.pan,slot.left[i],slot.right[i]);
        }
      }
    } catch(const std::exception& e) { setStatus(std::string("Playback preparation failed: ")+e.what()); }
      catch(...) { setStatus("Playback preparation failed."); }
    slot.generation=generation;slot.seekGeneration=seek;slot.rateEpoch=re;slot.tempoEpoch=te;
    slot.startBeat=cursor;slot.tempo=tempo;slot.rate=rate;
    slot.ready.store(1,std::memory_order_release);
    cursor+=blockFrames*tempo/(60.0*rate);
    writeSlot_=(writeSlot_+1)%ringSlots;
  }
  active.reset();
}
}
