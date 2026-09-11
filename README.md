# Isli

CPU-oriented systems language with C-style static typing, an x86-64 JIT, multi-core SIMT `dispatch`, typed buffers, and AVX2 SIMD natives.

This repository is the `compiler/` tree: language core, VS Code extension, and portable install template. Full syntax reference: [`isli_compiler/SYNTAX.md`](isli_compiler/SYNTAX.md).

---

## Language Overview

Isli is designed for **data-parallel and numerical workloads on ordinary CPUs**, without a GPU runtime. The surface language looks like a small C dialect: every binding carries an explicit type (`int`, `float`/`double`, `string`, `bool`), control flow uses `if` / `while` / `for`, and functions support recursion and first-class closures. Under that familiar syntax sits a classical compiler pipeline that aims to keep hot paths close to machine code while still offering a dynamic, garbage-collected object model for arrays, maps, and heap structures.

### Working algorithm (how a program runs)

Isli does **not** ship an ahead-of-time object file for user scripts. Each run follows a fixed pipeline from text to machine work:

| Step | Algorithm | Detail |
| --- | --- | --- |
| 1. Lex | Scanner | Tokens: keywords, numbers, strings, operators |
| 2. Parse + emit | Pratt (precedence climbing) | Expressions compile directly to stack opcodes; statements emit jumps / locals / calls |
| 3. Chunk | Bytecode blob | Code + constant table + line info; opcode order matches the VM `dispatchTable[]` |
| 4. Interpret | Stack VM | Push/pop `Value`; function locals **are** the frame stack; closures capture upvalues |
| 5. JIT (hot path) | x86-64 Win64 | On first / eligible call: compile chunk to native; RCX=`VM*`, RDX=slots, R8=closure; R12–R15 pinned; unsupported ops set `jitDeclined` and fall back |
| 6. Natives | C++ helpers | `print`, SIMD, buffer I/O, containers — called from bytecode or JIT stubs |
| 7. SIMT (if `dispatch`) | Heartbeat + steal | Grid → work blocks; main + workers claim ranges (packed CAS); idle workers steal half a victim’s range; wait until `failed` or `remaining==0`; tree `reduce` merges lanes |

```text
.source.isli
    │  scan / Pratt compile
    ▼
 bytecode chunk ──► stack VM loop
                        │
            ┌───────────┼───────────┐
            ▼           ▼           ▼
         natives      JIT code    dispatch job
         (AVX2…)   (x86-64)    (heartbeat/steal)
```

**Value algorithm:** every slot is one `uint64_t` (**NaN-boxing**). If the bit pattern is a normal double, it is a number; otherwise high bits encode tags (nil, bool, obj pointer, …). Quiet NaNs that would collide with tags are canonicalized when boxed. Objects use refcount / clone-on-write style rules so parallel kernels can share or isolate heap graphs safely.

**Dispatch algorithm (large N):** build a static `ParallelJob`; partition the linearized index space; workers repeatedly `claimOwnBlock` or `stealHalf`; each index runs the kernel closure (interpreter or JIT); `atomic_add` uses hardware atomics on typed buffer cells; on kernel error, mark job failed (no silent `nil`, no whole-VM stack wipe from worker helpers). Nested `dispatch` on a worker thread runs **serially** so two barriers cannot nest-deadlock.

### Execution model (components)

| Stage | Role |
| --- | --- |
| Pratt parser + bytecode emitter | Front end → compact `OP_*` stream (`chunk.hpp`) |
| Stack bytecode VM | Portable interpreter; locals on the VM stack |
| x86-64 JIT (Win64 ABI) | Native hot functions; pinned regs for VM / slots / closure |
| Runtime natives | AVX2, typed buffers, SIMT scheduler, data structures |

### Types, arrays, and typed buffers

Scalars are declared explicitly; there is no `var` / `auto`. Collections split into two layers:

| Abstraction | Storage | Best use |
| --- | --- | --- |
| `Array` | NaN-boxed `Value` cells | Heterogeneous or object-heavy data |
| `Buffer` (`f32buf` / `f64buf` / `i32buf` / `u8buf`) | Dense C layout | Kernels, SIMD, atomics, tight loops |

```c
Array a = array(4, 0.0);
Buffer b = f64buf(1024, 1.0);
b[0] = a[0] + 2.5;
print len(b);        // 1024
print buf_type(b);   // f64
```

Built-ins such as `push` / `copy` / `sort`, zero-copy string views (`stakel`, `ssub`, `sfind`, …), and classic containers (`stack`, `list`, heaps, red-black / B-trees) sit on the same value model. User `struct` instances are flat heap objects with dynamic fields.

### SIMT parallelism on CPU

Parallelism is expressed with CUDA-like **`dispatch`**, not with manual threads:

```c
dispatch(dimX[, dimY[, dimZ]]) [tile(...)] [reduce(op) into name] {
    |i[, j[, k]]|
    /* kernel body */
}
```

| Construct | Meaning |
| --- | --- |
| `dispatch(N)` / 2D / 3D | Partition index space across cores |
| `reduce(+|*|min|max) into x` | Tree reduction; kernel must `return` a number |
| `tile(tx[, ty])` | 2D cache-friendly block traversal |
| `atomic_add(buf, i, d)` | Race-free add on `f64`/`f32`/`i32` buffers (returns post-update value) |
| `parallel { }` | Scoped block only; does **not** spawn threads |

Small grids run on one thread; large grids use a **heartbeat** scheduler (main thread participates; core 0 is kept for the host). Nested `dispatch` inside a worker runs **serially** to avoid barrier deadlock. Failed kernels surface as runtime errors rather than silent `nil`.

### SIMD and systems surface

`simd_fill`, `simd_copy`, `simd_add` / `mul` / `div`, `simd_fma`, `simd_sum`, and `simd_dot` map onto 256-bit AVX2/FMA paths and accept both arrays and buffers. Math and timing helpers (`sqrt`, `pow`, `clock`, `cpu_info`, …) complete the standard library. CLI modes:

| Command | Effect |
| --- | --- |
| `isli program.isli` | Compile and run (JIT enabled) |
| `isli --info` | CPU brand, cores, AVX2/FMA/AVX-512/AVX10 |
| `isli -rt` / `--realtime` | Affinity lock for low-jitter benchmarks |

### How Isli differs from Python, Lua, and Rust

| Axis | **Isli** | **Python** | **Lua** (5.x / PUC) | **Rust** |
| --- | --- | --- | --- | --- |
| Typing | Explicit surface types (`int`, `float`, …); runtime still NaN-boxes | Dynamic, everything is `PyObject*` | Dynamic tables / tagged values | Static ownership + traits; no VM by default |
| Execution | Bytecode VM **+** own x86-64 JIT + AVX2 natives | CPython bytecode (+ optional JIT elsewhere); GIL often serializes threads | Register/stack VM; JIT only via LuaJIT (separate impl.) | Ahead-of-time LLVM/codegen to native |
| Parallelism | First-class **SIMT `dispatch` / reduce / tile / atomic_add** on CPU | `threading`/`multiprocessing`/`asyncio`; no built-in index-space SIMT | Coroutines; true multi-core needs locks / C modules | `rayon`, threads, `async` — powerful but **library**, not language grid syntax |
| Numerics layout | `Array` (boxed) **and** dense `Buffer` (`f32`/`f64`/`i32`/`u8`) | `list`/`array` module; NumPy is external | Tables; no dense typed buffer in core | Slice/`Vec`/arrays — excellent, but you write the loops |
| SIMD | Built-in `simd_*` over arrays/buffers (AVX2 required) | Via NumPy / C extensions | Not in core language | `std::simd` / intrinsics / crates |
| Memory model | Refcounted heap objects + clone rules for parallel | Refcount + GC cycle detector | GC | Ownership / borrow checker (no GC) |
| Target niche | CUDA-like **CPU** kernels + scripting ergonomics | General scripting, ecosystem breadth | Embedding / game scripting | Safe systems software |

**Versus Python:** Isli keeps C-like control flow and explicit types, but ships a **language-level data-parallel grid** and **in-process AVX2** instead of relying on NumPy + process pools. Python wins on libraries and dynamism; Isli wins on a single binary path from `dispatch` to heartbeat workers without the GIL.

**Versus Lua:** Both are small embeddable VMs with closures. Lua’s core is tables and coroutines; Isli’s core adds **typed buffers**, **SIMT dispatch**, and a **homegrown Win64 JIT** aimed at numerical loops—not a general embed-first scripting story. LuaJIT can be faster on some scalar code; Isli’s distinguishing feature is the **SIMT + buffer + SIMD** stack for multi-core arrays.

**Versus Rust:** Rust produces faster, safer native binaries with zero-cost abstractions. Isli does **not** replace Rust for systems engineering. It offers a **script → bytecode → JIT** loop with CUDA-shaped `dispatch` syntax so experiments (stencil, reduce, bounce workloads) can be written without `unsafe`, rayon plumbing, or a separate build for every tweak. Benchmarks under `isli_compiler/benchmarks/` make the trade-off concrete: Rust/C++ lead on tight scalar kernels; Isli’s design bet is **expressiveness for CPU SIMT** and rapid iteration.

**Hardware gate:** Isli refuses to start without AVX2.

In short, Isli is a **research-oriented yet practical CPU language**: C-shaped ergonomics, a documented compile→VM→JIT→heartbeat algorithm, dense buffers, and an explicit SIMT model that Python/Lua do not provide as language primitives and that Rust provides only through libraries—see `SYNTAX.md`.

---

## Dil Özeti

Isli, **GPU runtime’ına ihtiyaç duymadan** sıradan CPU’larda veri-paralel ve sayısal iş yükleri için tasarlanmış bir sistem dilidir. Sözdizimi küçük bir C lehçesine benzer: her bağlama açık tip yazılır (`int`, `float`/`double`, `string`, `bool`); kontrol akışı `if` / `while` / `for` ile kurulur; fonksiyonlar özyineleme ve birinci sınıf kapanışları destekler. Bu tanıdık yüzeyin altında, sıcak yolları makine koduna yaklaştırırken dizi, harita ve yığın yapıları için toplanabilir bir nesne modeli sunan klasik bir derleyici boru hattı çalışır.

### Çalışma algoritması (program nasıl akar)

Isli kullanıcı betikleri için **önceden üretilmiş .obj/.exe** dağıtmaz. Her çalıştırma metinden makine işine sabit bir boru hattı izler:

| Adım | Algoritma | Ayrıntı |
| --- | --- | --- |
| 1. Lex | Tarayıcı | Anahtar sözcük, sayı, string, operatör |
| 2. Parse + üret | Pratt (öncelik tırmanışı) | İfadeler doğrudan yığıt opcode’una; deyimler atlama / yerel / çağrı üretir |
| 3. Chunk | Bayt kod bloğu | Kod + sabit tablosu + satır bilgisi; opcode sırası VM `dispatchTable[]` ile aynı |
| 4. Yorumla | Yığıt VM | `Value` push/pop; fonksiyon yerelleri **çerçeve yığıtının kendisi**; closure upvalue yakalar |
| 5. JIT (sıcak yol) | x86-64 Win64 | Uygun çağrıda native derleme; RCX=`VM*`, RDX=slot, R8=closure; R12–R15 sabit; desteklenmeyen op → `jitDeclined` + geri düşüş |
| 6. Native | C++ yardımcılar | `print`, SIMD, buffer, konteyner — bayt kod veya JIT stub’dan |
| 7. SIMT (`dispatch`) | Heartbeat + çalma | Izgara → iş blokları; ana + worker’lar CAS ile aralık alır; boşta kalan yarım çalar; `failed` veya `remaining==0` olana dek; `reduce` şeritleri birleştirir |

```text
.kaynak.isli
    │  tarama / Pratt derleme
    ▼
 bayt kod chunk ──► yığıt VM döngüsü
                        │
            ┌───────────┼───────────┐
            ▼           ▼           ▼
         native’ler   JIT kodu    dispatch işi
         (AVX2…)     (x86-64)   (heartbeat/çal)
```

**Değer algoritması:** her slot bir `uint64_t` (**NaN-boxing**). Normal double ise sayı; değilse üst bitler etiket (nil, bool, nesne …). Etiketle çakışan sessiz NaN’lar kutulanırken kanonikleşir. Nesneler refcount / gerekince klon kurallarıyla paralel kernel’de paylaşılır veya ayrılır.

**Dispatch algoritması (büyük N):** statik `ParallelJob`; lineer indeks bölünür; worker’lar `claimOwnBlock` / `stealHalf`; her indeks kernel closure’ını çalıştırır; `atomic_add` typed buffer hücresinde donanım atomik kullanır; hata → iş başarısız (sessiz `nil` yok). Worker içi iç içe `dispatch` **seri** (çift bariyer kilitlenmesi olmasın).

### Çalıştırma modeli (bileşenler)

| Aşama | Görev |
| --- | --- |
| Pratt ayrıştırıcı + bayt kod | Kaynak → kompakt `OP_*` (`chunk.hpp`) |
| Yığıt bayt kod VM’si | Taşınabilir yorumlayıcı; yereller VM yığıtında |
| x86-64 JIT (Win64 ABI) | Sıcak fonksiyonlar native; sabit register’lar |
| Çalışma zamanı native’leri | AVX2, typed buffer, SIMT, veri yapıları |

### Tipler, diziler ve typed buffer

Skalerler açıkça bildirilir; `var` / `auto` yoktur. Koleksiyonlar iki katmana ayrılır:

| Soyutlama | Bellek | Uygun kullanım |
| --- | --- | --- |
| `Array` | NaN-box `Value` hücreleri | Nesne ağırlıklı / karışık veri |
| `Buffer` (`f32buf` / `f64buf` / `i32buf` / `u8buf`) | Yoğun C düzeni | Kernel, SIMD, atomik, sıkı döngü |

```c
Array a = array(4, 0.0);
Buffer b = f64buf(1024, 1.0);
b[0] = a[0] + 2.5;
print len(b);        // 1024
print buf_type(b);   // f64
```

`push` / `copy` / `sort`, sıfır-kopya string görünümleri (`stakel`, `ssub`, `sfind`, …) ve klasik yapılar (`stack`, `list`, heap, kırmızı-siyah / B-ağacı) aynı değer modelinin üzerindedir. Kullanıcı `struct` örnekleri dinamik alanlı düz yığın nesneleridir.

### CPU üzerinde SIMT paralellik

Paralellik elle thread açmak yerine CUDA benzeri **`dispatch`** ile yazılır:

```c
dispatch(dimX[, dimY[, dimZ]]) [tile(...)] [reduce(op) into name] {
    |i[, j[, k]]|
    /* çekirdek gövdesi */
}
```

| Yapı | Anlam |
| --- | --- |
| `dispatch(N)` / 2D / 3D | İndeks uzayını çekirdeklere böler |
| `reduce(+|*|min|max) into x` | Ağaç indirgeme; kernel sayı `return` etmeli |
| `tile(tx[, ty])` | 2D’de önbellek dostu blok gezintisi |
| `atomic_add(buf, i, d)` | `f64`/`f32`/`i32` buffer’da yarışsız toplama (yeni değeri döner) |
| `parallel { }` | Yalnız kapsam; **thread açmaz** |

Küçük ızgaralar tek thread’de; büyükler **heartbeat** zamanlayıcıda çalışır (ana thread katılır; çekirdek 0 host’a bırakılır). Worker içindeki iç içe `dispatch` **seri** çalışır (bariyer kilitlenmesi olmasın). Kernel hataları sessiz `nil` yerine runtime hatasıdır.

### SIMD ve sistem yüzeyi

`simd_fill`, `simd_copy`, `simd_add` / `mul` / `div`, `simd_fma`, `simd_sum`, `simd_dot` 256-bit AVX2/FMA yollarına oturur; hem dizi hem buffer kabul eder. Matematik ve zamanlama (`sqrt`, `pow`, `clock`, `cpu_info`, …) standart kütüphaneyi tamamlar. CLI:

| Komut | Etki |
| --- | --- |
| `isli program.isli` | Derle ve çalıştır (JIT açık) |
| `isli --info` | Marka, çekirdek, AVX2/FMA/AVX-512/AVX10 |
| `isli -rt` / `--realtime` | Düşük jitter için çekirdek kilidi |

### Python, Lua ve Rust’tan ayrılan yönler

| Eksen | **Isli** | **Python** | **Lua** (5.x / PUC) | **Rust** |
| --- | --- | --- | --- | --- |
| Tip | Açık yüzey tipleri; runtime NaN-box | Dinamik `PyObject*` | Dinamik tablo / etiketli değer | Statik sahiplik + trait; varsayılan VM yok |
| Çalıştırma | Bayt kod VM **+** kendi x86-64 JIT + AVX2 | CPython bayt kod (+ isteğe bağlı JIT); GIL sık sık seri | Register/yığıt VM; JIT ancak LuaJIT (ayrı uygulama) | Önden LLVM/codegen → native |
| Paralellik | Dil düzeyi **SIMT `dispatch` / reduce / tile / atomic_add** | `threading`/`multiprocessing`/`asyncio`; yerleşik indeks-ızgara SIMT yok | Coroutine; çok çekirdek için kilit / C modülü | `rayon`, thread, `async` — güçlü ama **kütüphane**, dil grid sözdizimi değil |
| Sayısal bellek | `Array` (kutulu) **ve** yoğun `Buffer` | `list`/NumPy (dışarıda) | Tablo; çekirdekte typed buffer yok | Slice/`Vec` — mükemmel, döngüyü sen yazarsın |
| SIMD | Yerleşik `simd_*` (AVX2 zorunlu) | NumPy / C eklenti | Çekirdekte yok | `std::simd` / intrinsic / crate |
| Bellek | Refcount + paralel klon kuralları | Refcount + döngü GC | GC | Sahiplik / borrow (GC yok) |
| Niş | CUDA benzeri **CPU** kernel + betik ergonomisi | Genel betik, ekosistem | Gömme / oyun betiği | Güvenli sistem yazılımı |

**Python’a karşı:** Isli C-benzeri akış ve açık tip tutar; NumPy + süreç havuzu yerine **dil düzeyinde veri-paralel ızgara** ve **süreç içi AVX2** sunar. Python kütüphane genişliğinde önde; Isli tek ikili yolda `dispatch` → heartbeat worker (GIL yok).

**Lua’ya karşı:** İkisi de küçük, kapanışlı VM. Lua özü tablo + coroutine; Isli özü **typed buffer**, **SIMT dispatch** ve sayısal döngü için **Win64 JIT**. LuaJIT bazı skalerlerde daha hızlı olabilir; Isli’nin ayrımı **SIMT + buffer + SIMD** yığınıdır.

**Rust’a karşı:** Rust daha hızlı/güvenli native üretir. Isli sistem mühendisliğini değiştirmez. `unsafe` / rayon boru hattı / her denemede yeniden derleme olmadan CUDA-şekilli `dispatch` ile stencil, reduce, bounce denemesi yazdırır. `benchmarks/` skalerde Rust/C++ üstünlüğünü; Isli’nin bahsini **CPU SIMT ifadesi + hızlı iterasyon** olarak somutlar.

**Donanım şartı:** AVX2 yoksa süreç başlamaz.

Özetle Isli, **araştırma odaklı ama kullanılabilir bir CPU dili**dir: C benzeri ergonomi, derle→VM→JIT→heartbeat algoritması, yoğun buffer’lar ve Python/Lua’da dil ilkeli olmayan (Rust’ta çoğunlukla kütüphane olan) açık bir SIMT modeli — `SYNTAX.md`.

---

## Directories

| Folder | Contents |
| --- | --- |
| `isli_compiler/` | Compiler / VM (C++20, Makefile) |
| `vscode-isli/` | VS Code language extension |
| `Isli_Kurulum_Paketi/` | USB install template (`Kurulum.bat`) |

## Build (Windows, MSYS2 UCRT)

```bat
cd isli_compiler
set PATH=C:\msys64\ucrt64\bin;%PATH%
mingw32-make
isli.exe --info
```

Requires g++ C++20 and AVX2. Without AVX2 the process exits.

Tests:

```bat
cd isli_compiler
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

## VS Code extension

```bat
cd vscode-isli
npx --yes @vscode/vsce package
```

Install the resulting `.vsix` via Extensions → Install from VSIX. If `isli.compilerPath` is empty, the extension looks for `isli_compiler/isli.exe` or `isli` on `PATH`.

## Install package

`isli.exe` / `.vsix` are not committed. After a local build:

1. Copy `isli_compiler/isli.exe` → `Isli_Kurulum_Paketi/isli.exe`
2. Copy `vscode-isli/*.vsix` → `Isli_Kurulum_Paketi/` (e.g. `isli-lang-1.1.0.vsix`)
3. Run `Kurulum.bat` on the target machine

## CLI

```
isli file.isli
isli --info
isli -rt file.isli
```

Syntax guide: [`isli_compiler/SYNTAX.md`](isli_compiler/SYNTAX.md).
