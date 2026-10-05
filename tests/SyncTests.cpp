#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "stems/SyncService.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
using namespace jeff::daw;
namespace {
struct Signal { double bpm=120, offset=.25; bool accented=true, beatless=false; int rate=8000; double seconds=12; };
class SignalReader : public AudioSampleReader {
public:
  SignalReader(Signal s, int& reads, int& largest, Frame& last, int& failAt) : signal(s),reads_(reads),largest_(largest),last_(last),failAt_(failAt) { meta={2,s.rate,Frame(s.rate*s.seconds),{}}; }
  const DecodedAudio& metadata() const noexcept override { return meta; }
  Result<void> read(Frame start,int count,std::span<float* const> out,CancellationToken& token) override {
    if(token.isCancelled()) return Result<void>::failure(ErrorCode::cancelled,"Cancelled signal.");
    ++reads_; largest_=std::max(largest_,count);
    last_=std::max(last_,start+count);
    if(failAt_ && reads_==failAt_) return Result<void>::failure(ErrorCode::readFailure,"Interrupted signal read.");
    for(int f=0;f<count;++f) {
      const double time=double(start+f)/signal.rate;
      double amplitude=0;
      if(signal.beatless) amplitude=.1;
      else if(time>=signal.offset) {
        const auto beat=int(std::floor((time-signal.offset)*signal.bpm/60+1e-9));
        const double age=time-signal.offset-beat*60/signal.bpm;
        if(age<.015) amplitude=(signal.accented ? (beat%4==0 ? 1.0 : .35) : .8)*std::exp(-age*200);
      }
      out[0][f]=float(amplitude); out[1][f]=float(-amplitude); // Anti-phase must not cancel analysis.
    }
    return Result<void>::success();
  }
private: Signal signal; DecodedAudio meta; int& reads_; int& largest_; Frame& last_; int& failAt_;
};
class SignalDecoder : public AudioDecoder {
public:
  std::map<std::string,Signal> signals;
  std::vector<std::string> opened;
  int reads=0, largest=0;
  Frame last=0;
  int failAt=0, cancelOpenAt=0;
  Result<DecodedAudio> inspect(const std::filesystem::path&,CancellationToken&,std::size_t) override { return Result<DecodedAudio>::failure(ErrorCode::decodeFailure,"Unused inspection."); }
  Result<std::unique_ptr<AudioSampleReader>> openReader(const std::filesystem::path& p,CancellationToken& token) override {
    const auto name=p.filename().string(); opened.push_back(name);
    if(cancelOpenAt && int(opened.size())==cancelOpenAt) { token.cancel(); return Result<std::unique_ptr<AudioSampleReader>>::failure(ErrorCode::cancelled,"Cancelled open."); }
    const auto it=signals.find(name);
    if(it==signals.end()) return Result<std::unique_ptr<AudioSampleReader>>::failure(ErrorCode::readFailure,"Missing signal.");
    return Result<std::unique_ptr<AudioSampleReader>>::success(std::make_unique<SignalReader>(it->second,reads,largest,last,failAt));
  }
};
struct Fixture {
  std::filesystem::path root;
  SignalDecoder decoder;
  std::unique_ptr<MediaStore> store;
  Project project;
  Fixture() {
    static std::atomic<int> n{0};
    root=std::filesystem::current_path()/("jds-sync-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(n++));
    std::filesystem::create_directories(root);
    store=std::make_unique<MediaStore>(root);
    project.projectId="project"; project.revisionId="revision"; project.tempoBpm=90;
    add("guide","group",Signal{120,.25});
    add("pad","group",Signal{120,.25,true,true});
    add("reference","refgroup",Signal{90,.5});
  }
  ~Fixture() { std::error_code ec; std::filesystem::remove_all(root,ec); }
  void add(const Id& id,const Id& group,Signal signal) {
    decoder.signals[id+".audio"]=signal; std::ofstream(root/(id+".audio")).put('x');
    AudioAsset a{id+"asset",id+".audio","checksum",2,signal.rate,Frame(signal.rate*signal.seconds),{}};
    project.assets.push_back(a);
    if(std::none_of(project.groups.begin(),project.groups.end(),[&](const auto& g){return g.id==group;})) project.groups.push_back({group,TimingMode::preserve,preserveTimingMap(project.tempoBpm),preserveTimingMap(project.tempoBpm),{},false,{}});
    Track t; t.id=id; t.name=id; t.assetId=a.id; t.timingGroupId=group; t.trimEndSeconds=t.importedTrimEndSeconds=signal.seconds;
    project.tracks.push_back(t);
  }
};
}
TEST_CASE("SyncKnownOffset","[Sync]") {
  Fixture f; BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); CancellationToken token;
  auto proposal=sync.propose(f.project,"group",Id("reference"),SyncOptions{},token);
  REQUIRE(proposal); REQUIRE_FALSE(proposal.value().requiresCorrection);
  CHECK(proposal.value().sourceBpm==Catch::Approx(120).margin(1));
  // First source downbeat .25s should meet reference downbeat .5s = .75 project beats.
  CHECK(mapTime(proposal.value().timingMap,.25)+proposal.value().placementDeltaBeats==Catch::Approx(.75).margin(.035));
  CHECK(mapTime(proposal.value().timingMap,.75)+proposal.value().placementDeltaBeats==Catch::Approx(1.75).margin(.035));
  CHECK(f.decoder.opened==std::vector<std::string>{"guide.audio","reference.audio"});
  CHECK(f.decoder.largest<=4096);
}
TEST_CASE("SyncAmbiguousNeedsCorrection","[Sync]") {
  Fixture f; f.decoder.signals["guide.audio"].accented=false;
  BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); CancellationToken token;
  auto p=sync.propose(f.project,"group",std::nullopt,SyncOptions{},token);
  REQUIRE(p); CHECK(p.value().requiresCorrection);
  ProjectHistory history(f.project); CHECK_FALSE(applyProposal(p.value(),history));
  SyncOptions options; options.manualBpm=120;
  auto partial=sync.propose(f.project,"group",std::nullopt,options,token); REQUIRE(partial); CHECK(partial.value().requiresCorrection);
  options.manualDownbeatSeconds=.25;
  auto corrected=sync.propose(f.project,"group",std::nullopt,options,token); REQUIRE(corrected); CHECK_FALSE(corrected.value().requiresCorrection);
  f.decoder.signals["guide.audio"].beatless=true;
  auto beatless=sync.propose(f.project,"group",std::nullopt,SyncOptions{},token); REQUIRE(beatless); CHECK(beatless.value().requiresCorrection);
}
TEST_CASE("SyncStaleProposalRejected","[Sync]") {
  Fixture f; BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); CancellationToken token;
  auto p=sync.propose(f.project,"group",Id("reference"),SyncOptions{},token); REQUIRE(p);
  auto changed=f.project; changed.tempoBpm=110;
  ProjectHistory history(changed); CHECK_FALSE(applyProposal(p.value(),history));
  changed=f.project; changed.tracks.back().placementBeats=2;
  ProjectHistory referenceEdit(changed); CHECK_FALSE(applyProposal(p.value(),referenceEdit));
  ProjectHistory edited(f.project); REQUIRE(edited.apply(TrimTrack{"guide",.1,10})); CHECK_FALSE(applyProposal(p.value(),edited));
  changed=f.project; changed.groups.back().timingMap={{{0,0},{1,3}}};
  ProjectHistory sameRevisionMap(changed); CHECK_FALSE(applyProposal(p.value(),sameRevisionMap));
  changed=f.project; changed.assets.front().checksum="replaced";
  ProjectHistory changedMedia(changed); CHECK_FALSE(applyProposal(p.value(),changedMedia));
  changed=f.project; changed.groups.front().syncReferenceTrackId="reference";
  ProjectHistory changedReference(changed); CHECK_FALSE(applyProposal(p.value(),changedReference));
}
TEST_CASE("SyncReferenceCurrentTimingTrimsAndPlacement","[Sync]") {
  Fixture f;
  f.add("earlyref","refgroup",Signal{90,.5,true,true});
  f.project.tracks[2].placementBeats=4;
  f.project.tracks[2].trimStartSeconds=1.5;
  f.project.tracks[3].placementBeats=2;
  f.project.groups[1].mode=TimingMode::edit;
  f.project.groups[1].timingMap={{{0,0},{2,4},{4,10}}};
  BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); CancellationToken token;
  auto p=sync.propose(f.project,"group",Id("reference"),SyncOptions{},token); REQUIRE(p); REQUIRE_FALSE(p.value().requiresCorrection);
  auto snapshot=proposalSnapshot(f.project,p.value()); REQUIRE(snapshot);
  // Reference has 1s grid offset: placement delta2 / initial slope2.
  // First retained accented downbeat is3+1/6s; grid position4+1/6s
  // maps to10.5, plus group placement2 =>12.5projectbeats.
  auto first=sourceTimeToProjectBeat(snapshot.value(),snapshot.value().tracks[0],.25); REQUIRE(first);
  CHECK(first.value()==Catch::Approx(12.5).margin(.04));
  auto next=sourceTimeToProjectBeat(snapshot.value(),snapshot.value().tracks[0],.75); REQUIRE(next);
  CHECK(next.value()==Catch::Approx(14.5).margin(.04));
  CHECK(p.value().targetBpm==Catch::Approx(45).margin(1));
}
TEST_CASE("SyncLinkedGuideAndCommonSourceOffsets","[Sync]") {
  Fixture f; f.project.tracks[1].placementBeats=1;
  BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); CancellationToken token;
  auto p=sync.propose(f.project,"group",Id("reference"),SyncOptions{},token); REQUIRE(p);
  auto snapshot=proposalSnapshot(f.project,p.value()); REQUIRE(snapshot);
  const auto& pad=snapshot.value().tracks[1];
  CHECK(pad.placementBeats==Catch::Approx(.25+4.0/3).margin(.035));
  auto offset=sourceGridOffset(snapshot.value(),pad); REQUIRE(offset); CHECK(offset.value()==Catch::Approx(2.0/3).margin(.015));
  CHECK(snapshot.value().groups[0].mode==TimingMode::autoSync);
  CHECK(f.decoder.opened==std::vector<std::string>{"guide.audio","reference.audio"});
  SyncOptions choosePad; choosePad.rhythmicGuideTrackId="pad";
  auto unresolved=sync.propose(f.project,"group",std::nullopt,choosePad,token); REQUIRE(unresolved); CHECK(unresolved.value().requiresCorrection);
}
TEST_CASE("SyncApplyOneUndoAndDeletedReferenceRetention","[Sync]") {
  Fixture f; f.project.tracks[0].gain=.6; f.project.tracks[1].mute=true;
  f.project.groups[0].extensions["unknown"]="{\"future\":true}";
  BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); CancellationToken token;
  auto p=sync.propose(f.project,"group",Id("reference"),SyncOptions{},token); REQUIRE(p);
  ProjectHistory history(f.project); REQUIRE(applyProposal(p.value(),history));
  CHECK(history.current().revisionId!=f.project.revisionId);
  CHECK(history.current().tracks[0].gain==.6); CHECK(history.current().tracks[1].mute);
  CHECK(history.current().groups[0].extensions==f.project.groups[0].extensions);
  REQUIRE(history.undo()); CHECK(history.current().groups[0].mode==TimingMode::preserve); CHECK(history.current().tracks[0].placementBeats==0);
  CHECK_FALSE(history.undo()); REQUIRE(history.redo());
  const auto applied=history.current().groups[0].timingMap;
  REQUIRE(removeTrackWithSyncRetention("reference",history));
  CHECK(history.current().groups[0].referenceUnavailable); CHECK(history.current().groups[0].syncReferenceTrackId=="reference");
  CHECK(mapTime(history.current().groups[0].timingMap,.75)==mapTime(applied,.75));
  REQUIRE(history.undo()); CHECK_FALSE(history.current().groups[0].referenceUnavailable); CHECK(history.current().tracks.size()==3);
}
TEST_CASE("SyncPreviewAndCancellationNeverMutate","[Sync]") {
  Fixture f; BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); StretchRenderer renderer(f.decoder); CancellationToken token;
  auto p=sync.propose(f.project,"group",Id("reference"),SyncOptions{},token); REQUIRE(p);
  std::filesystem::path originalPath,proposedPath;
  {
    auto preview=sync.preview(f.project,p.value(),renderer,*f.store,token); REQUIRE(preview);
    CHECK(preview.value().original.revisionId=="revision"); CHECK(preview.value().proposed.revisionId=="revision");
    CHECK(preview.value().proposed.groups[0].mode==TimingMode::autoSync);
    CHECK(preview.value().originalAudio.channels==4); CHECK(preview.value().proposedAudio.channels==4);
    CHECK(preview.value().originalAudio.originBeats==0);
    CHECK(preview.value().proposedAudio.originBeats==Catch::Approx(.25).margin(.035));
    originalPath=preview.value().originalAudio.dataPath(); proposedPath=preview.value().proposedAudio.dataPath();
    CHECK(std::filesystem::exists(originalPath)); CHECK(std::filesystem::exists(proposedPath));
  }
  CHECK_FALSE(std::filesystem::exists(originalPath)); CHECK_FALSE(std::filesystem::exists(proposedPath));
  CHECK(f.project.revisionId=="revision"); CHECK(f.project.groups[0].mode==TimingMode::preserve); CHECK(f.project.tracks[0].placementBeats==0);
  // Cancel opening proposed audio after original has been fully prepared.
  f.decoder.cancelOpenAt=int(f.decoder.opened.size())+3;
  auto cancelled=sync.preview(f.project,p.value(),renderer,*f.store,token); REQUIRE_FALSE(cancelled); CHECK(cancelled.error().code==ErrorCode::cancelled);
  CHECK(f.project.revisionId=="revision");
  for(const auto& entry:std::filesystem::recursive_directory_iterator(f.root)) CHECK(entry.path().extension()!=".f32");
}
TEST_CASE("SyncAnalysisBoundsFailuresAndPreZeroCorrection","[Sync]") {
  Fixture f; CancellationToken token;
  BeatAnalysisLimits limits; limits.maxSeconds=2;
  BeatAnalyzer bounded(f.decoder,*f.store,limits);
  auto estimate=bounded.analyze(f.project.assets.front(),token); REQUIRE(estimate);
  CHECK(f.decoder.last<=16000); CHECK(f.decoder.largest<=4096);
  f.decoder.failAt=f.decoder.reads+1;
  auto failed=bounded.analyze(f.project.assets.front(),token); REQUIRE_FALSE(failed); CHECK(failed.error().code==ErrorCode::readFailure);
  auto unsafe=f.project.assets.front(); unsafe.relativePath="../guide.audio";
  auto bad=bounded.analyze(unsafe,token); CHECK_FALSE(bad);
  CancellationToken cancelled; cancelled.cancel();
  auto stopped=bounded.analyze(f.project.assets.front(),cancelled); REQUIRE_FALSE(stopped); CHECK(stopped.error().code==ErrorCode::cancelled);
  BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer);
  SyncOptions options; options.manualBpm=120; options.manualDownbeatSeconds=1;
  auto beforeZero=sync.propose(f.project,"group",Id("reference"),options,token); REQUIRE_FALSE(beforeZero);
  CHECK(beforeZero.error().code==ErrorCode::invalidCommand); CHECK(beforeZero.error().message.find("project zero")!=std::string::npos);
}
TEST_CASE("SyncStoredManualTimingAndReferenceCorrection","[Sync]") {
  Fixture f; f.decoder.signals["guide.audio"].beatless=true; f.decoder.signals["reference.audio"].accented=false;
  f.project.tracks[0].sourceBpm=100; f.project.tracks[0].downbeatSeconds=.4;
  f.project.tracks[2].sourceBpm=90; f.project.tracks[2].downbeatSeconds=.8;
  BeatAnalyzer analyzer(f.decoder,*f.store); SyncService sync(analyzer); CancellationToken token;
  auto p=sync.propose(f.project,"group",Id("reference"),SyncOptions{},token); REQUIRE(p); REQUIRE_FALSE(p.value().requiresCorrection);
  auto snapshot=proposalSnapshot(f.project,p.value()); REQUIRE(snapshot);
  auto beat=sourceTimeToProjectBeat(snapshot.value(),snapshot.value().tracks[0],.4); REQUIRE(beat);
  CHECK(beat.value()==Catch::Approx(1.2).margin(.001));
  CHECK(p.value().sourceBpm==100);
}
