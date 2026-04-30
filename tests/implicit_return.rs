fn classify(value: i32) -> i32 {
    if value < 0 {
        return 0;
    } else if value == 0 {
        return 1;
    } else {
        return value + 10;
    }
}

fn main() -> i32 {
    classify(5)
}
