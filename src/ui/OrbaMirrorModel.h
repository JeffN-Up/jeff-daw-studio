#pragma once
#include <array>
#include <string>
#include "midi/DeviceProfile.h"
#include "midi/SpscEventQueue.h"
namespace jeff::daw {struct OrbaPadState{bool held{};float intensity{},pressure{},pitch{.5f},timbre{};std::int64_t lastActivity{};};struct OrbaMirrorSnapshot{std::string modelLabel{"Generic MIDI"},mode{"Lead"};std::array<OrbaPadState,8>pads{};std::string warning;std::uint64_t revision{};};class OrbaMirrorModel{public:void consume(const PerformanceEvent&)noexcept;template<std::size_t C,typename O>std::size_t consumePending(SpscEventQueue<C>&q,std::size_t max,O obs)noexcept{auto d=q.droppedCount();if(d>observedDrops){overflow=true;observedDrops=d;}std::size_t n=0;while(n<max){auto e=q.pop();if(!e)break;apply(*e);obs(*e);++n;}if(n)++state.revision;return n;}template<std::size_t C>std::size_t consumePending(SpscEventQueue<C>&q,std::size_t m)noexcept{return consumePending(q,m,[](auto&){});}void tick(std::int64_t)noexcept;void setProfile(ProfileKind);const OrbaMirrorSnapshot&snapshot()const{return state;}private:void apply(const PerformanceEvent&)noexcept;OrbaMirrorSnapshot state;bool overflow{};std::uint64_t observedDrops{};};}
