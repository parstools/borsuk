#include "agas/generator/ArtifactGeneration.h"
#include "agas/generator/ContextualGeneration.h"
#include "agas/runtime/PackagedAgFrontend.h"
#include "agsem/DocumentGrammar.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    if (argc != 6)
      throw std::runtime_error("usage: compose_document AG_PACKAGE Ag.ag "
                               "Sema.ag Document.ag OUTPUT");
    const agas::runtime::PackagedAgFrontend frontend{argv[1]};
    std::string identity;
    const auto load = [&](const char *path) {
      std::ifstream input(path, std::ios::binary);
      if (!input)
        throw std::runtime_error("cannot read grammar source");
      const std::string text{std::istreambuf_iterator<char>{input}, {}};
      identity += text;
      auto parsed = frontend.parse(text);
      if (!parsed.accepted())
        throw std::runtime_error(std::string{path} +
                                 ": invalid composition source");
      return *parsed.document;
    };
    const auto ag = load(argv[2]);
    const auto actions = load(argv[3]);
    const auto envelope = load(argv[4]);
    const auto document = agsem::composeDocumentGrammar(ag, actions, envelope);
    const auto generated = agas::generator::generateContextualParser(document);
    std::cout << "states=" << generated.statistics.states
              << " conflicts=" << generated.table.conflicts().size()
              << " resolved=" << generated.resolved << '\n';
    for (const auto &conflict : generated.table.conflicts())
      std::cerr << conflict.dump() << '\n';
    if (generated.table.hasConflicts())
      return 1;
    const auto package = agas::generator::buildContextualArtifactPackage(
        document, generated, identity,
        {"agsem-document-v1", "shared-ag", "current"});
    agas::artifact::writeArtifactPackageDirectory(package, argv[5]);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
