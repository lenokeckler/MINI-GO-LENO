package main;

// test_errors.go — Errores semánticos (TypeChecker)
//
// Este archivo es sintácticamente correcto pero viola las
// reglas de tipos, alcances y control de flujo de MiniGo.
// El TypeChecker debe detectar y reportar CADA error con
// línea, columna y sugerencia "did you mean?" cuando aplique.

// (Todo esto NO debe compilar exitosamente.)


// Tipos y funciones auxiliares para las pruebas

type Point struct {
    x int;
    y int;
};

func twoArgs(a int, b int) int {
    return a + b;
};

// ERROR 1: Tipo de retorno incorrecto
func badReturn() int {
    return "esto es string, no int";
};

// ERROR 2: Función no-void sin return statement
func missingReturn() int {
    var x int = 42;
    println(x);
};

// ERROR 3: Inferencia de tipo no permitida para no-primitivos
func nonPrimitiveInference() {
    var p Point;
    q := p;
    println(q.x);
};

// ERROR 4: Compound assign con tipos incompatibles
func badCompoundAssign() {
    var n int = 5;
    n += "texto";
    println(n);
};

// ERROR 5: Case de switch con tipo distinto al switch
func badSwitchCase() {
    var n int = 5;
    switch n {
        case "hola": println(n);
    };
};

// ERROR 6: Switch sin expresión — case debe ser bool─
func badSwitchNoExprCase() {
    var n int = 5;
    switch {
        case 42: println(n);
    };
};

func main() {
    // ERROR 7: Redeclaración en el mismo scope─
    var x int = 10;
    var x int = 20;

    // ERROR 8: Identificador no declarado
    // 'prontln' no existe — did you mean 'println'?
    prontln(x);

    // ERROR 9: Tipos incompatibles en expresión
    var s string = "hola";
    var result int = x + s;
    println(result);

    // ERROR 10: Argumento con tipo incorrecto
    twoArgs(1, "texto");

    // ERROR 11: Return con valor en función void
    return 0;

    // ERROR 12: Llamada a función inexistente
    // 'twoArgz' no existe — did you mean 'twoArgs'?
    twoArgz(1, 2);

    // ERROR 13: Cantidad incorrecta de argumentos─
    twoArgs(1, 2, 3);

    // ERROR 14: Campo de struct inexistente─
    // did you mean 'x' o 'y'?
    var p Point;
    println(p.z);

    // ERROR 15: Índice de array con tipo incorrecto
    var arr [5]int;
    println(arr[1.5]);

    // ERROR 16: Tipo indefinido con typo─
    // did you mean 'Point'?
    var q Ponit;
    println(q.x);

    // ERROR 17: Variable declarada pero nunca usada
    var unused int = 99;

    // ERROR 18: break fuera de loop
    break;

    // ERROR 19: continue fuera de loop
    continue;
};
