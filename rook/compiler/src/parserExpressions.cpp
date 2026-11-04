// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// Expressions: section 6 of rook/spec/grammar.ebnf.

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

/// The levels of the grammar's table, loosest first. Prefix and postfix operators are rules of their own.
enum Precedence : std::uint32_t {
    kPrecedenceRange = 1,
    kPrecedenceOr,
    kPrecedenceAnd,
    kPrecedenceEquality,
    kPrecedenceRelational,
    kPrecedenceCoalesce,
    kPrecedenceAdditive,
    kPrecedenceMultiplicative,
};

/// The operators of these levels do not chain: `a < b < c` is an error.
[[nodiscard]] constexpr auto isNonAssociative(std::uint32_t precedence) noexcept -> bool {
    return precedence == kPrecedenceRange || precedence == kPrecedenceEquality ||
           precedence == kPrecedenceRelational;
}

/// How far a generic argument list is looked for: types this long do not stand in an expression, and
/// the bound keeps a line of `a < b < c < d` linear.
constexpr std::size_t kGenericArgumentsLookahead = 64;

/// How far a closure's parameters are looked for: the same reasoning.
constexpr std::size_t kClosureLookahead = 64;

} // namespace

void Parser::parseExpression(ExpressionRestrictions restrictions) {
    parseBinary(kPrecedenceRange, restrictions);
}

auto Parser::binaryOperatorAhead() const noexcept -> std::optional<BinaryOperator> {
    const auto single = [](TokenKind kind, std::uint32_t precedence) {
        return std::optional{BinaryOperator{.kind = kind, .precedence = precedence, .tokenCount = 1}};
    };
    const TokenKind kind = kindAhead();
    switch (kind) {
    case TokenKind::DotDot:
    case TokenKind::DotDotEqual:
        return single(kind, kPrecedenceRange);
    case TokenKind::PipePipe:
        return single(kind, kPrecedenceOr);
    case TokenKind::AmpersandAmpersand:
        return single(kind, kPrecedenceAnd);
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
        return single(kind, kPrecedenceEquality);
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::KeywordIs:
        return single(kind, kPrecedenceRelational);
    case TokenKind::Greater:
        if (kindAhead(1) == TokenKind::Equal && areAdjacent(0)) {
            return BinaryOperator{
                .kind = TokenKind::GreaterEqual, .precedence = kPrecedenceRelational, .tokenCount = 2};
        }
        return single(kind, kPrecedenceRelational);
    case TokenKind::QuestionQuestion:
        return single(kind, kPrecedenceCoalesce);
    case TokenKind::Plus:
    case TokenKind::Minus:
        if (atCompoundAssignment() || atIncrement()) {
            return std::nullopt;
        }
        return single(kind, kPrecedenceAdditive);
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
        if (atCompoundAssignment()) {
            return std::nullopt;
        }
        return single(kind, kPrecedenceMultiplicative);
    default:
        return std::nullopt;
    }
}

void Parser::parseBinary(std::uint32_t minimumPrecedence, ExpressionRestrictions restrictions) {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    const Checkpoint start = checkpoint();
    const std::size_t before = cursor;
    parsePrefix(restrictions);
    if (cursor == before) {
        return; // no operand, already reported: an operator after nothing is not read as one
    }
    std::uint32_t chained = 0;
    std::optional<std::uint32_t> lastNonAssociative;
    while (true) {
        const std::optional<BinaryOperator> binaryOperator = binaryOperatorAhead();
        if (!binaryOperator || binaryOperator->precedence < minimumPrecedence) {
            return;
        }
        // Every operator wraps what came before it, one level deeper.
        chained += 1;
        if (nesting + chained > kMaximumNesting) {
            abandonSource();
            return;
        }
        if (lastNonAssociative == binaryOperator->precedence) {
            report(DiagnosticCode::ChainedComparison, rangeAhead(), "these operators do not chain",
                   "a second one",
                   "write two comparisons joined by `&&`, as `a < b && b < c`, or use parentheses");
        }
        lastNonAssociative = isNonAssociative(binaryOperator->precedence)
                                 ? std::optional{binaryOperator->precedence}
                                 : std::nullopt;

        if (binaryOperator->kind == TokenKind::KeywordIs) {
            startNodeAt(start, SyntaxKind::IsExpression);
            bump();
            parsePattern();
            finishNode();
            continue;
        }
        const bool isRange = binaryOperator->precedence == kPrecedenceRange;
        startNodeAt(start, isRange ? SyntaxKind::RangeExpression : SyntaxKind::BinaryExpression);
        if (binaryOperator->tokenCount == 1) {
            bump();
        } else {
            bumpJoined(binaryOperator->kind, binaryOperator->tokenCount);
        }
        // `??` reads to its right: its right side may hold another of its level.
        const std::uint32_t nextMinimum = binaryOperator->precedence == kPrecedenceCoalesce
                                              ? binaryOperator->precedence
                                              : binaryOperator->precedence + 1;
        parseBinary(nextMinimum, restrictions);
        finishNode();
    }
}

void Parser::parsePrefix(ExpressionRestrictions restrictions) {
    const TokenKind kind = kindAhead();
    if (kind == TokenKind::Bang || kind == TokenKind::Minus || kind == TokenKind::KeywordAwait) {
        const NestingScope scope{*this};
        if (!scope) {
            return;
        }
        startNode(kind == TokenKind::KeywordAwait ? SyntaxKind::AwaitExpression
                                                  : SyntaxKind::PrefixExpression);
        bump();
        parsePrefix(restrictions);
        finishNode();
        return;
    }
    parsePostfix(restrictions);
}

void Parser::parsePostfix(ExpressionRestrictions restrictions) {
    const Checkpoint start = checkpoint();
    const bool isPathLike = at(TokenKind::Identifier);
    if (!parsePrimary(restrictions)) {
        return;
    }
    bool mayStartStructLiteral = isPathLike && !restrictions.noStructLiteral;
    std::uint32_t chained = 0;
    while (true) {
        const TokenKind kind = kindAhead();
        if (kind != TokenKind::Dot && kind != TokenKind::LeftParenthesis && kind != TokenKind::LeftBracket &&
            kind != TokenKind::Question && !(kind == TokenKind::LeftBrace && mayStartStructLiteral)) {
            return;
        }
        // Every postfix wraps what came before it, one level deeper.
        chained += 1;
        if (nesting + chained > kMaximumNesting) {
            abandonSource();
            return;
        }
        switch (kind) {
        case TokenKind::Dot:
            parseMemberAccess(start);
            break;
        case TokenKind::LeftParenthesis:
            startNodeAt(start, SyntaxKind::CallExpression);
            parseArgumentList();
            finishNode();
            mayStartStructLiteral = false;
            break;
        case TokenKind::LeftBracket:
            startNodeAt(start, SyntaxKind::IndexExpression);
            bump();
            parseExpression();
            expect(TokenKind::RightBracket);
            finishNode();
            mayStartStructLiteral = false;
            break;
        case TokenKind::Question:
            startNodeAt(start, SyntaxKind::TryExpression);
            bump();
            finishNode();
            mayStartStructLiteral = false;
            break;
        default:
            startNodeAt(start, SyntaxKind::StructLiteral);
            parseFieldInitializers();
            finishNode();
            mayStartStructLiteral = false;
            break;
        }
    }
}

void Parser::parseMemberAccess(Checkpoint start) {
    startNodeAt(start, SyntaxKind::MemberExpression);
    bump();
    expectFieldName();
    if (at(TokenKind::Less) && atGenericArguments()) {
        parseGenericArguments();
    }
    finishNode();
}

auto Parser::parsePrimary(ExpressionRestrictions restrictions) -> bool {
    switch (kindAhead()) {
    case TokenKind::IntegerLiteral:
    case TokenKind::FloatLiteral:
    case TokenKind::CharacterLiteral:
    case TokenKind::RawStringLiteral:
    case TokenKind::KeywordTrue:
    case TokenKind::KeywordFalse:
    case TokenKind::KeywordNone:
        parseLiteral();
        return true;
    case TokenKind::StringStart:
        parseString();
        return true;
    case TokenKind::Identifier:
        parseNameExpression();
        return true;
    case TokenKind::KeywordSelf:
    case TokenKind::KeywordSuper:
    case TokenKind::KeywordSome:
    case TokenKind::KeywordOk:
    case TokenKind::KeywordErr:
        parseKeywordName();
        return true;
    case TokenKind::Dot:
        parseImplicitMember();
        return true;
    case TokenKind::LeftParenthesis:
        if (!restrictions.noClosure && atClosure()) {
            parseClosure(restrictions);
        } else {
            parseParenthesized();
        }
        return true;
    case TokenKind::LeftBracket:
        parseArray();
        return true;
    case TokenKind::KeywordIf:
        parseIf(BlockUse::Value);
        return true;
    case TokenKind::KeywordMatch:
        parseMatch(BlockUse::Value);
        return true;
    default:
        report(DiagnosticCode::ExpectedExpression, pointAhead(),
               std::format("expected an expression, found {}", describeToken(kindAhead())),
               "expected a value here", "write a value here");
        return false;
    }
}

void Parser::parseLiteral() {
    startNode(SyntaxKind::LiteralExpression);
    bump();
    finishNode();
}

void Parser::parseString() {
    startNode(SyntaxKind::StringExpression);
    bump();
    while (true) {
        if (at(TokenKind::StringText)) {
            bump();
        } else if (at(TokenKind::InterpolationStart)) {
            parseInterpolation();
        } else {
            break;
        }
    }
    eat(TokenKind::StringEnd); // always there, but where the source was abandoned
    finishNode();
}

void Parser::parseInterpolation() {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    startNode(SyntaxKind::Interpolation);
    bump();
    if (at(TokenKind::InterpolationEnd) || at(TokenKind::Colon)) {
        report(DiagnosticCode::EmptyInterpolation, rangeAhead(), "an interpolation holds a value",
               "nothing between the braces",
               R"(write a value between the braces, or `\{` for a brace in the text)");
    } else {
        parseExpression();
    }
    if (eat(TokenKind::Colon)) {
        eat(TokenKind::FormatSpecifier);
    }
    if (!at(TokenKind::InterpolationEnd)) {
        reportExpectedToken(TokenKind::InterpolationEnd);
        startNode(SyntaxKind::Error);
        std::uint32_t inner = 0;
        while (!atEnd() && !(at(TokenKind::InterpolationEnd) && inner == 0)) {
            inner += at(TokenKind::InterpolationStart) ? 1U : 0U;
            inner -= at(TokenKind::InterpolationEnd) ? 1U : 0U;
            bump();
        }
        finishNode();
    }
    eat(TokenKind::InterpolationEnd);
    finishNode();
}

void Parser::parseNameExpression() {
    startNode(SyntaxKind::NameExpression);
    bump();
    if (at(TokenKind::Less) && atGenericArguments()) {
        parseGenericArguments();
    }
    finishNode();
}

void Parser::parseKeywordName() {
    startNode(SyntaxKind::NameExpression);
    bump();
    finishNode();
}

void Parser::parseImplicitMember() {
    startNode(SyntaxKind::ImplicitMemberExpression);
    bump();
    expectName();
    finishNode();
}

auto Parser::atGenericArguments() const noexcept -> bool {
    std::uint32_t openAngles = 0;
    for (std::size_t ahead = 0; ahead < kGenericArgumentsLookahead; ++ahead) {
        switch (kindAhead(ahead)) {
        case TokenKind::Less:
            openAngles += 1;
            break;
        case TokenKind::Greater:
            openAngles -= 1;
            if (openAngles == 0) {
                const TokenKind after = kindAhead(ahead + 1);
                return after == TokenKind::LeftParenthesis || after == TokenKind::LeftBrace;
            }
            break;
        case TokenKind::Identifier:
        case TokenKind::KeywordVoid:
        case TokenKind::KeywordFn:
        case TokenKind::Dot:
        case TokenKind::Comma:
        case TokenKind::Question:
        case TokenKind::Arrow:
        case TokenKind::Semicolon:
        case TokenKind::IntegerLiteral:
        case TokenKind::LeftParenthesis:
        case TokenKind::RightParenthesis:
        case TokenKind::LeftBracket:
        case TokenKind::RightBracket:
            break;
        default:
            return false;
        }
    }
    return false;
}

auto Parser::atFieldLabel() const noexcept -> bool {
    return (at(TokenKind::Identifier) || isReservedWord(kindAhead())) && kindAhead(1) == TokenKind::Colon;
}

auto Parser::atClosure() const noexcept -> bool {
    for (std::size_t ahead = 1; ahead < kClosureLookahead; ++ahead) {
        switch (kindAhead(ahead)) {
        case TokenKind::Identifier:
        case TokenKind::Comma:
            break;
        case TokenKind::RightParenthesis:
            return kindAhead(ahead + 1) == TokenKind::FatArrow;
        default:
            return false;
        }
    }
    return false;
}

void Parser::parseClosure(ExpressionRestrictions restrictions) {
    startNode(SyntaxKind::ClosureExpression);
    startNode(SyntaxKind::ClosureParameterList);
    bump();
    parseCommaList(TokenKind::RightParenthesis, [this] { expectName(); });
    finishNode();
    expect(TokenKind::FatArrow);
    if (at(TokenKind::LeftBrace)) {
        parseBlock(BlockUse::Statements);
    } else {
        parseExpression(restrictions);
    }
    finishNode();
}

void Parser::parseParenthesized() {
    const Checkpoint start = checkpoint();
    bump();
    if (at(TokenKind::RightParenthesis)) {
        report(DiagnosticCode::ExpectedExpression, rangeAhead(), "expected an expression, found `)`",
               "expected a value here", "write a value between the parentheses");
        startNodeAt(start, SyntaxKind::Error);
        bump();
        finishNode();
        return;
    }
    parseExpression();
    parseAssignmentAsValue();
    if (at(TokenKind::Comma)) {
        startNodeAt(start, SyntaxKind::TupleExpression);
        bump();
        parseCommaList(TokenKind::RightParenthesis, [this] { parseExpression(); });
        finishNode();
        return;
    }
    startNodeAt(start, SyntaxKind::ParenthesizedExpression);
    expect(TokenKind::RightParenthesis);
    finishNode();
}

void Parser::parseArray() {
    startNode(SyntaxKind::ArrayExpression);
    bump();
    parseCommaList(TokenKind::RightBracket, [this] { parseExpression(); });
    finishNode();
}

void Parser::parseArgumentList() {
    startNode(SyntaxKind::ArgumentList);
    bump();
    parseCommaList(TokenKind::RightParenthesis, [this] { parseArgument(); });
    finishNode();
}

void Parser::parseArgument() {
    startNode(SyntaxKind::Argument);
    if (atFieldLabel()) {
        bump();
        bump();
    }
    eat(TokenKind::Ampersand);
    parseExpression();
    finishNode();
}

void Parser::parseCondition() {
    if (at(TokenKind::KeywordLet)) {
        startNode(SyntaxKind::LetCondition);
        bump();
        parsePattern();
        expect(TokenKind::Equal);
        parseExpression();
        finishNode();
        return;
    }
    parseExpression();
    parseAssignmentAsValue();
}

void Parser::parseIf(BlockUse use) {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    startNode(SyntaxKind::IfExpression);
    bump();
    if (eat(TokenKind::LeftParenthesis)) {
        parseCondition();
        expectClosingParenthesis();
    } else {
        report(DiagnosticCode::ConditionWithoutParentheses, pointAhead(), "a condition goes in parentheses",
               "expected `(` here", "write `if (condition) { ... }`");
        parseExpression(ExpressionRestrictions{.noStructLiteral = true});
    }
    parseBody(use);
    if (eat(TokenKind::KeywordElse)) {
        if (at(TokenKind::KeywordIf)) {
            parseIf(use);
        } else {
            parseBody(use);
        }
    }
    finishNode();
}

void Parser::parseMatch(BlockUse use) {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    startNode(SyntaxKind::MatchExpression);
    bump();
    if (eat(TokenKind::LeftParenthesis)) {
        parseExpression();
        expectClosingParenthesis();
    } else {
        report(DiagnosticCode::ConditionWithoutParentheses, pointAhead(),
               "what a `match` tests goes in parentheses", "expected `(` here",
               "write `match (value) { ... }`");
        parseExpression(ExpressionRestrictions{.noStructLiteral = true});
    }
    if (eat(TokenKind::LeftBrace)) {
        parseCommaList(TokenKind::RightBrace, [this, use] { parseMatchArm(use); });
    } else {
        report(DiagnosticCode::BodyWithoutBraces, pointAhead(), "the arms of a `match` go in braces",
               "expected `{` here", "write `match (value) { pattern => result, }`");
    }
    finishNode();
}

void Parser::parseMatchArm(BlockUse use) {
    startNode(SyntaxKind::MatchArm);
    parsePattern();
    if (at(TokenKind::KeywordIf)) {
        startNode(SyntaxKind::MatchGuard);
        bump();
        parseExpression(ExpressionRestrictions{.noClosure = true});
        finishNode();
    }
    expect(TokenKind::FatArrow);
    if (at(TokenKind::LeftBrace)) {
        parseBlock(use);
    } else {
        parseExpression();
    }
    finishNode();
}

} // namespace rook::compiler
