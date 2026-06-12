package main;

// Test: arrays de enteros, acceso por índice, len

func sumArray(arr [5]int) int {
    var total int = 0;
    var i int = 0;
    for i < 5 {
        total = total + arr[i];
        i++;
    };
    return total;
};

func main() {
    var nums [5]int;
    nums[0] = 10;
    nums[1] = 20;
    nums[2] = 30;
    nums[3] = 40;
    nums[4] = 50;

    println(nums[0]);   // 10
    println(nums[4]);   // 50

    var size int = len(nums);
    println(size);      // 5

    var total int = sumArray(nums);
    println(total);     // 150

    // Array declarado con literales implícitos (var decl tipada)
    var fib [6]int;
    fib[0] = 0;
    fib[1] = 1;
    var j int = 2;
    for j < 6 {
        fib[j] = fib[j-1] + fib[j-2];
        j++;
    };
    println(fib[5]);    // 5
};
