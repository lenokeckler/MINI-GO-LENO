package main;

// Test: type checker con structs y slices (TypeChecker solamente, no codegen)

type Point struct {
    x int;
    y int;
};

type Rectangle struct {
    topLeft     Point;
    bottomRight Point;
};

// Funciones que reciben structs por valor
func distance(p Point) int {
    return p.x * p.x + p.y * p.y;
};

// Slices, solo type checking, no codegen necesario
func firstElement(s []int) int {
    return s[0];
};

func appendTest() {
    var s []int;
    s = append(s, 1);
    s = append(s, 2);
    s = append(s, 3);
    println(len(s));    // 3
    println(cap(s));
};

func main() {
    var p Point;
    p.x = 3;
    p.y = 4;
    println(distance(p));  // 25 (3*3 + 4*4)

    appendTest();
};
