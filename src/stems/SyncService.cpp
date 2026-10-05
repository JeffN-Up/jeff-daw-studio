#include "stems/SyncService.h"
#include <algorithm>
#include <cmath>
namespace jeff::daw {
namespace {
template <typename T> const T* find(const std::vector<T>& entities,const Id& id) {
  auto it=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.id==id;});
  return it==entities.end() ? nullptr : &*it;
}
bool validManual(double bpm,double downbeat,const Track& track) {
  return std::isfinite(bpm) && bpm>=40 && bpm<=240 && std::isfinite(downbeat) && downbeat>=track.trimStartSeconds && downbeat<track.trimEndSeconds;
}
}
Result<SyncProposal> SyncService::propose(const Project& project,Id group,std::optional<Id> reference,std::optional<double> bpm,CancellationToken& token) {
  SyncOptions options; options.manualBpm=bpm;
  return propose(project,std::move(group),std::move(reference),options,token);
}
Result<SyncProposal> SyncService::propose(const Project& project,Id groupId,std::optional<Id> referenceId,const SyncOptions& options,CancellationToken& token) {
  using R=Result<SyncProposal>;
  try {
    if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Sync cancelled.");
    if(auto valid=validate(project); !valid) return R::failure(valid.error().code,valid.error().message);
    const auto* group=find(project.groups,groupId);
    if(!group) return R::failure(ErrorCode::missingEntity,"Sync group does not exist.");
    const Track* guide=nullptr;
    if(options.rhythmicGuideTrackId) guide=find(project.tracks,*options.rhythmicGuideTrackId);
    else for(const auto& track:project.tracks) if(track.timingGroupId==groupId) {guide=&track; break;}
    if(!guide || guide->timingGroupId!=groupId) return R::failure(ErrorCode::missingEntity,"Choose a rhythmic guide within the sync group.");
    const Track* reference=referenceId ? find(project.tracks,*referenceId) : nullptr;
    if(referenceId && (!reference || reference->timingGroupId==groupId)) return R::failure(ErrorCode::invalidCommand,"Choose a reference outside the sync group.");
    if(options.manualBpm && (!std::isfinite(*options.manualBpm) || *options.manualBpm<40 || *options.manualBpm>240)) return R::failure(ErrorCode::invalidCommand,"Manual BPM must be within 40–240.");
    if(options.manualDownbeatSeconds && (!std::isfinite(*options.manualDownbeatSeconds) || *options.manualDownbeatSeconds<guide->trimStartSeconds || *options.manualDownbeatSeconds>=guide->trimEndSeconds)) return R::failure(ErrorCode::invalidCommand,"Manual downbeat must be inside the guide trim.");
    if(options.referenceCorrection && (!reference || !validManual(options.referenceCorrection->bpm,options.referenceCorrection->downbeatSeconds,*reference))) return R::failure(ErrorCode::invalidCommand,"Reference correction needs BPM and downbeat inside its trim.");
    SyncProposal proposal;
    proposal.projectId=project.projectId; proposal.sourceRevision=project.revisionId; proposal.sourceSnapshot=project;
    proposal.groupId=groupId; proposal.guideTrackId=guide->id; proposal.referenceTrackId=referenceId;
    auto estimate=analyzer_.analyze(*find(project.assets,guide->assetId),guide->trimStartSeconds,guide->trimEndSeconds,token);
    if(!estimate) return R::failure(estimate.error().code,estimate.error().message);
    proposal.guideEstimate=std::move(estimate.value());
    const bool storedGuide=guide->sourceBpm>0 && validManual(guide->sourceBpm,guide->downbeatSeconds,*guide);
    proposal.sourceBpm=options.manualBpm.value_or(storedGuide ? guide->sourceBpm : proposal.guideEstimate.tempoBpm);
    proposal.downbeatSeconds=options.manualDownbeatSeconds.value_or(storedGuide ? guide->downbeatSeconds : proposal.guideEstimate.candidateDownbeatSeconds);
    const bool guideManual=(options.manualBpm.has_value() && options.manualDownbeatSeconds.has_value()) || (storedGuide && !options.manualBpm && !options.manualDownbeatSeconds);
    proposal.requiresCorrection=proposal.guideEstimate.needsCorrection && !guideManual;
    if(proposal.requiresCorrection) proposal.correctionReason=proposal.guideEstimate.correctionReason;
    proposal.confidence=proposal.guideEstimate.confidence;
    double referenceBpm=0,referenceDownbeat=0;
    if(reference) {
      auto refEstimate=analyzer_.analyze(*find(project.assets,reference->assetId),reference->trimStartSeconds,reference->trimEndSeconds,token);
      if(!refEstimate) return R::failure(refEstimate.error().code,refEstimate.error().message);
      proposal.referenceEstimate=std::move(refEstimate.value());
      const bool storedReference=reference->sourceBpm>0 && validManual(reference->sourceBpm,reference->downbeatSeconds,*reference);
      referenceBpm=options.referenceCorrection ? options.referenceCorrection->bpm : storedReference ? reference->sourceBpm : proposal.referenceEstimate->tempoBpm;
      referenceDownbeat=options.referenceCorrection ? options.referenceCorrection->downbeatSeconds : storedReference ? reference->downbeatSeconds : proposal.referenceEstimate->candidateDownbeatSeconds;
      if(proposal.referenceEstimate->needsCorrection && !options.referenceCorrection && !storedReference) {
        proposal.requiresCorrection=true; proposal.correctionReason="Reference timing is uncertain; enter its BPM and downbeat.";
      }
      proposal.confidence=std::min(proposal.confidence,proposal.referenceEstimate->confidence);
    }
    // Unresolved suggestions remain displayable; no usable map is fabricated.
    if(proposal.requiresCorrection) return R::success(std::move(proposal));
    auto offset=sourceGridOffset(project,*guide);
    auto origin=groupOriginBeats(project,groupId);
    if(!offset) return R::failure(offset.error().code,offset.error().message);
    if(!origin) return R::failure(origin.error().code,origin.error().message);
    const double firstSource=offset.value()+proposal.downbeatSeconds;
    const double period=60/proposal.sourceBpm;
    double firstTarget=0;
    if(reference) {
      auto beat=sourceTimeToProjectBeat(project,*reference,referenceDownbeat);
      if(!beat) return R::failure(beat.error().code,beat.error().message);
      firstTarget=beat.value();
    } else {
      auto currentBeat=sourceTimeToProjectBeat(project,*guide,proposal.downbeatSeconds);
      if(!currentBeat) return R::failure(currentBeat.error().code,currentBeat.error().message);
      // Next project grid beat preserves leading silence without a hidden clamp.
      firstTarget=std::ceil(currentBeat.value());
    }
    auto nextTarget=reference ? sourceTimeToProjectBeat(project,*reference,referenceDownbeat+60/referenceBpm) : Result<double>::success(firstTarget+1);
    if(!nextTarget) return R::failure(nextTarget.error().code,nextTarget.error().message);
    const double step=nextTarget.value()-firstTarget;
    if(!std::isfinite(step) || step<=0) return R::failure(ErrorCode::invalidTimingMap,"Reference timing must advance.");
    const double newOrigin=firstTarget-firstSource*step/period;
    if(!std::isfinite(newOrigin) || newOrigin<0) return R::failure(ErrorCode::invalidCommand,"Alignment places leading audio before project zero; adjust source downbeat or reference placement.");
    proposal.placementDeltaBeats=newOrigin-origin.value();
    proposal.targetBpm=project.tempoBpm/step;
    proposal.timingMap.markers.push_back({0,0});
    // One group map. Reference beats use its current source grid, stored edits
    // and placement; guide cadence is mapped to those beats without audio mutation.
    const double guideEnd=std::min(guide->trimEndSeconds,proposal.downbeatSeconds+180);
    const int beats=std::max(1,std::min(720,int(std::ceil((guideEnd-proposal.downbeatSeconds)/period))));
    for(int k=0;k<=beats;++k) {
      if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Sync cancelled.");
      const double source=firstSource+k*period;
      if(source==0) continue;
      double target=firstTarget+k;
      if(reference) {
        auto beat=sourceTimeToProjectBeat(project,*reference,referenceDownbeat+k*60/referenceBpm);
        if(!beat) return R::failure(beat.error().code,beat.error().message);
        target=beat.value();
      }
      proposal.timingMap.markers.push_back({source,target-newOrigin});
    }
    if(auto valid=validate(proposal.timingMap); !valid) return R::failure(valid.error().code,valid.error().message);
    for(const auto& track:project.tracks) if(track.timingGroupId==groupId) {
      auto memberOffset=sourceGridOffset(project,track);
      if(!memberOffset) return R::failure(memberOffset.error().code,memberOffset.error().message);
      double downbeat=firstSource-memberOffset.value();
      // Store the first equivalent beat within this member's source bounds.
      if(downbeat<track.trimStartSeconds) downbeat+=std::ceil((track.trimStartSeconds-downbeat)/period)*period;
      if(downbeat>=track.trimEndSeconds) return R::failure(ErrorCode::invalidCommand,"Linked member has no downbeat inside its trim; choose a longer trim or unlink it.");
      proposal.memberTiming.push_back({track.id,newOrigin+mapTime(proposal.timingMap,memberOffset.value()),downbeat});
    }
    auto checked=proposalSnapshot(project,proposal);
    if(!checked) return R::failure(checked.error().code,checked.error().message);
    return R::success(std::move(proposal));
  } catch(const std::exception& e) {
    return R::failure(ErrorCode::invalidTimingMap,"Sync alignment failed: "+std::string(e.what()));
  }
}
Result<SyncPreview> SyncService::preview(const Project& project,const SyncProposal& proposal,StretchRenderer& renderer,MediaStore& store,CancellationToken& token,const StretchOptions& options) {
  using R=Result<SyncPreview>;
  if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Sync preview cancelled.");
  auto snapshot=proposalSnapshot(project,proposal);
  if(!snapshot) return R::failure(snapshot.error().code,snapshot.error().message);
  auto original=renderer.prepareGroup(project,proposal.groupId,store,token,options);
  if(!original) return R::failure(original.error().code,original.error().message);
  auto proposed=renderer.prepareGroup(snapshot.value(),proposal.groupId,store,token,options);
  if(!proposed) return R::failure(proposed.error().code,proposed.error().message);
  if(token.isCancelled()) return R::failure(ErrorCode::cancelled,"Sync preview cancelled.");
  return R::success({project,std::move(snapshot.value()),std::move(original.value()),std::move(proposed.value())});
}
} // namespace jeff::daw
