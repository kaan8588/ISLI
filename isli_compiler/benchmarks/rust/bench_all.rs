use std::time::Instant;
use std::env;
use std::collections::BTreeMap;
use std::rc::Rc;
use std::cell::RefCell;

fn run_bench(id: i32) -> f64 {
    match id {
        1 => { // 01_arithmetic (10M iters)
            let t0 = Instant::now();
            let mut sum: f64 = 0.0;
            for i in 0..10000000 {
                sum += (i * 3 - (i / 2) + (i % 7)) as f64;
            }
            std::hint::black_box(sum);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        2 => { // 02_fibonacci (fib(32))
            fn fib(n: i32) -> i32 {
                if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
            }
            let t0 = Instant::now();
            let res = fib(32);
            std::hint::black_box(res);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        3 => { // 03_ackermann (A(3, 6))
            fn ack(m: i32, n: i32) -> i32 {
                if m == 0 { n + 1 }
                else if n == 0 { ack(m - 1, 1) }
                else { ack(m - 1, ack(m, n - 1)) }
            }
            let t0 = Instant::now();
            let res = ack(3, 6);
            std::hint::black_box(res);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        4 => { // 04_mandelbrot (300x300 grid, 50 max iters)
            let (width, height, max_iter) = (300, 300, 50);
            let t0 = Instant::now();
            let mut count = 0;
            for y in 0..height {
                let ci = (y as f64 * 2.0 / height as f64) - 1.0;
                for x in 0..width {
                    let cr = (x as f64 * 3.0 / width as f64) - 2.0;
                    let mut zr = 0.0;
                    let mut zi = 0.0;
                    let mut iter = 0;
                    while zr * zr + zi * zi <= 4.0 && iter < max_iter {
                        let temp = zr * zr - zi * zi + cr;
                        zi = 2.0 * zr * zi + ci;
                        zr = temp;
                        iter += 1;
                    }
                    if iter == max_iter { count += 1; }
                }
            }
            std::hint::black_box(count);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        5 => { // 05_nbody (50,000 steps)
            let mut x = vec![0.0, 1.0, -1.0];
            let mut y = vec![0.0, 0.5, -0.5];
            let mut vx = vec![0.0, 0.01, -0.01];
            let mut vy = vec![0.0, -0.01, 0.01];
            let mass = vec![100.0, 1.0, 1.0];
            let dt = 0.01;
            let steps = 50000;
            let t0 = Instant::now();
            for _ in 0..steps {
                for i in 0..3 {
                    let mut fx = 0.0;
                    let mut fy = 0.0;
                    for j in 0..3 {
                        if i != j {
                            let dx = x[j] - x[i];
                            let dy = y[j] - y[i];
                            let dist_sq: f64 = dx * dx + dy * dy + 0.001;
                            let dist = dist_sq.sqrt();
                            let f = mass[j] / (dist_sq * dist);
                            fx += f * dx;
                            fy += f * dy;
                        }
                    }
                    vx[i] += fx * dt;
                    vy[i] += fy * dt;
                }
                for i in 0..3 {
                    x[i] += vx[i] * dt;
                    y[i] += vy[i] * dt;
                }
            }
            std::hint::black_box((x, y));
            t0.elapsed().as_secs_f64() * 1000.0
        }
        6 => { // 06_array_fill (1,000,000 doubles)
            let n = 1000000;
            let mut a = vec![0.0f64; n];
            let t0 = Instant::now();
            for i in 0..n {
                a[i] = 42.5;
            }
            std::hint::black_box(a);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        7 => { // 07_array_copy (1,000,000 doubles)
            let n = 1000000;
            let a = vec![123.456f64; n];
            let mut b = vec![0.0f64; n];
            let t0 = Instant::now();
            for i in 0..n {
                b[i] = a[i];
            }
            std::hint::black_box(b);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        8 => { // 08_matrix_mult (150x150)
            let n = 150;
            let a = vec![1.5f64; n * n];
            let b = vec![2.5f64; n * n];
            let mut c = vec![0.0f64; n * n];
            let t0 = Instant::now();
            for i in 0..n {
                let i_offset = i * n;
                for j in 0..n {
                    let mut sum = 0.0;
                    for k in 0..n {
                        sum += a[i_offset + k] * b[k * n + j];
                    }
                    c[i_offset + j] = sum;
                }
            }
            std::hint::black_box(c);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        9 => { // 09_quicksort (30,000 ints)
            let n = 30000;
            let mut arr = vec![0i32; n];
            let mut seed: u64 = 12345;
            for i in 0..n {
                seed = (seed * 1103515245 + 12345) % 2147483648;
                arr[i] = (seed % 100000) as i32;
            }
            fn partition(arr: &mut [i32], low: usize, high: usize) -> usize {
                let pivot = arr[high];
                let mut i = low;
                for j in low..high {
                    if arr[j] <= pivot {
                        arr.swap(i, j);
                        i += 1;
                    }
                }
                arr.swap(i, high);
                i
            }
            fn quick_sort(arr: &mut [i32], low: usize, high: usize) {
                if low < high {
                    let pi = partition(arr, low, high);
                    if pi > 0 { quick_sort(arr, low, pi - 1); }
                    quick_sort(arr, pi + 1, high);
                }
            }
            let t0 = Instant::now();
            quick_sort(&mut arr, 0, n - 1);
            std::hint::black_box(arr);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        10 => { // 10_radix_sort (30,000 ints)
            let n = 30000;
            let mut arr = vec![0i32; n];
            let mut seed: u64 = 12345;
            for i in 0..n {
                seed = (seed * 1103515245 + 12345) % 2147483648;
                arr[i] = (seed % 100000) as i32;
            }
            let t0 = Instant::now();
            let mut max_val = arr[0];
            for &v in arr.iter().skip(1) { if v > max_val { max_val = v; } }
            let mut exp = 1;
            while max_val / exp > 0 {
                let mut output = vec![0i32; n];
                let mut count = [0usize; 10];
                for i in 0..n {
                    let digit = ((arr[i] / exp) % 10) as usize;
                    count[digit] += 1;
                }
                for i in 1..10 { count[i] += count[i - 1]; }
                for i in (0..n).rev() {
                    let digit = ((arr[i] / exp) % 10) as usize;
                    output[count[digit] - 1] = arr[i];
                    count[digit] -= 1;
                }
                arr.copy_from_slice(&output);
                exp *= 10;
            }
            std::hint::black_box(arr);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        11 => { // 11_binary_trees (Depth 12)
            struct TreeNode {
                val: i32,
                left: Option<Box<TreeNode>>,
                right: Option<Box<TreeNode>>,
            }
            fn make_tree(depth: i32) -> Option<Box<TreeNode>> {
                if depth <= 0 {
                    return Some(Box::new(TreeNode { val: 1, left: None, right: None }));
                }
                Some(Box::new(TreeNode {
                    val: depth,
                    left: make_tree(depth - 1),
                    right: make_tree(depth - 1),
                }))
            }
            fn check_tree(node: &Option<Box<TreeNode>>) -> i32 {
                match node {
                    None => 0,
                    Some(n) => n.val + check_tree(&n.left) + check_tree(&n.right),
                }
            }
            let t0 = Instant::now();
            let root = make_tree(12);
            let checksum = check_tree(&root);
            std::hint::black_box(checksum);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        12 => { // 12_hash_map (15,000 ops)
            let n = 15000;
            let mut map = BTreeMap::new();
            let t0 = Instant::now();
            for i in 0..n {
                map.insert(i, i * 3);
            }
            for i in 0..n {
                let _ = map.get(&i);
            }
            for i in (0..n).step_by(2) {
                map.remove(&i);
            }
            std::hint::black_box(map);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        13 => { // 13_string_slicing (50,000 ops)
            let long_str = "Antigravity_Advanced_Agentic_Compiler_Optimization_And_Virtual_Machine_Architecture_Benchmark_String_Slice_1234567890_ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            let n = 50000;
            let t0 = Instant::now();
            for _ in 0..n {
                let s1 = &long_str[..30];
                let s2 = &long_str[long_str.len() - 30..];
                let s3 = &long_str[10..30];
                std::hint::black_box((s1, s2, s3));
            }
            t0.elapsed().as_secs_f64() * 1000.0
        }
        14 => { // 14_closures (10,000 closures)
            let n = 10000;
            let mut list: Vec<Box<dyn Fn(i32) -> i32>> = Vec::with_capacity(n);
            let t0 = Instant::now();
            for i in 0..n {
                let counter = Rc::new(RefCell::new(i as i32));
                let closure = Box::new(move |step: i32| {
                    let mut val = counter.borrow_mut();
                    *val += step;
                    *val
                });
                list.push(closure);
            }
            let mut total = 0;
            for fn_item in list.iter() {
                total += fn_item(1) + fn_item(2);
            }
            std::hint::black_box(total);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        15 => { // 15_dot_product (1,000,000 doubles)
            let n = 1000000;
            let a = vec![1.5f64; n];
            let b = vec![2.5f64; n];
            let t0 = Instant::now();
            let mut total = 0.0f64;
            for i in 0..n {
                total += a[i] * b[i];
            }
            std::hint::black_box(total);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        16 => { // 16_stress_simt_allocation (50,000 arrays)
            let t0 = Instant::now();
            for i in 0..50000 {
                let mut temp = vec![0.0f64; 20];
                for j in 0..20 {
                    temp[j] = (i + j) as f64 * 1.5;
                }
                temp.push(999.0);
                std::hint::black_box(&temp);
            }
            t0.elapsed().as_secs_f64() * 1000.0
        }
        17 => { // 17_deep_recursion_nan (depth 1000 x 500)
            fn deep_rec(depth: i32, acc: f64) -> f64 {
                if depth <= 0 {
                    return acc;
                }
                let val = acc * 1.0001 + (depth as f64).sqrt();
                deep_rec(depth - 1, val)
            }
            let t0 = Instant::now();
            let mut total = 0.0f64;
            for _ in 0..500 {
                total += deep_rec(1000, 1.0);
            }
            std::hint::black_box(total);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        18 => { // 18_massive_bouncing (2,000,000 items)
            let n = 2000000;
            let mut data = vec![1.0f64; n];
            let t0 = Instant::now();
            for i in 0..n {
                data[i] = data[i] * 1.05 + 0.5;
            }
            std::hint::black_box(&data);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        19 => { // 19_dispatch_reduce (500k ones)
            let n = 500000;
            let t0 = Instant::now();
            let s: f64 = (0..n).map(|_| 1.0f64).sum();
            std::hint::black_box(s);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        20 => { // 20_typed_buffer_fill (1M)
            let n = 1000000;
            let mut buf = vec![0.0f64; n];
            let t0 = Instant::now();
            for i in 0..n {
                buf[i] = i as f64 * 0.5;
            }
            std::hint::black_box(&buf);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        21 => { // 21_uneven_steal (30k, 1..64 sqrts)
            let n = 30000;
            let mut buf = vec![0.0f64; n];
            let t0 = Instant::now();
            for i in 0..n {
                let inner = (i % 64) + 1;
                let mut acc = 0.0f64;
                for k in 0..inner {
                    acc += (k as f64 + 1.0).sqrt();
                }
                buf[i] = acc;
            }
            std::hint::black_box(&buf);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        22 => { // 22_tiled_stencil (256x256)
            let w = 256usize;
            let h = 256usize;
            let n = w * h;
            let src = vec![1.0f64; n];
            let mut dst = vec![0.0f64; n];
            let t0 = Instant::now();
            for y in 0..h {
                for x in 0..w {
                    let idx = x + y * w;
                    let mut acc = src[idx];
                    if x > 0 { acc += src[idx - 1]; }
                    if x < w - 1 { acc += src[idx + 1]; }
                    if y > 0 { acc += src[idx - w]; }
                    if y < h - 1 { acc += src[idx + w]; }
                    dst[idx] = acc * 0.2;
                }
            }
            std::hint::black_box(&dst);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        23 => { // 23_nested_atomic (8000 x 8)
            let mut b = 0.0f64;
            let t0 = Instant::now();
            for _ in 0..8000 {
                for _ in 0..8 {
                    b += 1.0;
                }
            }
            std::hint::black_box(b);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        24 => { // 24_jit_many_locals (500k, 16 locals)
            #[inline(never)]
            fn mix(v: i32) -> i32 {
                let a1 = v + 1; let a2 = v + 2; let a3 = v + 3; let a4 = v + 4;
                let a5 = v + 5; let a6 = v + 6; let a7 = v + 7; let a8 = v + 8;
                let a9 = v + 9; let a10 = v + 10; let a11 = v + 11; let a12 = v + 12;
                let a13 = v + 13; let a14 = v + 14; let a15 = v + 15; let a16 = v + 16;
                a1+a2+a3+a4+a5+a6+a7+a8+a9+a10+a11+a12+a13+a14+a15+a16
            }
            let n = 500000;
            let t0 = Instant::now();
            let mut s = 0i32;
            for i in 0..n {
                s = s.wrapping_add(mix(i));
            }
            std::hint::black_box(s);
            t0.elapsed().as_secs_f64() * 1000.0
        }
        _ => 0.0,
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() > 1 {
        let id: i32 = args[1].parse().unwrap_or(1);
        println!("{}", run_bench(id));
    } else {
        for i in 1..=24 {
            println!("Bench {}: {} ms", i, run_bench(i));
        }
    }
}

