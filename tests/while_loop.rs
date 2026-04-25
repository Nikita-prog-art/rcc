fn sum_to(limit: i32) -> i32 {
    let mut i: i32 = 0;
    let mut total: i32 = 0;

    while i < limit {
        total = total + i;
        i = i + 1;
    }

    return total;
}

fn main() -> i32 {
    return sum_to(5);
}
