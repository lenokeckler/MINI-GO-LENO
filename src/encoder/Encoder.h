#pragma once

#include "antlr4-runtime.h"
#include "MiniGoBaseVisitor.h"
#include "typechecker/SymbolTable.h"
#include <string>
#include <map>
#include <stack>
#include <any>

// LLVM C++ API — headers completos
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/TargetParser/Triple.h"


// Encoder — visitor que recorre el AST de ANTLR4 y emite LLVM IR
class Encoder : public MiniGoBaseVisitor {
public:
    Encoder(const std::map<antlr4::tree::ParseTree*, Symbol*>& referenceMap,
            const std::map<antlr4::ParserRuleContext*, std::map<std::string, Symbol*>>& declSymbolMap);

    // Escribe el IR a stdout, a output.ll y emite el objeto x86 output.o.
    // Retorna false si el módulo NO pasa la verificación (IR inválido).
    bool finalize();

    // Retorna el último IR generado (para mostrarlo en el GUI)
    const std::string& getIR() const { return lastIR_; }

    // Mensaje de error de verificación LLVM (vacío si el módulo es válido)
    const std::string& getVerifyError() const { return verifyError_; }

private:
    // ---- LLVM state ----
    llvm::LLVMContext                      ctx_;
    std::unique_ptr<llvm::Module>          module_;
    std::unique_ptr<llvm::IRBuilder<>>     builder_;

    // ---- printf / malloc ----
    llvm::Function*     printfFunc_{nullptr};
    llvm::FunctionType* printfType_{nullptr};
    llvm::Function*     mallocFunc_{nullptr};
    llvm::FunctionType* mallocType_{nullptr};
    llvm::Function*     strlenFunc_{nullptr};
    llvm::FunctionType* strlenType_{nullptr};

    // ---- Tablas internas ----
    struct FunctionEntry {
        llvm::Function*     func{nullptr};
        llvm::FunctionType* type{nullptr};
    };
    std::map<std::string, FunctionEntry>          functionTable_;
    std::stack<llvm::Function*>                   currentFunctionStack_;

    // Symbol* → AllocaInst* o GlobalVariable* (ambos son Value*)
    std::map<Symbol*, llvm::Value*>               ptrMap_;

    // Cache de tipos struct/slice ya creados (nombre → StructType*)
    std::map<std::string, llvm::StructType*>      structTypeMap_;

    // Mapa de declaraciones: nodo AST → (nombre → Symbol*)
    const std::map<antlr4::ParserRuleContext*,
                   std::map<std::string, Symbol*>>&  declSymbolMap_;

    // Mapa de usos: nodo identificador → Symbol*
    const std::map<antlr4::tree::ParseTree*, Symbol*>& referenceMap_;

    bool        returnEmitted_{false};
    std::string lastIR_;               // IR generado (para el GUI)
    std::string verifyError_;          // mensaje de verifyModule (vacío si OK)

    // ---- Destinos de break/continue ----
    // Pilas separadas: los loops empujan a ambas; switch solo a breakStack_.
    // Así 'continue' dentro de un switch anidado en un loop salta a la
    // iteración del loop (semántica de Go), no al final del switch.
    std::stack<llvm::BasicBlock*> breakStack_;     // destino de break
    std::stack<llvm::BasicBlock*> continueStack_;  // destino de continue

    // ---- Conversión tipo ----
    llvm::Type* typeInfoToLLVM(const TypeInfo& t);

    // ---- Alloca izada al bloque entry (evita crecimiento de pila en loops) ----
    llvm::AllocaInst* createEntryAlloca(llvm::Type* ty, const std::string& name);

    // ---- Pre-declaración de función (firma sin cuerpo, para forward refs) ----
    void declareFunction(MiniGoParser::FuncDeclContext* ctx);

    // ---- Emisión del objeto x86 (output.o) vía TargetMachine ----
    void emitObjectFile(const std::string& path);

    // ---- Impresión de un valor escalar (maneja bool → true/false) ----
    void emitPrintScalar(llvm::Value* val, const std::string& prefix, bool newline);

    // ---- Lookup de puntero para nodo identificador (uso) ----
    llvm::Value* getPtrForNode(antlr4::tree::ParseTree* node);

    // ---- Tipo apuntado por un Value* (AllocaInst o GlobalVariable) ----
    llvm::Type* getPointeeType(llvm::Value* ptr);

    // ---- Lookup de Symbol* desde la tabla de declaraciones ----
    Symbol* findDeclSymbol(antlr4::ParserRuleContext* ctx, const std::string& name);

    // ---- Helpers para compound assigns e inc/dec ----
    llvm::Value* loadFromExpr(MiniGoParser::ExpressionContext* expr);
    llvm::Value* getPtrFromExpr(MiniGoParser::ExpressionContext* expr);

    // ---- GEP para elemento de array o slice ----
    llvm::Value* getIndexedElemPtr(antlr4::tree::ParseTree* idNode,
                                   llvm::Value* idx, llvm::Type** outElemType = nullptr);

    // ---- GEP para campo de struct ----
    llvm::Value* getStructFieldPtr(antlr4::tree::ParseTree* idNode,
                                   const std::string& fieldName,
                                   llvm::Type** outElemType = nullptr);

    // ---- Utilidades printf ----
    std::string selectFormat(llvm::Type* t) const;
    void emitPrintf(llvm::Value* val, bool newline);

    // ---- if helper ----
    void emitIfElse(llvm::Value* cond,
                    MiniGoParser::BlockContext* thenBlock,
                    MiniGoParser::BlockContext* elseBlock);

    // ---- switch helper ----
    void emitSwitch(llvm::Value* switchVal,   // nullptr si no hay expresión (switch{})
                    MiniGoParser::ExpressionCaseClauseListContext* clauses);

    // ---- Coerce a bool i1 (para condBr) ----
    llvm::Value* toBool(llvm::Value* val);

    // ---- Literales string ----
    static std::string unescapeInterpreted(const std::string& raw);
    static char        parseRuneLiteral(const std::string& raw);

    // ---- Visitor overrides ----
    std::any visitRoot(MiniGoParser::RootContext* ctx) override;
    std::any visitTopDeclarationList(MiniGoParser::TopDeclarationListContext* ctx) override;

    // Variable declarations
    std::any visitVariableDecl(MiniGoParser::VariableDeclContext* ctx) override;
    std::any visitVarDeclTypedInit(MiniGoParser::VarDeclTypedInitContext* ctx) override;
    std::any visitVarDeclInferred(MiniGoParser::VarDeclInferredContext* ctx) override;
    std::any visitVarDeclNoInit(MiniGoParser::VarDeclNoInitContext* ctx) override;
    std::any visitSingleVarDeclNoExps(MiniGoParser::SingleVarDeclNoExpsContext* ctx) override;

    // Type declarations
    std::any visitTypeDecl(MiniGoParser::TypeDeclContext* ctx) override;
    std::any visitSingleTypeDecl(MiniGoParser::SingleTypeDeclContext* ctx) override;

    // Function declarations
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
