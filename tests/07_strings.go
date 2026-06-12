package main;

// Test: cadenas — raw strings (`...`) que NO interpretan escapes, e
// interpreted strings ("...") que SÍ los interpretan (\n, \t, \", \\).
// Todas impresas dentro de println, incluyendo string como parámetro
// y string guardada en variable.

func banner(title string) {
    println(title);
};

func main() {
    // Raw string: el \n queda literal (no es salto de línea)
    println(`Raw: \n queda literal`);

    // Interpreted strings con secuencias de escape
    println("Interpreted:\nsegunda linea");   // salto de línea real
    println("Tab:\tcol");                      // tabulación real
    println("Comillas: \"hi\"");               // comillas escapadas
    println("Backslash: \\");                  // barra invertida

    banner("Como parametro");                  // string como argumento

    var s string = "en variable";
    println(s);                                // string desde variable
};
