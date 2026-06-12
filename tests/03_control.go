package main;

// Test: control de flujo — if/else, for con 3 cláusulas, for condición

func fizzbuzz(n int) {
    var i int = 1;
    for i <= n {
        var fb bool = false;
        if i % 3 == 0 {
            println(`Fizz`);
            fb = true;
        };
        if i % 5 == 0 {
            println(`Buzz`);
            fb = true;
        };
        if !fb {
            println(i);
        };
        i++;
    };
};

func countDown(from int) {
    for i := from; i > 0; i-- {
        println(i);
    };
    println(0);
};

func isPrime(n int) bool {
    if n < 2 {
        return false;
    };
    var i int = 2;
    for i*i <= n {
        if n % i == 0 {
            return false;
        };
        i++;
    };
    return true;
};

func main() {
    fizzbuzz(15);

    countDown(5);

    println(isPrime(7));    // true (1)
    println(isPrime(10));   // false (0)
    println(isPrime(13));   // true (1)

    // for con tres cláusulas
    var sum int = 0;
    for i := 1; i <= 10; i++ {
        sum = sum + i;
    };
    println(sum);   // 55

    // if/else anidado
    var x int = 42;
    if x < 0 {
        println(`negativo`);
    } else {
        if x == 0 {
            println(`cero`);
        } else {
            println(`positivo`);
        };
    };
};
