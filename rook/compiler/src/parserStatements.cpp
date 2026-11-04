// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// Statements: section 5 of rook/spec/grammar.ebnf.

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

/// `new`, Pawn's declaration: an identifier in Rook, told apart by what follows it.
constexpr std::string_view kWordNew = "new";

[[nodiscard]] constexpr auto isAssignable(SyntaxKind kind) noexcept -> bool {
    return kind == SyntaxKind::NameExpression || kind == SyntaxKind::MemberExpression ||
           kind == SyntaxKind::IndexExpression;
}

[[nodiscard]] constexpr auto isArithmetic(TokenKind kind) noexcept -> bool {
    return kind == TokenKind::Plus || kind == TokenKind::Minus || kind == TokenKind::Star ||
           kind == TokenKind::Slash || kind == TokenKind::Percent;
}

} // namespace

void Parser::parseBlock(BlockUse use) {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    startNode(SyntaxKind::Block);
    bump();
    while (!at(TokenKind::RightBrace) && !atEnd()) {
        const std::size_t before = cursor;
        parseStatement(use);
        if (cursor == before) {
            skipStatementRecovery();
        }
    }
    expect(TokenKind::RightBrace);
    finishNode();
}

void Parser::parseBody(BlockUse use) {
    if (at(TokenKind::LeftBrace)) {
        parseBlock(use);
        return;
    }
    report(DiagnosticCode::BodyWithoutBraces, pointAhead(), "a body goes in braces", "expected `{` here",
           "write the body as `{ ... }`, even for one statement");
    // The statement that should have been in braces is read as if it were, so nothing more is reported.
    if (!at(TokenKind::RightBrace) && !at(TokenKind::Semicolon) && !atEnd()) {
        const NestingScope scope{*this};
        if (!scope) {
            return;
        }
        startNode(SyntaxKind::Block);
        parseStatement(BlockUse::Statements);
        finishNode();
    }
}

void Parser::parseStatement(BlockUse use) {
    switch (kindAhead()) {
    case TokenKind::KeywordLet:
        return parseLetStatement();
    case TokenKind::KeywordVar:
        return parseVarStatement();
    case TokenKind::KeywordIf:
        return parseIf(use);
    case TokenKind::KeywordMatch:
        return parseMatch(use);
    case TokenKind::KeywordWhile:
        return parseWhile(checkpoint());
    case TokenKind::KeywordFor:
        return parseFor(checkpoint());
    case TokenKind::KeywordLoop:
        return parseLoop(checkpoint());
    case TokenKind::Label:
        return parseLabeledLoop();
    case TokenKind::KeywordDefer:
        return parseDefer();
    case TokenKind::KeywordReturn:
        return parseReturn();
    case TokenKind::KeywordBreak:
        return parseJump(SyntaxKind::BreakStatement);
    case TokenKind::KeywordContinue:
        return parseJump(SyntaxKind::ContinueStatement);
    case TokenKind::Semicolon:
        parseStraySemicolon();
        return;
    default:
        break;
    }
    if (atWord(kWordNew) && kindAhead(1) == TokenKind::Identifier) {
        return parsePawnDeclaration();
    }
    parseExpressionStatement(use);
}

void Parser::parsePawnDeclaration() {
    report(DiagnosticCode::PawnDeclaration, rangeAhead(), "Rook declares with `let` and `var`",
           "Pawn's `new`", "write `let name = value;`, or `var name = value;` for one that changes");
    startNode(SyntaxKind::Error);
    while (!at(TokenKind::Semicolon) && !at(TokenKind::RightBrace) && !atEnd()) {
        bump();
    }
    eat(TokenKind::Semicolon);
    finishNode();
}

void Parser::parseLetStatement() {
    startNode(SyntaxKind::LetStatement);
    bump();
    parsePattern();
    if (eat(TokenKind::Colon)) {
        parseType();
    }
    if (eat(TokenKind::Equal)) {
        parseExpression();
    } else {
        report(DiagnosticCode::VariableWithoutValue, pointAhead(),
               "every variable has a value from its declaration", "expected `=` and a value here",
               "write its value, as `let name = value;`");
    }
    expectSemicolon();
    finishNode();
}

void Parser::parseVarStatement() {
    startNode(SyntaxKind::VarStatement);
    bump();
    expectName();
    if (eat(TokenKind::Colon)) {
        parseType();
    }
    if (eat(TokenKind::Equal)) {
        parseExpression();
    } else {
        report(DiagnosticCode::VariableWithoutValue, pointAhead(),
               "every variable has a value from its declaration", "expected `=` and a value here",
               "write its value, as `var name = value;`");
    }
    expectSemicolon();
    finishNode();
}

auto Parser::atCompoundAssignment() const noexcept -> bool {
    return isArithmetic(kindAhead()) && kindAhead(1) == TokenKind::Equal && areAdjacent(0);
}

auto Parser::atIncrement() const noexcept -> bool {
    const TokenKind kind = kindAhead();
    if (kind != TokenKind::Plus && kind != TokenKind::Minus) {
        return false;
    }
    const TokenKind after = kindAhead(2);
    return kindAhead(1) == kind && areAdjacent(0) &&
           (after == TokenKind::Semicolon || after == TokenKind::RightParenthesis ||
            after == TokenKind::RightBrace);
}

void Parser::parseCompoundAssignment(Checkpoint start) {
    report(DiagnosticCode::CompoundAssignment,
           TextRange{.start = rangeAhead().start, .end = rangeAhead(1).end}, "Rook has one assignment",
           "not an operator of Rook", "write it in full, as `count = count + 1;`");
    startNodeAt(start, SyntaxKind::AssignmentStatement);
    const bool isIncrement = atIncrement();
    bump();
    bump();
    if (!isIncrement) {
        parseExpression();
    }
    expectSemicolon();
    finishNode();
}

void Parser::parseExpressionStatement(BlockUse use) {
    const Checkpoint start = checkpoint();
    const std::size_t before = cursor;
    parseExpression();
    if (cursor == before) {
        return; // the expression reported why, and the block recovers
    }
    if (at(TokenKind::Equal)) {
        if (const auto target = builder.lastChildKind(); !target || !isAssignable(*target)) {
            report(DiagnosticCode::InvalidAssignmentTarget, rangeAhead(), "this cannot be assigned to",
                   "not a variable", "assign to a variable, a field or an element");
        }
        startNodeAt(start, SyntaxKind::AssignmentStatement);
        bump();
        parseExpression();
        expectSemicolon();
        finishNode();
        return;
    }
    if (atCompoundAssignment() || atIncrement()) {
        parseCompoundAssignment(start);
        return;
    }
    if (use == BlockUse::Value && at(TokenKind::RightBrace)) {
        return; // the block's value
    }
    startNodeAt(start, SyntaxKind::ExpressionStatement);
    expectSemicolon();
    finishNode();
}

void Parser::parseLabeledLoop() {
    const Checkpoint start = checkpoint();
    bump();
    expect(TokenKind::Colon);
    switch (kindAhead()) {
    case TokenKind::KeywordWhile:
        return parseWhile(start);
    case TokenKind::KeywordFor:
        return parseFor(start);
    case TokenKind::KeywordLoop:
        return parseLoop(start);
    default:
        report(DiagnosticCode::ExpectedLoopAfterLabel, pointAhead(),
               std::format("a label names a loop, found {}", describeToken(kindAhead())),
               "expected a loop here", "write `while`, `for` or `loop` after the label, or remove the label");
        startNodeAt(start, SyntaxKind::Error);
        finishNode();
        return;
    }
}

void Parser::parseWhile(Checkpoint start) {
    startNodeAt(start, SyntaxKind::WhileStatement);
    bump();
    if (eat(TokenKind::LeftParenthesis)) {
        parseExpression();
        parseAssignmentAsValue();
        expectClosingParenthesis();
    } else {
        report(DiagnosticCode::ConditionWithoutParentheses, pointAhead(), "a condition goes in parentheses",
               "expected `(` here", "write `while (condition) { ... }`");
        parseExpression(ExpressionRestrictions{.noStructLiteral = true});
    }
    parseBody(BlockUse::Statements);
    finishNode();
}

void Parser::parseFor(Checkpoint start) {
    startNodeAt(start, SyntaxKind::ForStatement);
    bump();
    const bool hasParenthesis = eat(TokenKind::LeftParenthesis);
    if (!hasParenthesis) {
        report(DiagnosticCode::ConditionWithoutParentheses, pointAhead(),
               "what a `for` walks goes in parentheses", "expected `(` here",
               "write `for (item in items) { ... }`");
    }
    parsePattern();
    if (expect(TokenKind::KeywordIn)) {
        parseExpression(ExpressionRestrictions{.noStructLiteral = !hasParenthesis});
    }
    if (hasParenthesis) {
        expectClosingParenthesis();
    }
    parseBody(BlockUse::Statements);
    finishNode();
}

void Parser::parseLoop(Checkpoint start) {
    startNodeAt(start, SyntaxKind::LoopStatement);
    bump();
    parseBody(BlockUse::Statements);
    finishNode();
}

void Parser::parseDefer() {
    startNode(SyntaxKind::DeferStatement);
    bump();
    parseBody(BlockUse::Statements);
    finishNode();
}

void Parser::parseReturn() {
    startNode(SyntaxKind::ReturnStatement);
    bump();
    if (!at(TokenKind::Semicolon) && !at(TokenKind::RightBrace) && !atEnd()) {
        parseExpression();
    }
    expectSemicolon();
    finishNode();
}

void Parser::parseJump(SyntaxKind kind) {
    startNode(kind);
    bump();
    eat(TokenKind::Label);
    expectSemicolon();
    finishNode();
}

} // namespace rook::compiler
