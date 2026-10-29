CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
LDLIBS = -lcrypto -pthread

REPETICOES ?= 5
TAMANHO ?= 20971520
BLOCO ?= 1024

SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)

all: peer

peer: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d)

demo: peer
	bash scripts/demo.sh $(TAMANHO) $(BLOCO)

test: peer
	python3 scripts/testes.py --repeticoes $(REPETICOES)

run: peer
	bash scripts/demo.sh $(TAMANHO) $(BLOCO)
	python3 scripts/testes.py --repeticoes $(REPETICOES)

clean:
	rm -rf build peer

.PHONY: all demo test run clean
