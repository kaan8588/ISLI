import time
import math
import sys

def run_bench(id):
    if id == 1: # 01_arithmetic (10M iters)
        t0 = time.perf_counter()
        total = 0.0
        for i in range(10000000):
            total += (i * 3 - (i // 2) + (i % 7))
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 2: # 02_fibonacci (fib(32))
        def fib(n):
            if n < 2:
                return n
            return fib(n - 1) + fib(n - 2)
        t0 = time.perf_counter()
        res = fib(32)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 3: # 03_ackermann (A(3, 6))
        sys.setrecursionlimit(50000)
        def ack(m, n):
            if m == 0:
                return n + 1
            elif n == 0:
                return ack(m - 1, 1)
            else:
                return ack(m - 1, ack(m, n - 1))
        t0 = time.perf_counter()
        res = ack(3, 6)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 4: # 04_mandelbrot (300x300 grid, 50 max iters)
        width, height, max_iter = 300, 300, 50
        t0 = time.perf_counter()
        count = 0
        for y in range(height):
            ci = (y * 2.0 / height) - 1.0
            for x in range(width):
                cr = (x * 3.0 / width) - 2.0
                zr, zi = 0.0, 0.0
                iter = 0
                while zr * zr + zi * zi <= 4.0 and iter < max_iter:
                    temp = zr * zr - zi * zi + cr
                    zi = 2.0 * zr * zi + ci
                    zr = temp
                    iter += 1
                if iter == max_iter:
                    count += 1
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 5: # 05_nbody (50,000 steps)
        x = [0.0, 1.0, -1.0]
        y = [0.0, 0.5, -0.5]
        vx = [0.0, 0.01, -0.01]
        vy = [0.0, -0.01, 0.01]
        mass = [100.0, 1.0, 1.0]
        dt = 0.01
        steps = 50000
        t0 = time.perf_counter()
        for s in range(steps):
            for i in range(3):
                fx, fy = 0.0, 0.0
                for j in range(3):
                    if i != j:
                        dx = x[j] - x[i]
                        dy = y[j] - y[i]
                        dist_sq = dx * dx + dy * dy + 0.001
                        dist = math.sqrt(dist_sq)
                        f = mass[j] / (dist_sq * dist)
                        fx += f * dx
                        fy += f * dy
                vx[i] += fx * dt
                vy[i] += fy * dt
            for i in range(3):
                x[i] += vx[i] * dt
                y[i] += vy[i] * dt
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 6: # 06_array_fill (1,000,000 doubles)
        N = 1000000
        a = [0.0] * N
        t0 = time.perf_counter()
        for i in range(N):
            a[i] = 42.5
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 7: # 07_array_copy (1,000,000 doubles)
        N = 1000000
        a = [123.456] * N
        b = [0.0] * N
        t0 = time.perf_counter()
        for i in range(N):
            b[i] = a[i]
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 8: # 08_matrix_mult (150x150)
        N = 150
        a = [1.5] * (N * N)
        b = [2.5] * (N * N)
        c = [0.0] * (N * N)
        t0 = time.perf_counter()
        for i in range(N):
            i_offset = i * N
            for j in range(N):
                sum_val = 0.0
                for k in range(N):
                    sum_val += a[i_offset + k] * b[k * N + j]
                c[i_offset + j] = sum_val
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 9: # 09_quicksort (30,000 ints)
        N = 30000
        arr = [0] * N
        seed = 12345
        for i in range(N):
            seed = (seed * 1103515245 + 12345) % 2147483648
            arr[i] = seed % 100000
        def partition(low, high):
            pivot = arr[high]
            i = low - 1
            for j in range(low, high):
                if arr[j] <= pivot:
                    i += 1
                    arr[i], arr[j] = arr[j], arr[i]
            arr[i + 1], arr[high] = arr[high], arr[i + 1]
            return i + 1
        def qs(low, high):
            if low < high:
                pi = partition(low, high)
                qs(low, pi - 1)
                qs(pi + 1, high)
        t0 = time.perf_counter()
        qs(0, N - 1)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 10: # 10_radix_sort (30,000 ints)
        N = 30000
        arr = [0] * N
        seed = 12345
        for i in range(N):
            seed = (seed * 1103515245 + 12345) % 2147483648
            arr[i] = seed % 100000
        t0 = time.perf_counter()
        max_val = max(arr)
        exp = 1
        while max_val // exp > 0:
            output = [0] * N
            count = [0] * 10
            for i in range(N):
                digit = (arr[i] // exp) % 10
                count[digit] += 1
            for i in range(1, 10):
                count[i] += count[i - 1]
            for i in range(N - 1, -1, -1):
                digit = (arr[i] // exp) % 10
                output[count[digit] - 1] = arr[i]
                count[digit] -= 1
            for i in range(N):
                arr[i] = output[i]
            exp *= 10
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 11: # 11_binary_trees (Depth 12)
        class TreeNode:
            __slots__ = ('val', 'left', 'right')
            def __init__(self, val, left=None, right=None):
                self.val = val
                self.left = left
                self.right = right
        def make_tree(depth):
            if depth <= 0:
                return TreeNode(1)
            return TreeNode(depth, make_tree(depth - 1), make_tree(depth - 1))
        def check_tree(node):
            if node is None:
                return 0
            return node.val + check_tree(node.left) + check_tree(node.right)
        t0 = time.perf_counter()
        root = make_tree(12)
        checksum = check_tree(root)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 12: # 12_hash_map (15,000 ops)
        N = 15000
        d = {}
        t0 = time.perf_counter()
        for i in range(N):
            d[i] = i * 3
        for i in range(N):
            v = d[i]
        for i in range(0, N, 2):
            del d[i]
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 13: # 13_string_slicing (50,000 ops)
        long_str = "Antigravity_Advanced_Agentic_Compiler_Optimization_And_Virtual_Machine_Architecture_Benchmark_String_Slice_1234567890_ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        N = 50000
        t0 = time.perf_counter()
        for _ in range(N):
            s1 = long_str[:30]
            s2 = long_str[-30:]
            s3 = long_str[10:30]
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 14: # 14_closures (10,000 closures)
        N = 10000
        def make_adder(x):
            counter = [x]
            def add_step(step):
                counter[0] += step
                return counter[0]
            return add_step
        t0 = time.perf_counter()
        fn_list = [make_adder(i) for i in range(N)]
        total = 0
        for fn in fn_list:
            total += fn(1) + fn(2)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 15: # 15_dot_product (1,000,000 doubles)
        N = 1000000
        a = [1.5] * N
        b = [2.5] * N
        t0 = time.perf_counter()
        total = 0.0
        for i in range(N):
            total += a[i] * b[i]
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 16: # 16_stress_simt_allocation (50,000 arrays)
        t0 = time.perf_counter()
        for i in range(50000):
            temp = [0.0] * 20
            for j in range(20):
                temp[j] = (i + j) * 1.5
            temp.append(999.0)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 17: # 17_deep_recursion_nan (depth 1000 x 500)
        sys.setrecursionlimit(50000)
        def deep_rec(depth, acc):
            if depth <= 0:
                return acc
            val = acc * 1.0001 + math.sqrt(depth * 1.0)
            return deep_rec(depth - 1, val)
        t0 = time.perf_counter()
        total = 0.0
        for i in range(500):
            total += deep_rec(1000, 1.0)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 18: # 18_massive_bouncing (2,000,000 items)
        N = 2000000
        data = [1.0] * N
        t0 = time.perf_counter()
        for i in range(N):
            data[i] = data[i] * 1.05 + 0.5
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 19:  # 19_dispatch_reduce (500k ones)
        N = 500000
        t0 = time.perf_counter()
        s = 0.0
        for i in range(N):
            s += 1.0
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 20:  # 20_typed_buffer_fill (1M)
        N = 1000000
        buf = [0.0] * N
        t0 = time.perf_counter()
        for i in range(N):
            buf[i] = i * 0.5
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 21:  # 21_uneven_steal (30k, 1..64 sqrts)
        N = 30000
        buf = [0.0] * N
        t0 = time.perf_counter()
        for i in range(N):
            inner = (i % 64) + 1
            acc = 0.0
            for k in range(inner):
                acc += math.sqrt(k + 1.0)
            buf[i] = acc
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 22:  # 22_tiled_stencil (256x256)
        W, H = 256, 256
        N = W * H
        src = [1.0] * N
        dst = [0.0] * N
        t0 = time.perf_counter()
        for y in range(H):
            for x in range(W):
                idx = x + y * W
                acc = src[idx]
                if x > 0:
                    acc += src[idx - 1]
                if x < W - 1:
                    acc += src[idx + 1]
                if y > 0:
                    acc += src[idx - W]
                if y < H - 1:
                    acc += src[idx + W]
                dst[idx] = acc * 0.2
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 23:  # 23_nested_atomic (8000 x 8)
        b = [0.0]
        t0 = time.perf_counter()
        for i in range(8000):
            for j in range(8):
                b[0] += 1.0
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    elif id == 24:  # 24_jit_many_locals (500k, 16 locals)
        def mix(v):
            a1 = v + 1; a2 = v + 2; a3 = v + 3; a4 = v + 4
            a5 = v + 5; a6 = v + 6; a7 = v + 7; a8 = v + 8
            a9 = v + 9; a10 = v + 10; a11 = v + 11; a12 = v + 12
            a13 = v + 13; a14 = v + 14; a15 = v + 15; a16 = v + 16
            return a1+a2+a3+a4+a5+a6+a7+a8+a9+a10+a11+a12+a13+a14+a15+a16
        N = 500000
        t0 = time.perf_counter()
        s = 0
        for i in range(N):
            s += mix(i)
        t1 = time.perf_counter()
        return (t1 - t0) * 1000.0
    return 0.0

if __name__ == "__main__":
    if len(sys.argv) > 1:
        bid = int(sys.argv[1])
        print(run_bench(bid))
    else:
        for i in range(1, 25):
            print(f"Bench {i}: {run_bench(i):.2f} ms")
