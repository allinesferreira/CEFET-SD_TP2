CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-deprecated-declarations -pthread
LDLIBS = -lcrypto -pthread
all: peer
peer: src/peer.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)
clean:
	rm -f peer
.PHONY: all clean
