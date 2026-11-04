// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// Files and declarations: sections 2 and 3 of rook/spec/grammar.ebnf.

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

constexpr std::string_view kWordExtends = "extends";
constexpr std::string_view kWordFrom = "from";
constexpr std::string_view kWordDerive = "derive";

[[nodiscard]] constexpr auto startsFunctionModifiers(TokenKind kind) noexcept -> bool {
    return kind == TokenKind::KeywordFn || kind == TokenKind::KeywordPure || kind == TokenKind::KeywordAsync;
}

} // namespace

void Parser::expectName() {
    if (eat(TokenKind::Identifier)) {
        return;
    }
    if (isReservedWord(kindAhead())) {
        report(DiagnosticCode::ReservedWordAsName, rangeAhead(),
               std::format("{} is a reserved word", describeToken(kindAhead())), "not a name",
               "choose another name");
        bump();
        return;
    }
    report(DiagnosticCode::ExpectedName, pointAhead(),
           std::format("expected a name, found {}", describeToken(kindAhead())), "expected a name here",
           "write a name here");
}

void Parser::expectFieldName() {
    if (at(TokenKind::Identifier) || isReservedWord(kindAhead())) {
        bump();
        return;
    }
    report(DiagnosticCode::ExpectedName, pointAhead(),
           std::format("expected a name, found {}", describeToken(kindAhead())), "expected a name here",
           "write a name here");
}

void Parser::expectSemicolon() {
    if (eat(TokenKind::Semicolon)) {
        return;
    }
    report(DiagnosticCode::MissingSemicolon, pointAfterLastToken(),
           std::format("expected `;`, found {}", describeToken(kindAhead())), "expected `;` here",
           "end this with `;`");
}

void Parser::parseImport() {
    startNode(SyntaxKind::ImportDeclaration);
    bump();
    expectName();
    while (at(TokenKind::Dot)) {
        const TokenKind next = kindAhead(1);
        bump();
        if (next == TokenKind::LeftBrace) {
            parseImportList();
            break;
        }
        if (next == TokenKind::Star) {
            bump();
            break;
        }
        expectName();
    }
    expectSemicolon();
    finishNode();
}

void Parser::parseImportList() {
    startNode(SyntaxKind::ImportList);
    bump();
    parseCommaList(TokenKind::RightBrace, [this] { expectName(); });
    finishNode();
}

void Parser::parsePlugin() {
    startNode(SyntaxKind::PluginDeclaration);
    bump();
    parseFieldInitializers();
    finishNode();
}

auto Parser::beginItem() -> Checkpoint {
    // A documentation comment belongs to what follows it: everything before the first one goes to
    // the node open now, and the node about to open gets the rest.
    const std::size_t next = significant[cursor];
    while (emitted < next && tokens[emitted].kind != TokenKind::DocumentationComment) {
        builder.addToken(tokens[emitted]);
        emitted += 1;
    }
    return builder.checkpoint();
}

void Parser::parseAttributes() {
    while (at(TokenKind::At)) {
        startNode(SyntaxKind::Attribute);
        bump();
        expectName();
        if (at(TokenKind::LeftParenthesis)) {
            parseArgumentList();
        }
        finishNode();
    }
}

void Parser::parseItem() {
    if (at(TokenKind::Semicolon)) {
        parseStraySemicolon();
        return;
    }
    if (at(TokenKind::KeywordImport)) {
        report(DiagnosticCode::ImportAfterDeclaration, rangeAhead(),
               "an `import` comes before the declarations", "after a declaration",
               "move this `import` to the top of the file");
        parseImport();
        return;
    }
    if (at(TokenKind::KeywordPlugin)) {
        report(DiagnosticCode::MisplacedPlugin, rangeAhead(),
               "a file has one `plugin`, right after its imports", "misplaced",
               "move this to right after the imports, once");
        parsePlugin();
        return;
    }
    const Checkpoint start = beginItem();
    parseAttributes();
    eat(TokenKind::KeywordPub);
    parseDeclaration(start);
}

void Parser::parseDeclaration(Checkpoint start) {
    switch (kindAhead()) {
    case TokenKind::KeywordFn:
    case TokenKind::KeywordPure:
    case TokenKind::KeywordAsync:
        return parseFunction(start);
    case TokenKind::KeywordConst:
        if (startsFunctionModifiers(kindAhead(1))) {
            return parseFunction(start);
        }
        return parseConstant(start);
    case TokenKind::KeywordForward:
        return parseForward(start);
    case TokenKind::KeywordStruct:
        return parseStruct(start, SyntaxKind::StructDeclaration);
    case TokenKind::KeywordComponent:
        return parseStruct(start, SyntaxKind::ComponentDeclaration);
    case TokenKind::KeywordEnum:
        return parseEnum(start);
    case TokenKind::KeywordError:
        if (kindAhead(1) == TokenKind::KeywordEnum) {
            return parseEnum(start);
        }
        break;
    case TokenKind::KeywordOpen:
    case TokenKind::KeywordAbstract:
    case TokenKind::KeywordClass:
        return parseClass(start);
    case TokenKind::KeywordInterface:
        return parseInterface(start);
    case TokenKind::KeywordImpl:
        return parseImplementation(start);
    case TokenKind::KeywordNewtype:
        return parseNewtype(start);
    case TokenKind::KeywordHandle:
        return parseHandle(start);
    case TokenKind::KeywordVars:
        return parseVars(start);
    case TokenKind::KeywordVar:
    case TokenKind::KeywordPersist:
        return parseGlobal(start);
    case TokenKind::KeywordType:
        return parseTypeAlias(start);
    default:
        break;
    }
    report(DiagnosticCode::ExpectedDeclaration, pointAhead(),
           std::format("expected a declaration, found {}", describeToken(kindAhead())),
           "expected a declaration here",
           "a file holds imports and declarations such as `fn` and `struct`; a statement goes in a function");
    startNodeAt(start, SyntaxKind::Error);
    skipItemRecovery();
    finishNode();
}

void Parser::parseFunction(Checkpoint start) {
    startNodeAt(start, SyntaxKind::FunctionDeclaration);
    if (!eat(TokenKind::KeywordConst)) {
        eat(TokenKind::KeywordPure);
    }
    eat(TokenKind::KeywordAsync);
    expect(TokenKind::KeywordFn);
    expectName();
    if (at(TokenKind::Less)) {
        parseGenericParameters();
    }
    parseParameterList();
    if (eat(TokenKind::Arrow)) {
        parseType();
    }
    parseFunctionBody();
    finishNode();
}

void Parser::parseFunctionBody() {
    if (at(TokenKind::LeftBrace)) {
        parseBlock(BlockUse::Statements);
    } else if (!eat(TokenKind::Semicolon)) {
        report(DiagnosticCode::BodyWithoutBraces, pointAhead(),
               std::format("expected the body in `{{`, found {}", describeToken(kindAhead())),
               "expected a body here", "write the body as `{ ... }`, or `;` for a declaration without one");
    }
}

void Parser::parseForward(Checkpoint start) {
    startNodeAt(start, SyntaxKind::ForwardDeclaration);
    bump();
    expectName();
    parseParameterList();
    if (eat(TokenKind::Arrow)) {
        parseType();
    }
    parseFunctionBody();
    finishNode();
}

void Parser::parseParameterList() {
    startNode(SyntaxKind::ParameterList);
    if (eat(TokenKind::LeftParenthesis)) {
        parseCommaList(TokenKind::RightParenthesis, [this] { parseParameter(); });
    } else {
        reportExpectedToken(TokenKind::LeftParenthesis);
    }
    finishNode();
}

void Parser::parseParameter() {
    startNode(SyntaxKind::Parameter);
    if (eat(TokenKind::KeywordSelf)) {
        finishNode();
        return;
    }
    eat(TokenKind::KeywordInout);
    expectName();
    expect(TokenKind::Colon);
    parseType();
    if (eat(TokenKind::Equal)) {
        parseExpression();
    }
    finishNode();
}

void Parser::parseGenericParameters() {
    startNode(SyntaxKind::GenericParameterList);
    bump();
    parseCommaList(TokenKind::Greater, [this] {
        startNode(SyntaxKind::GenericParameter);
        expectName();
        if (eat(TokenKind::Colon)) {
            parsePathType();
        }
        finishNode();
    });
    finishNode();
}

void Parser::parseFieldList() {
    startNode(SyntaxKind::FieldList);
    if (eat(TokenKind::LeftBrace)) {
        parseCommaList(TokenKind::RightBrace, [this] { parseField(); });
    } else {
        reportExpectedToken(TokenKind::LeftBrace);
    }
    finishNode();
}

void Parser::parseField() {
    startNodeAt(beginItem(), SyntaxKind::Field);
    parseAttributes();
    expectFieldName();
    expect(TokenKind::Colon);
    parseType();
    if (eat(TokenKind::Equal)) {
        parseExpression();
    }
    finishNode();
}

void Parser::parseStruct(Checkpoint start, SyntaxKind kind) {
    startNodeAt(start, kind);
    bump();
    expectName();
    if (kind == SyntaxKind::StructDeclaration && at(TokenKind::Less)) {
        parseGenericParameters();
    }
    parseFieldList();
    finishNode();
}

void Parser::parseEnum(Checkpoint start) {
    startNodeAt(start, SyntaxKind::EnumDeclaration);
    eat(TokenKind::KeywordError);
    bump();
    expectName();
    if (at(TokenKind::Less)) {
        parseGenericParameters();
    }
    if (eat(TokenKind::LeftBrace)) {
        parseCommaList(TokenKind::RightBrace, [this] { parseVariant(); });
    } else {
        reportExpectedToken(TokenKind::LeftBrace);
    }
    finishNode();
}

void Parser::parseVariant() {
    startNodeAt(beginItem(), SyntaxKind::Variant);
    parseAttributes();
    if (atWord(kWordFrom) && kindAhead(1) == TokenKind::Identifier) {
        bump();
    }
    expectName();
    if (eat(TokenKind::LeftParenthesis)) {
        parseCommaList(TokenKind::RightParenthesis, [this] { parseVariantField(); });
    }
    finishNode();
}

void Parser::parseVariantField() {
    startNode(SyntaxKind::VariantField);
    if (atFieldLabel()) {
        bump();
        bump();
    }
    parseType();
    finishNode();
}

void Parser::parseClass(Checkpoint start) {
    startNodeAt(start, SyntaxKind::ClassDeclaration);
    if (!eat(TokenKind::KeywordOpen)) {
        eat(TokenKind::KeywordAbstract);
    }
    expect(TokenKind::KeywordClass);
    expectName();
    if (at(TokenKind::Less)) {
        parseGenericParameters();
    }
    if (atWord(kWordExtends)) {
        bump();
        parsePathType();
    }
    parseMemberList();
    finishNode();
}

void Parser::parseInterface(Checkpoint start) {
    startNodeAt(start, SyntaxKind::InterfaceDeclaration);
    bump();
    expectName();
    if (at(TokenKind::Less)) {
        parseGenericParameters();
    }
    parseMemberList();
    finishNode();
}

void Parser::parseImplementation(Checkpoint start) {
    startNodeAt(start, SyntaxKind::ImplementationDeclaration);
    bump();
    parsePathType();
    expect(TokenKind::KeywordFor);
    parsePathType();
    parseMemberList();
    finishNode();
}

void Parser::parseMemberList() {
    const NestingScope scope{*this};
    if (!scope) {
        return;
    }
    startNode(SyntaxKind::MemberList);
    if (!eat(TokenKind::LeftBrace)) {
        reportExpectedToken(TokenKind::LeftBrace);
        finishNode();
        return;
    }
    while (!at(TokenKind::RightBrace) && !atEnd()) {
        const std::size_t before = cursor;
        parseMember();
        if (cursor == before) {
            skipMemberRecovery();
        }
    }
    expect(TokenKind::RightBrace);
    finishNode();
}

void Parser::parseMember() {
    if (at(TokenKind::Semicolon)) {
        parseStraySemicolon();
        return;
    }
    const Checkpoint start = beginItem();
    parseAttributes();
    eat(TokenKind::KeywordPub);
    const bool hasModifier =
        eat(TokenKind::KeywordOpen) || eat(TokenKind::KeywordAbstract) || eat(TokenKind::KeywordOverride);
    if (at(TokenKind::KeywordConst) || startsFunctionModifiers(kindAhead())) {
        parseFunction(start);
    } else if (!hasModifier && (at(TokenKind::KeywordLet) || at(TokenKind::KeywordVar))) {
        parseMemberVariable(start);
    } else {
        report(DiagnosticCode::ExpectedMember, pointAhead(),
               std::format("expected a function or a variable of this type, found {}",
                           describeToken(kindAhead())),
               "expected a member here", "a type holds `fn`, `let` and `var` declarations");
    }
}

void Parser::parseMemberVariable(Checkpoint start) {
    startNodeAt(start, SyntaxKind::MemberVariable);
    bump();
    expectName();
    expect(TokenKind::Colon);
    parseType();
    if (eat(TokenKind::Equal)) {
        parseExpression();
    }
    expectSemicolon();
    finishNode();
}

void Parser::parseNewtype(Checkpoint start) {
    startNodeAt(start, SyntaxKind::NewtypeDeclaration);
    bump();
    expectName();
    expect(TokenKind::LeftParenthesis);
    parseType();
    expect(TokenKind::RightParenthesis);
    if (at(TokenKind::KeywordSuffix)) {
        startNode(SyntaxKind::SuffixClause);
        bump();
        expectName();
        while (eat(TokenKind::Comma)) {
            expectName();
        }
        finishNode();
    }
    if (at(TokenKind::KeywordDimension)) {
        startNode(SyntaxKind::DimensionClause);
        bump();
        expect(TokenKind::LeftParenthesis);
        parseType();
        if (!eat(TokenKind::Slash) && !eat(TokenKind::Star)) {
            reportExpectedToken(TokenKind::Slash);
        }
        parseType();
        expect(TokenKind::RightParenthesis);
        finishNode();
    }
    if (at(TokenKind::KeywordRange)) {
        startNode(SyntaxKind::RangeClause);
        bump();
        expect(TokenKind::LeftParenthesis);
        parseExpression();
        expect(TokenKind::RightParenthesis);
        finishNode();
    }
    if (atWord(kWordDerive)) {
        startNode(SyntaxKind::DeriveClause);
        bump();
        if (eat(TokenKind::LeftParenthesis)) {
            parseCommaList(TokenKind::RightParenthesis, [this] { expectName(); });
        } else {
            reportExpectedToken(TokenKind::LeftParenthesis);
        }
        finishNode();
    }
    expectSemicolon();
    finishNode();
}

void Parser::parseHandle(Checkpoint start) {
    startNodeAt(start, SyntaxKind::HandleDeclaration);
    bump();
    expectName();
    if (atWord(kWordExtends)) {
        bump();
        parsePathType();
    }
    expectSemicolon();
    finishNode();
}

void Parser::parseVars(Checkpoint start) {
    startNodeAt(start, SyntaxKind::VarsDeclaration);
    bump();
    expectName();
    if (eat(TokenKind::LeftBrace)) {
        parseCommaList(TokenKind::RightBrace, [this] { parseVarsField(); });
    } else {
        reportExpectedToken(TokenKind::LeftBrace);
    }
    finishNode();
}

void Parser::parseVarsField() {
    startNodeAt(beginItem(), SyntaxKind::VarsField);
    parseAttributes();
    // `readonly` is the mark only before a name: a field may be named so, as any reserved word.
    if (at(TokenKind::KeywordReadonly) && kindAhead(1) != TokenKind::Colon) {
        bump();
    }
    expectFieldName();
    expect(TokenKind::Colon);
    parseType();
    if (eat(TokenKind::KeywordNarrows)) {
        parseType();
    }
    if (eat(TokenKind::Equal)) {
        parseExpression();
    }
    finishNode();
}

void Parser::parseValueWithType(std::string_view what) {
    if (eat(TokenKind::Colon)) {
        parseType();
    } else {
        report(DiagnosticCode::DeclarationWithoutType, pointAhead(), std::format("{} names its type", what),
               "expected `:` and a type here",
               "write the type, as `name: Type = value`; only function bodies infer");
    }
    if (eat(TokenKind::Equal)) {
        parseExpression();
    } else {
        report(DiagnosticCode::VariableWithoutValue, pointAhead(),
               std::format("{} has a value from the start", what), "expected `=` and a value here",
               "write its value, as `name: Type = value`");
    }
    expectSemicolon();
}

void Parser::parseConstant(Checkpoint start) {
    startNodeAt(start, SyntaxKind::ConstantDeclaration);
    bump();
    expectName();
    parseValueWithType("a constant");
    finishNode();
}

void Parser::parseGlobal(Checkpoint start) {
    startNodeAt(start, SyntaxKind::GlobalDeclaration);
    eat(TokenKind::KeywordPersist);
    expect(TokenKind::KeywordVar);
    expectName();
    parseValueWithType("a global variable");
    finishNode();
}

void Parser::parseTypeAlias(Checkpoint start) {
    startNodeAt(start, SyntaxKind::TypeAlias);
    bump();
    expectName();
    if (at(TokenKind::Less)) {
        parseGenericParameters();
    }
    expect(TokenKind::Equal);
    parseType();
    expectSemicolon();
    finishNode();
}

void Parser::parseFieldInitializers() {
    startNode(SyntaxKind::FieldInitializerList);
    if (eat(TokenKind::LeftBrace)) {
        parseCommaList(TokenKind::RightBrace, [this] { parseFieldInitializer(); });
    } else {
        reportExpectedToken(TokenKind::LeftBrace);
    }
    finishNode();
}

void Parser::parseFieldInitializer() {
    startNode(SyntaxKind::FieldInitializer);
    expectFieldName();
    expect(TokenKind::Colon);
    parseExpression();
    finishNode();
}

} // namespace rook::compiler
