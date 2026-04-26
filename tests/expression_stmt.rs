fn side_effect(value: i32) -> i32 {
    return value;
}

fn main() -> i32 {
    side_effect(42);
    return 0;
}
