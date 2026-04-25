fn main() -> i32 {
    let value: i32 = 1;

    {
        let value: i32 = 41;
        {
            let inner: i32 = value + 1;
            return inner;
        }
    }
}
