# Targeted, deterministic build for the authorized_edit test and example.
# Serial (-j1) build and run from the repository root:
#   make -j1 -C tests/authorized_edit run

CXX      ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -pedantic
CPPFLAGS += -I../../include

BINS := test_authorized_edit example_authorized_edit

all: $(BINS)

test_authorized_edit: test_authorized_edit.cpp ../../include/nlohmann/authorized_edit.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

example_authorized_edit: example_authorized_edit.cpp ../../include/nlohmann/authorized_edit.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

run-test: test_authorized_edit
	./test_authorized_edit

run-example: example_authorized_edit
	./example_authorized_edit

run: run-test run-example

clean:
	rm -f $(BINS) $(BINS:=.exe)

.PHONY: all run run-test run-example clean
