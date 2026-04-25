fn classify(value: i32) -> i32 {
    if value < 0 {
        0
    } else if value == 0 {
        1
    } else {
        value + 10
    }
}

fn main() -> i32 {
    classify(5)
}
