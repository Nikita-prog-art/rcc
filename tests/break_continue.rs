fn main() -> i32 {
    let mut i: i32 = 0;
    let mut total: i32 = 0;

    while i < 10 {
        i = i + 1;

        if i == 3 {
            continue;
        } else {
            let keep_going: i32 = 1;
        }

        if i == 7 {
            break;
        } else {
            let still_running: i32 = 1;
        }

        total = total + i;
    }

    return total;
}
