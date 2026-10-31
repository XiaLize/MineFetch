CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
LDFLAGS += -municode -static -static-libgcc -static-libstdc++ -s
LDLIBS += -lwinhttp -lbcrypt -lole32 -luuid -lshell32

SRC := $(wildcard src/*.cpp)
OBJDIR := build/obj
OBJ := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRC))
TARGET := minefetch.exe

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJDIR)/%.o: src/%.cpp
	@if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	@if exist build rmdir /s /q build
	@if exist $(TARGET) del /q $(TARGET)

test: $(TARGET)
	.\$(TARGET) --self-test
