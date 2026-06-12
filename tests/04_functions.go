package main;

// Test: funciones, parámetros, retorno, recursión

func factorial(n int) int {
    if n <= 1 {
        return 1;
    };
    return n * factorial(n - 1);
};

func fibonacci(n int) int {
    if n <= 0 {
        return 0;
    };
    if n == 1 {
        return 1;
    };
    return fibonacci(n-1) + fibonacci(n-2);
};

func max(a int, b int) int {
    if a > b {
        return a;
    };
    return b;
};

func greet(name string) {
    println(`Hola, `);
    println(name);
};

func main() {
    println(factorial(5));     // 120
    println(factorial(10));    // 3628800

    println(fibonacci(0));     // 0
    println(fibonacci(7));     // 13

    println(max(10, 42));      // 42
    println(max(-5, -3));      // -3

    greet("MiniGo");
};
