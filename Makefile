# Host-side unit tests for lib/energy_core. No ESP32, Arduino runtime,
# network access or mains hardware is involved.
#
#   make test     build with -Wall -Wextra -Werror and run
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -pedantic -O1 -g
INCLUDES := -Ilib/energy_core/src -Itest/native
CORE_SRC := $(wildcard lib/energy_core/src/*.cpp)
TEST_SRC := $(wildcard test/native/*.cpp)
BIN      := build/native_tests

.PHONY: test clean
test: $(BIN)
	./$(BIN)

$(BIN): $(CORE_SRC) $(TEST_SRC) $(wildcard lib/energy_core/src/*.h) $(wildcard test/native/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(CORE_SRC) $(TEST_SRC) -o $@

clean:
	rm -rf build
