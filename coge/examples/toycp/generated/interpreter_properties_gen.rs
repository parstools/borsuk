// Generated boundary checks from execution_model.properties; not an exhaustive proof.
fn check_execution_properties(__executor: &Interpreter<'_>) {
    const I32_CASES: &[i32] = &[-2147483648_i32, -2147483647_i32, -65536_i32, -46341_i32, -46340_i32, -256_i32, -2_i32, -1_i32, 0_i32, 1_i32, 2_i32, 255_i32, 256_i32, 46340_i32, 46341_i32, 65536_i32, 2147483646_i32, 2147483647_i32, ];
    let __property_source = InputSpan { begin_byte: 0, end_byte: 0 };
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            for &b in I32_CASES {
                if !(i32::try_from(i128::from(a) + i128::from(b)).is_ok()) { continue; }
                __checked += 1;
                assert_eq!(__executor.checked_add_i32(a, b, __property_source).map(i128::from), Ok(i128::from(a) + i128::from(b)), "property 1 (checked_add_i32) a={a} b={b}");
            }
        }
        assert!(__checked > 0, "property 1 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            for &b in I32_CASES {
                if !(!i32::try_from(i128::from(a) + i128::from(b)).is_ok()) { continue; }
                __checked += 1;
                let __fault = __executor.checked_add_i32(a, b, __property_source).expect_err(&format!("property 2 (checked_add_i32) a={a} b={b}"));
                assert_eq!(__fault.message, "integer overflow", "property 2 (checked_add_i32) a={a} b={b}");
                assert_eq!(__fault.source, __property_source, "property 2 (checked_add_i32) a={a} b={b}");
            }
        }
        assert!(__checked > 0, "property 2 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            for &b in I32_CASES {
                if !(i32::try_from(i128::from(a) - i128::from(b)).is_ok()) { continue; }
                __checked += 1;
                assert_eq!(__executor.checked_sub_i32(a, b, __property_source).map(i128::from), Ok(i128::from(a) - i128::from(b)), "property 3 (checked_sub_i32) a={a} b={b}");
            }
        }
        assert!(__checked > 0, "property 3 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            for &b in I32_CASES {
                if !(!i32::try_from(i128::from(a) - i128::from(b)).is_ok()) { continue; }
                __checked += 1;
                let __fault = __executor.checked_sub_i32(a, b, __property_source).expect_err(&format!("property 4 (checked_sub_i32) a={a} b={b}"));
                assert_eq!(__fault.message, "integer overflow", "property 4 (checked_sub_i32) a={a} b={b}");
                assert_eq!(__fault.source, __property_source, "property 4 (checked_sub_i32) a={a} b={b}");
            }
        }
        assert!(__checked > 0, "property 4 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            for &b in I32_CASES {
                if !(i32::try_from(i128::from(a) * i128::from(b)).is_ok()) { continue; }
                __checked += 1;
                assert_eq!(__executor.checked_mul_i32(a, b, __property_source).map(i128::from), Ok(i128::from(a) * i128::from(b)), "property 5 (checked_mul_i32) a={a} b={b}");
            }
        }
        assert!(__checked > 0, "property 5 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            for &b in I32_CASES {
                if !(!i32::try_from(i128::from(a) * i128::from(b)).is_ok()) { continue; }
                __checked += 1;
                let __fault = __executor.checked_mul_i32(a, b, __property_source).expect_err(&format!("property 6 (checked_mul_i32) a={a} b={b}"));
                assert_eq!(__fault.message, "integer overflow", "property 6 (checked_mul_i32) a={a} b={b}");
                assert_eq!(__fault.source, __property_source, "property 6 (checked_mul_i32) a={a} b={b}");
            }
        }
        assert!(__checked > 0, "property 6 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            if !(i32::try_from(-i128::from(a)).is_ok()) { continue; }
            __checked += 1;
            assert_eq!(__executor.checked_neg_i32(a, __property_source).map(i128::from), Ok(-i128::from(a)), "property 7 (checked_neg_i32) a={a}");
        }
        assert!(__checked > 0, "property 7 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            if !(!i32::try_from(-i128::from(a)).is_ok()) { continue; }
            __checked += 1;
            let __fault = __executor.checked_neg_i32(a, __property_source).expect_err(&format!("property 8 (checked_neg_i32) a={a}"));
            assert_eq!(__fault.message, "integer overflow", "property 8 (checked_neg_i32) a={a}");
            assert_eq!(__fault.source, __property_source, "property 8 (checked_neg_i32) a={a}");
        }
        assert!(__checked > 0, "property 8 has no matching boundary cases");
    }
    {
        let mut __checked = 0usize;
        for &a in I32_CASES {
            for &b in I32_CASES {
                if !((i128::from(b) == 0_i128)) { continue; }
                __checked += 1;
                let __fault = __executor.checked_div_i32(a, b, __property_source).expect_err(&format!("property 9 (checked_div_i32) a={a} b={b}"));
                assert_eq!(__fault.message, "division by zero", "property 9 (checked_div_i32) a={a} b={b}");
                assert_eq!(__fault.source, __property_source, "property 9 (checked_div_i32) a={a} b={b}");
            }
        }
        assert!(__checked > 0, "property 9 has no matching boundary cases");
    }
}
