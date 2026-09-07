$env:PATH = "C:\msys64\ucrt64\bin;C:\Python314;$env:USERPROFILE\.cargo\bin;$env:PATH"

$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot\..

Write-Host "Compiling native benches..."
g++ -std=c++20 -O3 -mavx2 -mfma -fopenmp -o benchmarks\cpp\bench_cpp.exe benchmarks\cpp\bench_all.cpp
if ($LASTEXITCODE -ne 0) { throw "C++ bench compile failed" }

rustc -C opt-level=3 -C target-cpu=native -o benchmarks\rust\bench_rust.exe benchmarks\rust\bench_all.rs
if ($LASTEXITCODE -ne 0) { throw "Rust bench compile failed" }

$benchNames = @(
    "01. Arithmetic Loop (10M iters)",
    "02. Recursive Fibonacci (fib(32))",
    "03. Ackermann Function (A(3,6))",
    "04. Mandelbrot Fractal (300x300)",
    "05. N-Body Simulation (50k steps)",
    "06. Vector Array Fill (1M doubles)",
    "07. Vector Array Copy (1M doubles)",
    "08. Matrix Multiplication (150x150)",
    "09. In-Place Quicksort (30k ints)",
    "10. Base-10 Radix Sort (30k ints)",
    "11. Binary Trees Allocation (Depth 12)",
    "12. Map / Tree Insertion & Search (15k)",
    "13. String Slicing (50k zero-copy)",
    "14. Functional Closures (10k captures)",
    "15. Vector Dot Product (1M FMA)",
    "16. Stress SIMT Alloc (50k iters)",
    "17. Deep Recursion NaN (1000x500)",
    "18. Massive SIMT Bounce (2M jobs)",
    "19. Dispatch Reduce 500k (+)",
    "20. Typed f64buf Fill (1M dispatch)",
    "21. Uneven Steal (30k x 1..64 sqrt)",
    "22. Tiled 256x256 5-pt Stencil",
    "23. Nested Dispatch + atomic_add",
    "24. JIT 16-Local Mixer (500k)"
)

$isliFiles = @(
    "01_arithmetic.isli",
    "02_fibonacci.isli",
    "03_ackermann.isli",
    "04_mandelbrot.isli",
    "05_nbody.isli",
    "06_array_fill.isli",
    "07_array_copy.isli",
    "08_matrix_mult.isli",
    "09_quicksort.isli",
    "10_radix_sort.isli",
    "11_binary_trees.isli",
    "12_hash_map.isli",
    "13_string_slicing.isli",
    "14_closures.isli",
    "15_dot_product.isli",
    "16_stress_simt_allocation.isli",
    "17_deep_recursion_nan.isli",
    "18_massive_bouncing.isli",
    "19_dispatch_reduce.isli",
    "20_typed_buffer_fill.isli",
    "21_uneven_steal.isli",
    "22_tiled_stencil.isli",
    "23_nested_atomic.isli",
    "24_jit_many_locals.isli"
)

function Get-Ms($raw) {
    $t = ($raw | Out-String).Trim()
    if ($t -eq "") { return $null }
    $line = ($t -split "[\r\n]+" | Where-Object { $_ -match '^-?\d' } | Select-Object -Last 1)
    if (-not $line) { return $null }
    try { return [Math]::Round([double]$line, 2) } catch { return $null }
}

$results = @()
$count = $benchNames.Count

for ($i = 1; $i -le $count; $i++) {
    $name = $benchNames[$i - 1]
    $isliFile = "benchmarks\isli\" + $isliFiles[$i - 1]

    $isliMs = Get-Ms (& .\isli.exe $isliFile 2>$null)
    $cppMs  = Get-Ms (& .\benchmarks\cpp\bench_cpp.exe $i 2>$null)
    $rustMs = Get-Ms (& .\benchmarks\rust\bench_rust.exe $i 2>$null)
    $luaMs  = Get-Ms (& lua .\benchmarks\lua\bench_all.lua $i 2>$null)
    $pyMs   = Get-Ms (& python .\benchmarks\python\bench_all.py $i 2>$null)

    $obj = [PSCustomObject]@{
        Id = $i
        Benchmark = $name
        "Isli (ms)" = $isliMs
        "C++ (ms)" = $cppMs
        "Rust (ms)" = $rustMs
        "Lua (ms)" = $luaMs
        "Python (ms)" = $pyMs
    }
    $results += $obj
    Write-Host ("Completed {0}: Isli={1} C++={2} Rust={3} Lua={4} Py={5}" -f $name, $isliMs, $cppMs, $rustMs, $luaMs, $pyMs)
}

$results | Format-Table -AutoSize
$results | Export-Csv -Path "benchmarks\benchmark_results.csv" -NoTypeInformation -Encoding UTF8
Write-Host "Wrote benchmarks\benchmark_results.csv"
