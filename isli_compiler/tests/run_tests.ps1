# Automated Test Suite Runner for Isli Compiler
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "          ISLI COMPILER AUTOMATED TEST SUITE              " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host ""

$testsDir = $PSScriptRoot
$isliExe = Join-Path $testsDir "..\isli.exe"

if (-not (Test-Path $isliExe)) {
    Write-Host "Error: isli.exe not found at $isliExe" -ForegroundColor Red
    exit 1
}

$tests = @(
    @{ File = "tc01_jit_fallback.isli"; Expected = "34`n56`n90"; Name = "TC-01: JIT to Interpreter Fallback" },
    @{ File = "tc02_binary_tree.isli"; Expected = "42"; Name = "TC-02: Recursive Binary Tree Checksum" },
    @{ File = "tc03_jit_types.isli"; Expected = "IsliLang`ntrue`ntrue`ntrue`ntrue`n-50`ntrue`nfalse"; Name = "TC-03: JIT Type Safety and Comparisons" },
    @{ File = "tc04_many_locals.isli"; Expected = "325"; Name = "TC-04: Many Locals (25 Locals, No Stack Collision)" },
    @{ File = "tc05_struct_return.isli"; Expected = "40`n60"; Name = "TC-05: Struct Return and Property Mutation" },
    @{ File = "tc06_string_views.isli"; Expected = "20`nCompiler`nArchitecture`nArch`n8`n-1`nuser`ndomain.com`nCompilerArchitecture`nCompilerArchitecture`nlerArchitecture"; Name = "TC-06: Zero-Copy String Views & Bounds" },
    @{ File = "tc07_int_format.isli"; Expected = "0`n1234567`n10000000`n-9999999`n3.14159`n0.000125`n42`n42.5"; Name = "TC-07: Integer vs Float Print Formatting" },
    @{ File = "tc08_compound_and_comments.isli"; Expected = "125`n110`n220`n55"; Name = "TC-08: Block Comments & Compound Operators (+=, -=, *=, /=)" },
    @{ File = "tc09_simt_parallel.isli"; Expected = "0`n1000`n199998`n100000"; Name = "TC-09: Multi-threaded SIMT 100k Parallel Dispatch" },
    @{ File = "tc10_radix_sort.isli"; Expected = "2`n24`n45`n66`n75`n90`n170`n802"; Name = "TC-10: In-Place Radix Sort on JIT" },
    @{ File = "tc11_avx2_simd.isli"; Expected = "5`n6`n8`n32`n96"; Name = "TC-11: 256-Bit AVX2 SIMD Vector Arithmetic" },
    @{ File = "tc12_data_structures.isli"; Expected = "20`n10`n50`n100`n10`n50`n7500`ntrue`nfalse`n555`n777"; Name = "TC-12: Built-in Data Structures (Stack, List, Heap, RBTree, BTree)" },
    @{ File = "tc13_upvalue_jit.isli"; Expected = "125`n175`n145"; Name = "TC-13: JIT Upvalue Read/Write" },
    @{ File = "tc14_dispatch_upvalue.isli"; Expected = "0`n15`n27"; Name = "TC-14: Dispatch Kernel Upvalue Array" },
    @{ File = "tc15_typed_buffers.isli"; Expected = "8`nf32`n1.5`n42.25`n16`nf64`n0`n25`n24997.5`n48`n8`n2"; Name = "TC-15: Typed Buffers + Dispatch + SIMD" },
    @{ File = "tc16_cpu_info.isli"; Expected = "true"; Name = "TC-16: CPU Info Native" },
    @{ File = "tc17_reduce.isli"; Expected = "4950`n120`n3`n7`n10000"; Name = "TC-17: Deterministic Dispatch Reduce" },
    @{ File = "tc18_nested_dispatch.isli"; Expected = "10000"; Name = "TC-18: Nested Dispatch + atomic_add" },
    @{ File = "tc19_tile.isli"; Expected = "64"; Name = "TC-19: 2D Tiled Dispatch" },
    @{ File = "tc20_nested_large.isli"; Expected = "5000"; Name = "TC-20: Nested Dispatch Inner >= 4096" },
    @{ File = "tc21_reduce_fail.isli"; Expected = "1"; Name = "TC-21: Reduce Fail Does Not Hang" },
    @{ File = "tc22_jit_kernel_alloc.isli"; Expected = "1"; Name = "TC-22: JIT Kernel Array Alloc Drop" },
    @{ File = "tc23_dispatch_overflow.isli"; Expected = "0"; Name = "TC-23: Dispatch Dimension Overflow" }
)

$passed = 0
$failed = 0

foreach ($t in $tests) {
    $filePath = Join-Path $testsDir $t.File
    $proc = Start-Process -FilePath $isliExe -ArgumentList "`"$filePath`"" -NoNewWindow -PassThru -RedirectStandardOutput "$testsDir\out.tmp" -RedirectStandardError "$testsDir\err.tmp" -Wait
    $out = ""
    if (Test-Path "$testsDir\out.tmp") {
        $out = (Get-Content "$testsDir\out.tmp" -Raw)
        if ($out) { $out = $out.Trim().Replace("`r`n", "`n").Replace("`r", "`n") }
    }
    $exp = $t.Expected.Trim().Replace("`r`n", "`n").Replace("`r", "`n")
    
    if ($out -eq $exp) {
        Write-Host "  [PASS] $($t.Name)" -ForegroundColor Green
        $passed++
    } else {
        Write-Host "  [FAIL] $($t.Name)" -ForegroundColor Red
        Write-Host "    Expected: $exp" -ForegroundColor DarkGray
        Write-Host "    Got:      $out" -ForegroundColor Yellow
        if (Test-Path "$testsDir\err.tmp") {
            $err = Get-Content "$testsDir\err.tmp" -Raw
            if ($err) { Write-Host "    Stderr:   $err" -ForegroundColor Red }
        }
        $failed++
    }
}

if (Test-Path "$testsDir\out.tmp") { Remove-Item "$testsDir\out.tmp" -Force }
if (Test-Path "$testsDir\err.tmp") { Remove-Item "$testsDir\err.tmp" -Force }

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
$summaryColor = "Green"
if ($failed -gt 0) { $summaryColor = "Red" }
Write-Host "  Test Sonucu: $passed PASSED, $failed FAILED (Toplam: $($passed + $failed))" -ForegroundColor $summaryColor
Write-Host "==========================================================" -ForegroundColor Cyan

if ($failed -gt 0) { exit 1 } else { exit 0 }
