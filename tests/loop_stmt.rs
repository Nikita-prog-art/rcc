fn main() -> i32 {
    let mut i: i32 = 0;
    let mut sum: i32 = 0;

    loop {
        if i == 5 {
            break;
        } else {
            let still_running: i32 = 1;
        }

        i = i + 1;
        if i % 2 == 0 {
            continue;
        } else {
            let odd: i32 = 1;
        }

        sum = sum + i;
    }

    return sum;
}
