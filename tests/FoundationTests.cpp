#include "app/AppMetadata.h"
#include "audio/AudioBackend.h"
#include "audio/AudioEngine.h"
#include "midi/MidiDeviceService.h"
#include "ui/DeviceCenterModel.h"
#include "ui/MidiMonitorModel.h"
#include "ui/OrbaLayout.h"
#include "ui/OrbaMirrorModel.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <unordered_map>
using namespace jeff::daw;
TEST_CASE("application identity is stable") {
  REQUIRE(AppMetadata::name == "Jeff DAW Studio");
  REQUIRE(AppMetadata::projectExtension == ".jds");
}
TEST_CASE("queue is bounded and ordered") {
  SpscEventQueue<3> q;
  REQUIRE(q.push(PerformanceEvent::noteOn(0, 1, .5f, 1)));
  REQUIRE(q.push(PerformanceEvent::noteOff(0, 1, 2)));
  REQUIRE_FALSE(q.push(PerformanceEvent::noteOn(0, 2, 1, 3)));
  REQUIRE(q.droppedCount() == 1);
  REQUIRE(q.pop()->sampleTime == 1);
}
TEST_CASE("profiles match controllers and decode MPE") {
  ProfileRegistry r;
  REQUIRE(r.match({"a", "Artiphon Orba 2"}).kind() == ProfileKind::orba2);
  REQUIRE(r.match({"b", "JT-4000 MICRO"}).kind() == ProfileKind::jt4000);
  auto b = r.match({"a", "Orba 2"}).decode({MidiType::pressure, 2, 60, 0, 1.2f}, 44);
  REQUIRE(b.size == 1);
  REQUIRE(b.events[0].value == 1.0f);
  REQUIRE(b.events[0].sampleTime == 44);
}
TEST_CASE("metronome is sample accurate") {
  TransportClock c;
  c.prepare(48000);
  c.setTempo(120);
  c.setPlaying(true);
  c.setMetronomeEnabled(true);
  auto b = c.process(48001);
  REQUIRE(b.clickCount == 3);
  REQUIRE(b.clickOffsets[1] == 24000);
}
TEST_CASE("audio engine renders bounded audition") {
  AudioEngine e;
  e.prepare(48000, 512, 2);
  e.setAuditionNote(69, .5f);
  float a[512]{}, b[512]{};
  float *ch[]{a, b};
  e.processBlock(ch, 2, 512);
  REQUIRE(e.peak() > 0);
  REQUIRE(e.peak() <= .1251f);
  REQUIRE(a[100] == Catch::Approx(b[100]));
}
class FakeMidi : public MidiBackend {
public:
  std::vector<DeviceIdentity> list;
  std::unordered_map<std::string, Callback> cb;
  std::vector<DeviceIdentity> devices() const override { return list; }
  bool open(const std::string &id, Callback c) override {
    cb[id] = std::move(c);
    return true;
  }
  void close(const std::string &id) override { cb.erase(id); }
  void emit(const std::string &id, MidiMessage m) { cb.at(id)(id, m); }
};
TEST_CASE("MIDI service reconnects stable id with duplicate names") {
  FakeMidi b;
  b.list = {{"left", "Orba 2"}, {"right", "Orba 2"}};
  SpscEventQueue<4096> q;
  MidiDeviceService s(b, q);
  REQUIRE(s.connect("right"));
  b.list = {};
  s.refresh();
  REQUIRE(s.statusFor("right") == DeviceStatus::disconnected);
  b.list = {{"right", "Orba 2"}, {"left", "Orba 2"}};
  s.refresh();
  REQUIRE(s.statusFor("right") == DeviceStatus::connected);
  REQUIRE_FALSE(b.cb.contains("left"));
}
TEST_CASE("Orba mirror preserves simultaneous touch and recovers overflow") {
  OrbaMirrorModel m;
  SpscEventQueue<4> q;
  q.push(PerformanceEvent::noteOn(0, 1, .8f, 0));
  q.push(PerformanceEvent::noteOn(0, 3, .7f, 0));
  q.push(PerformanceEvent::expression(0, 0, 3, ExpressionKind::pressure, .9f, 0, 1));
  q.push(PerformanceEvent::noteOff(0, 1, 2));
  m.consumePending(q, 3);
  REQUIRE(m.snapshot().pads[1].held);
  REQUIRE(m.snapshot().pads[3].held);
  m.tick(3000);
  REQUIRE_FALSE(m.snapshot().pads[1].held);
}
TEST_CASE("Orba geometry keeps pads distinct and inside body") {
  auto l = calculateOrbaLayout({0, 0, 320, 320});
  for (int i = 0; i < 8; ++i) {
    REQUIRE(distance(l.centre, l.pads[i].centre) + l.pads[i].hitRadius <= l.bodyRadius);
    REQUIRE(distance(l.pads[i].centre, l.pads[(i + 1) % 8].centre) >
            l.pads[i].hitRadius + l.pads[(i + 1) % 8].hitRadius);
  }
}
TEST_CASE("MIDI monitor retains newest 128 rows") {
  MidiMonitorModel m;
  for (int i = 0; i < 140; ++i)
    m.append("Orba", PerformanceEvent::noteOn(0, i, 1, i));
  REQUIRE(m.rows().size() == 128);
  REQUIRE(m.rows().front().find("Pad 12") != std::string::npos);
}
TEST_CASE("Device Center uses stable ids and actionable errors") {
  DeviceCenterModel m;
  m.setAudio(AudioStatus::configurationFailed, "96 kHz unavailable.");
  m.setMidi({{"one", "Orba 2"}, {"two", "Orba 2"}}, {"two"});
  REQUIRE(m.rows()[0].id == "one");
  REQUIRE_FALSE(m.rows()[0].selected);
  REQUIRE(m.rows()[1].selected);
  REQUIRE(m.audioMessage().find("retry") != std::string::npos);
}
TEST_CASE("Hardware MIDI produces audition audio without virtual pad transposition", "[audition]") {
  AudioEngine engine;
  engine.prepare(48000, 128, 2);
  float left[128]{}, right[128]{};
  float *output[]{left, right};
  engine.handlePerformanceEvent(PerformanceEvent::noteOn(0, 69, 1, 0), false);
  engine.processBlock(output, 2, 128);
  REQUIRE(engine.peak() > 0.1f);
  engine.handlePerformanceEvent(PerformanceEvent::noteOff(0, 68, 0), false);
  engine.processBlock(output, 2, 128);
  REQUIRE(engine.peak() > 0.1f);
  engine.handlePerformanceEvent(PerformanceEvent::noteOff(0, 69, 0), false);
  engine.processBlock(output, 2, 128);
  REQUIRE(engine.peak() == 0);
}
