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

// Writes the counters of every installed module to the log.
void ReportMimalloc();
void ReportVerifyPathListing();
void ReportExistsIndex();
void ReportTriggerIntern();
void ReportMetascriptSubst();

}  // namespace sb
