#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <functional>
#include <atomic>
#include <immintrin.h>

using namespace std::chrono;

double run_bench(int id) {
    if (id == 1) { // 01_arithmetic (10M iters)
        auto t0 = high_resolution_clock::now();
        volatile double sum = 0.0;
        for (int i = 0; i < 10000000; i++) {
            sum += (i * 3 - (i / 2) + (i % 7));
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 2) { // 02_fibonacci (fib(32))
        std::function<int(int)> fib = [&](int n) -> int {
            if (n < 2) return n;
            return fib(n - 1) + fib(n - 2);
        };
        auto t0 = high_resolution_clock::now();
        volatile int res = fib(32);
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 3) { // 03_ackermann (A(3, 6))
        std::function<int(int, int)> ack = [&](int m, int n) -> int {
            if (m == 0) return n + 1;
            if (n == 0) return ack(m - 1, 1);
            return ack(m - 1, ack(m, n - 1));
        };
        auto t0 = high_resolution_clock::now();
        volatile int res = ack(3, 6);
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 4) { // 04_mandelbrot (300x300 grid, 50 max iters)
        int width = 300, height = 300, maxIter = 50;
        auto t0 = high_resolution_clock::now();
        volatile int count = 0;
        for (int y = 0; y < height; y++) {
            double ci = (y * 2.0 / height) - 1.0;
            for (int x = 0; x < width; x++) {
                double cr = (x * 3.0 / width) - 2.0;
                double zr = 0.0, zi = 0.0;
                int iter = 0;
                while (zr * zr + zi * zi <= 4.0 && iter < maxIter) {
                    double temp = zr * zr - zi * zi + cr;
                    zi = 2.0 * zr * zi + ci;
                    zr = temp;
                    iter++;
                }
                if (iter == maxIter) count++;
            }
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 5) { // 05_nbody (50,000 steps)
        std::vector<double> x = {0.0, 1.0, -1.0};
        std::vector<double> y = {0.0, 0.5, -0.5};
        std::vector<double> vx = {0.0, 0.01, -0.01};
        std::vector<double> vy = {0.0, -0.01, 0.01};
        std::vector<double> mass = {100.0, 1.0, 1.0};
        double dt = 0.01;
        int steps = 50000;
        auto t0 = high_resolution_clock::now();
        for (int s = 0; s < steps; s++) {
            for (int i = 0; i < 3; i++) {
                double fx = 0.0, fy = 0.0;
                for (int j = 0; j < 3; j++) {
                    if (i != j) {
                        double dx = x[j] - x[i];
                        double dy = y[j] - y[i];
                        double distSq = dx * dx + dy * dy + 0.001;
                        double dist = std::sqrt(distSq);
                        double f = mass[j] / (distSq * dist);
                        fx += f * dx;
                        fy += f * dy;
                    }
                }
                vx[i] += fx * dt;
                vy[i] += fy * dt;
            }
            for (int i = 0; i < 3; i++) {
                x[i] += vx[i] * dt;
                y[i] += vy[i] * dt;
            }
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 6) { // 06_array_fill (1,000,000 doubles)
        int N = 1000000;
        std::vector<double> a(N, 0.0);
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            a[i] = 42.5;
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 7) { // 07_array_copy (1,000,000 doubles)
        int N = 1000000;
        std::vector<double> a(N, 123.456);
        std::vector<double> b(N, 0.0);
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            b[i] = a[i];
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 8) { // 08_matrix_mult (150x150)
        int N = 150;
        std::vector<double> a(N * N, 1.5);
        std::vector<double> b(N * N, 2.5);
        std::vector<double> c(N * N, 0.0);
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            int iOffset = i * N;
            for (int j = 0; j < N; j++) {
                double sum = 0.0;
                for (int k = 0; k < N; k++) {
                    sum += a[iOffset + k] * b[k * N + j];
                }
                c[iOffset + j] = sum;
            }
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 9) { // 09_quicksort (30,000 ints)
        int N = 30000;
        std::vector<int> arr(N, 0);
        unsigned int seed = 12345;
        for (int i = 0; i < N; i++) {
            seed = (seed * 1103515245 + 12345) % 2147483648U;
            arr[i] = seed % 100000;
        }
        std::function<int(int, int)> partition = [&](int low, int high) -> int {
            int pivot = arr[high];
            int i = low - 1;
            for (int j = low; j < high; j++) {
                if (arr[j] <= pivot) {
                    i++;
                    std::swap(arr[i], arr[j]);
                }
            }
            std::swap(arr[i + 1], arr[high]);
            return i + 1;
        };
        std::function<void(int, int)> qs = [&](int low, int high) {
            if (low < high) {
                int pi = partition(low, high);
                qs(low, pi - 1);
                qs(pi + 1, high);
            }
        };
        auto t0 = high_resolution_clock::now();
        qs(0, N - 1);
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 10) { // 10_radix_sort (30,000 ints)
        int N = 30000;
        std::vector<int> arr(N, 0);
        unsigned int seed = 12345;
        for (int i = 0; i < N; i++) {
            seed = (seed * 1103515245 + 12345) % 2147483648U;
            arr[i] = seed % 100000;
        }
        auto countSort = [&](int exp) {
            std::vector<int> output(N, 0);
            int count[10] = {0};
            for (int i = 0; i < N; i++) {
                int digit = (arr[i] / exp) % 10;
                count[digit]++;
            }
            for (int i = 1; i < 10; i++) count[i] += count[i - 1];
            for (int i = N - 1; i >= 0; i--) {
                int digit = (arr[i] / exp) % 10;
                output[count[digit] - 1] = arr[i];
                count[digit]--;
            }
            for (int i = 0; i < N; i++) arr[i] = output[i];
        };
        auto t0 = high_resolution_clock::now();
        int maxVal = arr[0];
        for (int i = 1; i < N; i++) if (arr[i] > maxVal) maxVal = arr[i];
        for (int exp = 1; maxVal / exp > 0; exp *= 10) {
            countSort(exp);
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 11) { // 11_binary_trees (Depth 12)
        struct TreeNode {
            int val;
            std::unique_ptr<TreeNode> left;
            std::unique_ptr<TreeNode> right;
            TreeNode(int v, std::unique_ptr<TreeNode> l, std::unique_ptr<TreeNode> r)
                : val(v), left(std::move(l)), right(std::move(r)) {}
        };
        std::function<std::unique_ptr<TreeNode>(int)> makeTree = [&](int depth) -> std::unique_ptr<TreeNode> {
            if (depth <= 0) return std::make_unique<TreeNode>(1, nullptr, nullptr);
            return std::make_unique<TreeNode>(depth, makeTree(depth - 1), makeTree(depth - 1));
        };
        std::function<int(const TreeNode*)> checkTree = [&](const TreeNode* node) -> int {
            if (!node) return 0;
            return node->val + checkTree(node->left.get()) + checkTree(node->right.get());
        };
        auto t0 = high_resolution_clock::now();
        auto root = makeTree(12);
        volatile int checksum = checkTree(root.get());
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 12) { // 12_hash_map (15,000 ops)
        int N = 15000;
        std::map<int, int> map;
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            map[i] = i * 3;
        }
        for (int i = 0; i < N; i++) {
            volatile int v = map[i];
        }
        for (int i = 0; i < N; i += 2) {
            map.erase(i);
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 13) { // 13_string_slicing (50,000 ops)
        std::string longStr = "Antigravity_Advanced_Agentic_Compiler_Optimization_And_Virtual_Machine_Architecture_Benchmark_String_Slice_1234567890_ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        int N = 50000;
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            std::string_view sv(longStr);
            std::string_view s1 = sv.substr(0, 30);
            std::string_view s2 = sv.substr(sv.length() - 30);
            std::string_view s3 = sv.substr(10, 20);
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 14) { // 14_closures (10,000 closures)
        int N = 10000;
        std::vector<std::function<int(int)>> list(N);
        auto makeAdder = [](int x) {
            auto counter = std::make_shared<int>(x);
            return [counter](int step) -> int {
                *counter += step;
                return *counter;
            };
        };
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            list[i] = makeAdder(i);
        }
        volatile int total = 0;
        for (int i = 0; i < N; i++) {
            total += list[i](1) + list[i](2);
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 15) { // 15_dot_product (1,000,000 doubles AVX2)
        int N = 1000000;
        std::vector<double> a(N, 1.5);
        std::vector<double> b(N, 2.5);
        auto t0 = high_resolution_clock::now();
        __m256d acc0 = _mm256_setzero_pd();
        __m256d acc1 = _mm256_setzero_pd();
        __m256d acc2 = _mm256_setzero_pd();
        __m256d acc3 = _mm256_setzero_pd();
        int i = 0;
        for (; i + 16 <= N; i += 16) {
            acc0 = _mm256_fmadd_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i]), acc0);
            acc1 = _mm256_fmadd_pd(_mm256_loadu_pd(&a[i+4]), _mm256_loadu_pd(&b[i+4]), acc1);
            acc2 = _mm256_fmadd_pd(_mm256_loadu_pd(&a[i+8]), _mm256_loadu_pd(&b[i+8]), acc2);
            acc3 = _mm256_fmadd_pd(_mm256_loadu_pd(&a[i+12]), _mm256_loadu_pd(&b[i+12]), acc3);
        }
        acc0 = _mm256_add_pd(acc0, acc1);
        acc2 = _mm256_add_pd(acc2, acc3);
        acc0 = _mm256_add_pd(acc0, acc2);
        for (; i + 4 <= N; i += 4) {
            acc0 = _mm256_fmadd_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i]), acc0);
        }
        double temp[4];
        _mm256_storeu_pd(temp, acc0);
        volatile double total = temp[0] + temp[1] + temp[2] + temp[3];
        for (; i < N; i++) total += a[i] * b[i];
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 16) { // 16_stress_simt_allocation (50,000 arrays parallel OpenMP)
        auto t0 = high_resolution_clock::now();
        #pragma omp parallel for
        for (int i = 0; i < 50000; i++) {
            std::vector<double> temp(20, 0.0);
            for (int j = 0; j < 20; j++) {
                temp[j] = (i + j) * 1.5;
            }
            temp.push_back(999.0);
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 17) { // 17_deep_recursion_nan (depth 1000 x 500)
        auto deep_rec = [](auto& self, int depth, double acc) -> double {
            if (depth <= 0) return acc;
            double val = acc * 1.0001 + std::sqrt(depth * 1.0);
            return self(self, depth - 1, val);
        };
        auto t0 = high_resolution_clock::now();
        volatile double total = 0.0;
        for (int i = 0; i < 500; i++) {
            total += deep_rec(deep_rec, 1000, 1.0);
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 18) { // 18_massive_bouncing (2,000,000 items)
        int N = 2000000;
        std::vector<double> data(N, 1.0);
        auto t0 = high_resolution_clock::now();
        #pragma omp parallel for
        for (int i = 0; i < N; i++) {
            data[i] = data[i] * 1.05 + 0.5;
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 19) { // 19_dispatch_reduce (500k ones)
        int N = 500000;
        auto t0 = high_resolution_clock::now();
        double s = 0.0;
        #pragma omp parallel for reduction(+:s)
        for (int i = 0; i < N; i++) {
            s += 1.0;
        }
        volatile double keep = s;
        (void)keep;
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 20) { // 20_typed_buffer_fill (1M)
        int N = 1000000;
        std::vector<double> buf(N, 0.0);
        auto t0 = high_resolution_clock::now();
        #pragma omp parallel for
        for (int i = 0; i < N; i++) {
            buf[i] = i * 0.5;
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 21) { // 21_uneven_steal (30k, 1..64 sqrts)
        int N = 30000;
        std::vector<double> buf(N, 0.0);
        auto t0 = high_resolution_clock::now();
        #pragma omp parallel for schedule(dynamic, 256)
        for (int i = 0; i < N; i++) {
            int inner = (i % 64) + 1;
            double acc = 0.0;
            for (int k = 0; k < inner; k++) {
                acc += std::sqrt(k + 1.0);
            }
            buf[i] = acc;
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 22) { // 22_tiled_stencil (256x256, 16x16 tiles)
        int W = 256, H = 256;
        int N = W * H;
        std::vector<double> src(N, 1.0);
        std::vector<double> dst(N, 0.0);
        auto t0 = high_resolution_clock::now();
        #pragma omp parallel for collapse(2)
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int idx = x + y * W;
                double acc = src[idx];
                if (x > 0) acc += src[idx - 1];
                if (x < W - 1) acc += src[idx + 1];
                if (y > 0) acc += src[idx - W];
                if (y < H - 1) acc += src[idx + W];
                dst[idx] = acc * 0.2;
            }
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 23) { // 23_nested_atomic (8000 x 8)
        std::atomic<double> b{0.0};
        auto t0 = high_resolution_clock::now();
        #pragma omp parallel for
        for (int i = 0; i < 8000; i++) {
            for (int j = 0; j < 8; j++) {
                double oldv = b.load(std::memory_order_relaxed);
                for (;;) {
                    double nv = oldv + 1.0;
                    if (b.compare_exchange_weak(oldv, nv, std::memory_order_acq_rel)) break;
                }
            }
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    else if (id == 24) { // 24_jit_many_locals (500k calls, 16 locals)
        auto mix = [](int v) -> int {
            int a1 = v + 1, a2 = v + 2, a3 = v + 3, a4 = v + 4;
            int a5 = v + 5, a6 = v + 6, a7 = v + 7, a8 = v + 8;
            int a9 = v + 9, a10 = v + 10, a11 = v + 11, a12 = v + 12;
            int a13 = v + 13, a14 = v + 14, a15 = v + 15, a16 = v + 16;
            return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15 + a16;
        };
        int N = 500000;
        auto t0 = high_resolution_clock::now();
        volatile int s = 0;
        for (int i = 0; i < N; i++) {
            s += mix(i);
        }
        auto t1 = high_resolution_clock::now();
        return duration<double, std::milli>(t1 - t0).count();
    }
    return 0.0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        int id = std::stoi(argv[1]);
        std::cout << run_bench(id) << std::endl;
    } else {
        for (int i = 1; i <= 24; i++) {
            std::cout << "Bench " << i << ": " << run_bench(i) << " ms" << std::endl;
        }
    }
    return 0;
}
