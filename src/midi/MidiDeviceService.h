#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "midi/MidiBackend.h"
#include "midi/SpscEventQueue.h"
namespace jeff::daw {enum class DeviceStatus{available,connected,disconnected};class MidiDeviceService{public:MidiDeviceService(MidiBackend&,SpscEventQueue<4096>&);~MidiDeviceService();void refresh();const std::vector<DeviceIdentity>&devices()const{return current;}bool connect(const std::string&);void disconnect(const std::string&);std::vector<std::string>selectedDeviceIds()const;DeviceStatus statusFor(const std::string&)const;private:bool openSelected(const DeviceIdentity&);MidiBackend&backend;SpscEventQueue<4096>&queue;ProfileRegistry profiles;std::vector<DeviceIdentity>current;std::unordered_set<std::string>selected,opened;std::unordered_map<std::string,DeviceStatus>status;std::int64_t tick{};};}
