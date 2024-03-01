Note that this overview document is not yet implemented fully.

## Types

Laye offers the following primitive types:

```
int  i8 i16 i32 i64
uint u8 u16 u32 u64
            f32 f64
bool b8 b16 b32 b64
```

This list is not actually exhaustive, it is simply the set of the most common and "standard" types.

Each of these four categories of types allows a wider range of values for `N` than just what is listed. The type `iN` is a signed integer type with bit width `N`, `uN` is an unsigned integer type with bit width `N`, `fN` is an IEEE 754 floating point type with bit width `N` and `bN` is a boolean type with bit width `N`.

For both integer types as well as the bool type the `N` may be any value greater than `0` and less than `65536`. For the float types, the bit width must be a valid IEEE 754 float bit width supported by the platform.
