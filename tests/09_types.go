package main;

// Test: impresión de TODOS los tipos simples con println, más dos formas
// de switch que test_new_features.go no cubre:
//   - switch con inicializador  (switch d := ...; d { ... })
//   - switch SIN expresión, con cases booleanos (switch { case cond: ... })

func classify(n int) {
    switch {
        case n < 0:
            println(`negativo`);
        case n == 0:
            println(`cero`);
        default:
            println(`positivo`);
    };
};

func main() {
    // Cada tipo simple impreso con println
    var i int = 42;          println(i);     // 42
    var f float64 = 3.5;     println(f);     // 3.500000
    var r rune = 'Z';        println(r);     // 90  (valor del code point)
    var flag bool = true;    println(flag);  // true
    var s string = "texto";  println(s);     // texto

    // switch con inicializador
    switch d := i % 2; d {
        case 0:  println(`par`);
        default: println(`impar`);
    };

    // switch sin expresión (cases booleanos)
    classify(-5);   // negativo
    classify(0);    // cero
    classify(8);    // positivo
};
