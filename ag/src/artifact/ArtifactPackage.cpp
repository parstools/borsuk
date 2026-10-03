#include "agas/artifact/ArtifactPackage.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

#include "agas/artifact/Sha256.h"

namespace agas::artifact {
namespace {

[[noreturn]] void fail(ArtifactPackageErrorCode code, std::string message) {
  throw ArtifactPackageError{code, std::move(message)};
}

auto sectionBytes(const ArtifactPackageSections &sections,
                  ArtifactSectionKind kind) -> const std::string * {
  switch (kind) {
  case ArtifactSectionKind::Symbols:
    return &sections.symbols;
  case ArtifactSectionKind::Lexer:
    return &sections.lexer;
  case ArtifactSectionKind::ParserTable:
    return &sections.parserTable;
  case ArtifactSectionKind::Productions:
    return &sections.productions;
  case ArtifactSectionKind::Reductions:
    return &sections.reductions;
  case ArtifactSectionKind::AstSchema:
    return &sections.astSchema;
  case ArtifactSectionKind::Diagnostics:
    return sections.diagnostics ? &*sections.diagnostics : nullptr;
  }
  return nullptr;
}

auto mutableSectionBytes(ArtifactPackageSections &sections,
                         ArtifactSectionKind kind) -> std::string * {
  switch (kind) {
  case ArtifactSectionKind::Symbols:
    return &sections.symbols;
  case ArtifactSectionKind::Lexer:
    return &sections.lexer;
  case ArtifactSectionKind::ParserTable:
    return &sections.parserTable;
  case ArtifactSectionKind::Productions:
    return &sections.productions;
  case ArtifactSectionKind::Reductions:
    return &sections.reductions;
  case ArtifactSectionKind::AstSchema:
    return &sections.astSchema;
  case ArtifactSectionKind::Diagnostics:
    sections.diagnostics.emplace();
    return &*sections.diagnostics;
  }
  return nullptr;
}

auto descriptor(ArtifactSectionKind kind, const std::string &bytes)
    -> ArtifactSectionDescriptor {
  return {kind,
          kind == ArtifactSectionKind::Lexer ? nlohmann::json::parse(bytes).at("version").get<std::uint32_t>() : 1,
          std::string{sectionFileName(kind)},
          kind != ArtifactSectionKind::Diagnostics,
          bytes.size(),
          sha256Hex(bytes)};
}

auto algorithmName(ArtifactParserAlgorithm algorithm) -> std::string_view {
  switch (algorithm) {
  case ArtifactParserAlgorithm::Lr:
    return "canonical-lr";
  case ArtifactParserAlgorithm::Lalr:
    return "lalr";
  case ArtifactParserAlgorithm::Slr:
    return "slr";
  }
  fail(ArtifactPackageErrorCode::InconsistentSections,
       "unknown parser-table algorithm");
}

void verifyDescriptor(const ArtifactSectionDescriptor &descriptor,
                      const std::string *bytes) {
  if (bytes == nullptr)
    fail(ArtifactPackageErrorCode::MissingSection,
         "artifact package is missing " +
             std::string{sectionKindName(descriptor.kind)});
  if (bytes->size() != descriptor.byteLength)
    fail(ArtifactPackageErrorCode::LengthMismatch,
         "artifact section length differs from manifest: " + descriptor.file);
  if (sha256Hex(*bytes) != descriptor.sha256)
    fail(ArtifactPackageErrorCode::HashMismatch,
         "artifact section hash differs from manifest: " + descriptor.file);
}

auto readRegularFile(const std::filesystem::path &path,
                     std::uint64_t maximumBytes,
                     std::optional<std::uint64_t> exactBytes = std::nullopt)
    -> std::string {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error)
    fail(ArtifactPackageErrorCode::IoError,
         "cannot inspect artifact file: " + path.string());
  if (std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status))
    fail(ArtifactPackageErrorCode::UnsafeFile,
         "artifact section is not a regular nonsymlink file: " + path.string());
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error)
    fail(ArtifactPackageErrorCode::IoError,
         "cannot determine artifact file size: " + path.string());
  if (size > maximumBytes || size > std::numeric_limits<std::size_t>::max())
    fail(ArtifactPackageErrorCode::ResourceLimit,
         "artifact file exceeds configured limit: " + path.string());
  if (exactBytes && size != *exactBytes)
    fail(ArtifactPackageErrorCode::LengthMismatch,
         "artifact file length differs from manifest: " + path.string());
  std::ifstream input(path, std::ios::binary);
  if (!input)
    fail(ArtifactPackageErrorCode::IoError,
         "cannot open artifact file: " + path.string());
  std::string result(static_cast<std::size_t>(size), '\0');
  if (!result.empty())
    input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input || input.peek() != std::char_traits<char>::eof())
    fail(ArtifactPackageErrorCode::IoError,
         "cannot read complete artifact file: " + path.string());
  return result;
}

void writeFile(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary);
  if (!output)
    fail(ArtifactPackageErrorCode::IoError,
         "cannot create artifact file: " + path.string());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output)
    fail(ArtifactPackageErrorCode::IoError,
         "cannot write artifact file: " + path.string());
}

void requireCanonical(bool condition, std::string_view section) {
  if (!condition)
    fail(ArtifactPackageErrorCode::InconsistentSections,
         "artifact section is not canonical: " + std::string{section});
}

void validateContext(const ArtifactLexer &lexer, const ArtifactParserTable &table,
                     const ArtifactProductions &productions) {
  if (!lexer.context) return;
  const auto &context = *lexer.context;
  if (context.rows.size() != table.actionStateRows.size())
    fail(ArtifactPackageErrorCode::InconsistentSections, "lexer context state count differs from parser");
  for (const auto &production : productions.productions)
    for (const auto &symbol : production.rhs)
      if (symbol.kind == ArtifactSymbolKind::Terminal && context.originalTerminals.at(symbol.id) == symbol.id)
        fail(ArtifactPackageErrorCode::InconsistentSections, "contextual parser must use scoped terminals");
  for (std::size_t state = 0; state < context.rows.size(); ++state) {
    const auto &nodes = context.rows[state];
    std::vector<ArtifactLookaheadWord> prefixes(nodes.size());
    const auto &row = table.actionRows.at(table.actionStateRows.at(state));
    for (std::size_t id = 0; id < nodes.size(); ++id) {
      const auto &node = nodes[id];
      if (prefixes[id].size() > table.lookahead ||
          (!node.edges.empty() && prefixes[id].size() == table.lookahead))
        fail(ArtifactPackageErrorCode::InconsistentSections, "lexer context exceeds lookahead");
      std::vector<std::uint32_t> sources;
      for (const auto &edge : node.edges) {
        auto word = prefixes[id];
        word.push_back({edge.terminal});
        prefixes[edge.target] = std::move(word);
        if (!edge.terminal) {
          if (!nodes[edge.target].edges.empty())
            fail(ArtifactPackageErrorCode::InconsistentSections, "lexer context continues after EOF");
          continue;
        }
        const auto source = context.originalTerminals.at(*edge.terminal);
        if (source == *edge.terminal || std::ranges::find(sources, source) != sources.end())
          fail(ArtifactPackageErrorCode::InconsistentSections, "invalid scoped context terminal");
        sources.push_back(source);
        bool available = false;
        for (std::size_t r = 0; r < lexer.rules.size(); ++r)
          if (lexer.rules[r].terminal == source && !lexer.rules[r].channel &&
              (context.requiredClasses[r] & node.active) == context.requiredClasses[r]) available = true;
        if (!available)
          fail(ArtifactPackageErrorCode::InconsistentSections, "no active lexer rule for contextual terminal");
      }
      if (node.edges.empty() && !prefixes[id].empty()) {
        if (prefixes[id].size() < table.lookahead && prefixes[id].back().terminal)
          fail(ArtifactPackageErrorCode::InconsistentSections, "short lexer context without EOF");
        if (!row.fallback && std::ranges::find(row.entries, prefixes[id], &ArtifactActionEntry::lookahead) == row.entries.end())
          fail(ArtifactPackageErrorCode::InconsistentSections, "lexer context has no parser action");
      }
    }
    for (const auto &entry : row.entries)
      if (std::ranges::find(prefixes, entry.lookahead) == prefixes.end())
        fail(ArtifactPackageErrorCode::InconsistentSections, "parser action is absent from lexer context");
  }
}

void validateCoverageDiagnostics(const std::optional<std::string> &diagnostics,
                                 std::size_t productionCount) {
  if (!diagnostics)
    return;
  try {
    const auto root = nlohmann::json::parse(*diagnostics);
    if (!root.is_object() || root.at("version") != 1)
      throw std::invalid_argument("invalid diagnostics version");
    if (!root.contains("productionCoverage"))
      return;
    const auto &items = root.at("productionCoverage");
    if (!items.is_array() || items.size() != productionCount)
      throw std::invalid_argument("coverage count differs from productions");
    for (std::size_t id = 0; id < items.size(); ++id) {
      const auto &item = items.at(id);
      if (!item.is_object() || item.at("id") != id ||
          !item.at("stableIdentity").is_string() ||
          !item.at("rule").is_string() ||
          !item.at("alternative").is_number_unsigned() ||
          !item.at("repetition").is_string() ||
          !item.at("role").is_string() ||
          !item.at("sourceLine").is_number_unsigned() ||
          !item.at("sourceColumn").is_number_unsigned())
        throw std::invalid_argument("invalid coverage production entry");
    }
  } catch (const std::exception &error) {
    fail(ArtifactPackageErrorCode::InconsistentSections,
         "invalid diagnostics section: " + std::string{error.what()});
  }
}

} // namespace

ArtifactPackageError::ArtifactPackageError(ArtifactPackageErrorCode code,
                                           std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

auto ArtifactPackageError::code() const noexcept -> ArtifactPackageErrorCode {
  return code_;
}

auto makeArtifactPackage(ArtifactManifest identity,
                         ArtifactPackageSections sections) -> ArtifactPackage {
  if (!identity.sections.empty())
    fail(ArtifactPackageErrorCode::InvalidPackage,
         "package identity must not contain section descriptors");
  constexpr ArtifactSectionKind required[]{
      ArtifactSectionKind::Symbols,     ArtifactSectionKind::Lexer,
      ArtifactSectionKind::ParserTable, ArtifactSectionKind::Productions,
      ArtifactSectionKind::Reductions,  ArtifactSectionKind::AstSchema};
  for (const ArtifactSectionKind kind : required)
    identity.sections.push_back(
        descriptor(kind, *sectionBytes(sections, kind)));
  if (sections.diagnostics)
    identity.sections.push_back(
        descriptor(ArtifactSectionKind::Diagnostics, *sections.diagnostics));
  validateArtifactManifest(identity);
  ArtifactPackage result{std::move(identity), {}, std::move(sections)};
  result.manifestJson = dumpArtifactManifestJson(result.manifest);
  static_cast<void>(loadArtifactPackage(result.manifestJson, result.sections));
  return result;
}

auto loadArtifactPackage(std::string_view manifestJson,
                         const ArtifactPackageSections &sections,
                         const ArtifactLoadLimits &limits)
    -> LoadedArtifactPackage {
  const ArtifactManifest manifest =
      parseArtifactManifestJson(manifestJson, limits);
  requireCanonical(dumpArtifactManifestJson(manifest) == manifestJson,
                   "manifest.json");
  bool hasDiagnostics = false;
  for (const auto &section : manifest.sections) {
    verifyDescriptor(section, sectionBytes(sections, section.kind));
    hasDiagnostics =
        hasDiagnostics || section.kind == ArtifactSectionKind::Diagnostics;
  }
  if (sections.diagnostics.has_value() != hasDiagnostics)
    fail(ArtifactPackageErrorCode::InconsistentSections,
         "diagnostics presence differs from manifest");

  ArtifactSymbols symbols = parseSymbolsJson(sections.symbols, limits);
  ArtifactProductions productions =
      parseProductionsJson(sections.productions, symbols, limits);
  ArtifactLexer lexer = parseLexerJson(sections.lexer, symbols.terminals.size(),
                                       symbols.channels.size(), limits);
  ArtifactParserTable parserTable =
      parseParserTableDsl(sections.parserTable, symbols, productions, limits);
  validateContext(lexer, parserTable, productions);
  for (const auto &section : manifest.sections)
    if (section.kind == ArtifactSectionKind::Lexer && section.version != lexer.version)
      fail(ArtifactPackageErrorCode::InconsistentSections, "lexer version differs from manifest");
  ArtifactReductions reductions =
      parseReductionsJson(sections.reductions, productions, limits);
  ArtifactAstSchema astSchema = parseAstSchemaJson(sections.astSchema, limits);
  validateCoverageDiagnostics(sections.diagnostics, productions.productions.size());

  requireCanonical(dumpSymbolsJson(symbols) == sections.symbols,
                   "symbols.json");
  requireCanonical(dumpProductionsJson(symbols, productions) ==
                       sections.productions,
                   "productions.json");
  requireCanonical(dumpLexerJson(lexer, symbols.terminals.size(),
                                 symbols.channels.size()) == sections.lexer,
                   "lexer.json");
  requireCanonical(dumpParserTableDsl(parserTable, symbols, productions) ==
                       sections.parserTable,
                   "parser.dsl");
  requireCanonical(dumpReductionsJson(productions, reductions.program) ==
                       sections.reductions,
                   "reductions.json");
  requireCanonical(dumpAstSchemaJson(astSchema.schema) == sections.astSchema,
                   "ast-schema.json");

  if (manifest.parserAlgorithm != algorithmName(parserTable.algorithm) ||
      manifest.lookahead != parserTable.lookahead)
    fail(ArtifactPackageErrorCode::InconsistentSections,
         "manifest parser profile differs from parser table");
  const auto start = std::ranges::find(
      symbols.nonterminals, manifest.startSymbol, &ArtifactNamedSymbol::name);
  if (start == symbols.nonterminals.end())
    fail(ArtifactPackageErrorCode::InconsistentSections,
         "manifest start symbol is absent from symbols section");

  return {manifest,
          std::move(symbols),
          std::move(lexer),
          std::move(parserTable),
          std::move(productions),
          std::move(reductions),
          std::move(astSchema),
          sections.diagnostics};
}

auto loadArtifactPackageDirectory(const std::filesystem::path &directory,
                                  const ArtifactLoadLimits &limits)
    -> LoadedArtifactPackage {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  if (error)
    fail(ArtifactPackageErrorCode::IoError,
         "cannot inspect artifact directory: " + directory.string());
  if (std::filesystem::is_symlink(status) ||
      !std::filesystem::is_directory(status))
    fail(ArtifactPackageErrorCode::UnsafeFile,
         "artifact package path is not a real directory");
  const std::string manifestJson =
      readRegularFile(directory / "manifest.json", limits.maximumManifestBytes);
  const ArtifactManifest manifest =
      parseArtifactManifestJson(manifestJson, limits);
  ArtifactPackageSections sections;
  for (const auto &descriptor : manifest.sections) {
    std::string *target = mutableSectionBytes(sections, descriptor.kind);
    *target =
        readRegularFile(directory / descriptor.file, limits.maximumSectionBytes,
                        descriptor.byteLength);
  }
  return loadArtifactPackage(manifestJson, sections, limits);
}

void writeArtifactPackageDirectory(const ArtifactPackage &package,
                                   const std::filesystem::path &directory) {
  static_cast<void>(
      loadArtifactPackage(package.manifestJson, package.sections));
  if (directory.empty() || directory.filename().empty())
    fail(ArtifactPackageErrorCode::UnsafeFile,
         "artifact output directory is empty or has no filename");
  std::filesystem::path temporary = directory;
  temporary += ".tmp";
  std::error_code error;
  const std::filesystem::path parent = directory.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error)
      fail(ArtifactPackageErrorCode::IoError,
           "cannot create artifact parent directories");
  }
  if (std::filesystem::exists(directory, error) || error ||
      std::filesystem::exists(temporary, error) || error)
    fail(ArtifactPackageErrorCode::IoError,
         "artifact output or temporary directory already exists");
  if (!std::filesystem::create_directory(temporary, error) || error)
    fail(ArtifactPackageErrorCode::IoError,
         "cannot create temporary artifact directory");
  try {
    writeFile(temporary / "manifest.json", package.manifestJson);
    for (const auto &descriptor : package.manifest.sections)
      writeFile(temporary / descriptor.file,
                *sectionBytes(package.sections, descriptor.kind));
    std::filesystem::rename(temporary, directory, error);
    if (error)
      fail(ArtifactPackageErrorCode::IoError,
           "cannot publish artifact directory");
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
    throw;
  }
}

} // namespace agas::artifact
