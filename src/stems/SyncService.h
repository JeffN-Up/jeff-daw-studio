#pragma once
#include "stems/SyncProposal.h"
namespace jeff::daw {
class SyncService {
public:
  explicit SyncService(BeatAnalyzer& analyzer) : analyzer_(analyzer) {}
  Result<SyncProposal> propose(const Project&, Id group, std::optional<Id> reference,
                               std::optional<double> manualBpm, CancellationToken&);
  Result<SyncProposal> propose(const Project&, Id group, std::optional<Id> reference,
                               const SyncOptions&, CancellationToken&);
  Result<SyncPreview> preview(const Project&, const SyncProposal&, StretchRenderer&,
                             MediaStore&, CancellationToken&, const StretchOptions& = {});
private:
  BeatAnalyzer& analyzer_;
};
} // namespace jeff::daw
