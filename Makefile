CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-deprecated-declarations -pthread
LDLIBS = -lcrypto -pthread

SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)

all: peer

peer: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d)

clean:
	rm -rf build peer

.PHONY: all clean
