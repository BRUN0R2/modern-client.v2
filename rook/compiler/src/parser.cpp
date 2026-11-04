// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

/// What ends a statement's recovery besides `;` and `}`: the start of the next statement.
[[nodiscard]] constexpr auto startsStatement(TokenKind kind) noexcept -> bool {
    switch (kind) {
    case TokenKind::KeywordLet:
    case TokenKind::KeywordVar:
    case TokenKind::KeywordIf:
    case TokenKind::KeywordMatch:
    case TokenKind::KeywordWhile:
    case TokenKind::KeywordFor:
    case TokenKind::KeywordLoop:
    case TokenKind::KeywordDefer:
    case TokenKind::KeywordReturn:
    case TokenKind::KeywordBreak:
    case TokenKind::KeywordContinue:
    case TokenKind::Label:
        return true;
    default:
        return false;
    }
}

/// What ends a declaration's recovery: the start of the next one.
[[nodiscard]] constexpr auto startsItem(TokenKind kind) noexcept -> bool {
    switch (kind) {
    case TokenKind::KeywordImport:
    case TokenKind::KeywordPlugin:
    case TokenKind::KeywordPub:
    case TokenKind::At:
    case TokenKind::KeywordFn:
    case TokenKind::KeywordPure:
    case TokenKind::KeywordAsync:
    case TokenKind::KeywordConst:
    case TokenKind::KeywordForward:
    case TokenKind::KeywordStruct:
    case TokenKind::KeywordComponent:
    case TokenKind::KeywordEnum:
    case TokenKind::KeywordError:
    case TokenKind::KeywordOpen:
    case TokenKind::KeywordAbstract:
    case TokenKind::KeywordClass:
    case TokenKind::KeywordInterface:
    case TokenKind::KeywordImpl:
    case TokenKind::KeywordNewtype:
    case TokenKind::KeywordHandle:
    case TokenKind::KeywordVars:
    case TokenKind::KeywordVar:
    case TokenKind::KeywordPersist:
    case TokenKind::KeywordType:
        return true;
    default:
        return false;
    }
}

} // namespace

Parser::NestingScope::NestingScope(Parser& owner) noexcept : parser{owner}, entered{false} {
    if (parser.isAbandoned) {
        return;
    }
    if (parser.nesting >= kMaximumNesting) {
        parser.abandonSource();
        return;
    }
    parser.nesting += 1;
    entered = true;
}

Parser::NestingScope::~NestingScope() {
    if (entered) {
        parser.nesting -= 1;
    }
}

Parser::Parser(std::string_view sourceText, const std::vector<Token>& allTokens)
    : source{sourceText}, tokens{allTokens} {
    significant.reserve(allTokens.size());
    for (std::size_t index = 0; index < allTokens.size(); ++index) {
        if (!isTrivia(allTokens[index].kind)) {
            significant.push_back(static_cast<std::uint32_t>(index));
        }
    }
}

auto Parser::kindAhead(std::size_t ahead) const noexcept -> TokenKind {
    const std::size_t place = std::min(cursor + ahead, significant.size() - 1);
    return tokens[significant[place]].kind;
}

auto Parser::rangeAhead(std::size_t ahead) const noexcept -> TextRange {
    const std::size_t place = std::min(cursor + ahead, significant.size() - 1);
    return tokens[significant[place]].range;
}

auto Parser::textAhead(std::size_t ahead) const noexcept -> std::string_view {
    const TextRange range = rangeAhead(ahead);
    return source.substr(range.start, range.end - range.start);
}

auto Parser::atWord(std::string_view word) const noexcept -> bool {
    return at(TokenKind::Identifier) && textAhead() == word;
}

auto Parser::areAdjacent(std::size_t ahead) const noexcept -> bool {
    return rangeAhead(ahead).end == rangeAhead(ahead + 1).start;
}

void Parser::flushTrivia() {
    const std::size_t next = significant[cursor];
    while (emitted < next) {
        builder.addToken(tokens[emitted]);
        emitted += 1;
    }
}

void Parser::bump() {
    if (atEnd()) {
        return; // the end of the file goes in last, as the root closes
    }
    flushTrivia();
    builder.addToken(tokens[emitted]);
    emitted += 1;
    cursor += 1;
}

void Parser::bumpJoined(TokenKind kind, std::uint32_t tokenCount) {
    flushTrivia();
    const TextRange first = rangeAhead();
    const TextRange last = rangeAhead(tokenCount - 1);
    builder.addToken(Token{.kind = kind, .range = TextRange{.start = first.start, .end = last.end}});
    emitted += tokenCount;
    cursor += tokenCount;
}

auto Parser::eat(TokenKind kind) -> bool {
    if (!at(kind)) {
        return false;
    }
    bump();
    return true;
}

auto Parser::expect(TokenKind kind) -> bool {
    if (eat(kind)) {
        return true;
    }
    reportExpectedToken(kind);
    return false;
}

void Parser::startNode(SyntaxKind kind) {
    flushTrivia();
    builder.startNode(kind);
}

auto Parser::checkpoint() -> Checkpoint {
    flushTrivia();
    return builder.checkpoint();
}

auto Parser::pointAhead() const noexcept -> TextRange {
    return atEnd() ? pointAfterLastToken() : rangeAhead();
}

auto Parser::pointAfterLastToken() const noexcept -> TextRange {
    if (cursor == 0) {
        return TextRange{.start = 0, .end = 0};
    }
    const ByteOffset end = tokens[significant[cursor - 1]].range.end;
    return TextRange{.start = end, .end = end};
}

void Parser::report(DiagnosticCode code, TextRange range, std::string message, std::string label,
                    std::string fix) {
    if (isAbandoned || lastReportAt == cursor) {
        return;
    }
    lastReportAt = cursor;
    addDiagnostic(ownDiagnostics, Diagnostic{.code = code,
                                             .range = range,
                                             .message = std::move(message),
                                             .label = std::move(label),
                                             .fix = std::move(fix)});
}

void Parser::reportExpectedToken(TokenKind expected) {
    const std::string_view wanted = describeToken(expected);
    report(DiagnosticCode::ExpectedToken, pointAhead(),
           std::format("expected {}, found {}", wanted, describeToken(kindAhead())),
           std::format("expected {} here", wanted), std::format("write {} here", wanted));
}

void Parser::abandonSource() {
    report(DiagnosticCode::NestedTooDeeply, pointAhead(),
           std::format("this nests more than {} levels deep", kMaximumNesting), "too deep",
           "split this into smaller functions or into named values");
    isAbandoned = true;
    startNode(SyntaxKind::Error);
    while (!atEnd()) {
        bump();
    }
    finishNode();
}

void Parser::skipInsideBraces(bool (*startsNext)(TokenKind) noexcept) {
    startNode(SyntaxKind::Error);
    std::uint32_t openBraces = 0;
    bool isFirst = true;
    while (!atEnd()) {
        const TokenKind kind = kindAhead();
        if (openBraces == 0 && (kind == TokenKind::RightBrace || (!isFirst && startsNext(kind)))) {
            break;
        }
        openBraces += kind == TokenKind::LeftBrace ? 1U : 0U;
        openBraces -= kind == TokenKind::RightBrace ? 1U : 0U;
        bump();
        isFirst = false;
        if (openBraces == 0 && kind == TokenKind::Semicolon) {
            break;
        }
    }
    finishNode();
}

void Parser::skipStatementRecovery() {
    skipInsideBraces(startsStatement);
}

void Parser::skipMemberRecovery() {
    skipInsideBraces(startsItem);
}

void Parser::skipItemRecovery() {
    std::uint32_t openBraces = 0;
    bool isFirst = true;
    while (!atEnd()) {
        const TokenKind kind = kindAhead();
        if (openBraces == 0 && !isFirst && startsItem(kind)) {
            return;
        }
        openBraces += kind == TokenKind::LeftBrace ? 1U : 0U;
        if (kind == TokenKind::RightBrace && openBraces > 0) {
            openBraces -= 1;
        }
        bump();
        isFirst = false;
        if (openBraces == 0 && (kind == TokenKind::Semicolon || kind == TokenKind::RightBrace)) {
            return;
        }
    }
}

void Parser::skipListRecovery(TokenKind close) {
    // Where a list of a bracket other than a brace meets `{`, a body is starting after it.
    const auto endsTheList = [close](TokenKind kind) {
        return kind == close || kind == TokenKind::Comma || kind == TokenKind::Semicolon ||
               kind == TokenKind::RightBrace || kind == TokenKind::LeftBrace || startsStatement(kind) ||
               startsItem(kind);
    };
    if (endsTheList(kindAhead())) {
        return;
    }
    startNode(SyntaxKind::Error);
    std::uint32_t openBrackets = 0;
    while (!atEnd()) {
        const TokenKind kind = kindAhead();
        if (openBrackets == 0 && endsTheList(kind)) {
            break;
        }
        const bool opens = kind == TokenKind::LeftParenthesis || kind == TokenKind::LeftBracket ||
                           kind == TokenKind::LeftBrace;
        const bool closes = kind == TokenKind::RightParenthesis || kind == TokenKind::RightBracket ||
                            kind == TokenKind::RightBrace;
        openBrackets += opens ? 1U : 0U;
        if (closes && openBrackets > 0) {
            openBrackets -= 1;
        }
        bump();
    }
    finishNode();
}

void Parser::expectClosingParenthesis() {
    if (eat(TokenKind::RightParenthesis)) {
        return;
    }
    reportExpectedToken(TokenKind::RightParenthesis);
    skipListRecovery(TokenKind::RightParenthesis);
    while (eat(TokenKind::Comma)) {
        skipListRecovery(TokenKind::RightParenthesis);
    }
    eat(TokenKind::RightParenthesis);
}

void Parser::parseStraySemicolon() {
    report(DiagnosticCode::StraySemicolon, rangeAhead(), "a `;` here ends nothing", "nothing to end",
           "remove this `;`");
    startNode(SyntaxKind::Error);
    bump();
    finishNode();
}

void Parser::parseAssignmentAsValue() {
    if (at(TokenKind::Equal)) {
        report(DiagnosticCode::AssignmentAsValue, rangeAhead(), "an assignment is not a value",
               "`=` where a value goes", "compare with `==`, or assign in a statement of its own");
        bump();
        parseExpression();
    }
}

void Parser::parseSourceFile() {
    builder.startNode(
        SyntaxKind::SourceFile); // without flushing: the root holds what comes before its first token
    while (at(TokenKind::KeywordImport)) {
        parseImport();
    }
    if (at(TokenKind::KeywordPlugin)) {
        parsePlugin();
    }
    while (!atEnd()) {
        const std::size_t before = cursor;
        parseItem();
        if (cursor == before) {
            bump(); // a rule consumed nothing: the file goes on, the token is kept
        }
    }
    flushTrivia();
    builder.addToken(tokens[emitted]);
    emitted += 1;
    finishNode();
}

auto Parser::finish(std::string sourceCopy, std::vector<Diagnostic>& diagnostics) && -> SyntaxTree {
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(ownDiagnostics.begin()),
                       std::make_move_iterator(ownDiagnostics.end()));
    return std::move(builder).finish(std::move(sourceCopy));
}

auto parse(std::string_view source) -> ParseResult {
    if (source.size() > kMaximumSourceBytes) {
        std::vector<Token> noTokens{Token{}};
        Parser parser{std::string_view{}, noTokens};
        parser.parseSourceFile();
        std::vector<Diagnostic> diagnostics;
        diagnostics.push_back(Diagnostic{
            .code = DiagnosticCode::SourceTooLarge,
            .range = TextRange{},
            .message = std::format("this source is over the limit of {} bytes", kMaximumSourceBytes),
            .label = "too long to read",
            .fix = "split it into smaller modules"});
        SyntaxTree tree = std::move(parser).finish(std::string{}, diagnostics);
        return ParseResult{.tree = std::move(tree), .diagnostics = std::move(diagnostics)};
    }

    TokenizedSource tokenized = tokenize(source);
    Parser parser{source, tokenized.tokens};
    parser.parseSourceFile();
    std::vector<Diagnostic> diagnostics = std::move(tokenized.diagnostics);
    SyntaxTree tree = std::move(parser).finish(std::string{source}, diagnostics);
    std::ranges::stable_sort(diagnostics, {},
                             [](const Diagnostic& diagnostic) { return diagnostic.range.start; });
    return ParseResult{.tree = std::move(tree), .diagnostics = std::move(diagnostics)};
}

} // namespace rook::compiler
