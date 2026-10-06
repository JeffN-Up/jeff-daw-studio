#include "app/RootComponent.h"
namespace jeff::daw {
RootComponent::RootComponent()
    : audioBackend(engine), audioController(audioBackend), midiService(midiBackend, queue),
      transport(engine), mirror([this](auto &e) { event(e); }), stemWorkspace(engine),
      audioSettings(audioBackend.deviceManager(), 0, 0, 0, 2, false, false, true, false) {
  juce::PropertiesFile::Options o;
  o.applicationName = "Jeff DAW Studio";
  o.filenameSuffix = "settings";
  o.osxLibrarySubFolder = "Application Support";
  properties.setStorageParameters(o);
  title.setText("Jeff DAW Studio", juce::dontSendNotification);
  title.setFont(juce::FontOptions(24, juce::Font::bold));
  monitorTitle.setText("MIDI Activity", juce::dontSendNotification);
  monitor.setReadOnly(true);
  monitor.setMultiLine(true);
  monitor.setScrollbarsShown(true);
  for (auto *c :
       {(juce::Component *)&title, (juce::Component *)&status, (juce::Component *)&jamTab,
        (juce::Component *)&stemTab, (juce::Component *)&transport, (juce::Component *)&mirror,
        (juce::Component *)&monitorTitle, (juce::Component *)&monitor,
        (juce::Component *)&deviceCenter, (juce::Component *)&stemWorkspace,
        (juce::Component *)&audioSettings, (juce::Component *)&settingsTab})
    addAndMakeVisible(*c);
  settingsTab.onClick = [this] {
    showWorkspace(false);
    showingAudio = true;
    for (auto *c : {(juce::Component *)&transport, (juce::Component *)&mirror,
                    (juce::Component *)&monitorTitle, (juce::Component *)&monitor,
                    (juce::Component *)&deviceCenter})
      c->setVisible(false);
    audioSettings.setVisible(true);
    resized();
  };
  jamTab.onClick = [this] { showWorkspace(false); };
  stemTab.onClick = [this] { showWorkspace(true); };
  jamTab.setClickingTogglesState(true);
  stemTab.setClickingTogglesState(true);
  auto ready = audioController.configure({});
  midiService.refresh();
  auto saved = properties.getUserSettings()->getValue("midiDeviceId").toStdString();
  if (!saved.empty())
    midiService.connect(saved);
  else if (!midiService.devices().empty()) {
    midiService.connect(midiService.devices()[0].stableId);
    properties.getUserSettings()->setValue("midiDeviceId",
                                           juce::String(midiService.devices()[0].stableId));
  }
  if (!midiService.devices().empty())
    mirrorModel.setProfile(profiles.match(midiService.devices()[0]).kind());
  deviceModel.setAudio(audioController.status(), audioController.message());
  deviceModel.setMidi(midiService.devices(), midiService.selectedDeviceIds());
  status.setText(ready ? "Audio ready" : "Set up audio & MIDI", juce::dontSendNotification);
  showWorkspace(true);
  startTimerHz(60);
}
void RootComponent::showWorkspace(bool stems) {
  showingAudio = false;
  audioSettings.setVisible(false);
  showingStems = stems;
  stemWorkspace.setVisible(stems);
  for (auto *c :
       {(juce::Component *)&transport, (juce::Component *)&mirror, (juce::Component *)&monitorTitle,
        (juce::Component *)&monitor, (juce::Component *)&deviceCenter})
    c->setVisible(!stems);
  stemTab.setToggleState(stems, juce::dontSendNotification);
  jamTab.setToggleState(!stems, juce::dontSendNotification);
  resized();
}
void RootComponent::paint(juce::Graphics &g) { g.fillAll(juce::Colour(0xff101319)); }
void RootComponent::resized() {
  auto r = getLocalBounds().reduced(18);
  auto h = r.removeFromTop(40);
  title.setBounds(h.removeFromLeft(230));
  jamTab.setBounds(h.removeFromLeft(125).reduced(2));
  stemTab.setBounds(h.removeFromLeft(110).reduced(2));
  settingsTab.setBounds(h.removeFromLeft(125).reduced(2));
  status.setBounds(h);
  if (showingAudio) {
    audioSettings.setBounds(r);
    return;
  }
  if (showingStems) {
    stemWorkspace.setBounds(r);
    return;
  }
  transport.setBounds(r.removeFromTop(54));
  deviceCenter.setBounds(r.removeFromBottom(190));
  if (r.getWidth() > 850) {
    auto side = r.removeFromRight(300);
    monitorTitle.setBounds(side.removeFromTop(28));
    monitor.setBounds(side.reduced(4));
    mirror.setBounds(r.reduced(6));
  } else {
    auto bottom = r.removeFromBottom(130);
    monitorTitle.setBounds(bottom.removeFromTop(26));
    monitor.setBounds(bottom);
    mirror.setBounds(r);
  }
}
void RootComponent::event(const PerformanceEvent &e) {
  mirrorModel.consume(e);
  monitorModel.append("Virtual Orba", e);
  engine.handlePerformanceEvent(e, true);
}
void RootComponent::timerCallback() {
  mirrorModel.consumePending(queue, 512, [this](auto &e) {
    monitorModel.append("MIDI Input", e);
    engine.handlePerformanceEvent(e, false);
  });
  mirrorModel.tick(juce::Time::currentTimeMillis());
  mirror.setSnapshot(mirrorModel.snapshot());
  juce::String s;
  for (auto &r : monitorModel.rows())
    s << r << juce::newLine;
  if (monitor.getText() != s) {
    monitor.setText(s, false);
    monitor.moveCaretToEnd();
  }
  if (++refreshCounter >= 60) {
    refreshCounter = 0;
    midiService.refresh();
  }
  deviceModel.setMidi(midiService.devices(), midiService.selectedDeviceIds());
  deviceModel.setDiagnostics(queue.droppedCount(), 0);
  deviceCenter.setModel(deviceModel);
}
} // namespace jeff::daw
