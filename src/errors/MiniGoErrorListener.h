#pragma once

#include "antlr4-runtime.h"
#include "MiniGoParser.h"   // token type constants (SEMI, RBRACE, etc.)
#include "ErrorUtils.h"

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <cctype>
#include <limits>
#include <memory>


// Helpers para display profesional de tokens ANTLR4

// Convierte un tipo de token a su representación legible para el usuario.
//   • Tokens con nombre literal en la gramática (keywords, operadores):
//     devuelve el carácter/texto real — ')', ';', 'println', 'if', etc.
//   • Tokens complejos sin nombre literal (IDENTIFIER, *LITERAL):
//     devuelve una descripción en lenguaje natural — 'identifier', 'string literal'.
inline std::string minigoTokenDisplay(int tokenType,
                                       const antlr4::dfa::Vocabulary& vocab) {
    // getLiteralName/getSymbolicName devuelven string_view en ANTLR4 reciente
    std::string lit = std::string(vocab.getLiteralName(tokenType));
    if (!lit.empty()) {
        // ANTLR4 envuelve los literales en comillas simples: "'token'" → quitar
        if (lit.size() >= 2 && lit.front() == '\'' && lit.back() == '\'')
            return lit.substr(1, lit.size() - 2);
        return lit;
    }

    // Tokens sin nombre literal: mapear simbólico → descripción en lenguaje natural
    std::string sym = std::string(vocab.getSymbolicName(tokenType));
    static const std::unordered_map<std::string, std::string> desc = {
        {"IDENTIFIER",               "identifier"},
        {"INTLITERAL",               "integer literal"},
        {"FLOATLITERAL",             "float literal"},
        {"INTERPRETEDSTRINGLITERAL", "string literal"},
        {"RAWSTRINGLITERAL",         "raw string literal"},
        {"RUNELITERAL",              "rune literal"},
        {"EOF",                      "end of file"},
    };
    auto it = desc.find(sym);
    return (it != desc.end()) ? it->second
                               : (sym.empty() ? "token" : sym);
}

// Formatea un IntervalSet de tokens esperados como texto legible.
// Muestra hasta 4 alternativas; si hay más se usa "..." al final.
inline std::string minigoFormatExpected(const antlr4::misc::IntervalSet& expected,
                                         const antlr4::dfa::Vocabulary& vocab) {
    std::vector<std::string> names;
    for (auto t : expected.toList()) {
        if (t <= 0) continue;                          // ignorar EOF / INVALID
        std::string d = minigoTokenDisplay((int)t, vocab);
        bool isDesc = (d.find(' ') != std::string::npos); // "string literal" tiene espacio
        names.push_back(isDesc ? d : "'" + d + "'");
        if ((int)names.size() >= 4) { names.push_back("..."); break; }
    }
    if (names.empty()) return "another token";
    if (names.size() == 1) return names[0];

    // Unir: "A, B or C"
    std::string res;
    for (size_t i = 0; i + 1 < names.size(); i++)
        res += names[i] + (i + 2 < names.size() ? ", " : " or ");
    return res + names.back();
}

// Umbral: si expected tiene más tokens que este valor se considera
// "contexto de expresión" (demasiados tokens para listar individualmente).
constexpr int MINIGO_EXPR_THRESHOLD = 6;


// MiniGoSyntaxErrorListener — implementación profesional
//
// Principios de diseño:
//   1. Usa las APIs directas de ANTLR4 (getExpectedTokens / getVocabulary)
//      en lugar de parsear el string del mensaje — robusto ante cambios del runtime.
//   2. Muestra el token real (')') nunca el nombre interno (RPAREN).
//   3. Separa errores léxicos de errores de parser para evitar cascadas.
//   4. Limita a un error por línea en la salida de parser errors.


class MiniGoSyntaxErrorListener : public antlr4::BaseErrorListener {
    std::vector<std::string> scannerErrors_;
    std::vector<std::string> parserErrors_;

    //  Helpers privados 

    // Ajusta línea/columna al final de la línea anterior cuando el token
    // "faltante" debería estar ahí (p.ej. ';' al fin de sentencia).
    // Ajusta posición al final del token anterior (donde debería estar el token
    // faltante). Correcto incluso si hay líneas en blanco entre statements:
    // println(msg) en línea 25 + línea en blanco + var en línea 27 → ajusta a 25.
    static bool tryFixToPrevLine(antlr4::Recognizer* rec, antlr4::Token* sym,
                                  size_t& line, size_t& col) {
        if (!sym || line <= 1) return false;
        if (auto* p = dynamic_cast<antlr4::Parser*>(rec)) {
            auto* ts = dynamic_cast<antlr4::CommonTokenStream*>(p->getTokenStream());
            if (ts && sym->getTokenIndex() > 0) {
                antlr4::Token* prev = ts->get(sym->getTokenIndex() - 1);
                // Condición correcta: el token previo está EN CUALQUIER LÍNEA ANTERIOR
                // (no solo line-1), lo que maneja líneas en blanco entre statements.
                if (prev && prev->getLine() > 0 && prev->getLine() < line) {
                    line = prev->getLine();
                    col  = prev->getCharPositionInLine() + prev->getText().size() + 1;
                    return true;
                }
            }
        }
        return false;
    }

    // Retorna el texto del token inmediatamente anterior al ofensivo.
    static std::string getPrevText(antlr4::Recognizer* rec, antlr4::Token* sym) {
        if (!sym) return {};
        if (auto* p = dynamic_cast<antlr4::Parser*>(rec)) {
            auto* ts = dynamic_cast<antlr4::CommonTokenStream*>(p->getTokenStream());
            if (ts && sym->getTokenIndex() > 0) {
                antlr4::Token* prev = ts->get(sym->getTokenIndex() - 1);
                if (prev) return prev->getText();
            }
        }
        return {};
    }

    // Retorna el token en posición relativa al ofensivo (offset=-1 → anterior, -2 → dos atrás).
    static antlr4::Token* getRelToken(antlr4::Recognizer* rec, antlr4::Token* sym, int offset) {
        if (!sym) return nullptr;
        if (auto* p = dynamic_cast<antlr4::Parser*>(rec)) {
            auto* ts = dynamic_cast<antlr4::CommonTokenStream*>(p->getTokenStream());
            if (!ts) return nullptr;
            int idx = (int)sym->getTokenIndex() + offset;
            if (idx >= 0 && idx < (int)ts->size()) return ts->get((size_t)idx);
        }
        return nullptr;
    }

    // Filtra la lista de errores de parser dejando solo el primero por línea.
    // Evita la cascada de "missing ';'" generada por un único token inválido.
    static std::vector<std::string> filterOnePerLine(
            const std::vector<std::string>& errs) {
        std::set<int> seen;
        std::vector<std::string> out;
        for (const auto& e : errs) {
            auto pos = e.rfind("line: ");
            int  line = -1;
            if (pos != std::string::npos)
                try { line = std::stoi(e.substr(pos + 6)); } catch (...) {}
            if (line < 0 || seen.insert(line).second)
                out.push_back(e);
        }
        return out;
    }

    // Construye el sufijo de posición estándar.
    static std::string pos(size_t line, size_t col) {
        return " (line: " + std::to_string(line) +
               ", column: " + std::to_string(col) + ")";
    }

public:
    bool hasErrors() const {
        return !scannerErrors_.empty() || !parserErrors_.empty();
    }

    // Si hay errores léxicos, devuelve SOLO esos — los de parser son artefactos
    // del stream de tokens corrupto causado por el error léxico.
    // Si solo hay errores de parser, aplica el filtro de uno por línea
    // y luego suprime las cascadas de estructura de bloque cuando se detectó
    // contenido inesperado después de ';'.
    std::vector<std::string> getErrors() const {
        if (!scannerErrors_.empty()) return scannerErrors_;

        auto filtered = filterOnePerLine(parserErrors_);

        // Detectar si hay algún error de "unexpected content after ';'"
        // (basura después de semicolon detectada por nuestro análisis contextual).
        int garbageLine = -1;
        for (const auto& e : filtered)
            if (e.find("unexpected content after") != std::string::npos) {
                auto pos = e.rfind("line: ");
                if (pos != std::string::npos)
                    try { garbageLine = std::stoi(e.substr(pos + 6)); } catch (...) {}
                break;
            }

        if (garbageLine < 0) return filtered;

        // Suprimir errores que son artefactos de la recuperación de ANTLR4
        // tras la basura detectada (confusión de estructura de bloque):
        //   • "Missing ';' at end of statement" — el bloque no se cerró bien
        //   • "Extraneous '}'" — la llave de cierre quedó en posición inesperada
        //   • "Unexpected '<EOF>'" — el parser perdió la pista del bloque
        std::vector<std::string> cleaned;
        for (const auto& e : filtered) {
            bool isCascade =
                (e.find("Missing ';' at end of statement") != std::string::npos) ||
                (e.find("Extraneous '}'") != std::string::npos) ||
                (e.find("Unexpected '<EOF>'") != std::string::npos);
            if (!isCascade || e.find("unexpected content after") != std::string::npos)
                cleaned.push_back(e);
        }
        return cleaned;
    }

    //  Punto de entrada principal 
    void syntaxError(antlr4::Recognizer* rec,
                     antlr4::Token*      sym,
                     size_t line, size_t charPos,
                     const std::string&  msg,
                     std::exception_ptr) override {

        bool isLex = (dynamic_cast<antlr4::Lexer*>(rec) != nullptr);
        size_t rLine = line;
        size_t rCol  = charPos + 1;
        std::string rMsg;

        //  Error léxico 
        if (isLex) {
            rMsg = minigoLexerErrorMsg(msg);
            scannerErrors_.push_back("SCANNER ERROR: " + rMsg + pos(rLine, rCol));
            return;
        }

        //  Error de parser: APIs directas ANTLR4 
        auto* parser      = dynamic_cast<antlr4::Parser*>(rec);
        const auto& vocab = parser->getVocabulary();
        auto  expected    = parser->getExpectedTokens();

        // Tokens esperados sin EOF ni INVALID
        std::vector<ssize_t> relevant;
        for (auto t : expected.toList())
            if (t > 0) relevant.push_back(t);
        int expCount = (int)relevant.size();

        std::string foundTxt = sym ? sym->getText() : "";
        std::string prevTxt  = getPrevText(rec, sym);

        // ── CHECK UNIVERSAL: keyword de control mal escrito ───────────
        // Patrón: 'vmar msg = ...' → expected={;}, prevTxt='vmar' ≈ 'var'
        // Se aplica antes de cualquier branch específico para capturar el typo
        // independientemente de si ANTLR4 reporta missing/mismatched/nva.
        if (expCount == 1 && (int)relevant[0] == MiniGoParser::SEMI &&
            minigoIsIdentifierLike(prevTxt)) {
            std::string kwSug = minigoSuggest(prevTxt);
            if (!kwSug.empty() && minigoIsControlKeyword(kwSug)) {
                // Punto al token mal escrito (prevTxt), no al token de error (sym)
                antlr4::Token* prevTok = getRelToken(rec, sym, -1);
                if (prevTok) {
                    rLine = prevTok->getLine();
                    rCol  = (size_t)prevTok->getCharPositionInLine() + 1;
                }
                rMsg = "Unexpected '" + prevTxt + "'"
                       "; did you mean the keyword '" + kwSug + "'?";
                parserErrors_.push_back("PARSER ERROR: " + rMsg + pos(rLine, rCol));
                return;
            }
        }

        //  MISSING: ANTLR4 insertó un token que no estaba
        if (msg.find("missing") != std::string::npos) {
            // Ajustar al final de la línea del token anterior
            tryFixToPrevLine(rec, sym, rLine, rCol);

            if (expCount == 1) {
                std::string exp1 = minigoTokenDisplay((int)relevant[0], vocab);
                if (exp1 == ";") {
                    //  Verificar si es basura después de ';' en la misma línea 
                    // Patrón: var ok bool = true;b\nif ok {
                    //   → 'b' y ';' están en la misma línea → 'b' es basura
                    // Contraste: var ok bool = true;\nb\nif ok {
                    //   → 'b' en línea diferente → le falta el ';' a 'b'
                    antlr4::Token* prevTok = getRelToken(rec, sym, -1); // 'b'
                    antlr4::Token* ppTok   = getRelToken(rec, sym, -2); // ';'

                    if (prevTok && ppTok &&
                        ppTok->getText() == ";" &&
                        prevTok->getLine() == ppTok->getLine() &&
                        prevTok->getLine() <= sym->getLine() && // <= permite múltiples palabras en misma línea
                        (minigoIsIdentifierLike(prevTxt) || minigoIsLiteral(prevTxt))) {
                        // 'b' está en la misma línea que ';' → es contenido inesperado
                        rMsg  = "Extraneous '" + prevTxt
                                + "' — unexpected content after ';'"
                                " (remove it or move to a new line)";
                        rLine = prevTok->getLine();
                        rCol  = (size_t)prevTok->getCharPositionInLine() + 1;
                    } else {
                        rMsg = "Missing ';' at end of statement";
                    }
                } else {
                    rMsg = "Missing '" + exp1 + "'";
                }
            } else if (expCount > 1) {
                rMsg = "Missing " + minigoFormatExpected(expected, vocab);
            } else {
                rMsg = "Missing token";
            }

        //  MISMATCHED: token incorrecto en esa posición 
        } else if (msg.find("mismatched") != std::string::npos) {
            if (expCount == 0) {
                // Sin información de expected: usar heurística
                rMsg = "Unexpected token '" + foundTxt + "'";
                std::string s = minigoSuggest(foundTxt);
                if (!s.empty()) rMsg += "; did you mean '" + s + "'?";

            } else if (expCount <= 3) {
                // Detectar el mismo patrón de basura que en "missing":
                // println("ok"); n\n}  →  '}' reportado como mismatched,
                // pero el problema real es 'n' como basura después de ';'
                antlr4::Token* prevTok2 = getRelToken(rec, sym, -1);
                antlr4::Token* ppTok2   = getRelToken(rec, sym, -2);
                if (prevTok2 && ppTok2 &&
                    ppTok2->getText() == ";" &&
                    prevTok2->getLine() == ppTok2->getLine() &&
                    prevTok2->getLine() <= sym->getLine() &&
                    (minigoIsIdentifierLike(prevTxt) || minigoIsLiteral(prevTxt))) {
                    rMsg  = "Extraneous '" + prevTxt + "' — unexpected content after ';'"
                            " (remove it or move to a new line)";
                    rLine = prevTok2->getLine();
                    rCol  = (size_t)prevTok2->getCharPositionInLine() + 1;
                } else {
                    // Pocos tokens esperados → mensaje muy preciso
                    std::string expStr = minigoFormatExpected(expected, vocab);
                    rMsg = "Unexpected '" + foundTxt + "' — expected " + expStr;
                }

            } else {
                // Muchos tokens esperados = contexto de expresión
                if (minigoIsExprDelimiter(foundTxt)) {
                    // Delimitador donde se esperaba un valor → falta expresión
                    rMsg = minigoMissingExprMsg(foundTxt, prevTxt);
                } else {
                    std::string s = minigoSuggest(foundTxt);
                    rMsg = "Unexpected token '" + foundTxt + "'";
                    if (!s.empty()) rMsg += "; did you mean '" + s + "'?";
                }
            }

        //  EXTRANEOUS: token de más que no encaja 
        } else if (msg.find("extraneous") != std::string::npos) {
            if (foundTxt == ")") {
                // ')' de más: distinguir "falta '('" de "sobra ')'"
                if (minigoIsIdentifierLike(prevTxt))
                    rMsg = "Missing '(' after '" + prevTxt
                           + "' — or remove the extra ')'";
                else
                    rMsg = "Extraneous ')' — extra closing parenthesis";

            } else if (expCount == 1 &&
                       (int)relevant[0] == MiniGoParser::SEMI &&
                       minigoIsIdentifierLike(prevTxt) &&
                       (minigoIsLiteral(foundTxt) || minigoIsIdentifierLike(foundTxt))) {
                // Patrón: var x int  10;  o  var x bool  true;
                // ANTLR4 ve "var x int" como declaración completa y "10" sobra
                // donde esperaba ";". En realidad falta "=" entre el tipo y el valor.
                //
                // Fix case 7: excluir 'package name garbage' → ppTok == "package"
                // En ese contexto no falta "=", es basura en la declaración de package.
                antlr4::Token* ppTokE = getRelToken(rec, sym, -2);
                if (ppTokE && ppTokE->getText() == "package") {
                    rMsg = "Unexpected '" + foundTxt + "' in package declaration"
                           " — expected ';' after the package name";
                } else {
                    rMsg = "Missing '=' before '" + foundTxt
                           + "' — did you forget the assignment operator?";
                }

            } else if (expCount > 0 && expCount <= 3) {
                std::string expStr = minigoFormatExpected(expected, vocab);
                rMsg = "Extraneous '" + foundTxt + "' — expected " + expStr;

            } else {
                rMsg = "Extraneous token '" + foundTxt + "'";
                std::string s = minigoSuggest(foundTxt);
                if (!s.empty()) rMsg += "; did you mean '" + s + "'?";
            }

        //  NO VIABLE ALTERNATIVE: ninguna regla encaja
        } else if (msg.find("no viable alternative") != std::string::npos) {
            // Comprobar primero si hay basura después de ';' en la misma línea.
            // Ejemplo: println("ok"); n\n}  →  '}' dispara "no viable alternative"
            // porque 'n' sin ';' hace que '}' no encaje con ninguna regla.
            antlr4::Token* prevTokNva = getRelToken(rec, sym, -1);
            antlr4::Token* ppTokNva   = getRelToken(rec, sym, -2);
            if (prevTokNva && ppTokNva &&
                ppTokNva->getText() == ";" &&
                prevTokNva->getLine() == ppTokNva->getLine() &&
                prevTokNva->getLine() <= sym->getLine() &&
                (minigoIsIdentifierLike(prevTxt) || minigoIsLiteral(prevTxt))) {
                rMsg  = "Extraneous '" + prevTxt + "' — unexpected content after ';'"
                        " (remove it or move to a new line)";
                rLine = prevTokNva->getLine();
                rCol  = (size_t)prevTokNva->getCharPositionInLine() + 1;
            } else if (minigoIsLiteral(foundTxt)) {
                // Literal sin '(' previo → falta '('
                if (minigoIsIdentifierLike(prevTxt))
                    rMsg = "Missing '(' after '" + prevTxt
                           + "' — did you forget the opening parenthesis?";
                else
                    rMsg = "Unexpected '" + foundTxt + "'";
            } else {
                std::string s = minigoSuggest(foundTxt);
                rMsg = "Unexpected token '" + foundTxt + "'";
                if (!s.empty()) rMsg += "; did you mean '" + s + "'?";
            }

        //  OTRO: conservar mensaje original capitalizado 
        } else {
            rMsg = msg;
            if (!rMsg.empty()) rMsg[0] = (char)std::toupper((unsigned char)rMsg[0]);
        }

        parserErrors_.push_back("PARSER ERROR: " + rMsg + pos(rLine, rCol));
    }
};


// MiniGoErrorStrategy — recuperación conservadora por statement
//
// Problema con DefaultErrorStrategy: ante un error recupera consumiendo
// hasta un punto de sincronización LEJANO (a veces el '}' del bloque),
// silenciando todos los errores posteriores en ese bloque.
//
// Solución: recuperar al ';' MÁS CERCANO (fin de statement en MiniGo).
// Esto aísla cada statement y permite reportar errores independientes
// en múltiples líneas del mismo bloque.


// MiniGoErrorStrategy — no modifica la recuperación del DefaultErrorStrategy.
// ANTLR4's DefaultErrorStrategy tiene el mejor balance entre detectar múltiples
// errores y evitar falsos positivos. Nuestras mejoras van en el listener
// (mensajes, líneas, filtros) — no en la estrategia de recuperación.
//
// Nota técnica: sobreescribir recover() para ir al ';' más cercano parece
// intuitivo, pero deja la pila de reglas del parser inconsistente y produce
// falsos positivos en statements válidos. La estrategia por defecto de ANTLR4
// usa follow-sets calculados a partir de la gramática completa, que es más
// preciso aunque a veces no detecte todos los errores en un solo pase.
using MiniGoErrorStrategy = antlr4::DefaultErrorStrategy;
