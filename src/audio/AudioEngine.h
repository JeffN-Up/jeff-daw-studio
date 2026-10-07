#pragma once
#include <atomic>
#include "stems/StemPlayback.h"
#include "transport/TransportClock.h"
namespace jeff::daw {class AudioEngine{public:void prepare(double,int,int)noexcept;void setAuditionNote(int,float)noexcept;void stopAuditionNote()noexcept{active=false;}void setPlaying(bool v)noexcept{clock.setPlaying(v);}void setMetronomeEnabled(bool v)noexcept{clock.setMetronomeEnabled(v);}void setTempo(double v)noexcept{clock.setTempo(v);}void seekBeats(double v)noexcept{clock.seekBeats(v);}void publishPlayback(std::unique_ptr<PlaybackSnapshot> p)noexcept{stems.publish(std::move(p));}StemPlayback&stemPlayback()noexcept{return stems;}void processBlock(float*const*,int,int)noexcept;float peak()const noexcept{return outPeak.load();}TransportClock&transportClock()noexcept{return clock;}private:TransportClock clock;StemPlayback stems;double rate{48000},phase{},delta{};float level{};bool active{};std::atomic<float>outPeak{};};}
