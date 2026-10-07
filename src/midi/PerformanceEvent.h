#pragma once
#include <algorithm>
#include <cstdint>
#include <type_traits>
namespace jeff::daw {
enum class PerformanceEventType:std::uint8_t{noteOn,noteOff,expression,modeChange,gesture};
enum class ExpressionKind:std::uint8_t{pressure,pitch,timbre,radiate,vibrato,tilt,shake,spin,none};
struct PerformanceEvent{PerformanceEventType type{PerformanceEventType::noteOff}; ExpressionKind expressionKind{ExpressionKind::none}; std::uint16_t deviceSlot{}; std::uint8_t channel{},note{}; float value{},secondaryValue{}; std::int64_t sampleTime{};
 static constexpr float norm(float v){return std::clamp(v,0.0f,1.0f);} static constexpr PerformanceEvent noteOn(std::uint16_t d,std::uint8_t n,float v,std::int64_t t,std::uint8_t c=0){return{PerformanceEventType::noteOn,ExpressionKind::none,d,c,n,norm(v),0,t};} static constexpr PerformanceEvent noteOff(std::uint16_t d,std::uint8_t n,std::int64_t t,std::uint8_t c=0){return{PerformanceEventType::noteOff,ExpressionKind::none,d,c,n,0,0,t};} static constexpr PerformanceEvent expression(std::uint16_t d,std::uint8_t c,std::uint8_t n,ExpressionKind k,float v,float v2,std::int64_t t){return{PerformanceEventType::expression,k,d,c,n,norm(v),norm(v2),t};}};
static_assert(std::is_trivially_copyable_v<PerformanceEvent>); }
