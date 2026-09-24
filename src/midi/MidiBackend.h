#pragma once
#include <functional>
#include <string>
#include <vector>
#include "midi/DeviceProfile.h"
namespace jeff::daw {class MidiBackend{public:using Callback=std::function<void(const std::string&,const MidiMessage&)>;virtual~MidiBackend()=default;virtual std::vector<DeviceIdentity>devices()const=0;virtual bool open(const std::string&,Callback)=0;virtual void close(const std::string&)=0;};}
