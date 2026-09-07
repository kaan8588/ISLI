#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>

static void enableRealTimeAndAffinity() {
    HANDLE thread = GetCurrentThread();
    DWORD_PTR affinityMask = 1; // Core 0
    if (!SetThreadAffinityMask(thread, affinityMask)) {
        fprintf(stderr, "[Warning] Thread CPU Pinning failed.\n");
    }

    HANDLE process = GetCurrentProcess();
    if (!SetPriorityClass(process, REALTIME_PRIORITY_CLASS)) {
        // If REALTIME fails (e.g. non-admin), fall back to HIGH_PRIORITY_CLASS
        SetPriorityClass(process, HIGH_PRIORITY_CLASS);
    }

    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    printf("[Isli-RT] Realtime Mode Activated (Core 0, Realtime Priority)\n");
}
#else
static void enableRealTimeAndAffinity() {
    // POSIX fallback if applicable
}
#endif

#include "chunk.hpp"
#include "common.hpp"
#include "debug.hpp"
#include "cpu_features.hpp"
#include "vm.hpp"

static void repl() {
    char line[1024];
    for (;;) {
        printf("isli > ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        interpret(line);
    }
}

static char* readFile(const char* path) {
    char normPath[1024];
    std::strncpy(normPath, path, sizeof(normPath) - 1);
    normPath[sizeof(normPath) - 1] = '\0';
    for (char* p = normPath; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    FILE* file = fopen(normPath, "rb");
    if (file == nullptr) {
        file = fopen(path, "rb");
    }
    if (file == nullptr) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = static_cast<char*>(std::malloc(fileSize + 1));
    if (buffer == nullptr) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    InterpretResult result = interpret(source);
    std::free(source);

    if (result == INTERPRET_COMPILE_ERROR) {
        vm.free();
        exit(65);
    }
    if (result == INTERPRET_RUNTIME_ERROR) {
        vm.free();
        exit(70);
    }
}

int main(int argc, char** argv) {
    bool isRealTime = false;
    const char* filePath = nullptr;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--info") == 0) {
            const CpuFeatures& f = cpuFeatures();
            printf("Isli CPU Hardware Detection:\n");
            printf("  Brand:            %s\n", f.brand);
            printf("  Logical Cores:    %d\n", f.logicalCores);
            printf("  AVX2:             %s\n", f.avx2 ? "Yes" : "No");
            printf("  FMA:              %s\n", f.fma ? "Yes" : "No");
            printf("  AVX-512F:         %s\n", f.avx512f ? "Yes" : "No");
            printf("  AVX10:            %s (version %d)\n", f.avx10 ? "Yes" : "No", f.avx10Version);
            printf("  Vector Width:     %d-bit\n", f.preferredVectorBytes * 8);
            return 0;
        } else if (std::strcmp(argv[i], "--realtime") == 0 || std::strcmp(argv[i], "-rt") == 0) {
            isRealTime = true;
        } else if (filePath == nullptr) {
            filePath = argv[i];
        } else {
            fprintf(stderr, "Usage: isli [--info] [--realtime] [path.isli]\n");
            exit(64);
        }
    }

    const CpuFeatures& cpu = cpuFeatures();
    if (!cpu.avx2) {
        fprintf(stderr, "[Fatal Error] Isli requires AVX2 support. Detected CPU lacks AVX2.\n");
        return 1;
    }

    if (isRealTime) {
        enableRealTimeAndAffinity();
    }

    vm.init();

    if (filePath == nullptr) {
        repl();
    } else {
        runFile(filePath);
    }

    vm.free();
    return 0;
}
