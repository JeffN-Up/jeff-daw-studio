#include "stems/SyncProposal.h"
#include <algorithm>
#include <cmath>
namespace jeff::daw {
namespace {
template <typename T> const T* find(const std::vector<T>& entities,const Id& id) {
  auto it=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.id==id;});
  return it==entities.end() ? nullptr : &*it;
}
bool sameMap(const TimingMap& a,const TimingMap& b) {
  if(a.markers.size()!=b.markers.size()) return false;
  for(std::size_t i=0;i<a.markers.size();++i) if(a.markers[i].sourceSeconds!=b.markers[i].sourceSeconds || a.markers[i].destinationBeats!=b.markers[i].destinationBeats) return false;
  return true;
}
bool sameTiming(const Project& a,const Project& b) {
  if(a.tempoBpm!=b.tempoBpm || a.tracks.size()!=b.tracks.size() || a.groups.size()!=b.groups.size() || a.assets.size()!=b.assets.size()) return false;
  for(const auto& t:a.tracks) {
    const auto* other=find(b.tracks,t.id);
    if(!other || t.assetId!=other->assetId || t.timingGroupId!=other->timingGroupId || t.placementBeats!=other->placementBeats ||
       t.trimStartSeconds!=other->trimStartSeconds || t.trimEndSeconds!=other->trimEndSeconds ||
       t.sourceBpm!=other->sourceBpm || t.downbeatSeconds!=other->downbeatSeconds) return false;
  }
  for(const auto& g:a.groups) {
    const auto* other=find(b.groups,g.id);
    if(!other || g.mode!=other->mode || !sameMap(g.timingMap,other->timingMap) ||
       g.syncReferenceTrackId!=other->syncReferenceTrackId || g.referenceUnavailable!=other->referenceUnavailable) return false;
  }
  for(const auto& asset:a.assets) {
    const auto* other=find(b.assets,asset.id);
    if(!other || asset.relativePath!=other->relativePath || asset.checksum!=other->checksum || asset.sourceRate!=other->sourceRate ||
       asset.channels!=other->channels || asset.frameCount!=other->frameCount) return false;
  }
  return true;
}
}
Result<Project> proposalSnapshot(const Project& project,const SyncProposal& proposal) {
  using R=Result<Project>;
  if(project.projectId!=proposal.projectId || project.revisionId!=proposal.sourceRevision || !sameTiming(project,proposal.sourceSnapshot))
    return R::failure(ErrorCode::invalidCommand,"Sync proposal is stale; analyze the current tempo, tracks and reference again.");
  if(proposal.requiresCorrection) return R::failure(ErrorCode::invalidCommand,"Sync requires explicit BPM and downbeat correction before audition or Apply.");
  if(!std::isfinite(proposal.sourceBpm) || proposal.sourceBpm<40 || proposal.sourceBpm>240)
    return R::failure(ErrorCode::invalidCommand,"Sync source BPM must be within 40–240.");
  if(auto valid=validate(project); !valid) return R::failure(valid.error().code,valid.error().message);
  auto snapshot=project;
  auto group=std::find_if(snapshot.groups.begin(),snapshot.groups.end(),[&](const auto& g){return g.id==proposal.groupId;});
  if(group==snapshot.groups.end()) return R::failure(ErrorCode::missingEntity,"Sync group no longer exists.");
  if(proposal.referenceTrackId && !find(snapshot.tracks,*proposal.referenceTrackId)) return R::failure(ErrorCode::missingEntity,"Sync reference no longer exists.");
  if(auto valid=validate(proposal.timingMap); !valid) return R::failure(valid.error().code,valid.error().message);
  group->timingMap=proposal.timingMap; group->mode=TimingMode::autoSync;
  group->syncReferenceTrackId=proposal.referenceTrackId.value_or(Id{}); group->referenceUnavailable=false;
  std::size_t count=0;
  for(auto& track:snapshot.tracks) if(track.timingGroupId==proposal.groupId) {
    const auto desired=std::find_if(proposal.memberTiming.begin(),proposal.memberTiming.end(),[&](const auto& member){return member.trackId==track.id;});
    if(desired==proposal.memberTiming.end() || !std::isfinite(desired->placementBeats) || desired->placementBeats<0)
      return R::failure(ErrorCode::invalidCommand,"Sync placement precedes project zero; adjust downbeat or reference placement.");
    track.placementBeats=desired->placementBeats; track.sourceBpm=proposal.sourceBpm; track.downbeatSeconds=desired->downbeatSeconds; ++count;
  }
  if(count==0 || count!=proposal.memberTiming.size()) return R::failure(ErrorCode::invalidCommand,"Sync proposal members do not match the timing group.");
  if(auto valid=validate(snapshot); !valid) return R::failure(valid.error().code,valid.error().message);
  return R::success(std::move(snapshot));
}
Result<void> applyProposal(const SyncProposal& proposal,ProjectHistory& history) {
  auto snapshot=proposalSnapshot(history.current(),proposal);
  if(!snapshot) return Result<void>::failure(snapshot.error().code,snapshot.error().message);
  return history.apply(std::move(snapshot.value()));
}
Result<void> removeTrackWithSyncRetention(const Id& id,ProjectHistory& history) {
  auto snapshot=history.current();
  auto track=std::find_if(snapshot.tracks.begin(),snapshot.tracks.end(),[&](const auto& t){return t.id==id;});
  if(track==snapshot.tracks.end()) return Result<void>::failure(ErrorCode::missingEntity,"Track does not exist.");
  snapshot.tracks.erase(track);
  for(auto& group:snapshot.groups) if(group.syncReferenceTrackId==id) group.referenceUnavailable=true;
  return history.apply(std::move(snapshot));
}
} // namespace jeff::daw
