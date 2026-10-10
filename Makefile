# Makefile -- build the mini search engine (mse). See plan.md section 13.
#
#   make          build build/mse
#   make test     build and run the unit tests
#   make clean    remove the build/ directory

CXX      ?= c++
CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BUILD    := build
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

all: $(BUILD)/mse

TEST_BIN  := $(BUILD)/test_tokenizer
TEST_SRCS := tests/test_tokenizer.cpp src/tokenizer.cpp

test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) include/tokenizer.hpp include/corpus.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $(TEST_SRCS)

$(BUILD)/mse: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

-include $(DEPS)

.PHONY: all test clean
