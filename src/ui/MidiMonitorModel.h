#pragma once
#include <deque>
#include <iomanip>
#include <sstream>
#include <string>
#include "midi/PerformanceEvent.h"
namespace jeff::daw {class MidiMonitorModel{public:void append(std::string_view dev,const PerformanceEvent&e){std::ostringstream s;s<<dev<<" | Ch "<<(int)e.channel<<" | "<<(e.type==PerformanceEventType::noteOn?"Note On":e.type==PerformanceEventType::noteOff?"Note Off":"Expression")<<" | Pad "<<(int)e.note<<" | "<<std::fixed<<std::setprecision(3)<<e.value<<" | t="<<e.sampleTime;rows_.push_back(s.str());while(rows_.size()>128)rows_.pop_front();}const std::deque<std::string>&rows()const{return rows_;}private:std::deque<std::string>rows_;};}
