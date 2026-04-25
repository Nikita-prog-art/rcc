fn classify(value: i32) -> i32 {
    if value < 0 {
        return 0;
    } else if value == 0 {
        return 1;
    } else if value < 10 {
        return 2;
    } else {
        return 3;
    }
}

fn main() -> i32 {
    return classify(5);
}
