CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O3 -mavx2 -mfma -static -static-libgcc -static-libstdc++

SRCS = chunk.cpp compiler.cpp cpu_features.cpp debug.cpp isli_api.cpp jit.cpp main.cpp memory.cpp object.cpp scanner.cpp table.cpp value.cpp vm.cpp
OBJS = $(SRCS:.cpp=.o)

ifeq ($(OS),Windows_NT)
    TARGET = isli.exe
    RM = cmd /C del /Q /F
else
    TARGET = isli
    RM = rm -f
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET)
