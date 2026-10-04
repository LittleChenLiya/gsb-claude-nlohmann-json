# Makefile for the authorized_edit example.
#
# Build with -j1 for deterministic, single-threaded compilation:
#   make -j1
#
# Or build directly with g++:
#   g++ -std=c++11 -I../include authorized_edit.cpp -o authorized_edit

CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -I../include

TARGET = authorized_edit
SRC = authorized_edit.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
