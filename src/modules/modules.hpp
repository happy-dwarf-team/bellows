#pragma once

#include "../common.hpp"

#include "symbols.gen.hpp"  // sym::<name>, written by anvil gen from plugin/symbols.toml

namespace sb {

// Each Install* requests its changes through the Anvil host and logs the
// outcome. Anvil verified every symbol of plugin/symbols.toml before the DLL
// was loaded, and the changes are enabled together when load() returns.
// Returns true when the module is active. A refused change throws
// anvil::Error; the module's sb::Undo removes what it already requested, so a
// failed module leaves the game unchanged for that module only.
bool InstallMimalloc(anvil::Host& host, Mode mode);
bool InstallVerifyPathListing(anvil::Host& host, Mode mode);
bool InstallExistsIndex(anvil::Host& host, Mode mode);
bool InstallTriggerIntern(anvil::Host& host, Mode mode);
bool InstallMetascriptSubst(anvil::Host& host, Mode mode);
bool InstallOpenWarmup(anvil::Host& host, Mode mode);
bool InstallAttrCache(anvil::Host& host, Mode mode);
bool InstallRamPrefetch(anvil::Host& host, Mode mode);
void SetRamPrefetchBudget(std::uint64_t megabytes);  // before InstallRamPrefetch
bool InstallZipProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallTokenDictProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallTokenDictCache(anvil::Host& host, Mode mode);
bool InstallRangeProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallRangeWitness(anvil::Host& host, Mode mode);

// Stops learning and saves the access profile (after the game-ready event).
void FinishOpenWarmup();

// Stops prefetching, frees unused buffers and saves the archive-member profile (after the game-ready event).
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
void ReportRangeWitness();

}  // namespace sb
