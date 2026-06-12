#pragma once

#include "antlr4-runtime.h"
#include "MiniGoBaseVisitor.h"
#include "SymbolTable.h"
#include <string>
#include <vector>
#include <stack>
#include <map>
#include <set>
#include <any>
#include <optional>


// TypeChecker — visitor semántico sobre el AST de ANTLR4.
//
// Patrón idéntico a AlphaCompilerTypeChecker (Java) portado a C++:
//  - retorna TypeInfo en std::any para expresiones
//  - retorna std::any{} (vacío) para statements
//  - construye referenceMap: nodo identificador → Symbol*
//    (consumido luego por el Encoder para resolver allocas)

class TypeChecker : public MiniGoBaseVisitor {
public:
    TypeChecker();

    bool                                          hasErrors()       const;
    const std::vector<std::string>&               getErrors()       const;
    const std::map<antlr4::tree::ParseTree*,
                   Symbol*>&                      getReferenceMap()    const;
    const std::map<std::string, TypeInfo>&        getTypeRegistry()    const;
    const std::map<antlr4::ParserRuleContext*,
                   std::map<std::string, Symbol*>>& getDeclSymbolMap() const;

    // Extrae TypeInfo de un std::any; devuelve UNKNOWN si vacío.
    static TypeInfo unpackType(const std::any& a);

    // Estos dos son públicos para que el Encoder pueda reutilizar
    // la lógica de resolución de tipo sin duplicarla.
    TypeInfo resolveTypeName(const std::string& name, antlr4::Token* tok);
    TypeInfo buildDeclType(MiniGoParser::DeclTypeContext* ctx);

private:
    SymbolTable                                        symbolTable_;
    std::vector<std::string>                           errors_;
    std::stack<TypeInfo>                               returnTypeStack_;
    std::stack<TypeInfo>                               switchTypeStack_;    // tipo de la expr del switch actual
    std::map<antlr4::tree::ParseTree*, Symbol*>                      referenceMap_;
    std::map<std::string, TypeInfo>                                  typeRegistry_;
    std::map<antlr4::ParserRuleContext*, std::map<std::string, Symbol*>> declSymbolMap_;

    //  Control de break / continue 
    int loopDepth_{0};      // profundidad de loops anidados
    int switchDepth_{0};    // profundidad de switch anidados

    //  Detección de función no-void sin return 
    std::stack<bool> funcHasReturnStack_; // true si la función actual tiene al menos un 'return expr'

    //  Detección de variables no usadas 
    std::set<Symbol*>    readSymbols_;        // símbolos que se leyeron (no solo escritos)
    std::vector<Symbol*> currentFuncLocals_;  // vars locales de la función actual (excluye params/globals)
    bool insideFunction_{false};              // true dentro del cuerpo de una función (tras procesar params)
    bool inLhsContext_{false};               // true cuando se visita el lado izquierdo de '='

    void reportError(const std::string& msg, antlr4::Token* tok);
    void reportError(const std::string& msg, antlr4::Token* tok,
                     const TypeInfo& expected, const TypeInfo& actual);
    void reportScopeError(const std::string& msg, antlr4::Token* tok);

    // Retorna true si el tipo es un tipo primitivo de MiniGo
    // (solo los primitivos permiten inferencia de tipo)
    bool isPrimitive(const TypeInfo& t) const;

    // Busca el identificador/tipo más parecido al nombre dado usando
    // distancia de edición. Considera todos los símbolos visibles,
    // keywords y tipos registrados. Retorna nullopt si no hay sugerencia.
    std::optional<std::string> findBestMatch(const std::string& name) const;

    std::vector<TypeInfo> visitExprList(MiniGoParser::ExpressionListContext* ctx);
    TypeInfo binaryResultType(const std::string& op,
                              const TypeInfo& left, const TypeInfo& right,
                              antlr4::Token* opTok);

    // ---- Visitor overrides ----
    std::any visitRoot(MiniGoParser::RootContext* ctx) override;
    std::any visitTopDeclarationList(MiniGoParser::TopDeclarationListContext* ctx) override;

    // Variable declarations
    std::any visitVariableDecl(MiniGoParser::VariableDeclContext* ctx) override;
    std::any visitInnerVarDecls(MiniGoParser::InnerVarDeclsContext* ctx) override;
    std::any visitVarDeclTypedInit(MiniGoParser::VarDeclTypedInitContext* ctx) override;
    std::any visitVarDeclInferred(MiniGoParser::VarDeclInferredContext* ctx) override;
    std::any visitVarDeclNoInit(MiniGoParser::VarDeclNoInitContext* ctx) override;
    std::any visitSingleVarDeclNoExps(MiniGoParser::SingleVarDeclNoExpsContext* ctx) override;

    // Type declarations
    std::any visitTypeDecl(MiniGoParser::TypeDeclContext* ctx) override;
    std::any visitSingleTypeDecl(MiniGoParser::SingleTypeDeclContext* ctx) override;

    // Function declarations
    // Registra solo la firma (nombre + tipos) en el scope global. Se ejecuta
    // en un pase previo para habilitar forward refs y recursión mutua.
    void registerFuncSignature(MiniGoParser::FuncDeclContext* ctx);
    std::any visitFuncDecl(MiniGoParser::FuncDeclContext* ctx) override;

    // Statements
    std::any visitStatementList(MiniGoParser::StatementListContext* ctx) override;
    std::any visitBlock(MiniGoParser::BlockContext* ctx) override;
    std::any visitPrintStmt(MiniGoParser::PrintStmtContext* ctx) override;
    std::any visitPrintlnStmt(MiniGoParser::PrintlnStmtContext* ctx) override;
    std::any visitReturnStmt(MiniGoParser::ReturnStmtContext* ctx) override;
    std::any visitBreakStmt(MiniGoParser::BreakStmtContext* ctx) override;
    std::any visitContinueStmt(MiniGoParser::ContinueStmtContext* ctx) override;
    std::any visitSimpleStmt(MiniGoParser::SimpleStmtContext* ctx) override;
    std::any visitBlockStmt(MiniGoParser::BlockStmtContext* ctx) override;
    std::any visitIfStmt(MiniGoParser::IfStmtContext* ctx) override;
    std::any visitLoopStmt(MiniGoParser::LoopStmtContext* ctx) override;
    std::any visitSwitchStmt(MiniGoParser::SwitchStmtContext* ctx) override;

    // simpleStatement variants
    std::any visitShortVarDeclStmt(MiniGoParser::ShortVarDeclStmtContext* ctx) override;
    std::any visitAssignStmt(MiniGoParser::AssignStmtContext* ctx) override;
    std::any visitAddAssignStmt(MiniGoParser::AddAssignStmtContext* ctx) override;
    std::any visitSubAssignStmt(MiniGoParser::SubAssignStmtContext* ctx) override;
    std::any visitMulAssignStmt(MiniGoParser::MulAssignStmtContext* ctx) override;
    std::any visitDivAssignStmt(MiniGoParser::DivAssignStmtContext* ctx) override;
    std::any visitModAssignStmt(MiniGoParser::ModAssignStmtContext* ctx) override;
    std::any visitShlAssignStmt(MiniGoParser::ShlAssignStmtContext* ctx) override;
    std::any visitShrAssignStmt(MiniGoParser::ShrAssignStmtContext* ctx) override;
    std::any visitAndAssignStmt(MiniGoParser::AndAssignStmtContext* ctx) override;
    std::any visitOrAssignStmt(MiniGoParser::OrAssignStmtContext* ctx) override;
    std::any visitXorAssignStmt(MiniGoParser::XorAssignStmtContext* ctx) override;
    std::any visitAndNotAssignStmt(MiniGoParser::AndNotAssignStmtContext* ctx) override;
    std::any visitIncStmt(MiniGoParser::IncStmtContext* ctx) override;
    std::any visitDecStmt(MiniGoParser::DecStmtContext* ctx) override;
    std::any visitExprStmt(MiniGoParser::ExprStmtContext* ctx) override;
    std::any visitEmptyStmt(MiniGoParser::EmptyStmtContext* ctx) override;

    // ifStatement variants
    std::any visitIfWithInitElseIf(MiniGoParser::IfWithInitElseIfContext* ctx) override;
    std::any visitIfWithInitElse(MiniGoParser::IfWithInitElseContext* ctx) override;
    std::any visitIfWithInit(MiniGoParser::IfWithInitContext* ctx) override;
    std::any visitIfElseIf(MiniGoParser::IfElseIfContext* ctx) override;
    std::any visitIfElse(MiniGoParser::IfElseContext* ctx) override;
    std::any visitIfSimple(MiniGoParser::IfSimpleContext* ctx) override;

    // loop variants
    std::any visitInfiniteLoop(MiniGoParser::InfiniteLoopContext* ctx) override;
    std::any visitCondLoop(MiniGoParser::CondLoopContext* ctx) override;
    std::any visitForLoop(MiniGoParser::ForLoopContext* ctx) override;
    std::any visitForLoopNoExpr(MiniGoParser::ForLoopNoExprContext* ctx) override;

    // switch variants
    std::any visitSwitchWithInit(MiniGoParser::SwitchWithInitContext* ctx) override;
    std::any visitSwitchSimple(MiniGoParser::SwitchSimpleContext* ctx) override;
    std::any visitSwitchInitNoExpr(MiniGoParser::SwitchInitNoExprContext* ctx) override;
    std::any visitSwitchNoExpr(MiniGoParser::SwitchNoExprContext* ctx) override;
    std::any visitExpressionCaseClause(MiniGoParser::ExpressionCaseClauseContext* ctx) override;

    // Expressions
    std::any visitPrimaryExpr(MiniGoParser::PrimaryExprContext* ctx) override;
    std::any visitUnaryExpr(MiniGoParser::UnaryExprContext* ctx) override;
    std::any visitMulExpr(MiniGoParser::MulExprContext* ctx) override;
    std::any visitAddExpr(MiniGoParser::AddExprContext* ctx) override;
    std::any visitRelExpr(MiniGoParser::RelExprContext* ctx) override;
    std::any visitAndExpr(MiniGoParser::AndExprContext* ctx) override;
    std::any visitOrExpr(MiniGoParser::OrExprContext* ctx) override;

    // primaryExpression variants
    std::any visitOperandExpr(MiniGoParser::OperandExprContext* ctx) override;
    std::any visitSelectorExpr(MiniGoParser::SelectorExprContext* ctx) override;
    std::any visitIndexExpr(MiniGoParser::IndexExprContext* ctx) override;
    std::any visitCallExpr(MiniGoParser::CallExprContext* ctx) override;
    std::any visitAppendExpr(MiniGoParser::AppendExprContext* ctx) override;
    std::any visitLenExpr(MiniGoParser::LenExprContext* ctx) override;
    std::any visitCapExpr(MiniGoParser::CapExprContext* ctx) override;

    // operand variants
    std::any visitLiteralOperand(MiniGoParser::LiteralOperandContext* ctx) override;
    std::any visitIdentifierOperand(MiniGoParser::IdentifierOperandContext* ctx) override;
    std::any visitParenOperand(MiniGoParser::ParenOperandContext* ctx) override;

    // literal variants
    std::any visitIntLit(MiniGoParser::IntLitContext* ctx) override;
    std::any visitFloatLit(MiniGoParser::FloatLitContext* ctx) override;
    std::any visitRuneLit(MiniGoParser::RuneLitContext* ctx) override;
    std::any visitRawStringLit(MiniGoParser::RawStringLitContext* ctx) override;
    std::any visitInterpretedStringLit(MiniGoParser::InterpretedStringLitContext* ctx) override;
};
