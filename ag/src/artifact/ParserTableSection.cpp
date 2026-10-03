#include "agas/artifact/ParserTableSection.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace agas::artifact {
namespace {

enum class TokenKind {
  Word,
  String,
  UnsignedInteger,
  LeftBrace,
  RightBrace,
  LeftBracket,
  RightBracket,
  Comma,
  Semicolon,
  Arrow,
  End,
};

struct Token {
  TokenKind kind{};
  std::string text;
  std::size_t offset{};
};

[[noreturn]] void fail(ParserTableSectionErrorCode code, std::size_t offset,
                       std::string message) {
  throw ParserTableSectionError{code, offset, std::move(message)};
}

class Lexer {
public:
  explicit Lexer(std::string_view input) : input_(input) {}

  auto next() -> Token {
    while (offset_ < input_.size() &&
           (input_[offset_] == ' ' || input_[offset_] == '\t' ||
            input_[offset_] == '\r' || input_[offset_] == '\n'))
      ++offset_;
    if (offset_ == input_.size())
      return {TokenKind::End, {}, offset_};

    const std::size_t start = offset_;
    const char character = input_[offset_++];
    switch (character) {
    case '{':
      return {TokenKind::LeftBrace, "{", start};
    case '}':
      return {TokenKind::RightBrace, "}", start};
    case '[':
      return {TokenKind::LeftBracket, "[", start};
    case ']':
      return {TokenKind::RightBracket, "]", start};
    case ',':
      return {TokenKind::Comma, ",", start};
    case ';':
      return {TokenKind::Semicolon, ";", start};
    case '=':
      if (offset_ < input_.size() && input_[offset_] == '>') {
        ++offset_;
        return {TokenKind::Arrow, "=>", start};
      }
      break;
    case '"':
      return stringToken(start);
    default:
      if (character >= '0' && character <= '9') {
        while (offset_ < input_.size() && input_[offset_] >= '0' &&
               input_[offset_] <= '9')
          ++offset_;
        return {TokenKind::UnsignedInteger,
                std::string{input_.substr(start, offset_ - start)}, start};
      }
      if ((character >= 'A' && character <= 'Z') ||
          (character >= 'a' && character <= 'z')) {
        while (offset_ < input_.size()) {
          const char next = input_[offset_];
          if (!((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z') ||
                next == '-'))
            break;
          ++offset_;
        }
        return {TokenKind::Word,
                std::string{input_.substr(start, offset_ - start)}, start};
      }
      break;
    }
    fail(ParserTableSectionErrorCode::InvalidDsl, start,
         "invalid parser-table character");
  }

private:
  auto stringToken(std::size_t start) -> Token {
    bool escaped = false;
    while (offset_ < input_.size()) {
      const auto character = static_cast<unsigned char>(input_[offset_++]);
      if (character < 0x20U)
        fail(ParserTableSectionErrorCode::InvalidDsl, offset_ - 1,
             "unescaped control character in string");
      if (!escaped && character == '"') {
        const std::string encoded{input_.substr(start, offset_ - start)};
        try {
          const auto value = nlohmann::json::parse(encoded);
          if (!value.is_string())
            fail(ParserTableSectionErrorCode::InvalidDsl, start,
                 "invalid parser-table string");
          return {TokenKind::String, value.get<std::string>(), start};
        } catch (const nlohmann::json::exception &) {
          fail(ParserTableSectionErrorCode::InvalidDsl, start,
               "invalid JSON string in parser table");
        }
      }
      if (!escaped && character == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
    }
    fail(ParserTableSectionErrorCode::InvalidDsl, start,
         "unterminated parser-table string");
  }

  std::string_view input_;
  std::size_t offset_{};
};

class Parser {
public:
  Parser(std::string_view input, const ArtifactSymbols &symbols,
         const ArtifactLoadLimits &loadLimits,
         const ParserTableSectionLimits &limits)
      : lexer_(input), symbols_(symbols), loadLimits_(loadLimits),
        limits_(limits), current_(lexer_.next()) {
    for (const auto &terminal : symbols.terminals)
      terminals_.emplace(terminal.name, terminal.id);
    for (const auto &nonterminal : symbols.nonterminals)
      nonterminals_.emplace(nonterminal.name, nonterminal.id);
  }

  auto parse() -> ArtifactParserTable {
    expectWord("compressed-table");
    const Token parserName = take(TokenKind::String, "parser name");
    ArtifactParserTable result;
    parseParserName(parserName, result);
    if (result.lookahead > loadLimits_.maximumLookahead)
      fail(ParserTableSectionErrorCode::ResourceLimit, parserName.offset,
           "parser lookahead exceeds configured limit");
    lookaheadLimit_ = result.lookahead;
    take(TokenKind::LeftBrace, "`{`");
    expectWord("start-state");
    result.startState = unsignedInteger();
    take(TokenKind::Semicolon, "`;`");

    while (isWord("action-row")) {
      result.actionRows.push_back(actionRow());
      checkRowCount(result.actionRows.size());
    }
    if (result.actionRows.empty())
      syntax("at least one action row");
    expectWord("action-state-rows");
    result.actionStateRows = indices();
    take(TokenKind::Semicolon, "`;`");

    while (isWord("goto-row")) {
      result.gotoRows.push_back(gotoRow());
      checkRowCount(result.gotoRows.size());
    }
    if (result.gotoRows.empty())
      syntax("at least one goto row");
    expectWord("goto-state-rows");
    result.gotoStateRows = indices();
    take(TokenKind::Semicolon, "`;`");
    take(TokenKind::RightBrace, "`}`");
    take(TokenKind::End, "end of parser table");
    return result;
  }

private:
  void parseParserName(const Token &token, ArtifactParserTable &result) {
    if (token.text == "SLR") {
      result.algorithm = ArtifactParserAlgorithm::Slr;
      result.lookahead = 1;
      return;
    }
    const auto parseParameterized = [&](std::string_view prefix,
                                        ArtifactParserAlgorithm algorithm) {
      if (!token.text.starts_with(prefix) || token.text.back() != ')')
        return false;
      const std::string_view number{token.text.data() + prefix.size(),
                                    token.text.size() - prefix.size() - 1};
      if (number.empty() || (number.size() > 1 && number.front() == '0'))
        fail(ParserTableSectionErrorCode::InvalidAlgorithm, token.offset,
             "invalid parser lookahead");
      std::uint32_t value{};
      const auto [end, error] =
          std::from_chars(number.data(), number.data() + number.size(), value);
      if (error != std::errc{} || end != number.data() + number.size() ||
          value == 0)
        fail(ParserTableSectionErrorCode::InvalidAlgorithm, token.offset,
             "invalid parser lookahead");
      result.algorithm = algorithm;
      result.lookahead = value;
      return true;
    };
    if (parseParameterized("LR(", ArtifactParserAlgorithm::Lr) ||
        parseParameterized("LALR(", ArtifactParserAlgorithm::Lalr))
      return;
    fail(ParserTableSectionErrorCode::InvalidAlgorithm, token.offset,
         "parser must be LR(k), LALR(k), or SLR");
  }

  auto actionRow() -> ArtifactActionRow {
    expectWord("action-row");
    ArtifactActionRow row;
    row.id = unsignedInteger();
    take(TokenKind::LeftBrace, "`{`");
    while (current_.kind == TokenKind::LeftBracket) {
      ArtifactActionEntry entry;
      entry.lookahead = lookahead();
      take(TokenKind::Arrow, "`=>`");
      entry.action = action();
      take(TokenKind::Semicolon, "`;`");
      row.entries.push_back(std::move(entry));
      checkEntryCount();
    }
    expectWord("any");
    take(TokenKind::Arrow, "`=>`");
    if (isWord("reduce")) {
      advance();
      row.fallback = ArtifactReduce{unsignedInteger()};
    } else {
      expectWord("error");
    }
    take(TokenKind::Semicolon, "`;`");
    take(TokenKind::RightBrace, "`}`");
    return row;
  }

  auto action() -> ArtifactParserAction {
    if (isWord("shift")) {
      advance();
      return ArtifactShift{unsignedInteger()};
    }
    if (isWord("reduce")) {
      advance();
      return ArtifactReduce{unsignedInteger()};
    }
    if (isWord("accept")) {
      advance();
      return ArtifactAccept{};
    }
    syntax("shift, reduce, or accept action");
  }

  auto lookahead() -> ArtifactLookaheadWord {
    ArtifactLookaheadWord result;
    take(TokenKind::LeftBracket, "`[`");
    result.push_back(lookaheadSymbol());
    checkLookaheadCount(result.size());
    while (current_.kind == TokenKind::Comma) {
      advance();
      result.push_back(lookaheadSymbol());
      checkLookaheadCount(result.size());
    }
    take(TokenKind::RightBracket, "`]`");
    return result;
  }

  auto lookaheadSymbol() -> ArtifactLookaheadSymbol {
    if (current_.kind == TokenKind::String) {
      const Token name = current_;
      advance();
      if (name.text.size() > limits_.maximumNameBytes)
        fail(ParserTableSectionErrorCode::ResourceLimit, name.offset,
             "terminal name exceeds configured limit");
      const auto found = terminals_.find(name.text);
      if (found == terminals_.end())
        fail(ParserTableSectionErrorCode::InvalidReference, name.offset,
             "unknown terminal in lookahead");
      return {found->second};
    }
    if (isWord("EOF")) {
      advance();
      return {std::nullopt};
    }
    syntax("terminal name or EOF");
  }

  auto gotoRow() -> ArtifactGotoRow {
    expectWord("goto-row");
    ArtifactGotoRow row;
    row.id = unsignedInteger();
    take(TokenKind::LeftBrace, "`{`");
    while (current_.kind == TokenKind::String) {
      const Token name = current_;
      advance();
      if (name.text.size() > limits_.maximumNameBytes)
        fail(ParserTableSectionErrorCode::ResourceLimit, name.offset,
             "nonterminal name exceeds configured limit");
      const auto found = nonterminals_.find(name.text);
      if (found == nonterminals_.end())
        fail(ParserTableSectionErrorCode::InvalidReference, name.offset,
             "unknown nonterminal in goto row");
      take(TokenKind::Arrow, "`=>`");
      row.entries.push_back({found->second, unsignedInteger()});
      checkEntryCount();
      take(TokenKind::Semicolon, "`;`");
    }
    take(TokenKind::RightBrace, "`}`");
    return row;
  }

  auto indices() -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> result;
    take(TokenKind::LeftBracket, "`[`");
    result.push_back(unsignedInteger());
    checkStateCount(result.size());
    while (current_.kind == TokenKind::Comma) {
      advance();
      result.push_back(unsignedInteger());
      checkStateCount(result.size());
    }
    take(TokenKind::RightBracket, "`]`");
    return result;
  }

  auto unsignedInteger() -> std::uint32_t {
    const Token token = take(TokenKind::UnsignedInteger, "unsigned integer");
    if (token.text.size() > 1 && token.text.front() == '0')
      fail(ParserTableSectionErrorCode::InvalidDsl, token.offset,
           "unsigned integer has a leading zero");
    std::uint32_t result{};
    const auto [end, error] = std::from_chars(
        token.text.data(), token.text.data() + token.text.size(), result);
    if (error != std::errc{} || end != token.text.data() + token.text.size())
      fail(ParserTableSectionErrorCode::InvalidId, token.offset,
           "unsigned integer exceeds u32");
    return result;
  }

  auto isWord(std::string_view word) const -> bool {
    return current_.kind == TokenKind::Word && current_.text == word;
  }

  void expectWord(std::string_view word) {
    if (!isWord(word))
      syntax("`" + std::string{word} + "`");
    advance();
  }

  auto take(TokenKind kind, std::string_view expected) -> Token {
    if (current_.kind != kind)
      syntax(expected);
    Token result = std::move(current_);
    advance();
    return result;
  }

  [[noreturn]] void syntax(std::string_view expected) const {
    fail(ParserTableSectionErrorCode::InvalidDsl, current_.offset,
         "expected " + std::string{expected});
  }

  void advance() { current_ = lexer_.next(); }

  void checkRowCount(std::size_t count) const {
    if (count > limits_.maximumRows)
      fail(ParserTableSectionErrorCode::ResourceLimit, current_.offset,
           "parser-table row count exceeds configured limit");
  }

  void checkEntryCount() {
    if (++entryCount_ > limits_.maximumEntries)
      fail(ParserTableSectionErrorCode::ResourceLimit, current_.offset,
           "parser-table entry count exceeds configured limit");
  }

  void checkStateCount(std::size_t count) const {
    if (count > limits_.maximumStates)
      fail(ParserTableSectionErrorCode::ResourceLimit, current_.offset,
           "parser state count exceeds configured limit");
  }

  void checkLookaheadCount(std::size_t count) const {
    if (count > lookaheadLimit_)
      fail(ParserTableSectionErrorCode::InvalidReference, current_.offset,
           "lookahead word exceeds parser lookahead");
  }

  Lexer lexer_;
  const ArtifactSymbols &symbols_;
  const ArtifactLoadLimits &loadLimits_;
  const ParserTableSectionLimits &limits_;
  Token current_;
  std::unordered_map<std::string, std::uint32_t> terminals_;
  std::unordered_map<std::string, std::uint32_t> nonterminals_;
  std::uint32_t lookaheadLimit_{};
  std::uint64_t entryCount_{};
};

auto parserName(const ArtifactParserTable &table) -> std::string {
  switch (table.algorithm) {
  case ArtifactParserAlgorithm::Lr:
    return "LR(" + std::to_string(table.lookahead) + ')';
  case ArtifactParserAlgorithm::Lalr:
    return "LALR(" + std::to_string(table.lookahead) + ')';
  case ArtifactParserAlgorithm::Slr:
    return "SLR";
  }
  throw std::logic_error("unknown artifact parser algorithm");
}

auto quote(std::string_view value) -> std::string {
  constexpr char hex[] = "0123456789abcdef";
  std::ostringstream out;
  out << '"';
  for (const unsigned char character : value) {
    switch (character) {
    case '\\':
      out << "\\\\";
      break;
    case '"':
      out << "\\\"";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (character < 0x20U)
        out << "\\u00" << hex[character >> 4U] << hex[character & 0x0FU];
      else
        out << static_cast<char>(character);
      break;
    }
  }
  out << '"';
  return out.str();
}

void dumpAction(std::ostringstream &out, const ArtifactParserAction &action) {
  std::visit(
      [&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ArtifactShift>)
          out << "shift " << value.state;
        else if constexpr (std::is_same_v<T, ArtifactReduce>)
          out << "reduce " << value.production;
        else
          out << "accept";
      },
      action);
}

auto lookaheadSymbolKey(const ArtifactLookaheadSymbol &symbol)
    -> std::uint64_t {
  return symbol.terminal
             ? *symbol.terminal
             : std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1U;
}

auto lookaheadLess(const ArtifactActionEntry *left,
                   const ArtifactActionEntry *right) -> bool {
  return std::lexicographical_compare(
      left->lookahead.begin(), left->lookahead.end(), right->lookahead.begin(),
      right->lookahead.end(),
      [](const auto &leftSymbol, const auto &rightSymbol) {
        return lookaheadSymbolKey(leftSymbol) < lookaheadSymbolKey(rightSymbol);
      });
}

} // namespace

ParserTableSectionError::ParserTableSectionError(
    ParserTableSectionErrorCode code, std::size_t offset, std::string message)
    : std::runtime_error(std::move(message) + " at byte " +
                         std::to_string(offset)),
      code_(code), offset_(offset) {}

auto ParserTableSectionError::code() const noexcept
    -> ParserTableSectionErrorCode {
  return code_;
}

auto ParserTableSectionError::offset() const noexcept -> std::size_t {
  return offset_;
}

void validateParserTableSection(const ArtifactParserTable &table,
                                const ArtifactSymbols &symbols,
                                const ArtifactProductions &productions,
                                const ArtifactLoadLimits &loadLimits,
                                const ParserTableSectionLimits &sectionLimits) {
  if (table.lookahead == 0 || table.lookahead > loadLimits.maximumLookahead ||
      (table.algorithm == ArtifactParserAlgorithm::Slr && table.lookahead != 1))
    fail(ParserTableSectionErrorCode::InvalidAlgorithm, 0,
         "invalid parser algorithm or lookahead");
  const std::size_t stateCount = table.actionStateRows.size();
  if (stateCount == 0 || stateCount != table.gotoStateRows.size() ||
      stateCount > sectionLimits.maximumStates)
    fail(stateCount > sectionLimits.maximumStates
             ? ParserTableSectionErrorCode::ResourceLimit
             : ParserTableSectionErrorCode::InvalidId,
         0, "ACTION and GOTO state mappings must have equal nonzero size");
  if (table.actionRows.empty() || table.gotoRows.empty() ||
      table.actionRows.size() > sectionLimits.maximumRows ||
      table.gotoRows.size() > sectionLimits.maximumRows)
    fail((table.actionRows.size() > sectionLimits.maximumRows ||
          table.gotoRows.size() > sectionLimits.maximumRows)
             ? ParserTableSectionErrorCode::ResourceLimit
             : ParserTableSectionErrorCode::InvalidId,
         0, "parser table must have nonempty bounded row pools");
  if (table.startState >= stateCount)
    fail(ParserTableSectionErrorCode::InvalidReference, 0,
         "start state is outside parser states");

  std::uint64_t entryCount = 0;
  for (std::size_t rowIndex = 0; rowIndex < table.actionRows.size();
       ++rowIndex) {
    const auto &row = table.actionRows[rowIndex];
    if (row.id != rowIndex)
      fail(ParserTableSectionErrorCode::InvalidId, 0,
           "action row identifiers must be dense and ordered");
    if (row.fallback &&
        row.fallback->production >= productions.productions.size())
      fail(ParserTableSectionErrorCode::InvalidReference, 0,
           "default reduction references an unknown production");
    entryCount += row.entries.size();
    if (entryCount > sectionLimits.maximumEntries)
      fail(ParserTableSectionErrorCode::ResourceLimit, 0,
           "parser table entries exceed configured limit");
    std::set<std::vector<std::uint64_t>> keys;
    for (const auto &entry : row.entries) {
      if (entry.lookahead.empty() || entry.lookahead.size() > table.lookahead)
        fail(ParserTableSectionErrorCode::InvalidReference, 0,
             "lookahead word has invalid length");
      std::vector<std::uint64_t> key;
      key.reserve(entry.lookahead.size());
      bool sawEof = false;
      for (std::size_t index = 0; index < entry.lookahead.size(); ++index) {
        const auto terminal = entry.lookahead[index].terminal;
        if (!terminal) {
          if (index + 1 != entry.lookahead.size())
            fail(ParserTableSectionErrorCode::InvalidReference, 0,
                 "EOF may occur only at the end of lookahead");
          sawEof = true;
          key.push_back(
              std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1U);
        } else {
          if (*terminal >= symbols.terminals.size())
            fail(ParserTableSectionErrorCode::InvalidReference, 0,
                 "lookahead references an unknown terminal");
          key.push_back(*terminal);
        }
      }
      if (entry.lookahead.size() < table.lookahead && !sawEof)
        fail(ParserTableSectionErrorCode::InvalidReference, 0,
             "short lookahead must end with EOF");
      if (!keys.insert(std::move(key)).second)
        fail(ParserTableSectionErrorCode::DuplicateEntry, 0,
             "action row contains a duplicate lookahead");
      std::visit(
          [&](const auto &action) {
            using T = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<T, ArtifactShift>) {
              if (action.state >= stateCount ||
                  !entry.lookahead.front().terminal)
                fail(ParserTableSectionErrorCode::InvalidReference, 0,
                     "shift action has an invalid target or EOF lookahead");
            } else if constexpr (std::is_same_v<T, ArtifactReduce>) {
              if (action.production >= productions.productions.size())
                fail(ParserTableSectionErrorCode::InvalidReference, 0,
                     "reduction references an unknown production");
            } else if (!sawEof) {
              fail(ParserTableSectionErrorCode::InvalidReference, 0,
                   "accept action requires an EOF lookahead");
            }
          },
          entry.action);
    }
  }
  for (const std::uint32_t row : table.actionStateRows)
    if (row >= table.actionRows.size())
      fail(ParserTableSectionErrorCode::InvalidReference, 0,
           "state references an unknown action row");

  for (std::size_t rowIndex = 0; rowIndex < table.gotoRows.size(); ++rowIndex) {
    const auto &row = table.gotoRows[rowIndex];
    if (row.id != rowIndex)
      fail(ParserTableSectionErrorCode::InvalidId, 0,
           "goto row identifiers must be dense and ordered");
    entryCount += row.entries.size();
    if (entryCount > sectionLimits.maximumEntries)
      fail(ParserTableSectionErrorCode::ResourceLimit, 0,
           "parser table entries exceed configured limit");
    std::set<std::uint32_t> keys;
    for (const auto &entry : row.entries) {
      if (entry.nonterminal >= symbols.nonterminals.size() ||
          entry.state >= stateCount)
        fail(ParserTableSectionErrorCode::InvalidReference, 0,
             "goto entry references an unknown symbol or state");
      if (!keys.insert(entry.nonterminal).second)
        fail(ParserTableSectionErrorCode::DuplicateEntry, 0,
             "goto row contains a duplicate nonterminal");
    }
  }
  for (const std::uint32_t row : table.gotoStateRows)
    if (row >= table.gotoRows.size())
      fail(ParserTableSectionErrorCode::InvalidReference, 0,
           "state references an unknown goto row");
}

auto dumpParserTableDsl(const ArtifactParserTable &table,
                        const ArtifactSymbols &symbols,
                        const ArtifactProductions &productions) -> std::string {
  validateParserTableSection(table, symbols, productions);
  std::ostringstream out;
  out << "compressed-table " << quote(parserName(table)) << " {\n"
      << "  start-state " << table.startState << ";\n\n";
  for (const auto &row : table.actionRows) {
    out << "  action-row " << row.id << " {\n";
    std::vector<const ArtifactActionEntry *> entries;
    entries.reserve(row.entries.size());
    for (const auto &entry : row.entries)
      entries.push_back(&entry);
    std::ranges::sort(entries, lookaheadLess);
    for (const auto *entry : entries) {
      out << "    [";
      for (std::size_t index = 0; index < entry->lookahead.size(); ++index) {
        if (index != 0)
          out << ", ";
        const auto terminal = entry->lookahead[index].terminal;
        out << (terminal ? quote(symbols.terminals.at(*terminal).name) : "EOF");
      }
      out << "] => ";
      dumpAction(out, entry->action);
      out << ";\n";
    }
    out << "    any => ";
    if (row.fallback)
      out << "reduce " << row.fallback->production;
    else
      out << "error";
    out << ";\n  }\n";
  }
  out << "\n  action-state-rows [";
  for (std::size_t index = 0; index < table.actionStateRows.size(); ++index) {
    if (index != 0)
      out << ", ";
    out << table.actionStateRows[index];
  }
  out << "];\n\n";
  for (const auto &row : table.gotoRows) {
    out << "  goto-row " << row.id << " {\n";
    std::vector<const ArtifactGotoEntry *> entries;
    entries.reserve(row.entries.size());
    for (const auto &entry : row.entries)
      entries.push_back(&entry);
    std::ranges::sort(entries, {}, &ArtifactGotoEntry::nonterminal);
    for (const auto *entry : entries)
      out << "    " << quote(symbols.nonterminals.at(entry->nonterminal).name)
          << " => " << entry->state << ";\n";
    out << "  }\n";
  }
  out << "\n  goto-state-rows [";
  for (std::size_t index = 0; index < table.gotoStateRows.size(); ++index) {
    if (index != 0)
      out << ", ";
    out << table.gotoStateRows[index];
  }
  out << "];\n}\n";
  return out.str();
}

auto parseParserTableDsl(std::string_view text, const ArtifactSymbols &symbols,
                         const ArtifactProductions &productions,
                         const ArtifactLoadLimits &loadLimits,
                         const ParserTableSectionLimits &sectionLimits)
    -> ArtifactParserTable {
  if (text.size() > loadLimits.maximumSectionBytes)
    fail(ParserTableSectionErrorCode::ResourceLimit, 0,
         "parser-table section exceeds configured byte limit");
  ArtifactParserTable result =
      Parser{text, symbols, loadLimits, sectionLimits}.parse();
  validateParserTableSection(result, symbols, productions, loadLimits,
                             sectionLimits);
  return result;
}

} // namespace agas::artifact
