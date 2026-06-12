package main;

// ============================================================
// test_errors_parser.go — Errores sintácticos (PARSER)
//
// Este archivo usa tokens válidos del lexer pero en posiciones
// que violan las reglas gramaticales de MiniGo.
//
// Salida esperada: PARSER ERROR por cada violación.
// ============================================================

// PARSER ERROR 1: 'funcc' es un identificador, no un keyword.
// El parser espera 'func', 'var' o 'type' a nivel global.
// → did you mean 'func'?
funcc badlyNamedFunc(x int) int {
    return x * 2;
};

func main() {
    var x int = 10;

    // PARSER ERROR 2: 'if' en MiniGo requiere bloque '{...}'.
    // No se puede omitir las llaves como en otros lenguajes.
    if x > 5
        println(x);
    ;

    // PARSER ERROR 3: expresión incompleta — falta el operando derecho.
    var y int = x + ;

    println(x);
};
