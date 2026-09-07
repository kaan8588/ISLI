# Isli

Isli: C-tarzı sözdizimi, x86-64 JIT, SIMT `dispatch`, typed buffer, AVX2 SIMD.

Bu depo `compiler/` ağacıdır: dil çekirdeği, VS Code eklentisi, taşınabilir kurulum şablonu.

## Dizinler

| Klasör | İçerik |
| --- | --- |
| `isli_compiler/` | Derleyici / VM (C++20, Makefile) |
| `vscode-isli/` | VS Code dil eklentisi |
| `Isli_Kurulum_Paketi/` | USB kurulum şablonu (`Kurulum.bat`) |
| `SYNTAX.md` | Dil kılavuzu (`isli_compiler/SYNTAX.md`) |

## Derleme (Windows, MSYS2 UCRT)

```bat
cd isli_compiler
set PATH=C:\msys64\ucrt64\bin;%PATH%
mingw32-make
isli.exe --info
```

Gereksinim: g++ C++20, AVX2. AVX2 yoksa süreç durur.

Test:

```bat
cd isli_compiler
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

## VS Code eklentisi

```bat
cd vscode-isli
npx --yes @vscode/vsce package
```

Oluşan `.vsix` dosyasını Extensions → Install from VSIX ile yükle. `isli.compilerPath` boşsa eklenti `isli_compiler/isli.exe` veya PATH’teki `isli` arar.

## Kurulum paketi

Git’e `isli.exe` / `.vsix` konmaz. Yerel derlemeden sonra:

1. `isli_compiler/isli.exe` → `Isli_Kurulum_Paketi/isli.exe`
2. `vscode-isli/*.vsix` → `Isli_Kurulum_Paketi/` (ör. `isli-lang-1.1.0.vsix`)
3. Hedef makinede `Kurulum.bat`

## CLI

```
isli dosya.isli
isli --info
```

Sözdizimi: `isli_compiler/SYNTAX.md`.
