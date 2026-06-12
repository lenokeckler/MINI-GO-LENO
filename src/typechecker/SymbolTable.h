#pragma once

#include "antlr4-runtime.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <deque>


// TypeInfo — sistema de tipos de MiniGo
enum class TypeKind : uint8_t {
    UNKNOWN = 0,
    INT,      // int (entero, 32 bits en codegen)
    FLOAT,    // float64
    BOOL,     // bool
    RUNE,     // rune (= int32 en Go)
    STRING,   // string
    ARRAY,    // [N]T
    SLICE,    // []T
    STRUCT,   // struct { ... }
    FUNC,     // func(params...) returnType
    VOID,     // sin valor de retorno
};

struct TypeInfo {
    TypeKind kind{TypeKind::UNKNOWN};

    // Para ARRAY: tamaño fijo y tipo elemento
    int64_t arraySize{0};
    std::shared_ptr<TypeInfo> elementType; // ARRAY o SLICE

    // Para STRUCT
    std::string structName; // vacío si anónimo
    std::vector<std::string> fieldNames;
    std::vector<TypeInfo>    fieldTypes;

    // Para FUNC
    std::vector<TypeInfo>    paramTypes;
    std::shared_ptr<TypeInfo> returnType;

    bool operator==(const TypeInfo& o) const {
        if (kind != o.kind) return false;
        switch (kind) {
            case TypeKind::ARRAY:
                return arraySize == o.arraySize &&
                       elementType && o.elementType &&
                       *elementType == *o.elementType;
            case TypeKind::SLICE:
                return elementType && o.elementType &&
                       *elementType == *o.elementType;
            case TypeKind::STRUCT:
                if (!structName.empty() && !o.structName.empty())
                    return structName == o.structName;
                return fieldNames == o.fieldNames && fieldTypes == o.fieldTypes;
            case TypeKind::FUNC: {
                if (paramTypes.size() != o.paramTypes.size()) return false;
                for (size_t i = 0; i < paramTypes.size(); i++)
                    if (paramTypes[i] != o.paramTypes[i]) return false;
                if (!returnType && !o.returnType) return true;
                if (!returnType || !o.returnType) return false;
                return *returnType == *o.returnType;
            }
            default:
                return true; // primitivos son iguales si el kind coincide
        }
    }

    bool operator!=(const TypeInfo& o) const { return !(*this == o); }

    std::string toString() const {
        switch (kind) {
            case TypeKind::INT:    return "int";
            case TypeKind::FLOAT:  return "float64";
            case TypeKind::BOOL:   return "bool";
            case TypeKind::RUNE:   return "rune";
            case TypeKind::STRING: return "string";
            case TypeKind::VOID:   return "void";
            case TypeKind::ARRAY:
                return "[" + std::to_string(arraySize) + "]" +
                       (elementType ? elementType->toString() : "?");
            case TypeKind::SLICE:
                return "[]" + (elementType ? elementType->toString() : "?");
            case TypeKind::STRUCT:
                return structName.empty() ? "struct{...}" : "struct " + structName;
            case TypeKind::FUNC:   return "func(...)";
            default:               return "unknown";
        }
    }

    bool isNumeric() const {
        return kind == TypeKind::INT || kind == TypeKind::FLOAT || kind == TypeKind::RUNE;
    }

    bool isOrdered() const {
        return kind == TypeKind::INT   || kind == TypeKind::FLOAT  ||
               kind == TypeKind::RUNE  || kind == TypeKind::STRING;
    }
};

// Constructores de conveniencia
inline TypeInfo makeInt()    { return TypeInfo{TypeKind::INT}; }
inline TypeInfo makeFloat()  { return TypeInfo{TypeKind::FLOAT}; }
inline TypeInfo makeBool()   { return TypeInfo{TypeKind::BOOL}; }
inline TypeInfo makeRune()   { return TypeInfo{TypeKind::RUNE}; }
inline TypeInfo makeString() { return TypeInfo{TypeKind::STRING}; }
inline TypeInfo makeVoid()   { return TypeInfo{TypeKind::VOID}; }
inline TypeInfo makeUnknown(){ return TypeInfo{TypeKind::UNKNOWN}; }

inline TypeInfo makeArray(int64_t size, TypeInfo elemType) {
    TypeInfo t;
    t.kind        = TypeKind::ARRAY;
    t.arraySize   = size;
    t.elementType = std::make_shared<TypeInfo>(std::move(elemType));
    return t;
}

inline TypeInfo makeSlice(TypeInfo elemType) {
    TypeInfo t;
    t.kind        = TypeKind::SLICE;
    t.elementType = std::make_shared<TypeInfo>(std::move(elemType));
    return t;
}


// Symbol — entrada de la tabla de símbolos
struct Symbol {
    std::string name;
    TypeInfo    type;
    bool        isConst{false};
    bool        isFunc{false};
    antlr4::ParserRuleContext* declCtx{nullptr};  // nodo AST de la declaración (para el Encoder)
    antlr4::Token*             nameTok{nullptr};  // token del identificador (para errores precisos)
    int         scopeLevel{-1};
};


// SymbolTable — scopes anidados con almacenamiento persistente

// Los Symbol se guardan en `permanentStorage` y nunca se mueven
// ni borran, por eso Symbol* sigue siendo válido después de
// closeScope(). Los scopes solo remueven las entradas del índice.
class SymbolTable {
public:
    SymbolTable() { openScope(); } // scope global en nivel 0

    void openScope() {
        scopes_.emplace_back();
        currentLevel_++;
    }

    void closeScope() {
        if (!scopes_.empty()) {
            scopes_.pop_back();
            currentLevel_--;
        }
    }

    // Inserta un símbolo. Retorna nullptr si ya existe en el nivel actual.
    Symbol* insert(Symbol sym) {
        if (scopes_.empty()) return nullptr;
        auto& top = scopes_.back();
        if (top.count(sym.name)) return nullptr; // redeclaración

        sym.scopeLevel = currentLevel_;
        auto& owned = permanentStorage_.emplace_back(std::make_unique<Symbol>(std::move(sym)));
        top[owned->name] = owned.get();
        return owned.get();
    }

    // Busca en todos los scopes (del más interno al más externo).
    Symbol* lookup(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return nullptr;
    }

    // Busca solo en el scope actual.
    Symbol* lookupCurrent(const std::string& name) {
        if (scopes_.empty()) return nullptr;
        auto found = scopes_.back().find(name);
        return found != scopes_.back().end() ? found->second : nullptr;
    }

    // Retorna todos los nombres visibles en cualquier scope abierto.
    // Usado por TypeChecker::findBestMatch para generar sugerencias.
    std::vector<std::string> getAllVisibleNames() const {
        std::vector<std::string> names;
        names.reserve(32);
        for (const auto& scope : scopes_)
            for (const auto& [name, sym] : scope)
                names.push_back(name);
        return names;
    }

    int currentLevel() const { return currentLevel_; }

private:
    // Almacenamiento permanente: los símbolos viven aquí para siempre.
    std::vector<std::unique_ptr<Symbol>> permanentStorage_;
    // Stack de scopes: cada scope es un mapa nombre → Symbol*.
    std::deque<std::unordered_map<std::string, Symbol*>> scopes_;
    int currentLevel_{-1};
};
