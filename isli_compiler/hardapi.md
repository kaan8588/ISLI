### 17.2. Ham C API (`isli_api.hpp`)

Daha düşük seviyeli C projelerinde veya yığıt tabanlı doğrudan kontrolde kullanılır. Bu yaklaşımda modern nesneler yoktur; Lua'nın eski C arayüzündeki gibi yığıt (`stack`) işlemlerini elle yapmak gerekir:

| C API Fonksiyonu | Açıklama |
| :--- | :--- |
| `IsliState* isli_open()` | Yeni bir Isli VM çalışma ortamı (state) oluşturur ve başlatır. |
| `void isli_close(IsliState* L)` | VM çalışma ortamını ve tüm kaynaklarını serbest bırakır. |
| `bool isli_dostring(IsliState* L, const char* kod)` | Verilen Isli kaynak kodunu anında derler ve çalıştırır. |
| `bool isli_dofile(IsliState* L, const char* dosyaYolu)` | Belirtilen Isli dosyasını okur, derler ve çalıştırır. |
| `void isli_pushnumber(IsliState* L, double n)` | VM yığıtına (stack) sayı iter. |
| `void isli_pushstring(IsliState* L, const char* s)` | VM yığıtına metin iter. |
| `void isli_pushboolean(IsliState* L, bool b)` | VM yığıtına boolean iter. |
| `double isli_tonumber(IsliState* L, int index)` | Yığıttaki belirtilen indeksteki sayıyı okur (-1: en üst). |
| `const char* isli_tostring(IsliState* L, int index)` | Yığıttaki metni okur. |
| `bool isli_getglobal(IsliState* L, const char* name)` | Global değişkeni arar ve yığıta iter. |
| `void isli_setglobal(IsliState* L, const char* name)` | Yığıtın tepesindeki değeri global değişkene atar. |
| `void isli_setcfunction(IsliState* L, const char* name, fn)` | C fonksiyonunu Isli global sembol tablosuna bağlar. |

#### Ham C API Örneği (Low-Level)
Aşağıdaki örnekte yukarıdaki "Ham" API'nin kullanımı gösterilmiştir (Sol2 tarzı sihirli işlemler yoktur, her şey yığıt üzerinden manuel yapılır):

```cpp
#include "isli_api.hpp"
#include <iostream>

int main() {
    // 1. VM'i başlat (Manuel)
    IsliState* L = isli_open();

    // 2. Isli betiğini çalıştır
    isli_dostring(L, "int a = 20; int b = 30; int sonuc = a + b;");

    // 3. Değişken Okuma (Manuel Stack İşlemi)
    if (isli_getglobal(L, "sonuc")) { 
        double sonuc = isli_tonumber(L, -1); // Yığıtın tepesindeki sayıyı al
        std::cout << "Sonuc: " << sonuc << std::endl; // 50
    }

    // 4. Değişken Gönderme (Manuel Stack İşlemi)
    isli_pushnumber(L, 1.5);
    isli_setglobal(L, "katsayi"); // Yığıttaki sayıyı katsayi'ye ata
    
    isli_pushstring(L, "ahmetmehmet");
    isli_setglobal(L, "kullanici"); // Yığıttaki yazıyı kullanici'ye ata

    // 5. VM'i temizle ve kapat (Manuel)
    isli_close(L);
    return 0;
}
```

---