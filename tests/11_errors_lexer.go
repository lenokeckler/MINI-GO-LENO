package main;

// ============================================================
// test_errors_lexer.go — Errores léxicos (SCANNER)
//
// Este archivo contiene caracteres que NO forman parte del
// alfabeto de MiniGo. El scanner los rechaza antes de intentar
// parsear o hacer chequeo de tipos.
//
// Salida esperada: SCANNER ERROR por cada carácter inválido.
// ============================================================

func main() {
    var x int = 5;

    // SCANNER ERROR: '@' no es un token válido en MiniGo
    var y int = x @ 2;

    // SCANNER ERROR: '$' no es un token válido en MiniGo
    var z int = $ + x;

    // SCANNER ERROR: '~' no es un token válido en MiniGo
    var w int = ~x;

    println(x);
};
