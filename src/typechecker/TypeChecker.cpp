#include "TypeChecker.h"
#include "MiniGoParser.h"
#include "errors/ErrorUtils.h"
#include <sstream>

using namespace std;
using namespace antlr4;


// Construcción
TypeChecker::TypeChecker() {
    // Predeclared bool constants (not keywords — parsed as IDENTIFIER)
    Symbol trueSym;  trueSym.name  = "true";  trueSym.type  = makeBool(); trueSym.isConst = true;
    Symbol falseSym; falseSym.name = "false"; falseSym.type = makeBool(); falseSym.isConst = true;
    symbolTable_.insert(trueSym);
    symbolTable_.insert(falseSym);

    // Tipos built-in disponibles como nombres de tipo en MiniGo
    typeRegistry_["int"]     = makeInt();
    typeRegistry_["int8"]    = makeInt();
    typeRegistry_["int16"]   = makeInt();
    typeRegistry_["int32"]   = makeInt();
    typeRegistry_["int64"]   = makeInt();
    typeRegistry_["uint"]    = makeInt();
    typeRegistry_["uint8"]   = makeInt();
    typeRegistry_["uint16"]  = makeInt();
    typeRegistry_["uint32"]  = makeInt();
    typeRegistry_["uint64"]  = makeInt();
    typeRegistry_["byte"]    = makeInt();
    typeRegistry_["float32"] = makeFloat();
    typeRegistry_["float64"] = makeFloat();
    typeRegistry_["bool"]    = makeBool();
    typeRegistry_["rune"]    = makeRune();
    typeRegistry_["string"]  = makeString();
}

bool TypeChecker::hasErrors() const { return !errors_.empty(); }
const vector<string>& TypeChecker::getErrors() const { return errors_; }
const map<tree::ParseTree*, Symbol*>& TypeChecker::getReferenceMap() const { return referenceMap_; }
const map<string, TypeInfo>& TypeChecker::getTypeRegistry() const { return typeRegistry_; }
const map<antlr4::ParserRuleContext*, map<string, Symbol*>>& TypeChecker::getDeclSymbolMap() const {
    return declSymbolMap_;
}


// Reportes de error (mismo formato que AlphaErrorListener)
void TypeChecker::reportError(const string& msg, Token* tok) {
    ostringstream oss;
    oss << "TYPE ERROR: " << msg
        << " (line: " << tok->getLine()
        << ", column: " << (tok->getCharPositionInLine() + 1) << ")";
    errors_.push_back(oss.str());
}

void TypeChecker::reportError(const string& msg, Token* tok,
                               const TypeInfo& expected, const TypeInfo& actual) {
    ostringstream oss;
    oss << "TYPE ERROR: " << msg
        << " - expected: " << expected.toString()
        << " but got: " << actual.toString()
        << " (line: " << tok->getLine()
        << ", column: " << (tok->getCharPositionInLine() + 1) << ")";
    errors_.push_back(oss.str());
}

void TypeChecker::reportScopeError(const string& msg, Token* tok) {
    ostringstream oss;
    oss << "SCOPE ERROR: " << msg
        << " (line: " << tok->getLine()
        << ", column: " << (tok->getCharPositionInLine() + 1) << ")";
    errors_.push_back(oss.str());
}


// Utilidades
TypeInfo TypeChecker::unpackType(const any& a) {
    if (!a.has_value()) return makeUnknown();
    try { return any_cast<TypeInfo>(a); }
    catch (...) { return makeUnknown(); }
}

TypeInfo TypeChecker::resolveTypeName(const string& name, Token* tok) {
    auto it = typeRegistry_.find(name);
    if (it != typeRegistry_.end()) return it->second;
    string msg = "Undefined type '" + name + "'";
    auto suggestion = findBestMatch(name);
    if (suggestion) msg += "; did you mean '" + *suggestion + "'?";
    reportScopeError(msg, tok);
    return makeUnknown();
}

TypeInfo TypeChecker::buildDeclType(MiniGoParser::DeclTypeContext* ctx) {
    if (!ctx) return makeUnknown();

    if (auto* named = dynamic_cast<MiniGoParser::NamedDeclTypeContext*>(ctx))
        return resolveTypeName(named->IDENTIFIER()->getText(),
                               named->IDENTIFIER()->getSymbol());

    if (auto* paren = dynamic_cast<MiniGoParser::ParenDeclTypeContext*>(ctx))
        return buildDeclType(paren->declType());

    if (auto* sl = dynamic_cast<MiniGoParser::SliceDeclTypeContext*>(ctx))
        return makeSlice(buildDeclType(sl->declType()));

    if (auto* arr = dynamic_cast<MiniGoParser::ArrayDeclTypeContext*>(ctx)) {
        int64_t size = stoll(arr->INTLITERAL()->getText());
        return makeArray(size, buildDeclType(arr->declType()));
    }

    if (auto* st = dynamic_cast<MiniGoParser::StructDeclTypeContext*>(ctx)) {
        TypeInfo t;
        t.kind = TypeKind::STRUCT;
        if (st->structMemDecls()) {
            for (auto* mem : st->structMemDecls()->singleVarDeclNoExps()) {
                TypeInfo ftype = buildDeclType(mem->declType());
                for (auto* id : mem->identifierList()->IDENTIFIER()) {
                    t.fieldNames.push_back(id->getText());
                    t.fieldTypes.push_back(ftype);
                }
            }
        }
        return t;
    }

    return makeUnknown();
}

vector<TypeInfo> TypeChecker::visitExprList(MiniGoParser::ExpressionListContext* ctx) {
    vector<TypeInfo> result;
    if (!ctx) return result;
    for (auto* e : ctx->expression())
        result.push_back(unpackType(visit(e)));
    return result;
}

bool TypeChecker::isPrimitive(const TypeInfo& t) const {
    return t.kind == TypeKind::INT   || t.kind == TypeKind::FLOAT  ||
           t.kind == TypeKind::BOOL  || t.kind == TypeKind::RUNE   ||
           t.kind == TypeKind::STRING;
}

optional<string> TypeChecker::findBestMatch(const string& name) const {
    // Pool de candidatos: símbolos visibles + tipos registrados
    vector<string> extras = symbolTable_.getAllVisibleNames();
    for (const auto& [tname, _] : typeRegistry_)
        extras.push_back(tname);

    string suggestion = minigoSuggest(name, extras);
    if (suggestion.empty()) return nullopt;
    return suggestion;
}

TypeInfo TypeChecker::binaryResultType(const string& op,
                                        const TypeInfo& L, const TypeInfo& R,
                                        Token* tok) {
    if (L.kind == TypeKind::UNKNOWN || R.kind == TypeKind::UNKNOWN) return makeUnknown();

    // MiniGo (a diferencia de Go) NO permite operar con strings: las cadenas
    // solo se imprimen (println). La gramatica acepta '+' entre cualquier par
    // de expresiones, pero aqui rechazamos la concatenacion con un error claro
    // en vez de aceptarla y fallar luego en la generacion de codigo.
    if (op == "+" && (L.kind == TypeKind::STRING || R.kind == TypeKind::STRING)) {
        reportError("String concatenation is not supported in MiniGo; "
                    "strings can only be used inside println", tok);
        return makeUnknown();
    }

    // Arithmetic
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        if (L.isNumeric() && L == R) return L;
        reportError("Operands of '" + op + "' must be numeric and matching", tok, L, R);
        return makeUnknown();
    }
    // Bitwise
    if (op == "&" || op == "|" || op == "^" || op == "&^" || op == "<<" || op == ">>") {
        if ((L.kind == TypeKind::INT || L.kind == TypeKind::RUNE) && L == R) return L;
        reportError("Bitwise operator '" + op + "' requires integer operands", tok, L, R);
        return makeUnknown();
    }
    // Equality
    if (op == "==" || op == "!=") {
        if (L == R) return makeBool();
        reportError("Operands of '" + op + "' must have the same type", tok, L, R);
        return makeUnknown();
    }
    // Ordered comparisons
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        if (L.isOrdered() && L == R) return makeBool();
        reportError("Operands of '" + op + "' must be ordered and matching", tok, L, R);
        return makeUnknown();
    }
    // Logical
    if (op == "&&" || op == "||") {
        if (L.kind == TypeKind::BOOL && R.kind == TypeKind::BOOL) return makeBool();
        reportError("Logical operator '" + op + "' requires bool operands", tok, L, R);
        return makeUnknown();
    }
    return makeUnknown();
}


// Root
any TypeChecker::visitRoot(MiniGoParser::RootContext* ctx) {
    //  Verificar nombre del package 
    auto* pkgTok = ctx->IDENTIFIER()->getSymbol();
    if (pkgTok->getText() != "main")
        reportScopeError("Package name must be 'main' in MiniGo; found '"
                         + pkgTok->getText() + "'", pkgTok);

    //  Recorrido en 3 pasadas para soportar referencias hacia adelante
    //  (una función puede llamar a otra declarada más abajo, y la recursión
    //   mutua es válida, igual que en Go).
    auto* topList = ctx->topDeclarationList();

    // Pasada 1: registrar todos los tipos declarados (type ...)
    for (auto* child : topList->children)
        if (auto* td = dynamic_cast<MiniGoParser::TypeDeclContext*>(child))
            visit(td);

    // Pasada 2: registrar las firmas de todas las funciones
    for (auto* child : topList->children)
        if (auto* fd = dynamic_cast<MiniGoParser::FuncDeclContext*>(child))
            registerFuncSignature(fd);

    // Pasada 3: variables globales + cuerpos de funciones (en orden de origen)
    for (auto* child : topList->children) {
        if (dynamic_cast<MiniGoParser::TypeDeclContext*>(child)) continue; // ya hecho
        visit(child);
    }

    //  Verificar que exista func main()
    Symbol* mainSym = symbolTable_.lookup("main");
    if (!mainSym || !mainSym->isFunc) {
        reportScopeError("Program must define 'func main()' — no entry point declared",
                         ctx->getStart());
    } else {
        if (!mainSym->type.paramTypes.empty())
            reportScopeError("'main()' must not take any parameters",
                             mainSym->nameTok ? mainSym->nameTok : ctx->getStart());
        if (mainSym->type.returnType &&
            mainSym->type.returnType->kind != TypeKind::VOID)
            reportScopeError("'main()' must not declare a return type",
                             mainSym->nameTok ? mainSym->nameTok : ctx->getStart());
    }

    return {};
}

any TypeChecker::visitTopDeclarationList(MiniGoParser::TopDeclarationListContext* ctx) {
    return visitChildren(ctx);
}


// Variable declarations


any TypeChecker::visitVariableDecl(MiniGoParser::VariableDeclContext* ctx) {
    return visitChildren(ctx);
}

any TypeChecker::visitInnerVarDecls(MiniGoParser::InnerVarDeclsContext* ctx) {
    return visitChildren(ctx);
}

// var a, b int = 1, 2
any TypeChecker::visitVarDeclTypedInit(MiniGoParser::VarDeclTypedInitContext* ctx) {
    TypeInfo declaredType = buildDeclType(ctx->declType());
    auto ids       = ctx->identifierList()->IDENTIFIER();
    auto initTypes = visitExprList(ctx->expressionList());

    if (ids.size() != initTypes.size()) {
        reportError("Identifier count does not match expression count in var declaration",
                    ctx->getStart());
    }
    for (size_t i = 0; i < ids.size(); i++) {
        auto* tok = ids[i]->getSymbol();
        if (declaredType.kind != TypeKind::UNKNOWN && i < initTypes.size() &&
            initTypes[i].kind != TypeKind::UNKNOWN && initTypes[i] != declaredType)
            reportError("Initializer type does not match declared type for '" +
                        tok->getText() + "'", tok, declaredType, initTypes[i]);

        Symbol sym;
        sym.name    = tok->getText();
        sym.type    = declaredType;
        sym.declCtx = ctx;
        sym.nameTok = tok;
        Symbol* inserted = symbolTable_.insert(sym);
        if (!inserted)
            reportScopeError("'" + sym.name + "' already declared in this scope", tok);
        else {
            declSymbolMap_[ctx][sym.name] = inserted;
            if (insideFunction_) currentFuncLocals_.push_back(inserted);
        }
    }
    return {};
}

// var a, b = 1, 2  (inferencia de tipo)
any TypeChecker::visitVarDeclInferred(MiniGoParser::VarDeclInferredContext* ctx) {
    auto ids       = ctx->identifierList()->IDENTIFIER();
    auto initTypes = visitExprList(ctx->expressionList());

    if (ids.size() != initTypes.size())
        reportError("Identifier count does not match expression count in var declaration",
                    ctx->getStart());

    for (size_t i = 0; i < ids.size(); i++) {
        auto* tok = ids[i]->getSymbol();
        TypeInfo t = (i < initTypes.size()) ? initTypes[i] : makeUnknown();
        if (t.kind == TypeKind::UNKNOWN) {
            reportError("Cannot infer type for '" + tok->getText() + "'", tok);
            continue;
        }
        // Spec: inferencia de tipos solamente para tipos primitivos
        if (!isPrimitive(t)) {
            reportError("Type inference (var " + tok->getText() +
                        " = ...) is only allowed for primitive types; "
                        "use an explicit type: var " + tok->getText() + " <type>", tok);
            continue;
        }
        Symbol sym;
        sym.name    = tok->getText();
        sym.type    = t;
        sym.declCtx = ctx;
        sym.nameTok = tok;
        Symbol* inserted = symbolTable_.insert(sym);
        if (!inserted)
            reportScopeError("'" + sym.name + "' already declared in this scope", tok);
        else {
            declSymbolMap_[ctx][sym.name] = inserted;
            if (insideFunction_) currentFuncLocals_.push_back(inserted);
        }
    }
    return {};
}

// var a int  (sin inicializador)
any TypeChecker::visitVarDeclNoInit(MiniGoParser::VarDeclNoInitContext* ctx) {
    return visit(ctx->singleVarDeclNoExps());
}

any TypeChecker::visitSingleVarDeclNoExps(MiniGoParser::SingleVarDeclNoExpsContext* ctx) {
    TypeInfo t = buildDeclType(ctx->declType());
    for (auto* id : ctx->identifierList()->IDENTIFIER()) {
        auto* tok = id->getSymbol();
        if (t.kind == TypeKind::UNKNOWN) {
            // El error ya fue reportado por buildDeclType/resolveTypeName
            continue;
        }
        Symbol sym;
        sym.name    = tok->getText();
        sym.type    = t;
        sym.declCtx = ctx;
        sym.nameTok = tok;
        Symbol* inserted = symbolTable_.insert(sym);
        if (!inserted)
            reportScopeError("'" + sym.name + "' already declared in this scope", tok);
        else {
            declSymbolMap_[ctx][sym.name] = inserted;
            if (insideFunction_) currentFuncLocals_.push_back(inserted);
        }
    }
    return {};
}


// Type declarations
any TypeChecker::visitTypeDecl(MiniGoParser::TypeDeclContext* ctx) {
    return visitChildren(ctx);
}

any TypeChecker::visitSingleTypeDecl(MiniGoParser::SingleTypeDeclContext* ctx) {
    string name = ctx->IDENTIFIER()->getText();
    TypeInfo t  = buildDeclType(ctx->declType());
    t.structName = name;
    typeRegistry_[name] = t;
    return {};
}


// Function declarations
// Pase previo: registra la firma (nombre, tipos de parámetros y de retorno)
// en el scope global. Permite forward refs y recursión mutua.
void TypeChecker::registerFuncSignature(MiniGoParser::FuncDeclContext* ctx) {
    auto* front = ctx->funcFrontDecl();
    auto* idTok = front->IDENTIFIER()->getSymbol();
    string name = idTok->getText();

    TypeInfo retType = front->declType() ? buildDeclType(front->declType()) : makeVoid();

    // Recopilar tipos de parámetros
    vector<TypeInfo> paramTypes;
    if (front->funcArgDecls()) {
        for (auto* arg : front->funcArgDecls()->singleVarDeclNoExps()) {
            TypeInfo pt = buildDeclType(arg->declType());
            for (size_t i = 0; i < arg->identifierList()->IDENTIFIER().size(); i++)
                paramTypes.push_back(pt);
        }
    }

    Symbol funcSym;
    funcSym.name              = name;
    funcSym.isFunc            = true;
    funcSym.declCtx           = front;
    funcSym.nameTok           = idTok;
    funcSym.type.kind         = TypeKind::FUNC;
    funcSym.type.paramTypes   = paramTypes;
    funcSym.type.returnType   = make_shared<TypeInfo>(retType);

    Symbol* insertedFunc = symbolTable_.insert(funcSym);
    if (!insertedFunc)
        reportScopeError("'" + name + "' already declared in this scope", idTok);
    else
        declSymbolMap_[front][name] = insertedFunc;
}

any TypeChecker::visitFuncDecl(MiniGoParser::FuncDeclContext* ctx) {
    auto* front = ctx->funcFrontDecl();
    auto* idTok = front->IDENTIFIER()->getSymbol();
    string name = idTok->getText();

    // La firma ya fue registrada en la pasada previa (registerFuncSignature).
    Symbol* funcSym = symbolTable_.lookup(name);
    TypeInfo retType = (funcSym && funcSym->isFunc && funcSym->type.returnType)
                     ? *funcSym->type.returnType : makeVoid();
    const vector<TypeInfo>* paramTypes =
        (funcSym && funcSym->isFunc) ? &funcSym->type.paramTypes : nullptr;

    // Scope de parámetros + cuerpo
    symbolTable_.openScope();

    if (front->funcArgDecls()) {
        size_t pIdx = 0;
        for (auto* arg : front->funcArgDecls()->singleVarDeclNoExps()) {
            for (auto* paramId : arg->identifierList()->IDENTIFIER()) {
                // Tomamos el tipo ya resuelto en la firma (evita reportar
                // dos veces un tipo de parámetro indefinido).
                TypeInfo pt = (paramTypes && pIdx < paramTypes->size())
                            ? (*paramTypes)[pIdx] : makeUnknown();
                pIdx++;
                Symbol paramSym;
                paramSym.name    = paramId->getText();
                paramSym.type    = pt;
                paramSym.declCtx = arg;
                paramSym.nameTok = paramId->getSymbol();
                Symbol* insertedParam = symbolTable_.insert(paramSym);
                if (!insertedParam)
                    reportScopeError("Duplicate parameter '" + paramSym.name + "'",
                                     paramId->getSymbol());
                else
                    declSymbolMap_[arg][paramSym.name] = insertedParam;
                // Los parámetros NO se agregan a currentFuncLocals_ — están exentos
            }
        }
    }

    // Preparar checks de retorno y variables no usadas
    funcHasReturnStack_.push(false);
    auto savedLocals = move(currentFuncLocals_);  // salvar contexto externo (seguridad)
    currentFuncLocals_.clear();
    insideFunction_ = true;

    returnTypeStack_.push(retType);
    visit(ctx->block());
    returnTypeStack_.pop();

    insideFunction_ = false;

    //  Check 2: función no-void sin return 
    bool hasReturn = funcHasReturnStack_.top();
    funcHasReturnStack_.pop();
    if (retType.kind != TypeKind::VOID && !hasReturn)
        reportError("Function '" + name + "' is declared to return "
                    + retType.toString() + " but has no return statement", idTok);

    //  Check 5: variables locales declaradas pero no usadas 
    for (Symbol* sym : currentFuncLocals_) {
        if (sym->name == "_") continue;  // identificador en blanco — siempre exento
        if (readSymbols_.find(sym) == readSymbols_.end()) {
            auto* errTok = sym->nameTok ? sym->nameTok : sym->declCtx->getStart();
            reportScopeError("Variable '" + sym->name + "' declared but never used", errTok);
        }
    }

    currentFuncLocals_ = move(savedLocals);  // restaurar
    symbolTable_.closeScope();
    return {};
}


// Statements
any TypeChecker::visitStatementList(MiniGoParser::StatementListContext* ctx) {
    return visitChildren(ctx);
}

// block abre su propio scope (independiente de funciones)
any TypeChecker::visitBlock(MiniGoParser::BlockContext* ctx) {
    symbolTable_.openScope();
    visitChildren(ctx);
    symbolTable_.closeScope();
    return {};
}

any TypeChecker::visitPrintStmt(MiniGoParser::PrintStmtContext* ctx) {
    if (ctx->expressionList()) visitExprList(ctx->expressionList());
    return {};
}

any TypeChecker::visitPrintlnStmt(MiniGoParser::PrintlnStmtContext* ctx) {
    if (ctx->expressionList()) visitExprList(ctx->expressionList());
    return {};
}

any TypeChecker::visitReturnStmt(MiniGoParser::ReturnStmtContext* ctx) {
    if (returnTypeStack_.empty()) {
        reportScopeError("'return' statement outside of a function", ctx->getStart());
        return {};
    }
    TypeInfo expected = returnTypeStack_.top();
    if (!ctx->expression()) {
        if (expected.kind != TypeKind::VOID)
            reportError("Missing return value; function expects " + expected.toString(),
                        ctx->getStart());
    } else {
        if (expected.kind == TypeKind::VOID) {
            reportError("Void function cannot return a value", ctx->getStart());
        } else {
            // Marcar que esta función tiene al menos un 'return expr'
            if (!funcHasReturnStack_.empty())
                funcHasReturnStack_.top() = true;
            TypeInfo actual = unpackType(visit(ctx->expression()));
            if (actual.kind != TypeKind::UNKNOWN && actual != expected)
                reportError("Return type mismatch", ctx->getStart(), expected, actual);
        }
    }
    return {};
}

any TypeChecker::visitBreakStmt(MiniGoParser::BreakStmtContext* ctx) {
    if (loopDepth_ == 0 && switchDepth_ == 0)
        reportScopeError("'break' can only be used inside a loop or switch statement",
                         ctx->getStart());
    return {};
}

any TypeChecker::visitContinueStmt(MiniGoParser::ContinueStmtContext* ctx) {
    if (loopDepth_ == 0)
        reportScopeError("'continue' can only be used inside a loop", ctx->getStart());
    return {};
}

any TypeChecker::visitSimpleStmt(MiniGoParser::SimpleStmtContext* ctx) {
    return visit(ctx->simpleStatement());
}

any TypeChecker::visitBlockStmt(MiniGoParser::BlockStmtContext* ctx) {
    return visit(ctx->block());
}

any TypeChecker::visitIfStmt(MiniGoParser::IfStmtContext* ctx) {
    return visit(ctx->ifStatement());
}

any TypeChecker::visitLoopStmt(MiniGoParser::LoopStmtContext* ctx) {
    return visit(ctx->loop());
}

any TypeChecker::visitSwitchStmt(MiniGoParser::SwitchStmtContext* ctx) {
    return visit(ctx->switchStatement());
}

// ---- simpleStatement variants ----

// a, b := expr1, expr2
any TypeChecker::visitShortVarDeclStmt(MiniGoParser::ShortVarDeclStmtContext* ctx) {
    auto leftExprs  = ctx->expressionList(0)->expression();
    auto rightTypes = visitExprList(ctx->expressionList(1));

    if (leftExprs.size() != rightTypes.size()) {
        reportError("Mismatch in ':=' - left and right counts differ", ctx->getStart());
        return {};
    }
    bool anyNew = false, anyExisting = false;
    for (size_t i = 0; i < leftExprs.size(); i++) {
        // Extraer identificador del lado izquierdo
        auto* primExpr = dynamic_cast<MiniGoParser::PrimaryExprContext*>(leftExprs[i]);
        auto* opExpr   = primExpr ? dynamic_cast<MiniGoParser::OperandExprContext*>(
                             primExpr->primaryExpression()) : nullptr;
        auto* idOp     = opExpr ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                             opExpr->operand()) : nullptr;

        if (!idOp) {
            reportScopeError("Left side of ':=' must be an identifier", ctx->getStart());
            continue;
        }

        auto* tok  = idOp->IDENTIFIER()->getSymbol();
        TypeInfo t = (i < rightTypes.size()) ? rightTypes[i] : makeUnknown();
        if (t.kind == TypeKind::UNKNOWN) continue;

        // Si ya existe en el scope actual, es una reasignación; si no, nueva variable
        if (!symbolTable_.lookupCurrent(tok->getText())) {
            // Spec: inferencia de tipos solamente para tipos primitivos
            if (!isPrimitive(t)) {
                reportError("Type inference (':=' for " + tok->getText() +
                            ") is only allowed for primitive types; "
                            "use an explicit type: var " + tok->getText() + " <type>", tok);
                continue;
            }
            Symbol sym;
            sym.name    = tok->getText();
            sym.type    = t;
            sym.declCtx = ctx;
            sym.nameTok = tok;
            Symbol* inserted = symbolTable_.insert(sym);
            if (inserted) {
                anyNew = true;
                declSymbolMap_[ctx][sym.name] = inserted;
                if (insideFunction_) currentFuncLocals_.push_back(inserted);
            }
        } else {
            // Reasignación — visitar la expresión izquierda para registrar en referenceMap_
            anyExisting = true;
            visit(leftExprs[i]);
        }
    }
    // Go: ':=' debe declarar al menos una variable nueva en el lado izquierdo.
    if (!anyNew && anyExisting)
        reportScopeError("No new variables on left side of ':='", ctx->getStart());
    return {};
}

// a = b  (asignación simple o múltiple)
any TypeChecker::visitAssignStmt(MiniGoParser::AssignStmtContext* ctx) {
    auto leftExprs  = ctx->expressionList(0)->expression();
    auto rightTypes = visitExprList(ctx->expressionList(1)); // derecha: son lecturas

    if (leftExprs.size() != rightTypes.size()) {
        reportError("Mismatch in '=' - left and right counts differ", ctx->getStart());
        return {};
    }
    // Izquierda: son escrituras — activar contexto LHS para no marcar como "leídas"
    inLhsContext_ = true;
    for (size_t i = 0; i < leftExprs.size(); i++) {
        TypeInfo L = unpackType(visit(leftExprs[i]));
        TypeInfo R = (i < rightTypes.size()) ? rightTypes[i] : makeUnknown();
        if (L.kind != TypeKind::UNKNOWN && R.kind != TypeKind::UNKNOWN && L != R)
            reportError("Type mismatch in assignment", ctx->getStart(), L, R);
    }
    inLhsContext_ = false;
    return {};
}

// Compound assigns: valida compatibilidad de tipos igual que la operación binaria equivalente
any TypeChecker::visitAddAssignStmt(MiniGoParser::AddAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("+", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitSubAssignStmt(MiniGoParser::SubAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("-", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitMulAssignStmt(MiniGoParser::MulAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("*", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitDivAssignStmt(MiniGoParser::DivAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("/", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitModAssignStmt(MiniGoParser::ModAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("%", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitShlAssignStmt(MiniGoParser::ShlAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("<<", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitShrAssignStmt(MiniGoParser::ShrAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType(">>", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitAndAssignStmt(MiniGoParser::AndAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("&", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitOrAssignStmt(MiniGoParser::OrAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("|", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitXorAssignStmt(MiniGoParser::XorAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("^", L, R, ctx->getStart());
    return {};
}
any TypeChecker::visitAndNotAssignStmt(MiniGoParser::AndNotAssignStmtContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    binaryResultType("&^", L, R, ctx->getStart());
    return {};
}

any TypeChecker::visitIncStmt(MiniGoParser::IncStmtContext* ctx) {
    TypeInfo t = unpackType(visit(ctx->expression()));
    if (t.kind != TypeKind::UNKNOWN && !t.isNumeric())
        reportError("'++' requires numeric operand", ctx->getStart(), makeInt(), t);
    return {};
}

any TypeChecker::visitDecStmt(MiniGoParser::DecStmtContext* ctx) {
    TypeInfo t = unpackType(visit(ctx->expression()));
    if (t.kind != TypeKind::UNKNOWN && !t.isNumeric())
        reportError("'--' requires numeric operand", ctx->getStart(), makeInt(), t);
    return {};
}

any TypeChecker::visitExprStmt(MiniGoParser::ExprStmtContext* ctx) {
    visit(ctx->expression());
    return {};
}

any TypeChecker::visitEmptyStmt(MiniGoParser::EmptyStmtContext*) { return {}; }

// ---- if statements ----

any TypeChecker::visitIfSimple(MiniGoParser::IfSimpleContext* ctx) {
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Condition in 'if' must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    visit(ctx->block());
    return {};
}

any TypeChecker::visitIfElse(MiniGoParser::IfElseContext* ctx) {
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Condition in 'if' must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    visit(ctx->block(0));
    visit(ctx->block(1));
    return {};
}

any TypeChecker::visitIfElseIf(MiniGoParser::IfElseIfContext* ctx) {
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Condition in 'if' must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    visit(ctx->block());
    visit(ctx->ifStatement());
    return {};
}

any TypeChecker::visitIfWithInit(MiniGoParser::IfWithInitContext* ctx) {
    symbolTable_.openScope();
    visit(ctx->simpleStatement());
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Condition in 'if' must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    visit(ctx->block());
    symbolTable_.closeScope();
    return {};
}

any TypeChecker::visitIfWithInitElse(MiniGoParser::IfWithInitElseContext* ctx) {
    symbolTable_.openScope();
    visit(ctx->simpleStatement());
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Condition in 'if' must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    visit(ctx->block(0));
    visit(ctx->block(1));
    symbolTable_.closeScope();
    return {};
}

any TypeChecker::visitIfWithInitElseIf(MiniGoParser::IfWithInitElseIfContext* ctx) {
    symbolTable_.openScope();
    visit(ctx->simpleStatement());
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Condition in 'if' must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    visit(ctx->block());
    visit(ctx->ifStatement());
    symbolTable_.closeScope();
    return {};
}

// ---- loops ----

any TypeChecker::visitInfiniteLoop(MiniGoParser::InfiniteLoopContext* ctx) {
    loopDepth_++;
    visit(ctx->block());
    loopDepth_--;
    return {};
}

any TypeChecker::visitCondLoop(MiniGoParser::CondLoopContext* ctx) {
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Loop condition must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    loopDepth_++;
    visit(ctx->block());
    loopDepth_--;
    return {};
}

any TypeChecker::visitForLoop(MiniGoParser::ForLoopContext* ctx) {
    symbolTable_.openScope();
    visit(ctx->simpleStatement(0));
    TypeInfo cond = unpackType(visit(ctx->expression()));
    if (cond.kind != TypeKind::UNKNOWN && cond.kind != TypeKind::BOOL)
        reportError("Loop condition must be bool",
                    ctx->expression()->getStart(), makeBool(), cond);
    loopDepth_++;
    visit(ctx->block());
    loopDepth_--;
    visit(ctx->simpleStatement(1));
    symbolTable_.closeScope();
    return {};
}

any TypeChecker::visitForLoopNoExpr(MiniGoParser::ForLoopNoExprContext* ctx) {
    symbolTable_.openScope();
    visit(ctx->simpleStatement(0));
    loopDepth_++;
    visit(ctx->block());
    loopDepth_--;
    visit(ctx->simpleStatement(1));
    symbolTable_.closeScope();
    return {};
}

// ---- switch ----

any TypeChecker::visitSwitchWithInit(MiniGoParser::SwitchWithInitContext* ctx) {
    symbolTable_.openScope();
    visit(ctx->simpleStatement());
    TypeInfo switchType = unpackType(visit(ctx->expression()));
    switchTypeStack_.push(switchType);
    switchDepth_++;
    visit(ctx->expressionCaseClauseList());
    switchDepth_--;
    switchTypeStack_.pop();
    symbolTable_.closeScope();
    return {};
}

any TypeChecker::visitSwitchSimple(MiniGoParser::SwitchSimpleContext* ctx) {
    TypeInfo switchType = unpackType(visit(ctx->expression()));
    switchTypeStack_.push(switchType);
    switchDepth_++;
    visit(ctx->expressionCaseClauseList());
    switchDepth_--;
    switchTypeStack_.pop();
    return {};
}

any TypeChecker::visitSwitchInitNoExpr(MiniGoParser::SwitchInitNoExprContext* ctx) {
    symbolTable_.openScope();
    visit(ctx->simpleStatement());
    switchTypeStack_.push(makeUnknown());
    switchDepth_++;
    visit(ctx->expressionCaseClauseList());
    switchDepth_--;
    switchTypeStack_.pop();
    symbolTable_.closeScope();
    return {};
}

any TypeChecker::visitSwitchNoExpr(MiniGoParser::SwitchNoExprContext* ctx) {
    switchTypeStack_.push(makeUnknown());
    switchDepth_++;
    visit(ctx->expressionCaseClauseList());
    switchDepth_--;
    switchTypeStack_.pop();
    return {};
}

any TypeChecker::visitExpressionCaseClause(MiniGoParser::ExpressionCaseClauseContext* ctx) {
    auto* switchCase = ctx->expressionSwitchCase();

    // Verificar tipos de las expresiones del case contra el tipo del switch
    if (auto* caseCtx = dynamic_cast<MiniGoParser::CaseClauseContext*>(switchCase)) {
        for (auto* expr : caseCtx->expressionList()->expression()) {
            TypeInfo caseType = unpackType(visit(expr));
            if (!switchTypeStack_.empty()) {
                TypeInfo switchType = switchTypeStack_.top();
                if (switchType.kind == TypeKind::UNKNOWN) {
                    // switch sin expresión → los cases deben ser bool
                    if (caseType.kind != TypeKind::UNKNOWN && caseType.kind != TypeKind::BOOL)
                        reportError("Case expression in expressionless 'switch' must be bool",
                                    expr->getStart(), makeBool(), caseType);
                } else {
                    // switch con expresión → el tipo del case debe coincidir
                    if (caseType.kind != TypeKind::UNKNOWN &&
                        switchType.kind != TypeKind::UNKNOWN &&
                        caseType != switchType)
                        reportError("Case expression type does not match switch expression type",
                                    expr->getStart(), switchType, caseType);
                }
            }
        }
    }
    // DEFAULT: sin expresiones que verificar

    visit(ctx->statementList());
    return {};
}


// Expressions
any TypeChecker::visitPrimaryExpr(MiniGoParser::PrimaryExprContext* ctx) {
    return visit(ctx->primaryExpression());
}

any TypeChecker::visitUnaryExpr(MiniGoParser::UnaryExprContext* ctx) {
    TypeInfo t  = unpackType(visit(ctx->expression()));
    string   op = ctx->op->getText();

    if (op == "+" || op == "-") {
        if (t.kind != TypeKind::UNKNOWN && !t.isNumeric())
            reportError("Unary '" + op + "' requires numeric type", ctx->op, makeInt(), t);
        return t;
    }
    if (op == "!") {
        if (t.kind != TypeKind::UNKNOWN && t.kind != TypeKind::BOOL)
            reportError("Unary '!' requires bool type", ctx->op, makeBool(), t);
        return makeBool();
    }
    if (op == "^") {
        if (t.kind != TypeKind::UNKNOWN && t.kind != TypeKind::INT && t.kind != TypeKind::RUNE)
            reportError("Unary '^' requires integer type", ctx->op, makeInt(), t);
        return t;
    }
    return t;
}

any TypeChecker::visitMulExpr(MiniGoParser::MulExprContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    return binaryResultType(ctx->op->getText(), L, R, ctx->op);
}

any TypeChecker::visitAddExpr(MiniGoParser::AddExprContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    return binaryResultType(ctx->op->getText(), L, R, ctx->op);
}

any TypeChecker::visitRelExpr(MiniGoParser::RelExprContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    return binaryResultType(ctx->op->getText(), L, R, ctx->op);
}

any TypeChecker::visitAndExpr(MiniGoParser::AndExprContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    return binaryResultType("&&", L, R, ctx->op);
}

any TypeChecker::visitOrExpr(MiniGoParser::OrExprContext* ctx) {
    TypeInfo L = unpackType(visit(ctx->expression(0)));
    TypeInfo R = unpackType(visit(ctx->expression(1)));
    return binaryResultType("||", L, R, ctx->op);
}


// primaryExpression variants


any TypeChecker::visitOperandExpr(MiniGoParser::OperandExprContext* ctx) {
    return visit(ctx->operand());
}

any TypeChecker::visitSelectorExpr(MiniGoParser::SelectorExprContext* ctx) {
    TypeInfo base = unpackType(visit(ctx->primaryExpression()));
    if (base.kind != TypeKind::STRUCT) {
        if (base.kind != TypeKind::UNKNOWN)
            reportError("Selector '.' requires a struct type", ctx->getStart());
        return makeUnknown();
    }
    string field = ctx->IDENTIFIER()->getText();
    for (size_t i = 0; i < base.fieldNames.size(); i++) {
        if (base.fieldNames[i] == field) return base.fieldTypes[i];
    }
    // Campo no encontrado — sugerir el campo más cercano del struct
    string msg = base.toString() + " has no field '" + field + "'";
    string suggestion = minigoSuggest(field, base.fieldNames);
    if (!suggestion.empty()) msg += "; did you mean '" + suggestion + "'?";
    reportScopeError(msg, ctx->IDENTIFIER()->getSymbol());
    return makeUnknown();
}

any TypeChecker::visitIndexExpr(MiniGoParser::IndexExprContext* ctx) {
    TypeInfo base = unpackType(visit(ctx->primaryExpression())); // base en contexto LHS si aplica
    // El índice SIEMPRE es una lectura, aunque la expresión entera sea LHS
    bool savedLhs = inLhsContext_;
    inLhsContext_ = false;
    TypeInfo idx = unpackType(visit(ctx->expression()));
    inLhsContext_ = savedLhs;

    if (idx.kind != TypeKind::UNKNOWN && idx.kind != TypeKind::INT)
        reportError("Array/slice index must be int", ctx->expression()->getStart(),
                    makeInt(), idx);

    if (base.kind == TypeKind::ARRAY || base.kind == TypeKind::SLICE) {
        if (base.elementType) return *base.elementType;
    } else if (base.kind != TypeKind::UNKNOWN) {
        reportError("Index operator requires array or slice", ctx->getStart());
    }
    return makeUnknown();
}

any TypeChecker::visitCallExpr(MiniGoParser::CallExprContext* ctx) {
    TypeInfo funcType = unpackType(visit(ctx->primaryExpression()));
    if (funcType.kind != TypeKind::FUNC) {
        if (funcType.kind != TypeKind::UNKNOWN)
            reportScopeError("Expression is not a function", ctx->getStart());
        if (ctx->expressionList()) visitExprList(ctx->expressionList());
        return makeUnknown();
    }

    vector<TypeInfo> args;
    if (ctx->expressionList()) args = visitExprList(ctx->expressionList());

    if (args.size() != funcType.paramTypes.size()) {
        ostringstream oss;
        oss << "Wrong argument count: expected " << funcType.paramTypes.size()
            << " but got " << args.size();
        reportScopeError(oss.str(), ctx->getStart());
    } else {
        for (size_t i = 0; i < funcType.paramTypes.size(); i++) {
            if (args[i].kind != TypeKind::UNKNOWN && args[i] != funcType.paramTypes[i])
                reportError("Argument " + to_string(i+1) + " type mismatch",
                            ctx->getStart(), funcType.paramTypes[i], args[i]);
        }
    }
    if (funcType.returnType) return *funcType.returnType;
    return makeVoid();
}

any TypeChecker::visitAppendExpr(MiniGoParser::AppendExprContext* ctx) {
    TypeInfo slice = unpackType(visit(ctx->expression(0)));
    TypeInfo elem  = unpackType(visit(ctx->expression(1)));

    if (slice.kind != TypeKind::UNKNOWN && slice.kind != TypeKind::SLICE)
        reportError("append() first argument must be a slice", ctx->getStart());

    if (slice.kind == TypeKind::SLICE && slice.elementType &&
        elem.kind != TypeKind::UNKNOWN && *slice.elementType != elem)
        reportError("append() element type mismatch",
                    ctx->expression(1)->getStart(), *slice.elementType, elem);

    return slice;
}

any TypeChecker::visitLenExpr(MiniGoParser::LenExprContext* ctx) {
    TypeInfo t = unpackType(visit(ctx->expression()));
    if (t.kind != TypeKind::UNKNOWN &&
        t.kind != TypeKind::ARRAY && t.kind != TypeKind::SLICE && t.kind != TypeKind::STRING)
        reportError("len() requires array, slice or string", ctx->getStart());
    return makeInt();
}

any TypeChecker::visitCapExpr(MiniGoParser::CapExprContext* ctx) {
    TypeInfo t = unpackType(visit(ctx->expression()));
    if (t.kind != TypeKind::UNKNOWN && t.kind != TypeKind::SLICE)
        reportError("cap() requires a slice", ctx->getStart());
    return makeInt();
}


// Operands


any TypeChecker::visitLiteralOperand(MiniGoParser::LiteralOperandContext* ctx) {
    return visit(ctx->literal());
}

any TypeChecker::visitIdentifierOperand(MiniGoParser::IdentifierOperandContext* ctx) {
    auto* tok  = ctx->IDENTIFIER()->getSymbol();
    const string& name = tok->getText();
    Symbol* sym = symbolTable_.lookup(name);
    if (!sym) {
        string msg = "Undefined identifier '" + name + "'";
        auto suggestion = findBestMatch(name);
        if (suggestion) msg += "; did you mean '" + *suggestion + "'?";
        reportScopeError(msg, tok);
        return makeUnknown();
    }
    referenceMap_[ctx] = sym; // ← enlace TypeChecker → Encoder
    // Registrar como "leído" solo fuera de contexto LHS (lado izquierdo de '=')
    if (!inLhsContext_ && !sym->isConst && !sym->isFunc)
        readSymbols_.insert(sym);
    return sym->type;
}

any TypeChecker::visitParenOperand(MiniGoParser::ParenOperandContext* ctx) {
    return visit(ctx->expression());
}


// Literals
any TypeChecker::visitIntLit(MiniGoParser::IntLitContext*)                      { return makeInt(); }
any TypeChecker::visitFloatLit(MiniGoParser::FloatLitContext*)                  { return makeFloat(); }
any TypeChecker::visitRuneLit(MiniGoParser::RuneLitContext*)                    { return makeRune(); }
any TypeChecker::visitRawStringLit(MiniGoParser::RawStringLitContext*)          { return makeString(); }
any TypeChecker::visitInterpretedStringLit(MiniGoParser::InterpretedStringLitContext*) {
    return makeString();
}
