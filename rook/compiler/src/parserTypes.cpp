// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// Types and patterns: sections 4 and 7 of rook/spec/grammar.ebnf.

module rook.compiler;

import std;

namespace rook::compiler {

void Parser::parseType() {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    const Checkpoint start = checkpoint();
    switch (kindAhead()) {
    case TokenKind::Identifier:
    case TokenKind::KeywordVoid:
        parsePathType();
        break;
    case TokenKind::LeftParenthesis:
        parseTupleType();
        break;
    case TokenKind::LeftBracket:
        parseArrayType();
        break;
    case TokenKind::KeywordFn:
        parseFunctionType();
        break;
    default:
        report(DiagnosticCode::ExpectedType, pointAhead(),
               std::format("expected a type, found {}", describeToken(kindAhead())), "expected a type here",
               "write a type here, such as `i32` or `List<Player>`");
        return;
    }
    if (at(TokenKind::Question)) {
        startNodeAt(start, SyntaxKind::OptionalType);
        bump();
        finishNode();
    }
}

void Parser::parsePathType() {
    startNode(SyntaxKind::PathType);
    if (eat(TokenKind::KeywordVoid)) {
        finishNode();
        return;
    }
    expectName();
    while (at(TokenKind::Dot) && kindAhead(1) == TokenKind::Identifier) {
        bump();
        bump();
    }
    if (at(TokenKind::Less)) {
        parseGenericArguments();
    }
    finishNode();
}

void Parser::parseGenericArguments() {
    startNode(SyntaxKind::GenericArgumentList);
    bump();
    parseCommaList(TokenKind::Greater, [this] { parseType(); });
    finishNode();
}

void Parser::parseTupleType() {
    startNode(SyntaxKind::TupleType);
    bump();
    parseCommaList(TokenKind::RightParenthesis, [this] { parseType(); });
    finishNode();
}

void Parser::parseArrayType() {
    startNode(SyntaxKind::ArrayType);
    bump();
    parseType();
    expect(TokenKind::Semicolon);
    parseExpression();
    expect(TokenKind::RightBracket);
    finishNode();
}

void Parser::parseFunctionType() {
    startNode(SyntaxKind::FunctionType);
    bump();
    if (eat(TokenKind::LeftParenthesis)) {
        parseCommaList(TokenKind::RightParenthesis, [this] { parseType(); });
    } else {
        reportExpectedToken(TokenKind::LeftParenthesis);
    }
    if (eat(TokenKind::Arrow)) {
        parseType();
    }
    finishNode();
}

void Parser::parsePattern() {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    const Checkpoint start = checkpoint();
    parsePrimaryPattern();
    if (!at(TokenKind::Pipe)) {
        return;
    }
    startNodeAt(start, SyntaxKind::OrPattern);
    while (eat(TokenKind::Pipe)) {
        parsePrimaryPattern();
    }
    finishNode();
}

void Parser::parsePrimaryPattern() {
    switch (kindAhead()) {
    case TokenKind::Underscore:
        startNode(SyntaxKind::WildcardPattern);
        bump();
        finishNode();
        return;
    case TokenKind::Minus:
    case TokenKind::IntegerLiteral:
    case TokenKind::FloatLiteral:
    case TokenKind::CharacterLiteral:
    case TokenKind::RawStringLiteral:
    case TokenKind::KeywordTrue:
    case TokenKind::KeywordFalse:
    case TokenKind::KeywordNone:
        startNode(SyntaxKind::LiteralPattern);
        if (eat(TokenKind::Minus) && !at(TokenKind::IntegerLiteral) && !at(TokenKind::FloatLiteral)) {
            report(DiagnosticCode::ExpectedPattern, pointAhead(),
                   std::format("expected a number after `-`, found {}", describeToken(kindAhead())),
                   "expected a number here", "write a number after the `-`");
        } else {
            bump();
        }
        finishNode();
        return;
    case TokenKind::StringStart:
        startNode(SyntaxKind::LiteralPattern);
        parseString();
        finishNode();
        return;
    case TokenKind::KeywordSome:
    case TokenKind::KeywordOk:
    case TokenKind::KeywordErr:
        parseWrapperPattern();
        return;
    case TokenKind::Identifier:
        parseNamePattern(SyntaxKind::NamePattern);
        return;
    case TokenKind::Dot:
        parseNamePattern(SyntaxKind::MemberPattern);
        return;
    case TokenKind::LeftParenthesis:
        parseTuplePattern();
        return;
    default:
        if (isReservedWord(kindAhead()) && kindAhead() != TokenKind::KeywordIn) {
            expectName();
            return;
        }
        report(DiagnosticCode::ExpectedPattern, pointAhead(),
               std::format("expected a pattern, found {}", describeToken(kindAhead())),
               "expected a pattern here", "write a pattern here, such as a name, `_` or `.Variant`");
        return;
    }
}

void Parser::parseNamePattern(SyntaxKind kind) {
    startNode(kind);
    if (eat(TokenKind::Dot)) {
        expectName();
    } else {
        bump();
    }
    while (at(TokenKind::Dot) && kindAhead(1) == TokenKind::Identifier) {
        bump();
        bump();
    }
    if (at(TokenKind::LeftParenthesis)) {
        parsePatternFields();
    }
    finishNode();
}

void Parser::parseWrapperPattern() {
    startNode(SyntaxKind::WrapperPattern);
    bump();
    if (expect(TokenKind::LeftParenthesis)) {
        if (!at(TokenKind::RightParenthesis)) {
            parsePattern();
        }
        expect(TokenKind::RightParenthesis);
    }
    finishNode();
}

void Parser::parseTuplePattern() {
    startNode(SyntaxKind::TuplePattern);
    bump();
    parseCommaList(TokenKind::RightParenthesis, [this] { parsePattern(); });
    finishNode();
}

void Parser::parsePatternFields() {
    startNode(SyntaxKind::PatternFieldList);
    bump();
    parseCommaList(TokenKind::RightParenthesis, [this] { parsePatternField(); });
    finishNode();
}

void Parser::parsePatternField() {
    startNode(SyntaxKind::PatternField);
    if (atFieldLabel()) {
        bump();
        bump();
    }
    parsePattern();
    finishNode();
}

} // namespace rook::compiler
