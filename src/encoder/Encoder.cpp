#include "Encoder.h"
#include "MiniGoParser.h"

using namespace std;

// Alias explícito para evitar ambigüedad std::any vs llvm::any
using any = std::any;

using namespace llvm;
using namespace antlr4;


// Constructor
Encoder::Encoder(const map<tree::ParseTree*, Symbol*>& referenceMap,
                 const map<ParserRuleContext*, map<string, Symbol*>>& declSymbolMap)
    : referenceMap_(referenceMap), declSymbolMap_(declSymbolMap)
{
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    module_  = make_unique<Module>("minigo_output", ctx_);
    builder_ = make_unique<IRBuilder<>>(ctx_);

    // Declarar printf
    Type* i8ptr  = PointerType::get(Type::getInt8Ty(ctx_), 0);
    printfType_  = FunctionType::get(Type::getInt32Ty(ctx_), {i8ptr}, /*vararg=*/true);
    printfFunc_  = Function::Create(printfType_, Function::ExternalLinkage,
                                    "printf", module_.get());
    functionTable_["printf"] = {printfFunc_, printfType_};

    // Declarar malloc — usado por append() para slices
    mallocType_ = FunctionType::get(i8ptr, {Type::getInt64Ty(ctx_)}, false);
    mallocFunc_ = Function::Create(mallocType_, Function::ExternalLinkage,
                                   "malloc", module_.get());

    // Declarar strlen — usado por len() sobre strings
    strlenType_ = FunctionType::get(Type::getInt64Ty(ctx_), {i8ptr}, false);
    strlenFunc_ = Function::Create(strlenType_, Function::ExternalLinkage,
                                   "strlen", module_.get());
}


// Finalización — retorna false si el IR no pasa la verificación
bool Encoder::finalize() {
    string ir;
    raw_string_ostream os(ir);
    module_->print(os, nullptr);
    os.flush();

    lastIR_ = ir;   // guardar para el GUI

    outs() << "|=== LLVM IR ===|\n" << ir << "\n";

    error_code ec;
    raw_fd_ostream outFile("output.ll", ec, sys::fs::OF_Text);
    if (!ec) {
        outFile << ir;
        outs() << "[INFO] IR escrito en output.ll\n";
    } else {
        errs() << "[ERROR] No se pudo escribir output.ll: " << ec.message() << "\n";
    }

    // Verificación: si falla, NO se reporta éxito ni se emite objeto.
    verifyError_.clear();
    raw_string_ostream verifyOs(verifyError_);
    if (verifyModule(*module_, &verifyOs)) {
        verifyOs.flush();
        errs() << "[LLVM VERIFY ERROR] " << verifyError_ << "\n";
        return false;
    }
    outs() << "[INFO] Módulo LLVM verificado correctamente.\n";

    // Emisión de código de máquina real para x86 (objeto output.o)
    emitObjectFile("output.o");
    return true;
}


// Emisión del objeto nativo x86 vía TargetMachine
void Encoder::emitObjectFile(const string& path) {
    string triple = sys::getDefaultTargetTriple();
    module_->setTargetTriple(Triple(triple));

    string err;
    const Target* target = TargetRegistry::lookupTarget(triple, err);
    if (!target) {
        errs() << "[ENCODER WARNING] No se pudo seleccionar el target x86 ("
               << err << "); se generó output.ll pero no output.o\n";
        return;
    }

    TargetOptions opt;
    TargetMachine* tm = target->createTargetMachine(
        Triple(triple), "generic", "", opt, std::optional<Reloc::Model>());
    if (!tm) {
        errs() << "[ENCODER WARNING] No se pudo crear el TargetMachine; "
               << "se generó output.ll pero no output.o\n";
        return;
    }

    module_->setDataLayout(tm->createDataLayout());

    error_code ec;
    raw_fd_ostream dest(path, ec, sys::fs::OF_None);
    if (ec) {
        errs() << "[ENCODER WARNING] No se pudo abrir " << path << ": "
               << ec.message() << "\n";
        delete tm;
        return;
    }

    legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, dest, nullptr, CodeGenFileType::ObjectFile)) {
        errs() << "[ENCODER WARNING] El TargetMachine no soporta emisión de objeto\n";
        delete tm;
        return;
    }
    pm.run(*module_);
    dest.flush();
    outs() << "[INFO] Código de máquina x86 escrito en " << path << "\n";
    delete tm;
}


// Alloca izada al inicio del bloque entry de la función actual.
// Evita que las allocas dentro de loops crezcan la pila en cada iteración.
AllocaInst* Encoder::createEntryAlloca(Type* ty, const string& name) {
    Function* fn = currentFunctionStack_.top();
    BasicBlock& entry = fn->getEntryBlock();
    IRBuilder<> tmp(&entry, entry.begin());
    return tmp.CreateAlloca(ty, nullptr, name);
}


// Conversión TypeInfo → LLVM Type
Type* Encoder::typeInfoToLLVM(const TypeInfo& t) {
    switch (t.kind) {
        case TypeKind::INT:    return Type::getInt32Ty(ctx_);
        case TypeKind::FLOAT:  return Type::getDoubleTy(ctx_);
        case TypeKind::BOOL:   return Type::getInt1Ty(ctx_);
        case TypeKind::RUNE:   return Type::getInt32Ty(ctx_);
        case TypeKind::STRING: return PointerType::get(Type::getInt8Ty(ctx_), 0);
        case TypeKind::VOID:   return Type::getVoidTy(ctx_);

        case TypeKind::ARRAY:
            if (t.elementType)
                return ArrayType::get(typeInfoToLLVM(*t.elementType),
                                      static_cast<uint64_t>(t.arraySize));
            return ArrayType::get(Type::getInt32Ty(ctx_), 0);

        // Slice → { T*, i32, i32 }  (data pointer + length + capacity)
        case TypeKind::SLICE: {
            Type* elemTy = t.elementType ? typeInfoToLLVM(*t.elementType)
                                         : Type::getInt32Ty(ctx_);
            string key = "slice_" + (t.elementType ? t.elementType->toString() : "int");
            auto it = structTypeMap_.find(key);
            if (it != structTypeMap_.end()) return it->second;
            StructType* sliceTy = StructType::get(ctx_, {
                PointerType::get(elemTy, 0),
                Type::getInt32Ty(ctx_),  // len
                Type::getInt32Ty(ctx_)   // cap
            });
            structTypeMap_[key] = sliceTy;
            return sliceTy;
        }

        // Struct → crea o reutiliza StructType con nombre
        case TypeKind::STRUCT: {
            if (!t.structName.empty()) {
                auto it = structTypeMap_.find(t.structName);
                if (it != structTypeMap_.end()) return it->second;
            }
            vector<Type*> fieldTys;
            for (const auto& ft : t.fieldTypes)
                fieldTys.push_back(typeInfoToLLVM(ft));
            StructType* st;
            if (!t.structName.empty()) {
                st = StructType::create(ctx_, fieldTys, t.structName);
                structTypeMap_[t.structName] = st;
            } else {
                st = StructType::get(ctx_, fieldTys);
            }
            return st;
        }

        default:
            errs() << "[ENCODER WARNING] Tipo desconocido → usando i32\n";
            return Type::getInt32Ty(ctx_);
    }
}


// Tipo apuntado por un alloca o global variable
Type* Encoder::getPointeeType(Value* ptr) {
    if (auto* alloca = dyn_cast<AllocaInst>(ptr))    return alloca->getAllocatedType();
    if (auto* gvar   = dyn_cast<GlobalVariable>(ptr)) return gvar->getValueType();
    return Type::getInt32Ty(ctx_);
}


// Lookup alloca/global para un nodo de uso de identificador
Value* Encoder::getPtrForNode(tree::ParseTree* node) {
    auto it = referenceMap_.find(node);
    if (it == referenceMap_.end()) return nullptr;
    auto it2 = ptrMap_.find(it->second);
    return it2 != ptrMap_.end() ? it2->second : nullptr;
}


// Lookup Symbol* desde la tabla de declaraciones
Symbol* Encoder::findDeclSymbol(ParserRuleContext* ctx, const string& name) {
    auto it = declSymbolMap_.find(ctx);
    if (it == declSymbolMap_.end()) return nullptr;
    auto it2 = it->second.find(name);
    return it2 != it->second.end() ? it2->second : nullptr;
}


// GEP para elemento de array o slice
// Devuelve puntero al elemento; *outElemType ← tipo del elemento
Value* Encoder::getIndexedElemPtr(tree::ParseTree* idNode, Value* idx,
                                   Type** outElemType) {
    Value* containerPtr = getPtrForNode(idNode);
    if (!containerPtr) return nullptr;

    Type* baseType = getPointeeType(containerPtr);

    if (baseType->isArrayTy()) {
        // [N]T — GEP directo
        Type* elemType = baseType->getArrayElementType();
        if (outElemType) *outElemType = elemType;
        return builder_->CreateInBoundsGEP(
            baseType, containerPtr,
            {ConstantInt::get(Type::getInt32Ty(ctx_), 0), idx});
    }

    if (baseType->isStructTy()) {
        // Slice { T*, i32 } — cargar data ptr, luego GEP
        Value* dataPtrGEP = builder_->CreateStructGEP(baseType, containerPtr, 0, "data_pp");
        Type* ptrToElemTy = cast<StructType>(baseType)->getElementType(0); // T*
        Value* dataPtr    = builder_->CreateLoad(ptrToElemTy, dataPtrGEP, "data_p");

        // Tipo del elemento desde Symbol
        auto refIt = referenceMap_.find(idNode);
        if (refIt == referenceMap_.end()) return nullptr;
        const TypeInfo& ti = refIt->second->type;
        if (!ti.elementType) return nullptr;
        Type* elemType = typeInfoToLLVM(*ti.elementType);
        if (outElemType) *outElemType = elemType;
        return builder_->CreateInBoundsGEP(elemType, dataPtr, {idx});
    }
    return nullptr;
}


// GEP para campo de struct
Value* Encoder::getStructFieldPtr(tree::ParseTree* idNode, const string& fieldName,
                                   Type** outElemType) {
    Value* structPtr = getPtrForNode(idNode);
    if (!structPtr) return nullptr;

    auto refIt = referenceMap_.find(idNode);
    if (refIt == referenceMap_.end()) return nullptr;
    const TypeInfo& typeInfo = refIt->second->type;

    int fieldIdx = -1;
    for (size_t i = 0; i < typeInfo.fieldNames.size(); i++) {
        if (typeInfo.fieldNames[i] == fieldName) { fieldIdx = (int)i; break; }
    }
    if (fieldIdx < 0) return nullptr;

    Type* structLLVMType = typeInfoToLLVM(typeInfo);
    if (outElemType)
        *outElemType = cast<StructType>(structLLVMType)->getElementType((unsigned)fieldIdx);
    return builder_->CreateStructGEP(structLLVMType, structPtr,
                                     (unsigned)fieldIdx, fieldName);
}


// Helpers para compound assigns e inc/dec
Value* Encoder::loadFromExpr(MiniGoParser::ExpressionContext* expr) {
    auto v = visit(expr);
    return v.has_value() ? any_cast<Value*>(v) : nullptr;
}

Value* Encoder::getPtrFromExpr(MiniGoParser::ExpressionContext* expr) {
    auto* prim = dynamic_cast<MiniGoParser::PrimaryExprContext*>(expr);
    auto* opEx = prim ? dynamic_cast<MiniGoParser::OperandExprContext*>(
                     prim->primaryExpression()) : nullptr;
    auto* idOp = opEx ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                     opEx->operand()) : nullptr;
    return idOp ? getPtrForNode(idOp) : nullptr;
}


// Printf helpers
string Encoder::selectFormat(Type* t) const {
    if (t->isIntegerTy(1))  return "%d";
    if (t->isIntegerTy(8))  return "%c";
    if (t->isIntegerTy(32)) return "%d";
    if (t->isDoubleTy())    return "%f";
    if (t->isPointerTy())   return "%s";
    return "%d";
}

void Encoder::emitPrintf(Value* val, bool newline) {
    emitPrintScalar(val, "", newline);
}

// Imprime un valor escalar con un prefijo opcional y newline opcional.
// Caso especial bool (i1): imprime "true"/"false" como en Go en vez de 1/0.
void Encoder::emitPrintScalar(Value* val, const string& prefix, bool newline) {
    string suffix = newline ? "\n" : "";
    if (val->getType()->isIntegerTy(1)) {
        Value* trueStr  = builder_->CreateGlobalStringPtr("true",  "bool_true");
        Value* falseStr = builder_->CreateGlobalStringPtr("false", "bool_false");
        Value* sel      = builder_->CreateSelect(val, trueStr, falseStr, "bool_str");
        Value* fmtStr   = builder_->CreateGlobalStringPtr(prefix + "%s" + suffix, "fmt_b");
        builder_->CreateCall(printfType_, printfFunc_, {fmtStr, sel});
        return;
    }
    string fmt = prefix + selectFormat(val->getType()) + suffix;
    Value* fmtStr = builder_->CreateGlobalStringPtr(fmt, "fmt");
    builder_->CreateCall(printfType_, printfFunc_, {fmtStr, val});
}


// Coerce a bool i1
Value* Encoder::toBool(Value* val) {
    Type* t = val->getType();
    if (t->isIntegerTy(1)) return val;
    return builder_->CreateICmpNE(val, ConstantInt::get(t, 0), "tobool");
}


// Cast entre tipos LLVM
static Value* coerceToType(IRBuilder<>& B, LLVMContext& ctx, Value* val, Type* target) {
    if (val->getType() == target) return val;
    if (target->isDoubleTy() && val->getType()->isIntegerTy())
        return B.CreateSIToFP(val, target, "itof");
    if (target->isIntegerTy() && val->getType()->isDoubleTy())
        return B.CreateFPToSI(val, target, "ftoi");
    if (target->isIntegerTy(1) && !val->getType()->isIntegerTy(1))
        return B.CreateICmpNE(val, ConstantInt::get(val->getType(), 0), "tobool");
    return val;
}


// String helpers
string Encoder::unescapeInterpreted(const string& raw) {
    string content = raw.substr(1, raw.size() - 2);
    string result;
    for (size_t i = 0; i < content.size(); i++) {
        if (content[i] == '\\' && i + 1 < content.size()) {
            switch (content[++i]) {
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"':  result += '"';  break;
                case '\'': result += '\''; break;
                case 'a':  result += '\a'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'v':  result += '\v'; break;
                default:   result += content[i]; break;
            }
        } else {
            result += content[i];
        }
    }
    return result;
}

char Encoder::parseRuneLiteral(const string& raw) {
    string inner = raw.substr(1, raw.size() - 2);
    if (inner.size() == 1) return inner[0];
    if (inner[0] == '\\') {
        switch (inner[1]) {
            case 'n':  return '\n';
            case 't':  return '\t';
            case 'r':  return '\r';
            case '\\': return '\\';
            case '\'': return '\'';
            case '"':  return '"';
            case 'a':  return '\a';
            case 'b':  return '\b';
            default:   return inner[1];
        }
    }
    return inner[0];
}


// Root


std::any Encoder::visitRoot(MiniGoParser::RootContext* ctx) {
    auto* topList = ctx->topDeclarationList();

    // Pass 1: variables globales (deben existir antes de los cuerpos de función)
    for (auto* child : topList->children)
        if (auto* vd = dynamic_cast<MiniGoParser::VariableDeclContext*>(child))
            visit(vd);

    // Pass 2: firmas de TODAS las funciones — habilita llamadas hacia adelante
    //          y recursión mutua (como en Go).
    for (auto* child : topList->children)
        if (auto* fd = dynamic_cast<MiniGoParser::FuncDeclContext*>(child))
            declareFunction(fd);

    // Pass 3: cuerpos de las funciones
    for (auto* child : topList->children)
        if (auto* fd = dynamic_cast<MiniGoParser::FuncDeclContext*>(child))
            visit(fd);

    return {};
}

std::any Encoder::visitTopDeclarationList(MiniGoParser::TopDeclarationListContext* ctx) {
    return visitChildren(ctx);
}


// Type declarations — registrar struct types en LLVM antes de usarlos
std::any Encoder::visitTypeDecl(MiniGoParser::TypeDeclContext* ctx) {
    return visitChildren(ctx);
}

std::any Encoder::visitSingleTypeDecl(MiniGoParser::SingleTypeDeclContext* ctx) {
    // Forzar creación del StructType en cache para uso posterior
    // El nombre se extrae del IDENTIFIER del nodo
    // No tenemos acceso directo al typeRegistry_ del TypeChecker,
    // pero la primera vez que se declare una variable del tipo, se creará.
    // Aquí solo procesamos si el nodo tiene info de tipo compuesto.
    return {};
}


// Variable declarations
std::any Encoder::visitVariableDecl(MiniGoParser::VariableDeclContext* ctx) {
    return visitChildren(ctx);
}

// var a, b int = expr1, expr2
std::any Encoder::visitVarDeclTypedInit(MiniGoParser::VarDeclTypedInitContext* ctx) {
    auto ids   = ctx->identifierList()->IDENTIFIER();
    auto exprs = ctx->expressionList()->expression();

    for (size_t i = 0; i < ids.size(); i++) {
        string name = ids[i]->getText();

        Value* initVal = nullptr;
        if (i < exprs.size()) {
            auto v = visit(exprs[i]);
            if (v.has_value()) initVal = any_cast<Value*>(v);
        }

        Symbol* sym = findDeclSymbol(ctx, name);
        if (!sym) continue;

        Type* llvmType = typeInfoToLLVM(sym->type);

        if (currentFunctionStack_.empty()) {
            Constant* initConst = Constant::getNullValue(llvmType);
            if (initVal) {
                if (Constant* c = dyn_cast<Constant>(initVal))
                    initConst = c;
                else
                    errs() << "[ENCODER ERROR] La variable global '" << name
                           << "' no puede inicializarse con una expresión no constante "
                           << "(línea " << ids[i]->getSymbol()->getLine()
                           << "); se inicializa a cero.\n";
            }
            ptrMap_[sym] = new GlobalVariable(*module_, llvmType, false,
                                              GlobalValue::ExternalLinkage, initConst, name);
        } else {
            AllocaInst* alloca = createEntryAlloca(llvmType, name);
            if (initVal) {
                Value* storeVal = coerceToType(*builder_, ctx_, initVal, llvmType);
                builder_->CreateStore(storeVal, alloca);
            } else {
                builder_->CreateStore(Constant::getNullValue(llvmType), alloca);
            }
            ptrMap_[sym] = alloca;
        }
    }
    return {};
}

// var a, b = expr1, expr2  (tipo inferido)
std::any Encoder::visitVarDeclInferred(MiniGoParser::VarDeclInferredContext* ctx) {
    auto ids   = ctx->identifierList()->IDENTIFIER();
    auto exprs = ctx->expressionList()->expression();

    for (size_t i = 0; i < ids.size(); i++) {
        string name = ids[i]->getText();

        Value* initVal = nullptr;
        if (i < exprs.size()) {
            auto v = visit(exprs[i]);
            if (v.has_value()) initVal = any_cast<Value*>(v);
        }
        if (!initVal) continue;

        Symbol* sym = findDeclSymbol(ctx, name);
        if (!sym) continue;

        Type* llvmType = typeInfoToLLVM(sym->type);

        if (currentFunctionStack_.empty()) {
            Constant* initConst = Constant::getNullValue(llvmType);
            if (Constant* c = dyn_cast<Constant>(initVal))
                initConst = c;
            else
                errs() << "[ENCODER ERROR] La variable global '" << name
                       << "' no puede inicializarse con una expresión no constante "
                       << "(línea " << ids[i]->getSymbol()->getLine()
                       << "); se inicializa a cero.\n";
            ptrMap_[sym] = new GlobalVariable(*module_, llvmType, false,
                                              GlobalValue::ExternalLinkage, initConst, name);
        } else {
            AllocaInst* alloca = createEntryAlloca(llvmType, name);
            Value* storeVal = coerceToType(*builder_, ctx_, initVal, llvmType);
            builder_->CreateStore(storeVal, alloca);
            ptrMap_[sym] = alloca;
        }
    }
    return {};
}

// var a int  (sin inicializador)
std::any Encoder::visitVarDeclNoInit(MiniGoParser::VarDeclNoInitContext* ctx) {
    return visit(ctx->singleVarDeclNoExps());
}

std::any Encoder::visitSingleVarDeclNoExps(MiniGoParser::SingleVarDeclNoExpsContext* ctx) {
    for (auto* id : ctx->identifierList()->IDENTIFIER()) {
        string name = id->getText();
        Symbol* sym = findDeclSymbol(ctx, name);
        if (!sym) continue;

        Type* llvmType = typeInfoToLLVM(sym->type);

        if (currentFunctionStack_.empty()) {
            ptrMap_[sym] = new GlobalVariable(*module_, llvmType, false,
                                              GlobalValue::ExternalLinkage,
                                              Constant::getNullValue(llvmType), name);
        } else {
            AllocaInst* alloca = createEntryAlloca(llvmType, name);
            builder_->CreateStore(Constant::getNullValue(llvmType), alloca);
            ptrMap_[sym] = alloca;
        }
    }
    return {};
}


// Function declarations
// Crea la firma de la función (sin cuerpo). Se llama en un pase previo para
// que todas las funciones sean visibles entre sí (forward refs / recursión mutua).
void Encoder::declareFunction(MiniGoParser::FuncDeclContext* ctx) {
    auto* front = ctx->funcFrontDecl();
    string name = front->IDENTIFIER()->getText();
    if (functionTable_.count(name)) return; // ya declarada

    Symbol* funcSym = findDeclSymbol(front, name);

    Type* retType = Type::getVoidTy(ctx_);
    if (funcSym && funcSym->type.returnType)
        retType = typeInfoToLLVM(*funcSym->type.returnType);

    // main() debe declararse como i32 en LLVM para que el proceso termine con
    // exit code 0 y no con el valor residual del último registro de retorno.
    bool isMain = (name == "main");
    if (isMain && retType->isVoidTy())
        retType = Type::getInt32Ty(ctx_);

    vector<Type*> paramTypes;
    if (front->funcArgDecls()) {
        for (auto* arg : front->funcArgDecls()->singleVarDeclNoExps()) {
            for (auto* paramId : arg->identifierList()->IDENTIFIER()) {
                Symbol* paramSym = findDeclSymbol(arg, paramId->getText());
                paramTypes.push_back(paramSym ? typeInfoToLLVM(paramSym->type)
                                              : Type::getInt32Ty(ctx_));
            }
        }
    }

    FunctionType* funcType = FunctionType::get(retType, paramTypes, false);
    Function* func = Function::Create(funcType, Function::ExternalLinkage,
                                      name, module_.get());
    functionTable_[name] = {func, funcType};
}

std::any Encoder::visitFuncDecl(MiniGoParser::FuncDeclContext* ctx) {
    auto* front = ctx->funcFrontDecl();
    string name = front->IDENTIFIER()->getText();

    // La firma ya fue creada en declareFunction (pass 2 de visitRoot).
    // Defensa: si por algún motivo no existe, crearla ahora.
    if (!functionTable_.count(name)) declareFunction(ctx);
    FunctionEntry fe = functionTable_[name];
    Function* func  = fe.func;
    Type* retType   = fe.type->getReturnType();
    bool isMain     = (name == "main");

    BasicBlock* entry = BasicBlock::Create(ctx_, "entry", func);
    BasicBlock* savedBlock = builder_->GetInsertBlock();

    currentFunctionStack_.push(func);
    bool savedReturn = returnEmitted_;
    returnEmitted_ = false;
    builder_->SetInsertPoint(entry);

    if (front->funcArgDecls()) {
        size_t argIdx = 0;
        for (auto* arg : front->funcArgDecls()->singleVarDeclNoExps()) {
            for (auto* paramId : arg->identifierList()->IDENTIFIER()) {
                string pname = paramId->getText();
                Argument* llvmArg = func->getArg(argIdx++);
                llvmArg->setName(pname);

                AllocaInst* alloca = createEntryAlloca(llvmArg->getType(), pname);
                builder_->CreateStore(llvmArg, alloca);

                Symbol* paramSym = findDeclSymbol(arg, pname);
                if (paramSym) ptrMap_[paramSym] = alloca;
            }
        }
    }

    visit(ctx->block());

    if (!returnEmitted_) {
        if (isMain)
            // main() siempre termina con i32 0 (éxito)
            builder_->CreateRet(ConstantInt::get(Type::getInt32Ty(ctx_), 0));
        else if (retType->isVoidTy())
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(Constant::getNullValue(retType));
    }

    currentFunctionStack_.pop();
    returnEmitted_ = savedReturn;
    if (savedBlock) builder_->SetInsertPoint(savedBlock);
    return {};
}


// Statements
std::any Encoder::visitStatementList(MiniGoParser::StatementListContext* ctx) {
    return visitChildren(ctx);
}

std::any Encoder::visitBlock(MiniGoParser::BlockContext* ctx) {
    return visitChildren(ctx);
}

std::any Encoder::visitPrintStmt(MiniGoParser::PrintStmtContext* ctx) {
    if (!ctx->expressionList()) return {};
    for (auto* e : ctx->expressionList()->expression()) {
        auto v = visit(e);
        if (v.has_value()) emitPrintf(any_cast<Value*>(v), false);
    }
    return {};
}

std::any Encoder::visitPrintlnStmt(MiniGoParser::PrintlnStmtContext* ctx) {
    if (!ctx->expressionList()) {
        Value* fmtStr = builder_->CreateGlobalStringPtr("\n", "fmt_nl");
        builder_->CreateCall(printfType_, printfFunc_, {fmtStr});
        return {};
    }
    // Go spec: println separa argumentos con espacio y agrega newline al final
    auto exprs = ctx->expressionList()->expression();
    for (size_t i = 0; i < exprs.size(); i++) {
        auto v = visit(exprs[i]);
        if (!v.has_value()) continue;
        Value* val = any_cast<Value*>(v);
        bool isLast = (i == exprs.size() - 1);
        // Prefijo " " entre argumentos, sufijo "\n" en el último
        emitPrintScalar(val, (i > 0 ? " " : ""), isLast);
    }
    return {};
}

std::any Encoder::visitReturnStmt(MiniGoParser::ReturnStmtContext* ctx) {
    if (ctx->expression()) {
        auto v = visit(ctx->expression());
        if (v.has_value()) builder_->CreateRet(any_cast<Value*>(v));
        else               builder_->CreateRetVoid();
    } else {
        builder_->CreateRetVoid();
    }
    returnEmitted_ = true;
    return {};
}

//  break: salta al destino de break más interno (loop o switch)
std::any Encoder::visitBreakStmt(MiniGoParser::BreakStmtContext*) {
    if (!breakStack_.empty()) {
        builder_->CreateBr(breakStack_.top());
        returnEmitted_ = true; // bloque ya terminado
    }
    return {};
}

//  continue: salta a la iteración del loop más interno (ignora switch)
std::any Encoder::visitContinueStmt(MiniGoParser::ContinueStmtContext*) {
    if (!continueStack_.empty()) {
        builder_->CreateBr(continueStack_.top());
        returnEmitted_ = true; // bloque ya terminado
    }
    return {};
}

std::any Encoder::visitSimpleStmt(MiniGoParser::SimpleStmtContext* ctx) {
    return visit(ctx->simpleStatement());
}

std::any Encoder::visitBlockStmt(MiniGoParser::BlockStmtContext* ctx) {
    return visit(ctx->block());
}

std::any Encoder::visitIfStmt(MiniGoParser::IfStmtContext* ctx) {
    return visit(ctx->ifStatement());
}

std::any Encoder::visitLoopStmt(MiniGoParser::LoopStmtContext* ctx) {
    return visit(ctx->loop());
}

std::any Encoder::visitSwitchStmt(MiniGoParser::SwitchStmtContext* ctx) {
    return visit(ctx->switchStatement());
}


// simpleStatement variants
// a, b := expr1, expr2
std::any Encoder::visitShortVarDeclStmt(MiniGoParser::ShortVarDeclStmtContext* ctx) {
    auto leftExprs  = ctx->expressionList(0)->expression();
    auto rightExprs = ctx->expressionList(1)->expression();

    for (size_t i = 0; i < leftExprs.size() && i < rightExprs.size(); i++) {
        auto rv = visit(rightExprs[i]);
        if (!rv.has_value()) continue;
        Value* rval = any_cast<Value*>(rv);

        auto* primExpr = dynamic_cast<MiniGoParser::PrimaryExprContext*>(leftExprs[i]);
        auto* opExpr   = primExpr ? dynamic_cast<MiniGoParser::OperandExprContext*>(
                             primExpr->primaryExpression()) : nullptr;
        auto* idOp     = opExpr ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                             opExpr->operand()) : nullptr;
        if (!idOp) continue;

        string idName = idOp->IDENTIFIER()->getText();
        Symbol* sym   = findDeclSymbol(ctx, idName);

        if (sym) {
            // Nueva variable
            Type* llvmType = typeInfoToLLVM(sym->type);
            AllocaInst* alloca = createEntryAlloca(llvmType, idName);
            builder_->CreateStore(coerceToType(*builder_, ctx_, rval, llvmType), alloca);
            ptrMap_[sym] = alloca;
        } else {
            // Reasignación
            Value* ptr = getPtrForNode(idOp);
            if (ptr) builder_->CreateStore(rval, ptr);
        }
    }
    return {};
}

// a = b
std::any Encoder::visitAssignStmt(MiniGoParser::AssignStmtContext* ctx) {
    auto leftExprs  = ctx->expressionList(0)->expression();
    auto rightExprs = ctx->expressionList(1)->expression();

    for (size_t i = 0; i < leftExprs.size() && i < rightExprs.size(); i++) {
        auto rv = visit(rightExprs[i]);
        if (!rv.has_value()) continue;
        Value* rval = any_cast<Value*>(rv);

        auto* primExpr = dynamic_cast<MiniGoParser::PrimaryExprContext*>(leftExprs[i]);
        if (!primExpr) continue;

        // Caso: arr[idx] = val
        if (auto* idxExpr = dynamic_cast<MiniGoParser::IndexExprContext*>(
                primExpr->primaryExpression())) {
            auto idxV = visit(idxExpr->expression());
            auto* baseOp = dynamic_cast<MiniGoParser::OperandExprContext*>(
                idxExpr->primaryExpression());
            auto* idOp = baseOp ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                baseOp->operand()) : nullptr;
            if (idOp && idxV.has_value()) {
                Value* idx = any_cast<Value*>(idxV);
                Value* elemPtr = getIndexedElemPtr(idOp, idx);
                if (elemPtr) builder_->CreateStore(rval, elemPtr);
            }
            continue;
        }

        // Caso: p.field = val — struct field assignment con GEP
        if (auto* selExpr = dynamic_cast<MiniGoParser::SelectorExprContext*>(
                primExpr->primaryExpression())) {
            auto* baseOp = dynamic_cast<MiniGoParser::OperandExprContext*>(
                selExpr->primaryExpression());
            auto* idOp = baseOp ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                baseOp->operand()) : nullptr;
            if (idOp) {
                string fieldName = selExpr->IDENTIFIER()->getText();
                Value* gep = getStructFieldPtr(idOp, fieldName);
                if (gep) builder_->CreateStore(rval, gep);
            }
            continue;
        }

        // Caso: identificador simple
        auto* opExpr = dynamic_cast<MiniGoParser::OperandExprContext*>(
            primExpr->primaryExpression());
        auto* idOp = opExpr ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
            opExpr->operand()) : nullptr;
        if (!idOp) continue;

        Value* ptr = getPtrForNode(idOp);
        if (ptr) builder_->CreateStore(rval, ptr);
    }
    return {};
}

//  Compound assigns aritméticos 
std::any Encoder::visitAddAssignStmt(MiniGoParser::AddAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) {
        Value* res = lv->getType()->isDoubleTy()
                   ? builder_->CreateFAdd(lv, rv, "fadd")
                   : builder_->CreateAdd(lv, rv, "add");
        builder_->CreateStore(res, dst);
    }
    return {};
}

std::any Encoder::visitSubAssignStmt(MiniGoParser::SubAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) {
        Value* res = lv->getType()->isDoubleTy()
                   ? builder_->CreateFSub(lv, rv, "fsub")
                   : builder_->CreateSub(lv, rv, "sub");
        builder_->CreateStore(res, dst);
    }
    return {};
}

std::any Encoder::visitMulAssignStmt(MiniGoParser::MulAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) {
        Value* res = lv->getType()->isDoubleTy()
                   ? builder_->CreateFMul(lv, rv, "fmul")
                   : builder_->CreateMul(lv, rv, "mul");
        builder_->CreateStore(res, dst);
    }
    return {};
}

std::any Encoder::visitDivAssignStmt(MiniGoParser::DivAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) {
        Value* res = lv->getType()->isDoubleTy()
                   ? builder_->CreateFDiv(lv, rv, "fdiv")
                   : builder_->CreateSDiv(lv, rv, "div");
        builder_->CreateStore(res, dst);
    }
    return {};
}

std::any Encoder::visitModAssignStmt(MiniGoParser::ModAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) {
        Value* res = lv->getType()->isDoubleTy()
                   ? builder_->CreateFRem(lv, rv, "frem")
                   : builder_->CreateSRem(lv, rv, "mod");
        builder_->CreateStore(res, dst);
    }
    return {};
}

//  Compound assigns bitwise (NUEVOS) 
std::any Encoder::visitShlAssignStmt(MiniGoParser::ShlAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) builder_->CreateStore(builder_->CreateShl(lv, rv, "shl"), dst);
    return {};
}

std::any Encoder::visitShrAssignStmt(MiniGoParser::ShrAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) builder_->CreateStore(builder_->CreateAShr(lv, rv, "shr"), dst);
    return {};
}

std::any Encoder::visitAndAssignStmt(MiniGoParser::AndAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) builder_->CreateStore(builder_->CreateAnd(lv, rv, "and"), dst);
    return {};
}

std::any Encoder::visitOrAssignStmt(MiniGoParser::OrAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) builder_->CreateStore(builder_->CreateOr(lv, rv, "or"), dst);
    return {};
}

std::any Encoder::visitXorAssignStmt(MiniGoParser::XorAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst) builder_->CreateStore(builder_->CreateXor(lv, rv, "xor"), dst);
    return {};
}

std::any Encoder::visitAndNotAssignStmt(MiniGoParser::AndNotAssignStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression(0));
    Value* rv  = loadFromExpr(ctx->expression(1));
    Value* dst = getPtrFromExpr(ctx->expression(0));
    if (lv && rv && dst)
        builder_->CreateStore(builder_->CreateAnd(lv, builder_->CreateNot(rv), "andnot"), dst);
    return {};
}

std::any Encoder::visitIncStmt(MiniGoParser::IncStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression());
    Value* dst = getPtrFromExpr(ctx->expression());
    if (lv && dst) builder_->CreateStore(
        builder_->CreateAdd(lv, ConstantInt::get(lv->getType(), 1), "inc"), dst);
    return {};
}

std::any Encoder::visitDecStmt(MiniGoParser::DecStmtContext* ctx) {
    Value* lv  = loadFromExpr(ctx->expression());
    Value* dst = getPtrFromExpr(ctx->expression());
    if (lv && dst) builder_->CreateStore(
        builder_->CreateSub(lv, ConstantInt::get(lv->getType(), 1), "dec"), dst);
    return {};
}

std::any Encoder::visitExprStmt(MiniGoParser::ExprStmtContext* ctx) {
    visit(ctx->expression());
    return {};
}

std::any Encoder::visitEmptyStmt(MiniGoParser::EmptyStmtContext*) { return {}; }


// if — con soporte de if-init
void Encoder::emitIfElse(Value* cond, MiniGoParser::BlockContext* thenBlock,
                          MiniGoParser::BlockContext* elseBlock) {
    Function* fn = currentFunctionStack_.top();

    BasicBlock* thenBB  = BasicBlock::Create(ctx_, "if.then",  fn);
    BasicBlock* elseBB  = BasicBlock::Create(ctx_, "if.else",  fn);
    BasicBlock* mergeBB = BasicBlock::Create(ctx_, "if.merge", fn);

    builder_->CreateCondBr(toBool(cond), thenBB, elseBB);

    builder_->SetInsertPoint(thenBB);
    bool savedReturn = returnEmitted_;
    returnEmitted_ = false;
    visit(thenBlock);
    if (!returnEmitted_) builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(elseBB);
    returnEmitted_ = false;
    if (elseBlock) visit(elseBlock);
    if (!returnEmitted_) builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(mergeBB);
    returnEmitted_ = savedReturn;
}

std::any Encoder::visitIfSimple(MiniGoParser::IfSimpleContext* ctx) {
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) return {};
    emitIfElse(any_cast<Value*>(cv), ctx->block(), nullptr);
    return {};
}

std::any Encoder::visitIfElse(MiniGoParser::IfElseContext* ctx) {
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) return {};
    emitIfElse(any_cast<Value*>(cv), ctx->block(0), ctx->block(1));
    return {};
}

std::any Encoder::visitIfElseIf(MiniGoParser::IfElseIfContext* ctx) {
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) return {};
    Function* fn = currentFunctionStack_.top();

    BasicBlock* thenBB  = BasicBlock::Create(ctx_, "if.then",  fn);
    BasicBlock* elseBB  = BasicBlock::Create(ctx_, "if.else",  fn);
    BasicBlock* mergeBB = BasicBlock::Create(ctx_, "if.merge", fn);

    builder_->CreateCondBr(toBool(any_cast<Value*>(cv)), thenBB, elseBB);

    builder_->SetInsertPoint(thenBB);
    bool saved = returnEmitted_; returnEmitted_ = false;
    visit(ctx->block());
    if (!returnEmitted_) builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(elseBB);
    returnEmitted_ = false;
    visit(ctx->ifStatement());
    if (!returnEmitted_) builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(mergeBB);
    returnEmitted_ = saved;
    return {};
}

std::any Encoder::visitIfWithInit(MiniGoParser::IfWithInitContext* ctx) {
    visit(ctx->simpleStatement());
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) return {};
    emitIfElse(any_cast<Value*>(cv), ctx->block(), nullptr);
    return {};
}

std::any Encoder::visitIfWithInitElse(MiniGoParser::IfWithInitElseContext* ctx) {
    visit(ctx->simpleStatement());
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) return {};
    emitIfElse(any_cast<Value*>(cv), ctx->block(0), ctx->block(1));
    return {};
}

std::any Encoder::visitIfWithInitElseIf(MiniGoParser::IfWithInitElseIfContext* ctx) {
    visit(ctx->simpleStatement());
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) return {};
    Function* fn = currentFunctionStack_.top();

    BasicBlock* thenBB  = BasicBlock::Create(ctx_, "if.then",  fn);
    BasicBlock* elseBB  = BasicBlock::Create(ctx_, "if.else",  fn);
    BasicBlock* mergeBB = BasicBlock::Create(ctx_, "if.merge", fn);

    builder_->CreateCondBr(toBool(any_cast<Value*>(cv)), thenBB, elseBB);

    builder_->SetInsertPoint(thenBB);
    bool saved = returnEmitted_; returnEmitted_ = false;
    visit(ctx->block());
    if (!returnEmitted_) builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(elseBB);
    returnEmitted_ = false;
    visit(ctx->ifStatement());
    if (!returnEmitted_) builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(mergeBB);
    returnEmitted_ = saved;
    return {};
}


// Loops — con soporte de break/continue via loopStack_
std::any Encoder::visitInfiniteLoop(MiniGoParser::InfiniteLoopContext* ctx) {
    Function* fn = currentFunctionStack_.top();
    BasicBlock* bodyBB = BasicBlock::Create(ctx_, "for.body", fn);
    BasicBlock* endBB  = BasicBlock::Create(ctx_, "for.end",  fn);

    builder_->CreateBr(bodyBB);
    builder_->SetInsertPoint(bodyBB);

    breakStack_.push(endBB); continueStack_.push(bodyBB);
    bool saved = returnEmitted_; returnEmitted_ = false;
    visit(ctx->block());
    breakStack_.pop(); continueStack_.pop();

    if (!returnEmitted_) builder_->CreateBr(bodyBB);

    builder_->SetInsertPoint(endBB);
    returnEmitted_ = saved;
    return {};
}

std::any Encoder::visitCondLoop(MiniGoParser::CondLoopContext* ctx) {
    Function* fn = currentFunctionStack_.top();
    BasicBlock* condBB = BasicBlock::Create(ctx_, "for.cond", fn);
    BasicBlock* bodyBB = BasicBlock::Create(ctx_, "for.body", fn);
    BasicBlock* endBB  = BasicBlock::Create(ctx_, "for.end",  fn);

    builder_->CreateBr(condBB);

    builder_->SetInsertPoint(condBB);
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) { builder_->CreateBr(endBB); builder_->SetInsertPoint(endBB); return {}; }
    builder_->CreateCondBr(toBool(any_cast<Value*>(cv)), bodyBB, endBB);

    builder_->SetInsertPoint(bodyBB);
    breakStack_.push(endBB); continueStack_.push(condBB);
    bool saved = returnEmitted_; returnEmitted_ = false;
    visit(ctx->block());
    breakStack_.pop(); continueStack_.pop();

    if (!returnEmitted_) builder_->CreateBr(condBB);

    builder_->SetInsertPoint(endBB);
    returnEmitted_ = saved;
    return {};
}

std::any Encoder::visitForLoop(MiniGoParser::ForLoopContext* ctx) {
    Function* fn = currentFunctionStack_.top();

    visit(ctx->simpleStatement(0)); // init

    BasicBlock* condBB = BasicBlock::Create(ctx_, "for.cond", fn);
    BasicBlock* bodyBB = BasicBlock::Create(ctx_, "for.body", fn);
    BasicBlock* postBB = BasicBlock::Create(ctx_, "for.post", fn);
    BasicBlock* endBB  = BasicBlock::Create(ctx_, "for.end",  fn);

    builder_->CreateBr(condBB);

    builder_->SetInsertPoint(condBB);
    auto cv = visit(ctx->expression());
    if (!cv.has_value()) { builder_->CreateBr(endBB); builder_->SetInsertPoint(endBB); return {}; }
    builder_->CreateCondBr(toBool(any_cast<Value*>(cv)), bodyBB, endBB);

    builder_->SetInsertPoint(bodyBB);
    breakStack_.push(endBB); continueStack_.push(postBB); // continue → post (ejecuta incremento)
    bool saved = returnEmitted_; returnEmitted_ = false;
    visit(ctx->block());
    breakStack_.pop(); continueStack_.pop();

    if (!returnEmitted_) builder_->CreateBr(postBB);

    builder_->SetInsertPoint(postBB);
    returnEmitted_ = false;
    visit(ctx->simpleStatement(1)); // post
    builder_->CreateBr(condBB);

    builder_->SetInsertPoint(endBB);
    returnEmitted_ = saved;
    return {};
}

// for init;; post — loop sin condición (infinito con init/post)
std::any Encoder::visitForLoopNoExpr(MiniGoParser::ForLoopNoExprContext* ctx) {
    Function* fn = currentFunctionStack_.top();

    visit(ctx->simpleStatement(0)); // init

    BasicBlock* bodyBB = BasicBlock::Create(ctx_, "for.body", fn);
    BasicBlock* postBB = BasicBlock::Create(ctx_, "for.post", fn);
    BasicBlock* endBB  = BasicBlock::Create(ctx_, "for.end",  fn);

    builder_->CreateBr(bodyBB);
    builder_->SetInsertPoint(bodyBB);

    breakStack_.push(endBB); continueStack_.push(postBB);
    bool saved = returnEmitted_; returnEmitted_ = false;
    visit(ctx->block());
    breakStack_.pop(); continueStack_.pop();

    if (!returnEmitted_) builder_->CreateBr(postBB);

    builder_->SetInsertPoint(postBB);
    returnEmitted_ = false;
    visit(ctx->simpleStatement(1)); // post
    builder_->CreateBr(bodyBB);

    builder_->SetInsertPoint(endBB);
    returnEmitted_ = saved;
    return {};
}


// switch — cadena de comparaciones (compatible con cualquier tipo)
// Helper central: genera los bloques de comparación y cuerpos de cada case
void Encoder::emitSwitch(Value* switchVal,
                          MiniGoParser::ExpressionCaseClauseListContext* clauses) {
    Function* fn = currentFunctionStack_.top();
    BasicBlock* mergeBB = BasicBlock::Create(ctx_, "sw.end", fn);

    // Recopilar cláusulas
    auto caseClauses = clauses->expressionCaseClause();

    // Pre-crear bloques de cuerpo
    vector<BasicBlock*> bodyBBs;
    for (size_t i = 0; i < caseClauses.size(); i++)
        bodyBBs.push_back(BasicBlock::Create(ctx_, "sw.case" + to_string(i), fn));
    BasicBlock* defaultBB = mergeBB; // si no hay default, cae al merge

    // Encontrar el default y reasignar
    int defaultIdx = -1;
    for (size_t i = 0; i < caseClauses.size(); i++) {
        auto* switchCase = caseClauses[i]->expressionSwitchCase();
        if (dynamic_cast<MiniGoParser::DefaultClauseContext*>(switchCase)) {
            defaultIdx = (int)i;
            defaultBB = bodyBBs[i];
            break;
        }
    }

    // Emitir cadena de comparaciones
    for (size_t i = 0; i < caseClauses.size(); i++) {
        auto* switchCase = caseClauses[i]->expressionSwitchCase();
        if (dynamic_cast<MiniGoParser::DefaultClauseContext*>(switchCase))
            continue; // el default se emite al final

        auto* caseCtx = dynamic_cast<MiniGoParser::CaseClauseContext*>(switchCase);
        if (!caseCtx) continue;

        // Para cada expresión en la lista del case → OR de comparaciones
        auto caseExprs = caseCtx->expressionList()->expression();

        // Calcular el siguiente bloque de comparación.
        // Si el siguiente clause ES el default (o no hay más), ir directo a defaultBB.
        BasicBlock* nextCheckBB = defaultBB;
        for (size_t j = i + 1; j < caseClauses.size(); j++) {
            auto* nc = caseClauses[j]->expressionSwitchCase();
            if (!dynamic_cast<MiniGoParser::DefaultClauseContext*>(nc)) {
                nextCheckBB = BasicBlock::Create(ctx_, "sw.check" + to_string(j), fn);
                break;
            }
        }

        // Si hay múltiples expresiones en el case (case e1, e2:), hacemos OR
        for (size_t j = 0; j < caseExprs.size(); j++) {
            auto cv = visit(caseExprs[j]);
            if (!cv.has_value()) continue;
            Value* caseVal = any_cast<Value*>(cv);

            Value* cmp;
            if (switchVal) {
                // switch expr { case val: }
                if (switchVal->getType()->isDoubleTy())
                    cmp = builder_->CreateFCmpOEQ(switchVal, caseVal, "sw.feq");
                else
                    cmp = builder_->CreateICmpEQ(switchVal, caseVal, "sw.eq");
            } else {
                // switch { case boolExpr: } — la expresión del case es la condición
                cmp = toBool(caseVal);
            }

            BasicBlock* nextExprBB = (j + 1 < caseExprs.size())
                                   ? BasicBlock::Create(ctx_, "sw.or", fn)
                                   : nextCheckBB;
            builder_->CreateCondBr(cmp, bodyBBs[i], nextExprBB);
            if (j + 1 < caseExprs.size())
                builder_->SetInsertPoint(nextExprBB);
        }

        if (nextCheckBB != defaultBB || defaultIdx < 0)
            builder_->SetInsertPoint(nextCheckBB);
    }

    // Si hay default y aún no hemos llegado, saltar a él
    // (el último check ya apuntó al defaultBB o a mergeBB)

    // Emitir cuerpos
    bool savedReturn = returnEmitted_;
    breakStack_.push(mergeBB); // break dentro de switch → merge (continue NO: sigue al loop)

    for (size_t i = 0; i < caseClauses.size(); i++) {
        builder_->SetInsertPoint(bodyBBs[i]);
        returnEmitted_ = false;
        visit(caseClauses[i]->statementList());
        if (!returnEmitted_) builder_->CreateBr(mergeBB);
    }

    breakStack_.pop();
    builder_->SetInsertPoint(mergeBB);
    returnEmitted_ = savedReturn;
}

std::any Encoder::visitSwitchSimple(MiniGoParser::SwitchSimpleContext* ctx) {
    auto sv = visit(ctx->expression());
    Value* switchVal = sv.has_value() ? any_cast<Value*>(sv) : nullptr;
    emitSwitch(switchVal, ctx->expressionCaseClauseList());
    return {};
}

std::any Encoder::visitSwitchWithInit(MiniGoParser::SwitchWithInitContext* ctx) {
    visit(ctx->simpleStatement());
    auto sv = visit(ctx->expression());
    Value* switchVal = sv.has_value() ? any_cast<Value*>(sv) : nullptr;
    emitSwitch(switchVal, ctx->expressionCaseClauseList());
    return {};
}

std::any Encoder::visitSwitchInitNoExpr(MiniGoParser::SwitchInitNoExprContext* ctx) {
    visit(ctx->simpleStatement());
    emitSwitch(nullptr, ctx->expressionCaseClauseList());
    return {};
}

std::any Encoder::visitSwitchNoExpr(MiniGoParser::SwitchNoExprContext* ctx) {
    emitSwitch(nullptr, ctx->expressionCaseClauseList());
    return {};
}


// Expressions


std::any Encoder::visitPrimaryExpr(MiniGoParser::PrimaryExprContext* ctx) {
    return visit(ctx->primaryExpression());
}

std::any Encoder::visitUnaryExpr(MiniGoParser::UnaryExprContext* ctx) {
    auto v = visit(ctx->expression());
    if (!v.has_value()) return v;
    Value* val = any_cast<Value*>(v);
    string op  = ctx->op->getText();

    if (op == "-") {
        if (val->getType()->isDoubleTy())
            return (std::any)(Value*)builder_->CreateFNeg(val, "fneg");
        return (std::any)(Value*)builder_->CreateNeg(val, "neg");
    }
    if (op == "!")  return (std::any)(Value*)builder_->CreateNot(val, "not");
    if (op == "^")  return (std::any)(Value*)builder_->CreateNot(val, "bitnot");
    if (op == "+")  return v;
    return v;
}

std::any Encoder::visitMulExpr(MiniGoParser::MulExprContext* ctx) {
    auto lv = visit(ctx->expression(0));
    auto rv = visit(ctx->expression(1));
    if (!lv.has_value() || !rv.has_value()) return lv;
    Value* L = any_cast<Value*>(lv);
    Value* R = any_cast<Value*>(rv);
    string op = ctx->op->getText();

    bool isFloat = L->getType()->isDoubleTy();
    if (op == "*")   return (std::any)(Value*)(isFloat ? builder_->CreateFMul(L, R, "fmul")
                                                       : builder_->CreateMul(L, R, "mul"));
    if (op == "/")   return (std::any)(Value*)(isFloat ? builder_->CreateFDiv(L, R, "fdiv")
                                                       : builder_->CreateSDiv(L, R, "div"));
    if (op == "%")   return (std::any)(Value*)(isFloat ? builder_->CreateFRem(L, R, "frem")
                                                       : builder_->CreateSRem(L, R, "mod"));
    if (op == "<<")  return (std::any)(Value*)builder_->CreateShl(L, R, "shl");
    if (op == ">>")  return (std::any)(Value*)builder_->CreateAShr(L, R, "shr");
    if (op == "&")   return (std::any)(Value*)builder_->CreateAnd(L, R, "and");
    if (op == "&^")  return (std::any)(Value*)builder_->CreateAnd(L, builder_->CreateNot(R), "andnot");
    return lv;
}

std::any Encoder::visitAddExpr(MiniGoParser::AddExprContext* ctx) {
    auto lv = visit(ctx->expression(0));
    auto rv = visit(ctx->expression(1));
    if (!lv.has_value() || !rv.has_value()) return lv;
    Value* L = any_cast<Value*>(lv);
    Value* R = any_cast<Value*>(rv);
    string op = ctx->op->getText();

    bool isFloat = L->getType()->isDoubleTy();
    if (op == "+")  return (std::any)(Value*)(isFloat ? builder_->CreateFAdd(L, R, "fadd")
                                                      : builder_->CreateAdd(L, R, "add"));
    if (op == "-")  return (std::any)(Value*)(isFloat ? builder_->CreateFSub(L, R, "fsub")
                                                      : builder_->CreateSub(L, R, "sub"));
    if (op == "|")  return (std::any)(Value*)builder_->CreateOr(L, R, "or");
    if (op == "^")  return (std::any)(Value*)builder_->CreateXor(L, R, "xor");
    return lv;
}

std::any Encoder::visitRelExpr(MiniGoParser::RelExprContext* ctx) {
    auto lv = visit(ctx->expression(0));
    auto rv = visit(ctx->expression(1));
    if (!lv.has_value() || !rv.has_value()) return lv;
    Value* L = any_cast<Value*>(lv);
    Value* R = any_cast<Value*>(rv);
    string op = ctx->op->getText();

    if (L->getType()->isDoubleTy()) {
        if (op == "==") return (std::any)(Value*)builder_->CreateFCmpOEQ(L, R, "feq");
        if (op == "!=") return (std::any)(Value*)builder_->CreateFCmpONE(L, R, "fne");
        if (op == "<")  return (std::any)(Value*)builder_->CreateFCmpOLT(L, R, "flt");
        if (op == "<=") return (std::any)(Value*)builder_->CreateFCmpOLE(L, R, "fle");
        if (op == ">")  return (std::any)(Value*)builder_->CreateFCmpOGT(L, R, "fgt");
        if (op == ">=") return (std::any)(Value*)builder_->CreateFCmpOGE(L, R, "fge");
    } else {
        if (op == "==") return (std::any)(Value*)builder_->CreateICmpEQ(L, R, "eq");
        if (op == "!=") return (std::any)(Value*)builder_->CreateICmpNE(L, R, "ne");
        if (op == "<")  return (std::any)(Value*)builder_->CreateICmpSLT(L, R, "lt");
        if (op == "<=") return (std::any)(Value*)builder_->CreateICmpSLE(L, R, "le");
        if (op == ">")  return (std::any)(Value*)builder_->CreateICmpSGT(L, R, "gt");
        if (op == ">=") return (std::any)(Value*)builder_->CreateICmpSGE(L, R, "ge");
    }
    return lv;
}

//  && con short-circuit (lazy evaluation) 
// Si LHS es false → resultado es false sin evaluar RHS
std::any Encoder::visitAndExpr(MiniGoParser::AndExprContext* ctx) {
    Function* fn = currentFunctionStack_.top();

    // Evaluar LHS en bloque actual
    auto lv = visit(ctx->expression(0));
    if (!lv.has_value()) return lv;
    Value* lhsVal = toBool(any_cast<Value*>(lv));
    BasicBlock* lhsBB = builder_->GetInsertBlock();

    // Si LHS es true → evaluar RHS
    BasicBlock* rhsBB   = BasicBlock::Create(ctx_, "and.rhs",   fn);
    BasicBlock* mergeBB = BasicBlock::Create(ctx_, "and.merge", fn);
    builder_->CreateCondBr(lhsVal, rhsBB, mergeBB);

    builder_->SetInsertPoint(rhsBB);
    auto rv = visit(ctx->expression(1));
    Value* rhsVal = rv.has_value() ? toBool(any_cast<Value*>(rv))
                                   : ConstantInt::getFalse(ctx_);
    BasicBlock* rhsEndBB = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(mergeBB);
    PHINode* phi = builder_->CreatePHI(Type::getInt1Ty(ctx_), 2, "and_res");
    phi->addIncoming(ConstantInt::getFalse(ctx_), lhsBB);
    phi->addIncoming(rhsVal, rhsEndBB);
    return (std::any)(Value*)phi;
}

//  || con short-circuit 
// Si LHS es true → resultado es true sin evaluar RHS
std::any Encoder::visitOrExpr(MiniGoParser::OrExprContext* ctx) {
    Function* fn = currentFunctionStack_.top();

    auto lv = visit(ctx->expression(0));
    if (!lv.has_value()) return lv;
    Value* lhsVal = toBool(any_cast<Value*>(lv));
    BasicBlock* lhsBB = builder_->GetInsertBlock();

    BasicBlock* rhsBB   = BasicBlock::Create(ctx_, "or.rhs",   fn);
    BasicBlock* mergeBB = BasicBlock::Create(ctx_, "or.merge", fn);
    builder_->CreateCondBr(lhsVal, mergeBB, rhsBB);

    builder_->SetInsertPoint(rhsBB);
    auto rv = visit(ctx->expression(1));
    Value* rhsVal = rv.has_value() ? toBool(any_cast<Value*>(rv))
                                   : ConstantInt::getFalse(ctx_);
    BasicBlock* rhsEndBB = builder_->GetInsertBlock();
    builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(mergeBB);
    PHINode* phi = builder_->CreatePHI(Type::getInt1Ty(ctx_), 2, "or_res");
    phi->addIncoming(ConstantInt::getTrue(ctx_), lhsBB);
    phi->addIncoming(rhsVal, rhsEndBB);
    return (std::any)(Value*)phi;
}


// primaryExpression variants


std::any Encoder::visitOperandExpr(MiniGoParser::OperandExprContext* ctx) {
    return visit(ctx->operand());
}

// p.field — lectura de campo de struct
std::any Encoder::visitSelectorExpr(MiniGoParser::SelectorExprContext* ctx) {
    auto* prim = dynamic_cast<MiniGoParser::OperandExprContext*>(ctx->primaryExpression());
    auto* idOp = prim ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                     prim->operand()) : nullptr;
    if (!idOp) {
        errs() << "[ENCODER ERROR] selector '." << ctx->IDENTIFIER()->getText()
               << "': la base debe ser un identificador simple "
               << "(los selectores encadenados como p.a.b no están soportados en MiniGo)\n";
        return {};
    }

    string fieldName = ctx->IDENTIFIER()->getText();
    Type* elemType = nullptr;
    Value* gep = getStructFieldPtr(idOp, fieldName, &elemType);
    if (!gep || !elemType) return {};
    return (std::any)(Value*)builder_->CreateLoad(elemType, gep, fieldName + "_val");
}

// arr[i] o slice[i] — GEP + load
std::any Encoder::visitIndexExpr(MiniGoParser::IndexExprContext* ctx) {
    auto idxV = visit(ctx->expression());
    if (!idxV.has_value()) return {};
    Value* idx = any_cast<Value*>(idxV);

    auto* baseOp = dynamic_cast<MiniGoParser::OperandExprContext*>(ctx->primaryExpression());
    auto* idOp   = baseOp ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                       baseOp->operand()) : nullptr;
    if (!idOp) {
        errs() << "[ENCODER ERROR] operador de índice '[...]': "
               << "la base debe ser un identificador simple de array o slice\n";
        return {};
    }

    Type* elemType = nullptr;
    Value* elemPtr = getIndexedElemPtr(idOp, idx, &elemType);
    if (!elemPtr || !elemType) return {};
    return (std::any)(Value*)builder_->CreateLoad(elemType, elemPtr, "elem");
}

// Llamadas a función
std::any Encoder::visitCallExpr(MiniGoParser::CallExprContext* ctx) {
    auto* baseOp = dynamic_cast<MiniGoParser::OperandExprContext*>(ctx->primaryExpression());
    auto* idOp   = baseOp ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                       baseOp->operand()) : nullptr;
    if (!idOp) return {};

    string fname = idOp->IDENTIFIER()->getText();
    auto it = functionTable_.find(fname);
    if (it == functionTable_.end()) {
        errs() << "[ENCODER ERROR] Función no encontrada: " << fname << "\n";
        return {};
    }

    vector<Value*> args;
    if (ctx->expressionList()) {
        for (auto* e : ctx->expressionList()->expression()) {
            auto v = visit(e);
            if (v.has_value()) args.push_back(any_cast<Value*>(v));
        }
    }

    FunctionEntry& fe = it->second;
    bool isVoid = fe.type->getReturnType()->isVoidTy();
    return (std::any)(Value*)builder_->CreateCall(fe.type, fe.func, args,
                                                   isVoid ? "" : fname + "_ret");
}

// append(s, x) — gestión real de capacidad con realloc condicional.
// Slice header: { T*, i32, i32 } (data, len, cap).
// Si len == cap: duplica capacidad con malloc+memcpy.
// Si len < cap: reutiliza el buffer existente.
// Retorna el nuevo slice header para asignación: s = append(s, x)
std::any Encoder::visitAppendExpr(MiniGoParser::AppendExprContext* ctx) {
    auto* primExpr = dynamic_cast<MiniGoParser::PrimaryExprContext*>(ctx->expression(0));
    auto* opExpr   = primExpr ? dynamic_cast<MiniGoParser::OperandExprContext*>(
                         primExpr->primaryExpression()) : nullptr;
    auto* idOp     = opExpr ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                         opExpr->operand()) : nullptr;

    if (!idOp) {
        errs() << "[ENCODER ERROR] append(): el primer argumento debe ser un identificador de slice\n";
        return {};
    }

    auto refIt = referenceMap_.find(idOp);
    if (refIt == referenceMap_.end() || !refIt->second->type.elementType) return {};
    const TypeInfo& sliceTypeInfo = refIt->second->type;

    Type* elemType    = typeInfoToLLVM(*sliceTypeInfo.elementType);
    Type* elemPtrType = PointerType::get(elemType, 0);
    Type* i32Ty       = Type::getInt32Ty(ctx_);
    Type* i64Ty       = Type::getInt64Ty(ctx_);
    Type* i8PtrTy     = PointerType::get(Type::getInt8Ty(ctx_), 0);

    // Evaluar el nuevo elemento antes de cualquier bifurcación
    auto ev = visit(ctx->expression(1));
    if (!ev.has_value()) return {};
    Value* newElem = any_cast<Value*>(ev);

    // Cargar slice header: { T*, i32, i32 }
    Value* slicePtr = getPtrForNode(idOp);
    if (!slicePtr) return {};
    Type*  sliceTy  = getPointeeType(slicePtr);
    Value* sliceVal = builder_->CreateLoad(sliceTy, slicePtr, "slice_cur");

    Value* oldDataPtr = builder_->CreateExtractValue(sliceVal, {0}, "old_data");
    Value* oldLen     = builder_->CreateExtractValue(sliceVal, {1}, "old_len");
    Value* oldCap     = builder_->CreateExtractValue(sliceVal, {2}, "old_cap");

    // Tamaño del elemento en bytes
    uint64_t elemSize = 4;
    if (elemType->isDoubleTy())         elemSize = 8;
    else if (elemType->isIntegerTy(1))  elemSize = 1;
    else if (elemType->isIntegerTy(8))  elemSize = 1;
    else if (elemType->isIntegerTy(64)) elemSize = 8;

    // Bifurcar: ¿se necesita reasignar? (len == cap)
    Value* needRealloc = builder_->CreateICmpEQ(oldLen, oldCap, "need_realloc");

    Function* fn = currentFunctionStack_.top();
    BasicBlock* reallocBB   = BasicBlock::Create(ctx_, "app.realloc", fn);
    BasicBlock* noReallocBB = BasicBlock::Create(ctx_, "app.keep",    fn);
    BasicBlock* appendBB    = BasicBlock::Create(ctx_, "app.store",   fn);

    builder_->CreateCondBr(needRealloc, reallocBB, noReallocBB);

    //  Rama realloc: newCap = (oldCap == 0) ? 1 : oldCap * 2 
    builder_->SetInsertPoint(reallocBB);
    Value* capIsZero = builder_->CreateICmpEQ(oldCap, ConstantInt::get(i32Ty, 0), "cap_zero");
    Value* doubleCap = builder_->CreateMul(oldCap, ConstantInt::get(i32Ty, 2), "double_cap");
    Value* newCapR   = builder_->CreateSelect(capIsZero,
                                               ConstantInt::get(i32Ty, 1),
                                               doubleCap, "new_cap_r");
    Value* newCapExt = builder_->CreateSExt(newCapR, i64Ty, "new_cap64");
    Value* capBytes  = builder_->CreateMul(newCapExt,
                                            ConstantInt::get(i64Ty, (int64_t)elemSize), "cap_bytes");
    Value* rawPtr    = builder_->CreateCall(mallocType_, mallocFunc_, {capBytes}, "raw_mem");
    Value* newDataR  = builder_->CreateBitCast(rawPtr, elemPtrType, "new_data_r");
    // memcpy: copiar elementos existentes al nuevo buffer
    Value* oldLenExt = builder_->CreateSExt(oldLen, i64Ty, "old_len64");
    Value* copyBytes = builder_->CreateMul(oldLenExt,
                                            ConstantInt::get(i64Ty, (int64_t)elemSize), "copy_bytes");
    Value* oldI8     = builder_->CreateBitCast(oldDataPtr, i8PtrTy, "old_i8");
    Value* newI8R    = builder_->CreateBitCast(newDataR, i8PtrTy, "new_i8_r");
    builder_->CreateMemCpy(newI8R, MaybeAlign(elemSize), oldI8, MaybeAlign(elemSize), copyBytes);
    builder_->CreateBr(appendBB);

    //  Rama sin realloc: reutilizar buffer existente 
    builder_->SetInsertPoint(noReallocBB);
    builder_->CreateBr(appendBB);

    //  Bloque de almacenamiento: PHI para data ptr y cap 
    builder_->SetInsertPoint(appendBB);
    PHINode* finalData = builder_->CreatePHI(elemPtrType, 2, "final_data");
    finalData->addIncoming(newDataR,    reallocBB);
    finalData->addIncoming(oldDataPtr,  noReallocBB);
    PHINode* finalCap  = builder_->CreatePHI(i32Ty, 2, "final_cap");
    finalCap->addIncoming(newCapR,  reallocBB);
    finalCap->addIncoming(oldCap,   noReallocBB);

    // Almacenar nuevo elemento en data[oldLen]
    Value* newElemPtr = builder_->CreateInBoundsGEP(elemType, finalData,
                                                     {oldLen}, "new_elem_ptr");
    builder_->CreateStore(newElem, newElemPtr);

    // newLen = oldLen + 1
    Value* newLen = builder_->CreateAdd(oldLen, ConstantInt::get(i32Ty, 1), "new_len");

    // Construir nuevo slice: { finalData, newLen, finalCap }
    Value* newSlice = UndefValue::get(sliceTy);
    newSlice = builder_->CreateInsertValue(newSlice, finalData, {0});
    newSlice = builder_->CreateInsertValue(newSlice, newLen,    {1});
    newSlice = builder_->CreateInsertValue(newSlice, finalCap,  {2});

    return (std::any)(Value*)newSlice;
}

// len(arr) o len(slice)
std::any Encoder::visitLenExpr(MiniGoParser::LenExprContext* ctx) {
    auto* prim = dynamic_cast<MiniGoParser::PrimaryExprContext*>(ctx->expression());
    auto* opEx = prim ? dynamic_cast<MiniGoParser::OperandExprContext*>(
                     prim->primaryExpression()) : nullptr;
    auto* idOp = opEx ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                     opEx->operand()) : nullptr;

    if (idOp) {
        Value* ptr = getPtrForNode(idOp);
        if (ptr) {
            Type* t = getPointeeType(ptr);
            if (t->isArrayTy()) {
                // Array fijo: tamaño conocido en compilación
                uint64_t size = t->getArrayNumElements();
                return (std::any)(Value*)ConstantInt::get(Type::getInt32Ty(ctx_),
                                                          static_cast<int64_t>(size));
            }
            if (t->isStructTy()) {
                // Slice { T*, i32, i32 } — campo 1 = len
                Value* lenGEP = builder_->CreateStructGEP(t, ptr, 1, "len_pp");
                return (std::any)(Value*)builder_->CreateLoad(
                    Type::getInt32Ty(ctx_), lenGEP, "len_val");
            }
        }
    }
    // len(string): la cadena es i8* → strlen, truncado a i32
    auto sv = visit(ctx->expression());
    if (sv.has_value()) {
        Value* sval = any_cast<Value*>(sv);
        if (sval->getType()->isPointerTy()) {
            Value* n64 = builder_->CreateCall(strlenType_, strlenFunc_, {sval}, "strlen");
            return (std::any)(Value*)builder_->CreateTrunc(
                n64, Type::getInt32Ty(ctx_), "len_s");
        }
    }
    return (std::any)(Value*)ConstantInt::get(Type::getInt32Ty(ctx_), 0);
}

// cap(slice) — retorna el campo capacity (índice 2) del slice { T*, i32, i32 }
std::any Encoder::visitCapExpr(MiniGoParser::CapExprContext* ctx) {
    auto* prim = dynamic_cast<MiniGoParser::PrimaryExprContext*>(ctx->expression());
    auto* opEx = prim ? dynamic_cast<MiniGoParser::OperandExprContext*>(
                     prim->primaryExpression()) : nullptr;
    auto* idOp = opEx ? dynamic_cast<MiniGoParser::IdentifierOperandContext*>(
                     opEx->operand()) : nullptr;

    if (idOp) {
        Value* ptr = getPtrForNode(idOp);
        if (ptr) {
            Type* t = getPointeeType(ptr);
            if (t->isStructTy()) {
                // Slice { T*, i32, i32 } — campo 2 = capacity
                Value* capGEP = builder_->CreateStructGEP(t, ptr, 2, "cap_pp");
                return (std::any)(Value*)builder_->CreateLoad(
                    Type::getInt32Ty(ctx_), capGEP, "cap_val");
            }
        }
    }
    return (std::any)(Value*)ConstantInt::get(Type::getInt32Ty(ctx_), 0);
}


// Operands


std::any Encoder::visitLiteralOperand(MiniGoParser::LiteralOperandContext* ctx) {
    return visit(ctx->literal());
}

// Uso de un identificador → load desde su alloca/global
std::any Encoder::visitIdentifierOperand(MiniGoParser::IdentifierOperandContext* ctx) {
    string name = ctx->IDENTIFIER()->getText();

    // Constantes predeclaradas (true/false)
    auto refIt = referenceMap_.find(ctx);
    if (refIt != referenceMap_.end() && refIt->second->isConst) {
        Symbol* sym = refIt->second;
        if (sym->type.kind == TypeKind::BOOL)
            return (std::any)(Value*)ConstantInt::get(Type::getInt1Ty(ctx_),
                                                       name == "true" ? 1 : 0);
    }

    Value* ptr = getPtrForNode(ctx);
    if (!ptr) {
        auto it = functionTable_.find(name);
        if (it != functionTable_.end())
            return (std::any)(Value*)it->second.func;
        errs() << "[ENCODER ERROR] puntero null para '" << name << "'\n";
        return (std::any)(Value*)ConstantInt::get(Type::getInt32Ty(ctx_), 0);
    }
    return (std::any)(Value*)builder_->CreateLoad(getPointeeType(ptr), ptr, name + "_val");
}

std::any Encoder::visitParenOperand(MiniGoParser::ParenOperandContext* ctx) {
    return visit(ctx->expression());
}


// Literals


std::any Encoder::visitIntLit(MiniGoParser::IntLitContext* ctx) {
    string text = ctx->INTLITERAL()->getText();
    int64_t val = 0;
    if (text.size() >= 2 && text[0] == '0') {
        char base = tolower(text[1]);
        if (base == 'x')      val = stoll(text.substr(2), nullptr, 16);
        else if (base == 'o') val = stoll(text.substr(2), nullptr, 8);
        else if (base == 'b') val = stoll(text.substr(2), nullptr, 2);
        else                  val = stoll(text, nullptr, 8);
    } else {
        val = stoll(text);
    }
    return (std::any)(Value*)ConstantInt::get(Type::getInt32Ty(ctx_), val, true);
}

std::any Encoder::visitFloatLit(MiniGoParser::FloatLitContext* ctx) {
    double val = stod(ctx->FLOATLITERAL()->getText());
    return (std::any)(Value*)ConstantFP::get(Type::getDoubleTy(ctx_), val);
}

std::any Encoder::visitRuneLit(MiniGoParser::RuneLitContext* ctx) {
    char ch = parseRuneLiteral(ctx->RUNELITERAL()->getText());
    return (std::any)(Value*)ConstantInt::get(Type::getInt32Ty(ctx_),
                                               static_cast<int64_t>(ch), false);
}

std::any Encoder::visitRawStringLit(MiniGoParser::RawStringLitContext* ctx) {
    string text = ctx->RAWSTRINGLITERAL()->getText();
    string content = text.substr(1, text.size() - 2);
    return (std::any)(Value*)builder_->CreateGlobalStringPtr(content, "rawstr", 0, module_.get());
}

std::any Encoder::visitInterpretedStringLit(MiniGoParser::InterpretedStringLitContext* ctx) {
    string content = unescapeInterpreted(ctx->INTERPRETEDSTRINGLITERAL()->getText());
    return (std::any)(Value*)builder_->CreateGlobalStringPtr(content, "str", 0, module_.get());
}
