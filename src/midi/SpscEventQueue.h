#pragma once
#include <array>
#include <atomic>
#include <optional>
#include "midi/PerformanceEvent.h"
namespace jeff::daw { template<std::size_t Capacity> class SpscEventQueue { static_assert(Capacity>1); public: bool push(const PerformanceEvent&e)noexcept{auto w=write.load(std::memory_order_relaxed);auto next=(w+1)%Capacity;if(next==read.load(std::memory_order_acquire)){drops.fetch_add(1,std::memory_order_relaxed);return false;} data[w]=e;write.store(next,std::memory_order_release);return true;} std::optional<PerformanceEvent> pop()noexcept{auto r=read.load(std::memory_order_relaxed);if(r==write.load(std::memory_order_acquire))return{};auto e=data[r];read.store((r+1)%Capacity,std::memory_order_release);return e;} std::uint64_t droppedCount()const noexcept{return drops.load(std::memory_order_relaxed);} private:std::array<PerformanceEvent,Capacity>data{};std::atomic_size_t read{},write{};std::atomic_uint64_t drops{};}; }
