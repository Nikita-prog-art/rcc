fn main() -> i32 {
    // single-line comment before declaration
    let value: i32 = 40;

    /*
      multi-line block comment
      with operators like -> == && that should be ignored
    */
    let extra: i32 = 2;

    return value + extra; // trailing comment
}
