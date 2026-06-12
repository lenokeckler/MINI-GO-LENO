#pragma once
#include <string>
#include <vector>
#include <set>
#include <algorithm>


// Utilidades compartidas para mensajes de error profesionales
// Usadas por el TypeChecker, main.cpp y Editor.cpp

// Distancia de edición de Levenshtein entre dos cadenas.
// Mide cuántas inserciones, borrados o sustituciones
// se necesitan para transformar 'a' en 'b'.
inline int minigoLevenshtein(const std::string& a, const std::string& b) {
    int m = (int)a.size(), n = (int)b.size();
    // Optimización: si la diferencia de longitudes ya supera el umbral máximo
    if (std::abs(m - n) > 3) return 99;
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;
    for (int i = 1; i <= m; i++)
        for (int j = 1; j <= n; j++)
            dp[i][j] = (a[i-1] == b[j-1])
                       ? dp[i-1][j-1]
                       : 1 + std::min({dp[i-1][j], dp[i][j-1], dp[i-1][j-1]});
    return dp[m][n];
}

// Lista canónica de todos los keywords, built-ins y tipos de MiniGo.
inline const std::vector<std::string>& minigoBuiltins() {
    static const std::vector<std::string> v = {
        // Keywords
        "package", "var", "type", "func", "struct",
        "if", "else", "for", "switch", "case", "default",
        "return", "break", "continue",
        // Built-in funciones
        "append", "len", "cap", "print", "println",
        // Constantes predeclaradas
        "true", "false",
        // Tipos primitivos
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "byte", "float32", "float64", "bool", "rune", "string"
    };
    return v;
}

// Retorna true si el texto es un identificador (solo letras, dígitos, _).
// Usado para distinguir "extraneous )" por missing ( vs extra ).
inline bool minigoIsIdentifierLike(const std::string& text) {
    if (text.empty()) return false;
    if (!std::isalpha((unsigned char)text[0]) && text[0] != '_') return false;
    for (char c : text)
        if (!std::isalnum((unsigned char)c) && c != '_') return false;
    return true;
}

// Retorna true si el token puede ser el inicio de una llamada sin paréntesis:
// un literal de string, raw string, rune o número inmediatamente después de
// un identificador/keyword significa que falta el '(' de apertura.
inline bool minigoIsLiteral(const std::string& text) {
    if (text.empty()) return false;
    char c = text[0];
    return c == '"' || c == '`' || c == '\''
        || std::isdigit((unsigned char)c);
}

// Retorna true si el texto es un keyword de control de MiniGo
// (los que más se escriben mal: var, func, for, if, return, etc.)
// Usado para detectar typos como 'vmar' → 'var', 'funcc' → 'func'.
inline bool minigoIsControlKeyword(const std::string& text) {
    static const std::set<std::string> kws = {
        "package", "var", "type", "func", "struct",
        "if", "else", "for", "switch", "case", "default",
        "return", "break", "continue"
    };
    return kws.count(text) > 0;
}

// Umbral de distancia permitida según la longitud del nombre.
// Palabras cortas toleran menos errores que palabras largas.
inline int minigoThreshold(int nameLen) {
    if (nameLen <= 3) return 1;   // "fo"  → acepta 1 error
    if (nameLen <= 7) return 2;   // "prontln" → acepta 2 errores
    return 3;                      // palabras largas → hasta 3
}

// Busca el candidato más similar a 'name' considerando:
//   • Todos los keywords y built-ins de MiniGo
//   • Cualquier candidato adicional en 'extras' (p.ej. símbolos visibles)
//  Detección de "falta expresión" (Opción 1 + 3) 

// Tokens que no pueden iniciar una expresión en MiniGo.
// Si aparecen donde se esperaba un valor → el programador olvidó escribirlo.
inline bool minigoIsExprDelimiter(const std::string& text) {
    return text == ";" || text == "," || text == ")"
        || text == "}" || text == "]";
}

// Retorna true si el texto es un operador de asignación (simple o compuesto).
inline bool minigoIsAssignOp(const std::string& text) {
    static const std::vector<std::string> ops = {
        "=",  "+=", "-=",  "*=", "/=",  "%=",
        "<<=",">>=" ,"&=", "|=", "^=", "&^="
    };
    return std::find(ops.begin(), ops.end(), text) != ops.end();
}

// Construye un mensaje claro cuando falta una expresión.
// 'offending' = token que encontró ANTLR4.
// 'prevToken' = texto del token anterior (proporciona contexto).
inline std::string minigoMissingExprMsg(const std::string& offending,
                                         const std::string& prevToken) {
    if (minigoIsAssignOp(prevToken))
        return "Missing value after '" + prevToken + "'"
               " — found '" + offending + "' where a value was expected";
    if (prevToken == "(" || prevToken == ",")
        return "Missing expression"
               " — found '" + offending + "' where a value was expected";
    if (prevToken == "[")
        return "Missing expression"
               " — found '" + offending + "' where an index was expected";
    if (prevToken == "return")
        return "Missing return value after 'return'";
    // Genérico: se esperaba una expresión pero apareció un delimitador
    return "Missing expression or value"
           " — found '" + offending + "' where a value was expected";
}

//  Mensajes limpios para errores léxicos 

// Convierte el mensaje crudo de ANTLR4 para un error léxico en uno legible.
// Detecta literales sin cerrar (string, raw string, rune) y caracteres inválidos.
// antlr4Msg ejemplo: "token recognition error at: '\"hello world);'"
inline std::string minigoLexerErrorMsg(const std::string& antlr4Msg) {
    // Extraer el texto que ANTLR4 no pudo tokenizar.
    // Formato ANTLR4: "token recognition error at: 'X'"
    // Usamos rfind para el cierre porque el contenido puede empezar con '
    // (ej: at: ''ab' — literal rune sin cerrar).
    std::string bad;
    auto pos = antlr4Msg.find("at: '");
    if (pos != std::string::npos) {
        size_t s = pos + 5;
        size_t e = antlr4Msg.rfind('\'');   // último ' = cierre del token
        if (e != std::string::npos && e > s)
            bad = antlr4Msg.substr(s, e - s);
        else if (e == s)
            bad = std::string(1, antlr4Msg[s]); // contenido de un solo char
    }
    if (bad.empty()) return antlr4Msg;

    char first = bad[0];

    // Literal de cadena sin cerrar: empieza con "
    if (first == '"')
        return "Unterminated string literal — missing closing '\"'";

    // Raw string sin cerrar: empieza con `
    if (first == '`')
        return "Unterminated raw string literal — missing closing '`'";

    // Rune literal sin cerrar: empieza con ' y tiene más de un carácter
    if (first == '\'' && bad.size() > 1)
        return "Unterminated rune literal — missing closing \"'\"";

    // Carácter inválido: mostrar solo el primer carácter (limpio)
    std::string display;
    switch (first) {
        case '\n': display = "\\n"; break;
        case '\r': display = "\\r"; break;
        case '\t': display = "\\t"; break;
        default:   display = std::string(1, first); break;
    }
    return "Invalid character '" + display + "' — not part of the MiniGo language";
}


// Retorna la mejor sugerencia o cadena vacía si ninguna es suficientemente cercana.
inline std::string minigoSuggest(const std::string& name,
                                  const std::vector<std::string>& extras = {}) {
    if (name.empty() || name == "<EOF>") return {};

    int threshold = minigoThreshold((int)name.size());
    std::string best;
    int bestDist = threshold + 1;

    auto tryCandidate = [&](const std::string& candidate) {
        if (candidate == name) return; // coincidencia exacta — no sugerir
        int d = minigoLevenshtein(name, candidate);
        if (d < bestDist) { bestDist = d; best = candidate; }
    };

    for (const auto& kw : minigoBuiltins()) tryCandidate(kw);
    for (const auto& ex : extras)           tryCandidate(ex);

    return best; // vacío si ningún candidato está dentro del umbral
}
