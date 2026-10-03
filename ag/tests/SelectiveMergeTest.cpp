#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

#include "AgLexer.h"
#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/ParserGeneration.h"
#include "lr/CompressedParseTable.h"
#include "lr/LRMachine.h"
#include "lr/SelectiveLRkMerger.h"
#include "SelectiveMergeTestSupport.h"

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        const auto document = agas::bootstrap::parseAgasFile(AGAS_TEST_GRAMMAR_FILE);
        require(document.accepted(), "cannot read Ag.ag");
        const auto model = agas::model::lowerToBnf(*document.document);
        const auto config = agas::generator::parserConfiguration(*document.document);
        const zbik::LRkDfa canonical(model.grammar(), config.lookahead);
        const auto merged = zbik::SelectiveLRkMerger::merge(canonical);
        const zbik::ParseTable source(canonical), target(merged.graph);
        require(merged.graph.states().size() < canonical.states().size(),
                "Ag.ag should benefit from selective merging");
        require(zbik::CompressedParseTable(target).statistics().compressedBytes <
                        zbik::CompressedParseTable(source).statistics().compressedBytes,
                "Ag.ag should produce a smaller packed table");

        const auto root = std::filesystem::path(AGAS_TEST_GRAMMAR_FILE).parent_path();
        std::vector<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ag") {
                files.push_back(entry.path());
            }
        }
        std::ranges::sort(files);
        std::mt19937 random(20260920);
        std::size_t positives = 0, mutations = 0, rejected = 0, maxDelay = 0;
        const auto compare = [&](const std::vector<zbik::TerminalId> &word) {
            const auto a = selective_test::run(source, word);
            const auto b = selective_test::run(target, word);
            require(a.accepted == b.accepted, "canonical/selective language mismatch");
            if (a.accepted) {
                require(a.reductions == b.reductions, "reduction trace mismatch");
            } else {
                ++rejected;
                require(b.offset >= a.offset, "merged parser failed before canonical parser");
                maxDelay = std::max(maxDelay, b.offset - a.offset);
            }
        };
        for (const auto &file : files) {
            std::ifstream input(file);
            const std::string text{std::istreambuf_iterator<char>(input), {}};
            antlr4::ANTLRInputStream stream(text);
            AgLexer lexer(&stream);
            std::vector<zbik::TerminalId> word;
            for (const auto &token : lexer.getAllTokens()) {
                const auto name = lexer.getVocabulary().getSymbolicName(token->getType());
                const auto terminal = model.grammar().findTerminal(name);
                require(terminal.has_value(), "ANTLR token has no BNF binding");
                word.push_back(*terminal);
            }
            require(selective_test::run(source, word).accepted,
                    "corpus .ag file rejected by canonical Ag.ag parser");
            require(zbik::LRMachine(target).parse(word).accepted,
                    "production LRMachine rejected an Agas fixture");
            compare(word);
            ++positives;
            for (std::size_t n = 0; n < 200 && !word.empty(); ++n) {
                const std::size_t pos = random() % word.size();
                const zbik::TerminalId terminal{
                        static_cast<std::uint32_t>(random() % model.grammar().terminalCount())};
                auto changed = word;
                if (n % 4 == 0) changed.erase(changed.begin() + pos);
                if (n % 4 == 1) changed.insert(changed.begin() + pos, terminal);
                if (n % 4 == 2) changed[pos] = terminal;
                if (n % 4 == 3) changed.resize(pos);
                compare(changed);
                ++mutations;
            }
        }
        require(positives >= 11 && rejected > 0, "insufficient corpus coverage");
        std::cout << "Ag.ag positives=" << positives << " mutations=" << mutations
                  << " rejected=" << rejected << " max_error_delay_tokens=" << maxDelay
                  << " mismatches=0\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
