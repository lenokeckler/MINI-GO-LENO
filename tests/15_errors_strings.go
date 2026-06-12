package main;

// 15_errors_strings.go — Operaciones invalidas con strings en MiniGo
//
// A diferencia de Go, en MiniGo las cadenas SOLO se usan para imprimir
// (println). La gramatica acepta '+' entre cualquier par de expresiones,
// pero el TypeChecker debe RECHAZAR cualquier intento de operar con
// strings, reportando cada caso con linea y columna.
//
// (Todo esto NO debe compilar exitosamente.)

func main() {
    var a string = "hola";
    var b string = "mundo";

    // El uso valido de strings es imprimirlas (esto SI se permite):
    println(a);
    println(b);

    // 1. Concatenacion de dos strings con '+'  -> TYPE ERROR
    var c string = a + b;
    println(c);

    // 2. Concatenacion de string con literal   -> TYPE ERROR
    var d string = a + "!";
    println(d);

    // 3. '+' mezclando string con int          -> TYPE ERROR
    var e string = a + 5;
    println(e);

    // 4. Otros operadores aritmeticos tampoco aplican a strings -> TYPE ERROR
    var f string = a - b;
    println(f);
};
