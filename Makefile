# Makefile -- build the mini search engine (mse). See plan.md section 13.
#
#   make          build build/mse
#   make clean    remove the build/ directory

CXX      ?= c++
CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BUILD    := build
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

all: $(BUILD)/mse

$(BUILD)/mse: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

-include $(DEPS)

.PHONY: all clean
