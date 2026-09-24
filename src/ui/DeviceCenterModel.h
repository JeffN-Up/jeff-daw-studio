#pragma once
#include <string>
#include <vector>
#include "audio/AudioBackend.h"
#include "midi/DeviceProfile.h"
namespace jeff::daw {struct DeviceRow{std::string id,label,status,action;bool selected{};};class DeviceCenterModel{public:void setAudio(AudioStatus,const std::string&);void setMidi(const std::vector<DeviceIdentity>&,const std::vector<std::string>&);void setDiagnostics(std::uint64_t drops,std::uint64_t underruns);const std::vector<DeviceRow>&rows()const{return data;}const std::string&audioMessage()const{return audio;}std::string diagnostics()const;private:std::vector<DeviceRow>data;std::string audio;std::uint64_t dropCount{},underrunCount{};};}
