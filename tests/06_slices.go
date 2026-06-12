package main;

// Test: slices — append, len, cap, índice (lectura y escritura),
// slice como parámetro y como tipo de retorno de una función.
// Complementa a test_arrays.go (arreglos de tamaño fijo).

func squares(n int) []int {
    var s []int;
    var i int = 0;
    for i < n {
        s = append(s, i * i);
        i++;
    };
    return s;
};

func sum(s []int) int {
    var total int = 0;
    var i int = 0;
    for i < len(s) {
        total = total + s[i];
        i++;
    };
    return total;
};

func main() {
    var sq []int = squares(6);
    println(len(sq));     // 6
    println(sq[0]);       // 0
    println(sq[5]);       // 25
    sq[2] = 100;          // escritura por índice
    println(sq[2]);       // 100
    println(sum(sq));     // 0+1+100+9+16+25 = 151
    println(cap(sq));     // capacidad interna (>= 6)

    var empty []int;
    println(len(empty));  // 0
    empty = append(empty, 7);
    empty = append(empty, 8);
    println(len(empty));  // 2
    println(empty[1]);    // 8
};
