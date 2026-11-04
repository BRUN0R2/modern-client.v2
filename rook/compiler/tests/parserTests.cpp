// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// The parser: what holds for any input, and the limits. What it accepts and refuses is the conformance
// suite's, in rook/spec/conformance.

#include <gtest/gtest.h>

import std;
import rook.compiler;

namespace {

using rook::compiler::Diagnostic;
using rook::compiler::DiagnosticCode;
using rook::compiler::kMaximumNesting;
using rook::compiler::NodeId;
using rook::compiler::parse;
using rook::compiler::ParseResult;
using rook::compiler::SyntaxElement;
using rook::compiler::SyntaxKind;
using rook::compiler::SyntaxTree;
using rook::compiler::Token;
using rook::compiler::TokenKind;

constexpr std::string_view kSample = R"(/// A plugin of every shape.
import cs.prelude.*;

plugin { name: "Sample", version: "1.0.0" }

@replicated
pub component Frozen { until: GameTime }

pub error enum SampleError { from Class(ClassError), Other(id: ClassId) }

newtype Speed(f32) suffix ups dimension(Distance / Duration) range(0..=1) derive(Ord);

pub vars Props { health: Health = 100, readonly isAlive: bool narrows AlivePlayer }

abstract class Ability<T> extends Base {
    var cooldown: Duration;
    pub abstract fn activate(self, inout player: Player) -> Result<void, AbilityError>;
}

pure async fn run<T: HasHealth>(items: List<T?>, callback: fn(Player) -> i32) -> [i32; 4]? {
    let (first, second) = pair;
    var count: i32 = 0_000 + 0xFF * 1.5e3s;
    'outer: for ((entity, frozen) in query) {
        while (count > 0 && !done) { count = count - 1; continue 'outer; }
        loop { break; }
    }
    defer { cleanUp(&count); }
    let text = "Hello, {player.name:<16}! \n \{ {"nested {value}"}";
    let chosen = match (source) {
        .Weapon(weapon) if isKnife(weapon) => applied(weapon),
        some(_) | none => { call(); 1 },
        _ => if (a) { 1 } else { 2 },
    };
    let closure = (player, other) => player.health ?? other.health;
    let made = Menu<Choice> { title: "x", items: [a, b,] };
    commands.removeComponent<Frozen>(entity)?;
    return (await task(amount: 1, r"raw"))?;
}
)";

/// Writes back the tokens of the tree in the order its nodes hold them.
[[nodiscard]] auto writeBack(const SyntaxTree& tree) -> std::string {
    std::string written;
    std::vector<std::span<const SyntaxElement>> pending{tree.children(tree.root())};
    while (!pending.empty()) {
        if (pending.back().empty()) {
            pending.pop_back();
            continue;
        }
        const SyntaxElement element = pending.back().front();
        pending.back() = pending.back().subspan(1);
        if (element.isToken()) {
            written += tree.text(tree.token(element.token()).range);
        } else {
            pending.push_back(tree.children(element.node()));
        }
    }
    return written;
}

/// What holds for any source at all.
void expectSound(std::string_view source) {
    const ParseResult result = parse(source);
    EXPECT_EQ(writeBack(result.tree), source);
    EXPECT_EQ(result.tree.source(), source);
    const rook::compiler::LineMap lines{result.tree.source()};
    std::uint32_t previousStart = 0;
    for (const Diagnostic& diagnostic : result.diagnostics) {
        EXPECT_LE(diagnostic.range.start, diagnostic.range.end);
        EXPECT_LE(diagnostic.range.end, source.size());
        EXPECT_GE(diagnostic.range.start, previousStart) << "the diagnostics are in source order";
        previousStart = diagnostic.range.start;
        EXPECT_FALSE(diagnostic.message.empty());
        EXPECT_FALSE(diagnostic.fix.empty());
        const std::string rendered =
            rook::compiler::renderDiagnostic(diagnostic, "sample.rook", result.tree.source(), lines);
        EXPECT_TRUE(rendered.contains("= fix: "));
    }
}

[[nodiscard]] auto codesOf(std::string_view source) -> std::vector<DiagnosticCode> {
    std::vector<DiagnosticCode> codes;
    for (const Diagnostic& diagnostic : parse(source).diagnostics) {
        codes.push_back(diagnostic.code);
    }
    return codes;
}

/// The kinds of `id`'s children that are nodes.
[[nodiscard]] auto childNodeKinds(const SyntaxTree& tree, NodeId id) -> std::vector<SyntaxKind> {
    std::vector<SyntaxKind> kinds;
    for (const SyntaxElement child : tree.children(id)) {
        if (!child.isToken()) {
            kinds.push_back(tree.kind(child.node()));
        }
    }
    return kinds;
}

[[nodiscard]] auto repeated(std::string_view text, std::size_t times) -> std::string {
    std::string result;
    result.reserve(text.size() * times);
    for (std::size_t index = 0; index < times; ++index) {
        result += text;
    }
    return result;
}

TEST(ParserTest, TheSampleParsesClean) {
    const ParseResult result = parse(kSample);
    for (const Diagnostic& diagnostic : result.diagnostics) {
        ADD_FAILURE() << diagnostic.message << " at byte " << diagnostic.range.start;
    }
    expectSound(kSample);
}

TEST(ParserTest, EveryPrefixOfASourceIsSound) {
    for (std::size_t length = 0; length <= kSample.size(); ++length) {
        expectSound(kSample.substr(0, length));
    }
}

TEST(ParserTest, EveryByteOfASourceCanBeWrong) {
    const std::array replacements{'\0', '{', ')', '"', '\\', '\n', '<', '\xFF'};
    std::string source{kSample};
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char original = source[index];
        for (const char replacement : replacements) {
            source[index] = replacement;
            expectSound(source);
        }
        source[index] = original;
    }
}

TEST(ParserTest, DroppingAnyTokenIsSound) {
    const auto tokens = rook::compiler::tokenize(kSample).tokens;
    for (const Token& token : tokens) {
        std::string source{kSample};
        source.erase(token.range.start, token.range.end - token.range.start);
        expectSound(source);
    }
}

TEST(ParserTest, AnEmptySourceIsAnEmptyFile) {
    const ParseResult result = parse("");
    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.tree.kind(result.tree.root()), SyntaxKind::SourceFile);
    ASSERT_EQ(result.tree.tokens().size(), 1U);
    EXPECT_EQ(result.tree.tokens().front().kind, TokenKind::EndOfFile);
}

TEST(ParserTest, KeepsLineEndsOfEveryKind) {
    expectSound("\xEF\xBB\xBF// comment\r\nfn main() {\r\n}\r\n");
    EXPECT_TRUE(codesOf("\xEF\xBB\xBF// comment\r\nfn main() {\r\n}\r\n").empty());
}

TEST(ParserTest, SaysOnceWhenASourceNestsTooDeeply) {
    // Each of these would overflow any stack if it recursed without a limit.
    constexpr std::size_t kDepth = 100'000;
    const std::array sources{
        std::format("const A: i32 = {};", repeated("(", kDepth)),
        std::format("const A: i32 = {};", repeated("[", kDepth)),
        std::format("const A: i32 = {};", repeated("!", kDepth)),
        std::format("const A: i32 = {};", repeated("-", kDepth)),
        std::format("const A: i32 = {};", repeated("await ", kDepth / 4)),
        std::format("const A: i32 = a{};", repeated(".b", kDepth)),
        std::format("const A: i32 = a{};", repeated("()", kDepth)),
        std::format("const A: i32 = a{};", repeated("?", kDepth)),
        std::format("const A: i32 = a{};", repeated(" + a", kDepth)),
        std::format("const A: i32 = {}a;", repeated("a ?? ", kDepth)),
        std::format("const A: i32 = {};", repeated("\"{", kDepth)),
        std::format("const A: {};", repeated("List<", kDepth)),
        std::format("const A: {};", repeated("(", kDepth)),
        std::format("const A: {};", repeated("[", kDepth)),
        std::format("fn body() {}", repeated("{", kDepth)),
        std::format("fn body() {{ {} }}", repeated("if (a) { ", kDepth / 4)),
        std::format("fn body() {{ {} }}", repeated("loop { ", kDepth / 4)),
        std::format("fn body() {{ {} }}", repeated("loop ", kDepth)),
        std::format("fn body() {{ {} }}", repeated("defer ", kDepth)),
        std::format("fn body() {{ {} }}", repeated("while (a) ", kDepth / 4)),
        std::format("fn body() {{ {} }}", repeated("for (a in b) ", kDepth / 4)),
        std::format("fn body() {{ {} }}", repeated("if (a) ", kDepth / 4)),
        std::format("const A: i32 = {};", repeated("f(", kDepth)),
        std::format("const A: i32 = {};", repeated("a[", kDepth)),
        std::format("const A: i32 = {};", repeated("A { b: ", kDepth / 4)),
        std::format("const A: i32 = {};", repeated("if (", kDepth)),
        std::format("const A: i32 = {};", repeated("match (", kDepth)),
        std::format("fn body() {{ {} }}", repeated("match (a) { b => ", kDepth / 8)),
        std::format("fn body() {{ {} }}", repeated("if (a) {} else ", kDepth / 8)),
        std::format("fn body() {{ {} }}", repeated("(a) => ", kDepth / 8)),
        std::format("fn body() {{ let {}; }}", repeated("(", kDepth)),
        std::format("fn body() {{ let {}; }}", repeated("some(", kDepth / 4)),
        std::format("class A {{ {} }}", repeated("class B { ", kDepth / 4)),
    };
    for (const std::string& source : sources) {
        const ParseResult result = parse(source);
        EXPECT_EQ(writeBack(result.tree), source);
        const auto deep =
            std::ranges::count(result.diagnostics, DiagnosticCode::NestedTooDeeply, &Diagnostic::code);
        EXPECT_LE(deep, 1) << source.substr(0, 30);
        EXPECT_FALSE(result.diagnostics.empty()) << source.substr(0, 30);
        EXPECT_FALSE(rook::compiler::printSyntaxTree(result.tree).empty());
    }
}

TEST(ParserTest, SaysOnceWhenASourceHasTooManyErrors) {
    constexpr std::size_t kAll = 100'000;
    const std::string source(kAll, '$');
    const ParseResult result = parse(source);
    EXPECT_EQ(writeBack(result.tree), source);
    ASSERT_EQ(result.diagnostics.size(), rook::compiler::kMaximumDiagnostics + 1);
    EXPECT_EQ(result.diagnostics.back().code, DiagnosticCode::TooManyDiagnostics);
    const auto said =
        std::ranges::count(result.diagnostics, DiagnosticCode::TooManyDiagnostics, &Diagnostic::code);
    EXPECT_EQ(said, 1);
}

TEST(ParserTest, AcceptsNestingUpToItsLimit) {
    const auto parentheses = [](std::size_t depth) {
        return std::format("const A: i32 = {}1{};", repeated("(", depth), repeated(")", depth));
    };
    EXPECT_TRUE(codesOf(parentheses(kMaximumNesting / 2)).empty());
    EXPECT_EQ(codesOf(parentheses(kMaximumNesting)), std::vector{DiagnosticCode::NestedTooDeeply});
}

TEST(ParserTest, ATreeNeverNestsPastItsLimit) {
    // Every pass may recurse over a tree, because of this: a few nodes for each level of nesting.
    constexpr std::size_t kNodesAtMostForALevel = 6;
    const ParseResult result = parse(std::format("const A: i32 = a{};", repeated(" + a", 100'000)));
    std::size_t deepest = 0;
    std::vector<std::pair<NodeId, std::size_t>> pending{{result.tree.root(), 1}};
    while (!pending.empty()) {
        const auto [node, depth] = pending.back();
        pending.pop_back();
        deepest = std::max(deepest, depth);
        for (const SyntaxElement child : result.tree.children(node)) {
            if (!child.isToken()) {
                pending.emplace_back(child.node(), depth + 1);
            }
        }
    }
    EXPECT_LE(deepest, kMaximumNesting * kNodesAtMostForALevel);
}

TEST(ParserTest, LongFlatInputsAreSound) {
    constexpr std::size_t kCount = 20'000;
    const std::array sources{
        std::format("const A: bool = a{};", repeated(" < a", kCount)),
        std::format("fn body() {{ {} }}", repeated("a < b; ", kCount)),
        std::format("fn body() {{ {} }}", repeated("(a); ", kCount)),
        std::format("fn body() {{ {} }}", repeated("f(", kMaximumNesting / 2) + repeated(") ", kCount)),
        repeated("; ", kCount),
        repeated("} ", kCount),
        repeated("fn ", kCount),
        repeated("@", kCount),
        repeated("\"", kCount),
        repeated("'", kCount),
        repeated("/*", kCount),
        repeated("\xFF", kCount),
        std::format("fn body() {{ {} }}", repeated("a: ", kCount)),
        std::format("fn f({})", repeated("a ", kCount)),
    };
    for (const std::string& source : sources) {
        const ParseResult result = parse(source);
        EXPECT_EQ(writeBack(result.tree), source);
    }
}

TEST(ParserTest, JoinsAGreaterSignWithAnEqualSignOnlyAsAnOperator) {
    const ParseResult comparison = parse("const A: bool = b >= c;");
    EXPECT_TRUE(comparison.diagnostics.empty());
    EXPECT_TRUE(std::ranges::any_of(
        comparison.tree.tokens(), [](const Token& token) { return token.kind == TokenKind::GreaterEqual; }));
    expectSound("const A: bool = b >= c;");

    const ParseResult list = parse("const A: List<i32>= b;");
    EXPECT_TRUE(list.diagnostics.empty());
    EXPECT_FALSE(std::ranges::any_of(
        list.tree.tokens(), [](const Token& token) { return token.kind == TokenKind::GreaterEqual; }));

    // Two tokens with a space between are no operator.
    EXPECT_FALSE(codesOf("const A: bool = b > = c;").empty());
}

TEST(ParserTest, DocumentationBelongsToTheDeclarationAfterIt) {
    const ParseResult result = parse("// plain\n\n/// documented\nfn a() {}");
    const auto children = result.tree.children(result.tree.root());
    ASSERT_FALSE(children.empty());

    NodeId function{};
    bool foundFunction = false;
    std::size_t tokensBeforeFunction = 0;
    for (const SyntaxElement child : children) {
        if (child.isToken()) {
            EXPECT_NE(result.tree.token(child.token()).kind, TokenKind::DocumentationComment);
            tokensBeforeFunction += foundFunction ? 0U : 1U;
        } else {
            function = child.node();
            foundFunction = true;
        }
    }
    ASSERT_TRUE(foundFunction);
    EXPECT_EQ(result.tree.kind(function), SyntaxKind::FunctionDeclaration);
    EXPECT_GE(tokensBeforeFunction, 2U); // the plain comment and its whitespace
    const auto first = result.tree.children(function).front();
    ASSERT_TRUE(first.isToken());
    EXPECT_EQ(result.tree.token(first.token()).kind, TokenKind::DocumentationComment);
}

TEST(ParserTest, ATreeIsAFileOfItsDeclarations) {
    const ParseResult result = parse("import a.b;\nplugin { name: \"x\" }\nfn c() {}\nstruct D {}\n");
    EXPECT_EQ(childNodeKinds(result.tree, result.tree.root()),
              (std::vector{SyntaxKind::ImportDeclaration, SyntaxKind::PluginDeclaration,
                           SyntaxKind::FunctionDeclaration, SyntaxKind::StructDeclaration}));
}

TEST(ParserTest, ANodeCoversItsTokens) {
    const ParseResult result = parse("  fn a() {}  ");
    const auto kinds = childNodeKinds(result.tree, result.tree.root());
    ASSERT_EQ(kinds, std::vector{SyntaxKind::FunctionDeclaration});
    for (const SyntaxElement child : result.tree.children(result.tree.root())) {
        if (!child.isToken()) {
            EXPECT_EQ(result.tree.text(result.tree.range(child.node())), "fn a() {}");
        }
    }
}

TEST(ParserTest, OneMistakeIsOneDiagnostic) {
    EXPECT_EQ(codesOf("fn a() { let b = ; let c = 1; }"), std::vector{DiagnosticCode::ExpectedExpression});
    EXPECT_EQ(codesOf("fn a() { if b { c(); } }"), std::vector{DiagnosticCode::ConditionWithoutParentheses});
    EXPECT_EQ(codesOf("fn a(b: i32 c: i32) {}\nfn d() {}"), std::vector{DiagnosticCode::ExpectedToken});
    EXPECT_EQ(codesOf("const A: i32 = 1 $;"), std::vector{DiagnosticCode::UnexpectedCharacter});
}

TEST(ParserTest, ASourceOverTheLimitIsADiagnostic) {
    const std::string source(rook::compiler::kMaximumSourceBytes + 1, ' ');
    const ParseResult result = parse(source);
    ASSERT_EQ(result.diagnostics.size(), 1U);
    EXPECT_EQ(result.diagnostics.front().code, DiagnosticCode::SourceTooLarge);
    EXPECT_TRUE(result.tree.source().empty());
    const std::string atTheLimit(rook::compiler::kMaximumSourceBytes, ' ');
    EXPECT_TRUE(parse(atTheLimit).diagnostics.empty());
}

} // namespace
