fn main() -> i32 {
    let mut x: i32 = -5;
    let y: i32 = 14 % 5;

    if !(y == 4) {
        return 1;
    } else {
        x = -x + y;
    }

    return x;
}
