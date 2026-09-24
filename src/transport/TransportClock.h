#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace jeff::daw { struct TransportBlock{std::int64_t startSample{},endSample{};bool isPlaying{};std::array<int,16>clickOffsets{};std::uint8_t clickCount{};}; class TransportClock{public:void prepare(double)noexcept;void setTempo(double)noexcept;double tempo()const noexcept{return bpm;}void setPlaying(bool p)noexcept{playing=p;}void setMetronomeEnabled(bool e)noexcept{metro=e;}TransportBlock process(int)noexcept;void reset()noexcept;std::int64_t positionSamples()const noexcept{return position;}private:double rate{44100},bpm{120},nextClick{};std::int64_t position{};bool playing{},metro{};}; }
