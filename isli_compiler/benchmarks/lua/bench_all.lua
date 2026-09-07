local function run_bench(id)
    if id == 1 then -- 01_arithmetic (10M iters)
        local t0 = os.clock()
        local sum = 0.0
        for i = 0, 9999999 do
            sum = sum + (i * 3 - math.floor(i / 2) + (i % 7))
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 2 then -- 02_fibonacci (fib(32))
        local function fib(n)
            if n < 2 then return n end
            return fib(n - 1) + fib(n - 2)
        end
        local t0 = os.clock()
        local res = fib(32)
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 3 then -- 03_ackermann (A(3, 6))
        local function ack(m, n)
            if m == 0 then return n + 1
            elseif n == 0 then return ack(m - 1, 1)
            else return ack(m - 1, ack(m, n - 1))
            end
        end
        local t0 = os.clock()
        local res = ack(3, 6)
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 4 then -- 04_mandelbrot (300x300 grid, 50 max iters)
        local width, height, maxIter = 300, 300, 50
        local t0 = os.clock()
        local count = 0
        for y = 0, height - 1 do
            local ci = (y * 2.0 / height) - 1.0
            for x = 0, width - 1 do
                local cr = (x * 3.0 / width) - 2.0
                local zr, zi = 0.0, 0.0
                local iter = 0
                while zr * zr + zi * zi <= 4.0 and iter < maxIter do
                    local temp = zr * zr - zi * zi + cr
                    zi = 2.0 * zr * zi + ci
                    zr = temp
                    iter = iter + 1
                end
                if iter == maxIter then count = count + 1 end
            end
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 5 then -- 05_nbody (50,000 steps)
        local x = {0.0, 1.0, -1.0}
        local y = {0.0, 0.5, -0.5}
        local vx = {0.0, 0.01, -0.01}
        local vy = {0.0, -0.01, 0.01}
        local mass = {100.0, 1.0, 1.0}
        local dt = 0.01
        local steps = 50000
        local t0 = os.clock()
        for s = 1, steps do
            for i = 1, 3 do
                local fx, fy = 0.0, 0.0
                for j = 1, 3 do
                    if i ~= j then
                        local dx = x[j] - x[i]
                        local dy = y[j] - y[i]
                        local distSq = dx * dx + dy * dy + 0.001
                        local dist = math.sqrt(distSq)
                        local f = mass[j] / (distSq * dist)
                        fx = fx + f * dx
                        fy = fy + f * dy
                    end
                end
                vx[i] = vx[i] + fx * dt
                vy[i] = vy[i] + fy * dt
            end
            for i = 1, 3 do
                x[i] = x[i] + vx[i] * dt
                y[i] = y[i] + vy[i] * dt
            end
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 6 then -- 06_array_fill (1,000,000 doubles)
        local N = 1000000
        local a = {}
        local t0 = os.clock()
        for i = 1, N do
            a[i] = 42.5
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 7 then -- 07_array_copy (1,000,000 doubles)
        local N = 1000000
        local a, b = {}, {}
        for i = 1, N do a[i] = 123.456 end
        local t0 = os.clock()
        for i = 1, N do
            b[i] = a[i]
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 8 then -- 08_matrix_mult (150x150)
        local N = 150
        local a, b, c = {}, {}, {}
        for i = 1, N * N do a[i] = 1.5; b[i] = 2.5; c[i] = 0.0 end
        local t0 = os.clock()
        for i = 0, N - 1 do
            local iOffset = i * N
            for j = 1, N do
                local sum = 0.0
                for k = 0, N - 1 do
                    sum = sum + a[iOffset + k + 1] * b[k * N + j]
                end
                c[iOffset + j] = sum
            end
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 9 then -- 09_quicksort (30,000 ints)
        local N = 30000
        local arr = {}
        local seed = 12345
        for i = 1, N do
            seed = (seed * 1103515245 + 12345) % 2147483648
            arr[i] = seed % 100000
        end
        local function partition(low, high)
            local pivot = arr[high]
            local i = low - 1
            for j = low, high - 1 do
                if arr[j] <= pivot then
                    i = i + 1
                    arr[i], arr[j] = arr[j], arr[i]
                end
            end
            arr[i + 1], arr[high] = arr[high], arr[i + 1]
            return i + 1
        end
        local function qs(low, high)
            if low < high then
                local pi = partition(low, high)
                qs(low, pi - 1)
                qs(pi + 1, high)
            end
        end
        local t0 = os.clock()
        qs(1, N)
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 10 then -- 10_radix_sort (30,000 ints)
        local N = 30000
        local arr = {}
        local seed = 12345
        for i = 1, N do
            seed = (seed * 1103515245 + 12345) % 2147483648
            arr[i] = seed % 100000
        end
        local t0 = os.clock()
        local maxVal = arr[1]
        for i = 2, N do if arr[i] > maxVal then maxVal = arr[i] end end
        local exp = 1
        while math.floor(maxVal / exp) > 0 do
            local output = {}
            local count = {0,0,0,0,0,0,0,0,0,0}
            for i = 1, N do
                local digit = math.floor(arr[i] / exp) % 10 + 1
                count[digit] = count[digit] + 1
            end
            for i = 2, 10 do count[i] = count[i] + count[i - 1] end
            for i = N, 1, -1 do
                local digit = math.floor(arr[i] / exp) % 10 + 1
                output[count[digit]] = arr[i]
                count[digit] = count[digit] - 1
            end
            for i = 1, N do arr[i] = output[i] end
            exp = exp * 10
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 11 then -- 11_binary_trees (Depth 12)
        local function makeTree(depth)
            if depth <= 0 then return { val = 1 } end
            return { val = depth, left = makeTree(depth - 1), right = makeTree(depth - 1) }
        end
        local function checkTree(node)
            if not node then return 0 end
            return node.val + checkTree(node.left) + checkTree(node.right)
        end
        local t0 = os.clock()
        local root = makeTree(12)
        local checksum = checkTree(root)
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 12 then -- 12_hash_map (15,000 ops)
        local N = 15000
        local map = {}
        local t0 = os.clock()
        for i = 0, N - 1 do map[i] = i * 3 end
        for i = 0, N - 1 do local v = map[i] end
        for i = 0, N - 1, 2 do map[i] = nil end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 13 then -- 13_string_slicing (50,000 ops)
        local longStr = "Antigravity_Advanced_Agentic_Compiler_Optimization_And_Virtual_Machine_Architecture_Benchmark_String_Slice_1234567890_ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        local N = 50000
        local t0 = os.clock()
        for i = 1, N do
            local s1 = string.sub(longStr, 1, 30)
            local s2 = string.sub(longStr, -30)
            local s3 = string.sub(longStr, 11, 30)
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 14 then -- 14_closures (10,000 closures)
        local N = 10000
        local list = {}
        local function makeAdder(x)
            local counter = x
            return function(step)
                counter = counter + step
                return counter
            end
        end
        local t0 = os.clock()
        for i = 1, N do list[i] = makeAdder(i) end
        local total = 0
        for i = 1, N do
            total = total + list[i](1) + list[i](2)
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 15 then -- 15_dot_product (1,000,000 doubles)
        local N = 1000000
        local a, b = {}, {}
        for i = 1, N do a[i] = 1.5; b[i] = 2.5 end
        local t0 = os.clock()
        local total = 0.0
        for i = 1, N do
            total = total + a[i] * b[i]
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 16 then -- 16_stress_simt_allocation (50,000 arrays)
        local t0 = os.clock()
        for i = 1, 50000 do
            local temp = {}
            for j = 1, 20 do
                temp[j] = (i + j) * 1.5
            end
            table.insert(temp, 999.0)
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 17 then -- 17_deep_recursion_nan (depth 1000 x 500)
        local function deepRec(depth, acc)
            if depth <= 0 then return acc end
            local val = acc * 1.0001 + math.sqrt(depth * 1.0)
            return deepRec(depth - 1, val)
        end
        local t0 = os.clock()
        local total = 0.0
        for i = 1, 500 do
            total = total + deepRec(1000, 1.0)
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 18 then -- 18_massive_bouncing (2,000,000 items)
        local N = 2000000
        local data = {}
        for i = 1, N do data[i] = 1.0 end
        local t0 = os.clock()
        for i = 1, N do
            data[i] = data[i] * 1.05 + 0.5
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 19 then -- 19_dispatch_reduce (500k ones)
        local N = 500000
        local t0 = os.clock()
        local s = 0.0
        for i = 1, N do
            s = s + 1.0
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 20 then -- 20_typed_buffer_fill (1M)
        local N = 1000000
        local buf = {}
        local t0 = os.clock()
        for i = 0, N - 1 do
            buf[i + 1] = i * 0.5
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 21 then -- 21_uneven_steal (30k, 1..64 sqrts)
        local N = 30000
        local buf = {}
        local t0 = os.clock()
        for i = 0, N - 1 do
            local inner = (i % 64) + 1
            local acc = 0.0
            for k = 0, inner - 1 do
                acc = acc + math.sqrt(k + 1.0)
            end
            buf[i + 1] = acc
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 22 then -- 22_tiled_stencil (256x256)
        local W, H = 256, 256
        local N = W * H
        local src, dst = {}, {}
        for i = 1, N do src[i] = 1.0; dst[i] = 0.0 end
        local t0 = os.clock()
        for y = 0, H - 1 do
            for x = 0, W - 1 do
                local idx = x + y * W + 1
                local acc = src[idx]
                if x > 0 then acc = acc + src[idx - 1] end
                if x < W - 1 then acc = acc + src[idx + 1] end
                if y > 0 then acc = acc + src[idx - W] end
                if y < H - 1 then acc = acc + src[idx + W] end
                dst[idx] = acc * 0.2
            end
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 23 then -- 23_nested_atomic (8000 x 8)
        local b = 0.0
        local t0 = os.clock()
        for i = 1, 8000 do
            for j = 1, 8 do
                b = b + 1.0
            end
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    elseif id == 24 then -- 24_jit_many_locals (500k, 16 locals)
        local function mix(v)
            local a1 = v + 1; local a2 = v + 2; local a3 = v + 3; local a4 = v + 4
            local a5 = v + 5; local a6 = v + 6; local a7 = v + 7; local a8 = v + 8
            local a9 = v + 9; local a10 = v + 10; local a11 = v + 11; local a12 = v + 12
            local a13 = v + 13; local a14 = v + 14; local a15 = v + 15; local a16 = v + 16
            return a1+a2+a3+a4+a5+a6+a7+a8+a9+a10+a11+a12+a13+a14+a15+a16
        end
        local N = 500000
        local t0 = os.clock()
        local s = 0
        for i = 0, N - 1 do
            s = s + mix(i)
        end
        local t1 = os.clock()
        return (t1 - t0) * 1000.0
    end
    return 0.0
end

local id = tonumber(arg[1])
if id then
    print(run_bench(id))
else
    for i = 1, 24 do
        print(string.format("Bench %d: %.2f ms", i, run_bench(i)))
    end
end
