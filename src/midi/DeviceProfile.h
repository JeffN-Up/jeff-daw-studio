#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "midi/PerformanceEvent.h"
namespace jeff::daw { struct DeviceIdentity{std::string stableId,name;}; enum class ProfileKind{generic,orba1,orba2,jt4000}; enum class MidiType{noteOn,noteOff,pressure,pitch,cc,unsupported}; struct MidiMessage{MidiType type{MidiType::unsupported};std::uint8_t channel{},data1{},data2{};float value{};}; struct DecodedEventBatch{std::array<PerformanceEvent,4>events{};std::uint8_t size{};}; class DeviceProfile{public:explicit DeviceProfile(ProfileKind k):profileKind(k){} ProfileKind kind()const noexcept{return profileKind;} DecodedEventBatch decode(const MidiMessage&,std::int64_t)const noexcept;private:ProfileKind profileKind;}; class ProfileRegistry{public:const DeviceProfile&match(const DeviceIdentity&)const noexcept;private:DeviceProfile generic{ProfileKind::generic},orba1{ProfileKind::orba1},orba2{ProfileKind::orba2},jt{ProfileKind::jt4000};}; }
