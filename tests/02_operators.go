package main;

// Test: operadores — aritméticos con agrupación, asignaciones compuestas,
// bitwise (en expresiones y compuestos), relacionales, lógicos con
// cortocircuito y agrupación, y unarios.

func main() {
    // Aritméticos con paréntesis y precedencia
    var a int = (2 + 3) * 4 - 6 / 2;   // 5*4 - 3 = 17
    println(a);                         // 17
    println(17 % 5);                    // 2

    // Asignaciones compuestas: += -= *= /= %=
    var x int = 100;
    x += 10; x -= 30; x *= 2; x /= 4; x %= 7;
    println(x);                         // 5

    // Bitwise en expresiones: & | ^ << >> &^
    println(12 & 10);                   // 8
    println(12 | 3);                    // 15
    println(6 ^ 3);                     // 5
    println(1 << 5);                    // 32
    println(64 >> 2);                   // 16
    println(12 &^ 10);                  // 4

    // Bitwise compuestos: |= &= ^= <<= >>=
    var b int = 0;
    b |= 5; b &= 6; b ^= 1; b <<= 3; b >>= 1;
    println(b);                         // 20

    // Relacionales + lógicos con cortocircuito y agrupación
    var n int = 7;
    println(n > 0 && n < 10);           // true
    println(n < 0 || n == 7);           // true
    println(!(n == 7));                 // false

    // Unarios: negación numérica y complemento bit a bit
    println(-n);                        // -7
    println(^0);                        // -1
};
