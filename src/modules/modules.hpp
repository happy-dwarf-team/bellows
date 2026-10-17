#pragma once

#include "../build.hpp"
#include "../common.hpp"

namespace sb {

// Each Install* checks its preconditions (expected bytes / targets), applies
// its patches through the Anvil host and logs the outcome. Returns true when
// the module is active. A refused or failed install leaves the game unchanged
// for that module.
bool InstallMimalloc(const Build& build, Mode mode);
bool InstallVerifyPathListing(const Build& build, Mode mode);
bool InstallExistsIndex(const Build& build, Mode mode);
bool InstallTriggerIntern(const Build& build, Mode mode);
bool InstallMetascriptSubst(const Build& build, Mode mode);
bool InstallOpenWarmup(const Build& build, Mode mode);
bool InstallAttrCache(const Build& build, Mode mode);
bool InstallRamPrefetch(const Build& build, Mode mode);
void SetRamPrefetchBudget(std::uint64_t megabytes);  // before InstallRamPrefetch
bool InstallZipProfile(const Build& build, Mode mode);  // diagnostic
bool InstallTokenDictProfile(const Build& build, Mode mode);  // diagnostic
bool InstallTokenDictCache(const Build& build, Mode mode);
bool InstallRangeProfile(const Build& build, Mode mode);  // diagnostic

// Stops learning and saves the access profile (call on the game-ready event).
void FinishOpenWarmup();

// Stops prefetching, frees unused buffers and saves the archive-member profile (game-ready event).
void FinishRamPrefetch();

// Writes the counters of every installed module to the log.
void ReportMimalloc();
void ReportVerifyPathListing();
void ReportExistsIndex();
void ReportTriggerIntern();
void ReportMetascriptSubst();
void ReportOpenWarmup();
void ReportAttrCache();
void ReportRamPrefetch();
void ReportZipProfile();
void ReportTokenDictProfile();
void ReportTokenDictCache();
void ReportRangeProfile();

}  // namespace sb
