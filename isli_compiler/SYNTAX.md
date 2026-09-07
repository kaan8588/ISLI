# ⚡ Isli Programlama Dili — Kapsamlı Sözdizimi ve Standart Kütüphane Kılavuzu

**Isli**; C-tarzı statik tiplere, donanım seviyesinde çalışan **x86_64 JIT (Just-In-Time) derleyicisine**, çok çekirdekli **SIMT (Veri Paralelizmi)** motoruna, **256-bit AVX2 SIMD** vektör kütüphanesine ve zengin dahili veri yapılarına sahip modern bir sistem programlama dilidir.

Bu kılavuz, Isli dilinde bulunan **tüm komutları, anahtar kelimeleri, veri yapılarını ve dahili fonksiyonları** ne işe yaradıkları ve kısa çalışır örnekleriyle birlikte açıklar.

---

## 📑 İçindekiler
1. [Veri Tipleri ve Değişkenler](#1-veri-tipleri-ve-değişkenler)
2. [Operatörler ve Mantıksal İfadeler](#2-operatörler-ve-mantıksal-ifadeler)
3. [Kontrol Akış Komutları (`if`, `else`, `while`, `for`)](#3-kontrol-akış-komutları)
4. [Fonksiyonlar, Özyineleme ve Kapanışlar (`return`, `Function`)](#4-fonksiyonlar-özyineleme-ve-kapanışlar)
5. [Diziler, Typed Buffer ve Dizi Fonksiyonları (`Array`, `Buffer`, `f64buf`, `len`)](#5-diziler-typed-buffer-ve-dizi-fonksiyonları)
6. [256-Bit AVX2 SIMD Donanımsal Vektör Fonksiyonları (`simd_*`)](#6-avx2-simd-vektör-fonksiyonları)
7. [Sıfır-Kopyalama String Fonksiyonları (String Views)](#7-string-view-fonksiyonları)
8. [Standart Veri Yapıları (`Stack`, `List`, `Heap`, `RBTree`, `BTree`)](#8-standart-veri-yapıları)
9. [Kullanıcı Tanımlı Yapılar (`struct`)](#9-kullanıcı-tanımlı-yapılar-struct)
10. [SIMT Çok Çekirdekli Paralel Programlama (`dispatch`, `reduce`, `tile`, `atomic_add`)](#10-simt-çok-çekirdekli-paralel-programlama-dispatch)
11. [Matematik ve Sistem Fonksiyonları (`clock`, `cpu_info`, `sqrt`, `floor`, `pow`, `abs`, `rand`)](#11-matematik-ve-sistem-fonksiyonları)
12. [Çalıştırma Modları ve CLI Bayrakları (`--info`, `-rt`, `--realtime`)](#12-çalıştırma-modları-ve-cli-bayrakları)

---

## 1. Veri Tipleri ve Değişkenler

Isli'de tüm değişken tipleri açıkça yazılmalıdır. `var` veya `auto` gibi dinamik tipler bulunmaz.

### 1.1 `int` (64-bit Tam Sayı)
Tam sayıları tutar.
```c
int yas = 25;
int negatif = -100;
print yas + negatif; // Çıktı: -75
```

### 1.2 `float` / `double` (64-bit Kayan Noktalı Sayı)
Ondalıklı hassas sayıları tutar.
```c
float pi = 3.14159;
double hassas = 0.000125;
print pi * 2.0; // Çıktı: 6.28318
```

### 1.3 `string` (Karakter Dizgisi)
Çift tırnak içinde metin verilerini tutar.
```c
string selamlama = "Merhaba Dünya!";
print selamlama; // Çıktı: Merhaba Dünya!
```

### 1.4 `bool` (`true`, `false`)
Mantıksal doğruluk değeridir.
```c
bool aktif = true;
bool bitti = false;
print aktif; // Çıktı: true
```

### 1.5 `null` / `nil` / `NULL`
Boş / tanımsız değeri ifade eder.
```c
int bosDeger = null;
print bosDeger; // Çıktı: NULL
```

---

## 2. Operatörler ve Mantıksal İfadeler

### 2.1 Aritmetik Operatörler (`+`, `-`, `*`, `/`, `%`)
```c
int a = 23;
int b = 5;
print a + b; // Toplama: 28
print a - b; // Çıkarma: 18
print a * b; // Çarpma: 115
print a / b; // Bölme: 4.6
print a % b; // Modülo (Kalan): 3
```

### 2.2 Karşılaştırma Operatörleri (`<`, `<=`, `>`, `>=`, `==`, `!=`)
```c
int x = 10;
int y = 20;
print x < y;  // true
print x >= y; // false
print x == y; // false
print x != y; // true
```

### 2.3 Mantıksal Operatörler (`and`, `or`, `!`)
```c
bool p = true;
bool q = false;
print p and q; // false
print p or q;  // true
print !p;      // false
```

---

## 3. Kontrol Akış Komutları

### 3.1 `if`, `else` (Şartlı Dallanma)
```c
int puan = 82;
if (puan >= 90) {
    print "Pekiyi";
} else if (puan >= 70) {
    print "İyi";
} else {
    print "Geliştirilmeli";
}
// Çıktı: İyi
```

### 3.2 `while` (Şartlı Döngü)
```c
int sayac = 3;
while (sayac > 0) {
    print sayac;
    sayac = sayac - 1;
}
// Çıktı: 3, 2, 1
```

### 3.3 `for` (Sayaçlı Donanım Döngüsü)
Isli JIT derleyicisi sayaçlı `for` döngülerini doğrudan CPU registerlarında çalıştırır:
```c
int toplam = 0;
for (int i = 1; i <= 5; i = i + 1) {
    toplam = toplam + i;
}
print toplam; // Çıktı: 15
```

---

## 4. Fonksiyonlar, Özyineleme ve Kapanışlar

### 4.1 Standart Fonksiyon ve `return`
```c
int topla(int x, int y) {
    return x + y;
}
print topla(15, 30); // Çıktı: 45
```

### 4.2 Donanım Düzeyinde Özyineleme (Direct Hardware Self-Recursion)
JIT derleyicisi özyinelemeli çağrıları donanım `CALL` komutuyla interpretersız çalıştırır:
```c
int faktoriyel(int n) {
    if (n <= 1) return 1;
    return n * faktoriyel(n - 1);
}
print faktoriyel(5); // Çıktı: 120
```

### 4.3 Kapanışlar (`Function` / Closures)
Fonksiyonlar değişkenlerde saklanabilir ve üst kapsamdaki değişkenleri yakalayabilir:
```c
Function carpanUret(int carpan) {
    int carp(int deger) {
        return deger * carpan;
    }
    return carp;
}

Function onKat = carpanUret(10);
print onKat(7); // Çıktı: 70
```

---

## 5. Diziler, Typed Buffer ve Dizi Fonksiyonları

### 5.1 `array(boyut, varsayilan)` (Dizi Oluşturma)
Belirtilen boyutta ve başlangıç değeriyle dizi oluşturur.
```c
Array arr = array(5, 0.0);
arr[0] = 10.5;
arr[4] = 99.9;
print arr[0]; // Çıktı: 10.5
```

### 5.2 `len(arr)` (Dizi / Metin Uzunluğu)
```c
Array arr = array(10, 0);
print len(arr); // Çıktı: 10
```

### 5.3 `push(arr, deger)` / `arr_push(arr, deger)` (Eleman Ekleme)
Dizinin sonuna dinamik eleman ekler.
```c
Array arr = array(0, 0);
push(arr, 42);
push(arr, 84);
print len(arr); // Çıktı: 2
print arr[1];   // Çıktı: 84
```

### 5.4 `copy(arr)` / `clone(arr)` (Dizi Kopyalama)
Dizinin tam derin kopyasını oluşturur.
```c
Array a = array(3, 7);
Array b = copy(a);
b[0] = 99;
print a[0]; // Çıktı: 7 (orijinal etkilenmez)
print b[0]; // Çıktı: 99
```

### 5.5 `sort(arr)` / `c_sort(arr)` (Hızlı Sıralama)
Diziyi yerel $O(N \log N)$ C hızında küçükten büyüğe sıralar.
```c
Array arr = array(4, 0);
arr[0] = 50; arr[1] = 10; arr[2] = 40; arr[3] = 20;
sort(arr);
print arr[0]; // Çıktı: 10
print arr[3]; // Çıktı: 50
```

### 5.6 Typed Buffer (`f32buf`, `f64buf`, `i32buf`, `u8buf`)
`Array` her hücrede NaN-box `Value` tutar (8 bayt + etiket). Buffer düz C dizisidir: `f64` = 8 bayt double, `f32` = 4 bayt float, `i32` = 4 bayt tam sayı, `u8` = 1 bayt. Kernel ve SIMD için daha sıkı bellek, daha hızlı indeks.

İndeks sözdizimi diziyle aynıdır: `buf[i]`, `buf[i] = x`. `len(buf)` eleman sayısını verir.

```c
Buffer f32 = f32buf(8, 1.5);     // 8 adet float32, hepsi 1.5
Buffer f64 = f64buf(16, 0.0);    // 8 bayt double
Buffer i32 = i32buf(4, 0);       // 32-bit tam sayı
Buffer u8  = u8buf(256, 0);      // bayt

print len(f32);        // 8
print buf_type(f32);   // f32
print f32[0];          // 1.5
f32[0] = 42.25;

Array a = buf_to_array(f64);           // Buffer -> Array (kopya)
Buffer b = buf_from_array(a, "f64");   // Array -> Buffer; 2. arg: "f32"|"f64"|"i32"|"u8"
```

`simd_fill`, `simd_copy`, `simd_add`, `simd_dot` vb. buffer üzerinde de çalışır.

---

## 6. AVX2 SIMD Vektör Fonksiyonları

İşlemcinin 256-bit AVX2/FMA vektör ünitelerini kullanarak 1 milyonluk dizileri tek komutta işler.

### 6.1 `simd_fill(arr, deger)`
Dizinin tüm elemanlarını SIMD ile anında doldurur.
```c
Array arr = array(1000, 0.0);
simd_fill(arr, 4.5);
print arr[500]; // Çıktı: 4.5
```

### 6.2 `simd_copy(hedef, kaynak)`
Bellek transferini AVX2 256-bit bloklarla ışık hızında yapar.
```c
Array a = array(1000, 3.14);
Array b = array(1000, 0.0);
simd_copy(b, a);
print b[0]; // Çıktı: 3.14
```

### 6.3 `simd_add`, `simd_sub`, `simd_mul`, `simd_div`
İki diziyi eleman eleman vektörel toplar, çıkarır, çarpar veya böler.
```c
Array a = array(100, 10.0);
Array b = array(100, 2.5);
Array c = simd_add(a, b);
Array d = simd_mul(a, b);
print c[0]; // Çıktı: 12.5
print d[0]; // Çıktı: 25
```

### 6.4 `simd_fma(a, b, c)` ($A \times B + C$)
Fused Multiply-Add donanım komutuyla tek döngüde hesaplar.
```c
Array a = array(100, 2.0);
Array b = array(100, 3.0);
Array c = array(100, 4.0);
Array sonuc = simd_fma(a, b, c); // (2*3)+4
print sonuc[0]; // Çıktı: 10
```

### 6.5 `simd_sum(arr)` ve `simd_dot(a, b)`
Dizi toplamını ve iki dizinin noktasal skaler çarpımını hesaplar.
```c
Array a = array(1000, 2.0);
Array b = array(1000, 3.0);
print simd_sum(a);    // Çıktı: 2000
print simd_dot(a, b);  // Çıktı: 6000
```

---

## 7. String-View Fonksiyonları (Sıfır Kopyalama)

Metinleri RAM'de kopyalamadan işaretçiler üzerinden işler.

| Fonksiyon | Açıklama | Örnek | Çıktı |
|:---|:---|:---|:---|
| `len(s)` | Metin karakter uzunluğu | `len("Isli")` | `4` |
| `stakel(s, n)` | Sol baştan $N$ karakter alır | `stakel("Programlama", 7)` | `"Program"` |
| `staker(s, n)` | Sağ sondan $N$ karakter alır | `staker("Programlama", 4)` | `"lama"` |
| `schopl(s, n)` | Sol baştan $N$ karakter kırpar | `schopl("Merhaba", 3)` | `"haba"` |
| `schopr(s, n)` | Sağ sondan $N$ karakter kırpar | `schopr("Merhaba", 2)` | `"Merha"` |
| `strim(s)` | Baş ve sondaki boşlukları siler | `strim("  test  ")` | `"test"` |
| `ssub(s, bas, boy)` | Alt metin dilimi alır | `ssub("Compiler", 0, 4)` | `"Comp"` |
| `sfind(s, hedef)` | Alt metnin indeksini bulur | `sfind("abcdef", "cd")` | `2` |
| `stakefl(s, ayr)` | Ayırıcıya kadar soldan böler | `stakefl("user@mail", "@")`| `"user"` |
| `stakefr(s, ayr)` | Ayırıcıdan sonrasını alır | `stakefr("user@mail", "@")`| `"mail"` |

---

## 8. Standart Veri Yapıları

### 8.1 `Stack` (LIFO Yığın)
Komutlar: `stack()`, `push(st, val)`, `pop(st)`, `peek(st)`, `is_empty(st)`
```c
ObjStack st = stack();
push(st, 10);
push(st, 20);
print peek(st); // Çıktı: 20
print pop(st);  // Çıktı: 20
print pop(st);  // Çıktı: 10
```

### 8.2 `List` (Çift Yönlü Bağlı Liste)
Komutlar: `list()`, `push_back`, `push_front`, `pop_back`, `pop_front`, `front`, `back`
```c
ObjList l = list();
push_back(l, 100);
push_front(l, 50);
print front(l); // Çıktı: 50
print back(l);  // Çıktı: 100
```

### 8.3 `Heap` (Öncelik Kuyruğu)
Komutlar: `min_heap()`, `max_heap()`, `heap_push`, `heap_pop`, `heap_peek`
```c
ObjHeap h = min_heap();
heap_push(h, 40);
heap_push(h, 10);
heap_push(h, 30);
print heap_pop(h); // Çıktı: 10 (En küçük önce çıkar)
print heap_pop(h); // Çıktı: 30
```

### 8.4 `RBTree` (Kırmızı-Siyah Ağaç Haritası)
Komutlar: `rbtree()`, `rb_insert`, `rb_get`, `rb_has`, `rb_remove`, `rb_min`, `rb_max`
```c
Map tree = rbtree();
rb_insert(tree, 101, 7500);
rb_insert(tree, 102, 9200);

if (rb_has(tree, 101)) {
    print rb_get(tree, 101); // Çıktı: 7500
}
print rb_max(tree); // Çıktı: 102
```

### 8.5 `BTree` (B-Ağacı)
Komutlar: `btree()`, `btree_insert`, `btree_get`, `btree_has`, `btree_remove`
```c
ObjBTree bt = btree();
btree_insert(bt, 1, 500);
btree_insert(bt, 2, 600);
print btree_get(bt, 2); // Çıktı: 600
```

---

## 9. Kullanıcı Tanımlı Yapılar (`struct`)

Düz bellek nesneleri oluşturur:
```c
struct Vektor3D {}

void main() {
    Vektor3D v = Vektor3D();
    v.x = 1.0;
    v.y = 2.0;
    v.z = 3.0;
    print v.x + v.y + v.z; // Çıktı: 6
}
main();
```

---

## 10. SIMT Çok Çekirdekli Paralel Programlama (`dispatch`)

`dispatch(dimX[, dimY[, dimZ]]) { |i[, j[, k]]| ... }` işi CPU çekirdeklerine böler. Küçük iş (`N < 4096`) tek thread; büyük iş **heartbeat** zamanlayıcı ile (ana thread + worker’lar, çekirdek 0 ana işe bırakılır).

Kernel gövdesi bir kapanıştır. Dışarıdaki dizi/buffer’a upvalue ile erişilir.

### 10.1 1D Paralel Dizi İşleme
```c
Array a = array(1000000, 2.0);
Array b = array(1000000, 3.0);
Array c = array(1000000, 0.0);

dispatch(1000000) {
    |i|
    c[i] = a[i] + b[i];
}
print c[0]; // Çıktı: 5
```

### 10.2 2D / 3D Grid
```c
dispatch(1000, 1000) {
    |x, y|
    // x, y paralel indeksler
}

dispatch(8, 8, 8) {
    |x, y, z|
    // 3D
}
```

### 10.3 `reduce(...) into degisken` (Paralel indirgeme)
Kernel **sayı döndürmeli**. Operatör: `+`, `*`, `min`, `max`. Sonuç `into` hedefe yazılır (hedef önceden tanımlı olmalı). Ağaç indirgeme: aynı girdi → aynı sonuç (iş parçacığı sırasına bağlı değil).

```c
double s = 0.0;
dispatch(1000) reduce(+) into s {
    |i|
    return i;
}
print s; // 0+1+...+999 = 499500
```

### 10.4 `tile(tx[, ty])` (Sadece 2D)
Döngü sırasını önbellek dostu karelere çevirir (önce 16×16 blok, sonra komşu blok).

```c
int W = 256;
int H = 256;
Buffer dst = f64buf(W * H, 0.0);
dispatch(W, H) tile(16, 16) {
    |x, y|
    dst[x + y * W] = 1.0;
}
```

`tile` ile `reduce` birlikte kullanılabilir: `dispatch(W, H) tile(16, 16) reduce(+) into s { ... }`.

### 10.5 İç içe `dispatch`
Worker içinden ikinci `dispatch` **seri** çalışır (aynı bariyerde kilitlenmesin diye). Küçük iç döngü veya `atomic_add` için uygundur.

```c
Buffer b = f64buf(1, 0.0);
dispatch(100) {
    |i|
    dispatch(4) {
        |j|
        atomic_add(b, 0, 1.0);
    }
}
print b[0]; // 400
```

### 10.6 `atomic_add(buf, indeks, delta)`
Paralel kernel’de aynı hücreye yarışsız toplama. Yalnız `f64` / `f32` / `i32` buffer. Dönen değer: işlemden **sonraki** yeni sayı. Array, `u8buf`, kötü indeks veya eksik argüman **runtime error** (sessiz `nil` yok).

```c
Buffer cnt = f64buf(1, 0.0);
dispatch(10000) {
    |i|
    atomic_add(cnt, 0, 1.0);
}
print cnt[0]; // 10000
```

### 10.7 `parallel { ... }`
Şimdilik yalnızca bir blok kapsamı (yeni thread açmaz). Rezerve sözcük; asıl paralellik `dispatch` ile.

---

## 11. Matematik ve Sistem Fonksiyonları

```c
// Zaman Ölçümü
float t0 = clock();
// ... ağır hesaplama ...
float t1 = clock();
float gecenMs = (t1 - t0) * 1000.0;

// Matematik
print sqrt(144.0); // Karekök: 12
print floor(7.8);  // Aşağı yuvarlama: 7
print pow(2.0, 8); // Üs alma: 256
print abs(-45.5);  // Mutlak değer: 45.5
print rand();      // 0 ile 1 arasında rastgele sayı

// CPU / ISA bilgisi (metin)
print cpu_info();
```

---

## 12. Çalıştırma Modları ve CLI Bayrakları

### Standart JIT Çalıştırma:
```powershell
isli program.isli
```

### CPU özellikleri (`--info`):
```powershell
isli --info
```
Marka, mantıksal çekirdek, AVX2 / FMA / AVX-512 / AVX10, tercih edilen vektör genişliği. AVX2 yoksa `isli` çalışmaz.

### Gerçek Zamanlı ve Çekirdek Kilitli Çalıştırma (`-rt` / `--realtime`):
Ağır benchmarklarda işletim sistemi context switch gecikmelerini sıfırlamak için süreci doğrudan Core 0'a kilitler:
```powershell
isli -rt program.isli
# veya
isli --realtime program.isli
```