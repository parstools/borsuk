#include <chrono>
#include <iostream>
#include <string_view>

#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/model/Validation.h"
#include "cli/Help.h"
#include "lr/CompressedParseTable.h"
#include "lr/LALRkDfa.h"
#include "lr/SelectiveLRkMerger.h"

namespace {
using Clock = std::chrono::steady_clock;

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

template<class Graph>
void report(const char *name, const Graph &graph, double seconds) {
    const zbik::ParseTable table(graph);
    if (table.hasConflicts()) {
        const auto stats = graph.statistics();
        std::cout << name << " states=" << stats.states
                  << " items=" << stats.items << " transitions=" << stats.transitions
                  << " conflicts=" << table.conflicts().size()
                  << " construction_seconds=" << seconds << '\n';
        return;
    }
    const zbik::CompressedParseTable packed(table);
    const auto stats = graph.statistics();
    std::cout << name << " states=" << stats.states << " items=" << stats.items
              << " transitions=" << stats.transitions
              << " conflicts=" << table.conflicts().size()
              << " table_bytes=" << packed.statistics().uncompressedBytes
              << " compressed_bytes=" << packed.statistics().compressedBytes
              << " dsl_bytes=" << packed.dumpDsl().size()
              << " construction_seconds=" << seconds << '\n';
}
}

int main(int argc, char **argv) {
    if (argc == 2 && (std::string_view{argv[1]} == "--help"
            || std::string_view{argv[1]} == "-h")) {
        agas::cli::printMergeReportHelp(std::cout);
        return 0;
    }
    if (argc > 2) {
        std::cerr << "usage: agas-merge-report [grammar.ag]\n";
        return 2;
    }
    try {
        const auto parsed = agas::bootstrap::parseAgasFile(
                argc == 2 ? argv[1] : AGAS_DEFAULT_GRAMMAR_FILE);
        if (!parsed.accepted()) throw std::runtime_error("invalid Agas source");
        const auto validation = agas::model::validateSyntaxModel(*parsed.document);
        if (!validation.valid()) throw std::runtime_error("invalid Agas model");
        const auto config = agas::generator::parserConfiguration(*parsed.document);
        const auto bnf = agas::model::lowerToBnf(*parsed.document);
        const auto start = Clock::now();
        const zbik::LRkDfa canonical(bnf.grammar(), config.lookahead);
        std::cout << "grammar=" << parsed.document->grammarName
                  << " k=" << config.lookahead
                  << " experiment=selective-merge\n";
        report("canonical", canonical, elapsed(start));
        const auto lalrStart = Clock::now();
        const zbik::LALRkDfa lalr(canonical);
        report("lalr", lalr, elapsed(lalrStart));
        for (auto mode : {zbik::SelectiveMergeMode::ExactActions,
                          zbik::SelectiveMergeMode::CompatibleUnion}) {
            const auto mergeStart = Clock::now();
            const auto merged = zbik::SelectiveLRkMerger::merge(canonical, {mode, 10000});
            report(mode == zbik::SelectiveMergeMode::ExactActions ? "exact" : "union",
                   merged.graph, elapsed(mergeStart));
            std::cout << "attempts=" << merged.statistics.attempts
                      << " used_lalr=" << merged.statistics.usedLalr
                      << " retained_canonical=" << merged.statistics.retainedCanonical
                      << " lalr_states=" << merged.statistics.lalrStates
                      << " lalr_conflicts=" << merged.statistics.lalrConflicts
                      << " committed=" << merged.statistics.committed
                      << " rejected=" << merged.statistics.rejected
                      << " budget_exhausted=" << merged.statistics.budgetExhausted
                      << " certificate=valid\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
