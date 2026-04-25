fn max(lhs: i32, rhs: i32) -> i32 {
    if lhs >= rhs {
        return lhs;
    } else {
        return rhs;
    }
}

fn main() -> i32 {
    return max(10, 7);
}
