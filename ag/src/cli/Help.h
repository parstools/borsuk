#pragma once

#include <iosfwd>

namespace agas::cli {

void printAgasHelp(std::ostream &output);
void printBootstrapHelp(std::ostream &output);
void printMergeReportHelp(std::ostream &output);

} // namespace agas::cli
