package main;

// Test: variables simples globales y locales, tipos básicos

var gCount int = 0;
var gName string = "MiniGo";
var gFlag bool = false;

func printInfo() {
    println(gName);
    println(gCount);
};

func main() {
    var x int = 10;
    var y int = 20;
    var z int;

    z = x + y;
    println(z);        // 30

    x += 5;
    println(x);        // 15

    var msg string = "Hola desde MiniGo";
    println(msg);

    var pi float64 = 3.14159;
    println(pi);

    var ok bool = true;
    if ok {
        println("ok es true");
    };

    gCount = 42;
    printInfo();
};
