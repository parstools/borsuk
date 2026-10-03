#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/runtime/AstParser.h"
#include "agas/runtime/AstWireJson.h"
#include "agas/runtime/PackagedAgFrontend.h"
#include "agas/model/Validation.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using agas::runtime::AstValue;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

auto field(const AstValue &value, const std::string &name) -> const AstValue & {
  for (std::size_t i = 0; i < value.fieldNames.size(); ++i)
    if (value.fieldNames[i] == name) return value.elements[i];
  throw std::runtime_error("missing field " + name);
}

void eraseSpans(AstValue &value) {
  value.sourceSpan = {};
  value.recognizedSpan = {};
  for (auto &child : value.elements) eraseSpans(child);
}
} // namespace

int main() {
  try {
    const agas::runtime::PackagedAgFrontend frontend{AGAS_PINNED_ARTIFACT_DIR};
    const auto parsed = frontend.parse(R"(
grammar Promotion;
options { parser = LALR; lookahead = 1; }
node document : value=statement EOF;
node statement : value=expression #Expression
               | RETURN value=expression SEMI #Return;
node expression : value=conditional;
node conditional : value=sum tail=conditionTail?;
inline conditionTail : QUESTION yes=expression COLON no=expression;
node sum : first=product rest=addTail*;
inline addTail : operator=PLUS value=product;
node product : first=primary rest=mulTail*;
inline mulTail : operator=STAR value=primary;
node primary : value=INTEGER #Integer
             | OPEN value=expression CLOSE #Group
             | MINUS value=primary #Negate;
INTEGER : [0-9]+;
RETURN : 'return'; SEMI : ';'; QUESTION : '?'; COLON : ':';
PLUS : '+'; STAR : '*'; MINUS : '-'; OPEN : '('; CLOSE : ')';
WS : [ \t\r\n]+ -> skip;
)");
    require(parsed.accepted(), "promotion grammar must parse");
    require(agas::model::validateSyntaxModel(*parsed.document).valid(),
            "promotion grammar must validate");
    const auto generated = agas::generator::generateParserTable(*parsed.document);
    require(!generated.table().hasConflicts(), "promotion must preserve LALR(1)");
    const auto lexer = agas::generator::compileLexerAutomaton(*parsed.document, generated.bnf());
    const agas::runtime::GeneratedAstParser parser{generated};
    const auto parse = [&](const std::string &source) {
      auto result = parser.parse(lexer.tokenize(source), source.size());
      require(result.accepted(), "promotion source must parse");
      return std::move(*result.root);
    };
    auto plain = parse("2+3");
    auto grouped = parse("((2))+((((3))))");
    const auto stats = agas::runtime::measureAst(plain);
    require(agas::runtime::measureAst(grouped).maximumDepth == stats.maximumDepth,
            "redundant grouping must not increase AST depth");
    eraseSpans(plain);
    eraseSpans(grouped);
    require(plain == grouped, "redundant parentheses must preserve exactly the AST shape");

    const auto deep = parse(std::string(100, '(') + "3" + std::string(100, ')'));
    const auto &leaf = field(deep, "value");
    require(leaf.typeName == "primary" && leaf.variantName == "Integer" &&
                leaf.sourceSpan == agas::runtime::InputSpan{100, 101} &&
                leaf.recognizedSpan == agas::runtime::InputSpan{0, 201} &&
                agas::runtime::measureAst(deep).maximumDepth == 2,
            "grouping must forward the leaf and retain both source ranges");
    const auto precedence = parse("2+3*4");
    const auto &sum = field(precedence, "value");
    require(sum.typeName == "sum" &&
            field(field(sum, "rest").elements.at(0), "value").typeName == "product",
            "multiplication must remain inside the sum operand");
    const auto parentheses = parse("(2+3)*4");
    const auto &product = field(parentheses, "value");
    require(product.typeName == "product" && field(product, "first").typeName == "sum" &&
                field(product, "first").recognizedSpan == agas::runtime::InputSpan{0, 5} &&
                field(product, "first").sourceSpan == agas::runtime::InputSpan{1, 4},
            "grouping must preserve precedence and its source evidence");
    const auto ternary = parse("2?3:4");
    require(field(ternary, "value").typeName == "conditional",
            "a present optional tail must retain the operation");
    const auto repeated = parse("2+3+4");
    require(field(field(repeated, "value"), "rest").elements.size() == 2,
            "nonempty tails must preserve every operation in source order");
    const auto statement = parse("return 3;");
    require(field(statement, "value").variantName == "Return",
            "an unbound keyword must prevent accidental statement promotion");
    const auto negative = parse("-3");
    require(field(negative, "value").variantName == "Negate",
            "an unbound operator must not be mistaken for grouping");
    const auto &schema = generated.astSchema();
    require(schema.rule("expression").resultTypes.at(0).kind == agas::generator::AstTypeKind::Rule &&
                schema.rule("sum").alternatives.at(0).resultType.kind == agas::generator::AstTypeKind::Choice,
            "schema must describe forwarded and conditional result types");
    AstValue nested;
    nested.kind = agas::runtime::AstValueKind::Token;
    nested.tokenText = "3";
    nested.sourceSpan = nested.recognizedSpan = {0, 1};
    for (unsigned i = 0; i < 200; ++i) {
      AstValue parent;
      parent.kind = agas::runtime::AstValueKind::List;
      parent.sourceSpan = parent.recognizedSpan = {0, 1};
      parent.elements.push_back(std::move(nested));
      nested = std::move(parent);
    }
    const std::string hash(64, '0');
    const agas::runtime::AstWireContext context{1, hash, hash, 1, "depth.vs", "3"};
    const auto wire = agas::runtime::dumpAstWireJson(nested, context);
    require(agas::runtime::parseAstWireJson(wire, context) == nested,
            "depth 200 must round trip through JSON");
    AstValue parent;
    parent.kind = agas::runtime::AstValueKind::List;
    parent.sourceSpan = parent.recognizedSpan = {0, 1};
    parent.elements.push_back(std::move(nested));
    bool rejected = false;
    try { static_cast<void>(agas::runtime::dumpAstWireJson(parent, context)); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "depth 201 must exceed the wire limit");
    std::cout << "AST chain promotion, grouping, precedence and spans passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
