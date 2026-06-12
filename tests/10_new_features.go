package main;

// Test: break, continue, switch, structs codegen, short-circuit

// Struct con GEP
type Point struct {
    x int;
    y int;
};

func makePoint(a int, b int) Point {
    var p Point;
    p.x = a;
    p.y = b;
    return p;
};

// Break en loop
func firstMultipleOf7() int {
    var i int = 1;
    for i <= 100 {
        if i % 7 == 0 {
            break;
        };
        i++;
    };
    return i;
};

// Continue en loop
func sumOdds(n int) int {
    var total int = 0;
    var i int = 1;
    for i <= n {
        if i % 2 == 0 {
            i++;
            continue;
        };
        total = total + i;
        i++;
    };
    return total;
};

// Switch
func grade(score int) {
    switch score {
        case 10:
            println(`Perfecto`);
        case 7, 8, 9:
            println(`Bien`);
        case 5, 6:
            println(`Regular`);
        default:
            println(`Reprobado`);
    };
};

// Short-circuit ||
func isPositiveOrZero(n int) bool {
    return n == 0 || n > 0;
};

func main() {
    // Test struct GEP
    var p Point;
    p.x = 10;
    p.y = 20;
    println(p.x);    // 10
    println(p.y);    // 20

    // Test break
    println(firstMultipleOf7());   // 7

    // Test continue
    println(sumOdds(10));   // 1+3+5+7+9 = 25

    // Test switch
    grade(10);     // Perfecto
    grade(8);      // Bien
    grade(3);      // Reprobado

    // Test short-circuit
    println(isPositiveOrZero(5));   // 1 (true)
    println(isPositiveOrZero(-1));  // 0 (false)
};
