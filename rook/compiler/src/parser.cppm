// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

export module rook.compiler:parser;

import std;
import :source;
import :token;
import :diagnostic;
import :syntaxTree;

export namespace rook::compiler {

/// How deep a tree may nest: blocks, brackets and chains such as `a.b.c` or `a + b + c`, one level for
/// each. Past it the rest of the source is one diagnostic and one error node. Every pass over a tree
/// may recurse, because of this limit, and no hand-written source comes near it.
inline constexpr std::uint32_t kMaximumNesting = 128;

/// A source parsed. There is always a tree: a source with errors gets one as far as it can be told.
struct ParseResult {
    /// Lossless: its tokens cover every byte of the source, with one exception, a source longer than
    /// kMaximumSourceBytes, which is not read and gets an empty tree.
    SyntaxTree tree;
    /// Those of the lexer and of the parser, in source order.
    std::vector<Diagnostic> diagnostics;
};

/// Parses any bytes as a Rook source. It never fails and never crashes: what is not Rook ends in a
/// diagnostic, and the tree keeps it under an error node.
[[nodiscard]] auto parse(std::string_view source) -> ParseResult;

} // namespace rook::compiler

namespace rook::compiler {

/// What an expression may not hold where it stands.
struct ExpressionRestrictions {
    /// A name followed by `{` does not start a struct literal: the brace is a body, in a condition
    /// written without its parentheses.
    bool noStructLiteral = false;
    /// `(a) =>` does not start a closure: the arrow is a match arm's, after a guard.
    bool noClosure = false;
};

/// Whether a block yields a value (rook/spec/grammar.ebnf, section 5).
enum class BlockUse : std::uint8_t {
    Statements,
    Value,
};

/// A recursive-descent parser over the tokens of one source, building its tree as it reads. Each rule of
/// the grammar is one function, in parserItems.cpp, parserStatements.cpp, parserExpressions.cpp and
/// parserTypes.cpp. Recovery follows two rules: an expected token that is missing is reported and not
/// consumed, and every loop consumes a token on each turn or ends.
class Parser {
public:
    Parser(std::string_view sourceText, const std::vector<Token>& allTokens);

    void parseSourceFile();

    /// The tree, once parseSourceFile has run, and the diagnostics the parser added to `diagnostics`.
    [[nodiscard]] auto finish(std::string source, std::vector<Diagnostic>& diagnostics) && -> SyntaxTree;

private:
    using Checkpoint = SyntaxTreeBuilder::Checkpoint;

    /// A binary operator ahead: how it reads, and how many tokens it joins.
    struct BinaryOperator {
        TokenKind kind;
        std::uint32_t precedence;
        std::uint32_t tokenCount;
    };

    /// Holds one level of nesting while a rule that recurses is open.
    class NestingScope {
    public:
        explicit NestingScope(Parser& owner) noexcept;
        ~NestingScope();
        NestingScope(const NestingScope&) = delete;
        auto operator=(const NestingScope&) -> NestingScope& = delete;

        /// False when the limit was passed: the rule returns at once, the rest of the source abandoned.
        [[nodiscard]] explicit operator bool() const noexcept { return entered; }

    private:
        Parser& parser;
        bool entered;
    };

    // parser.cpp: the tokens.
    [[nodiscard]] auto kindAhead(std::size_t ahead = 0) const noexcept -> TokenKind;
    [[nodiscard]] auto rangeAhead(std::size_t ahead = 0) const noexcept -> TextRange;
    [[nodiscard]] auto textAhead(std::size_t ahead = 0) const noexcept -> std::string_view;
    [[nodiscard]] auto at(TokenKind kind) const noexcept -> bool { return kindAhead() == kind; }
    [[nodiscard]] auto atEnd() const noexcept -> bool { return at(TokenKind::EndOfFile); }
    [[nodiscard]] auto atWord(std::string_view word) const noexcept -> bool;
    [[nodiscard]] auto areAdjacent(std::size_t ahead) const noexcept -> bool;
    void flushTrivia();
    void bump();
    void bumpJoined(TokenKind kind, std::uint32_t tokenCount);
    auto eat(TokenKind kind) -> bool;
    auto expect(TokenKind kind) -> bool;

    // parser.cpp: nodes.
    void startNode(SyntaxKind kind);
    [[nodiscard]] auto checkpoint() -> Checkpoint;
    void startNodeAt(Checkpoint place, SyntaxKind kind) { builder.startNodeAt(place, kind); }
    void finishNode() { builder.finishNode(); }

    // parser.cpp: diagnostics and recovery.
    void report(DiagnosticCode code, TextRange range, std::string message, std::string label,
                std::string fix);
    void reportExpectedToken(TokenKind expected);
    [[nodiscard]] auto pointAhead() const noexcept -> TextRange;
    [[nodiscard]] auto pointAfterLastToken() const noexcept -> TextRange;
    void abandonSource();
    void skipInsideBraces(bool (*startsNext)(TokenKind) noexcept);
    void skipStatementRecovery();
    void skipItemRecovery();
    void skipMemberRecovery();
    void skipListRecovery(TokenKind close);
    void expectClosingParenthesis();
    void parseStraySemicolon();
    void parseAssignmentAsValue();

    // parserItems.cpp.
    void parseImport();
    void parseImportList();
    void parsePlugin();
    void parseItem();
    [[nodiscard]] auto beginItem() -> Checkpoint;
    void parseAttributes();
    void parseDeclaration(Checkpoint start);
    void parseFunction(Checkpoint start);
    void parseFunctionBody();
    void parseForward(Checkpoint start);
    void parseParameterList();
    void parseParameter();
    void parseGenericParameters();
    void parseFieldList();
    void parseField();
    void parseStruct(Checkpoint start, SyntaxKind kind);
    void parseEnum(Checkpoint start);
    void parseVariant();
    void parseVariantField();
    void parseClass(Checkpoint start);
    void parseInterface(Checkpoint start);
    void parseImplementation(Checkpoint start);
    void parseMemberList();
    void parseMember();
    void parseMemberVariable(Checkpoint start);
    void parseNewtype(Checkpoint start);
    void parseHandle(Checkpoint start);
    void parseVars(Checkpoint start);
    void parseVarsField();
    void parseConstant(Checkpoint start);
    void parseGlobal(Checkpoint start);
    void parseTypeAlias(Checkpoint start);
    void parseFieldInitializers();
    void parseFieldInitializer();
    void parseValueWithType(std::string_view what);
    void expectName();
    void expectFieldName();
    void expectSemicolon();

    /// Reads elements up to `close`, which it consumes, separated by commas, a last one allowed.
    template <class ElementRule>
    void parseCommaList(TokenKind close, ElementRule rule);

    // parserTypes.cpp.
    void parseType();
    void parsePathType();
    void parseGenericArguments();
    void parseTupleType();
    void parseArrayType();
    void parseFunctionType();
    void parsePattern();
    void parsePrimaryPattern();
    void parseNamePattern(SyntaxKind kind);
    void parseWrapperPattern();
    void parseTuplePattern();
    void parsePatternFields();
    void parsePatternField();

    // parserStatements.cpp.
    void parseBlock(BlockUse use);
    void parseBody(BlockUse use);
    void parseStatement(BlockUse use);
    void parseLetStatement();
    void parseVarStatement();
    void parseExpressionStatement(BlockUse use);
    void parseLabeledLoop();
    void parseWhile(Checkpoint start);
    void parseFor(Checkpoint start);
    void parseLoop(Checkpoint start);
    void parseDefer();
    void parseReturn();
    void parseJump(SyntaxKind kind);
    void parsePawnDeclaration();
    void parseCompoundAssignment(Checkpoint start);
    [[nodiscard]] auto atCompoundAssignment() const noexcept -> bool;
    [[nodiscard]] auto atIncrement() const noexcept -> bool;

    // parserExpressions.cpp.
    void parseExpression(ExpressionRestrictions restrictions = {});
    void parseBinary(std::uint32_t minimumPrecedence, ExpressionRestrictions restrictions);
    [[nodiscard]] auto binaryOperatorAhead() const noexcept -> std::optional<BinaryOperator>;
    void parsePrefix(ExpressionRestrictions restrictions);
    void parsePostfix(ExpressionRestrictions restrictions);
    auto parsePrimary(ExpressionRestrictions restrictions) -> bool;
    void parseLiteral();
    void parseString();
    void parseInterpolation();
    void parseNameExpression();
    void parseKeywordName();
    void parseImplicitMember();
    void parseMemberAccess(Checkpoint start);
    void parseParenthesized();
    [[nodiscard]] auto atClosure() const noexcept -> bool;
    void parseClosure(ExpressionRestrictions restrictions);
    void parseArray();
    void parseArgumentList();
    void parseArgument();
    void parseIf(BlockUse use);
    void parseCondition();
    void parseMatch(BlockUse use);
    void parseMatchArm(BlockUse use);
    [[nodiscard]] auto atGenericArguments() const noexcept -> bool;
    [[nodiscard]] auto atFieldLabel() const noexcept -> bool;

    std::string_view source;
    std::span<const Token> tokens;
    /// The index in `tokens` of each token the parser reads, the end of the file last.
    std::vector<std::uint32_t> significant;
    /// The place in `significant` of the current token.
    std::size_t cursor = 0;
    /// The index in `tokens` of the first token not yet in the tree.
    std::size_t emitted = 0;
    SyntaxTreeBuilder builder;
    std::vector<Diagnostic> ownDiagnostics;
    std::uint32_t nesting = 0;
    /// Where the last diagnostic was reported: one error at one token is enough.
    std::optional<std::size_t> lastReportAt;
    /// The source passed the nesting limit: the rest is an error node, and the rules unwind silently.
    bool isAbandoned = false;
};

template <class ElementRule>
void Parser::parseCommaList(TokenKind close, ElementRule rule) {
    while (!at(close) && !atEnd()) {
        rule();
        if (eat(TokenKind::Comma)) {
            continue;
        }
        if (at(close) || atEnd()) {
            break;
        }
        reportExpectedToken(close);
        skipListRecovery(close);
        if (eat(TokenKind::Comma)) {
            continue;
        }
        if (!at(close)) {
            return; // nothing here belongs to the list: whoever comes next reads it
        }
        break;
    }
    expect(close);
}

} // namespace rook::compiler
