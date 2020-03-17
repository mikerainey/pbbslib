ifeq (, $(shell which jemalloc-config))
JEMALLOC =
else
JEMALLOCLD = $(shell jemalloc-config --libdir)
JEMALLOC = -L$(JEMALLOCLD) -ljemalloc 
endif

CONCEPTS = -fconcepts -DCONCEPTS
CFLAGS = -I ../ -I ../../mcsl-v2/include -I $(CMDLINE_INCLUDE_PATH) -O0 -g3 -mcx16 -std=c++17 -march=native -m64 --save-temps # -Wall -I ../

OMPFLAGS = -DOPENMP -fopenmp
CILKFLAGS = -DCILK -fcilkplus
HGFLAGS = -DMCSL -pthread -DMCSL_ENABLE_STATS -DMCSL_ENABLE_LOGGING

ifdef CLANG
CC = clang++
PFLAGS = $(CILKFLAGS)
else ifdef CILK
CC = g++
PFLAGS = $(CILKFLAGS)
else ifdef OPENMP
CC = g++
PFLAGS = $(OMPFLAGS)
else ifdef HOMEGROWN
CC = g++
PFLAGS = $(HGFLAGS)
else ifdef SERIAL
CC = g++
PFLAGS =
else # default is homegrown
CC = g++
PFLAGS = $(HGFLAGS)
endif

EXAMPLES = mcss wc grep build_index primes longest_repeated_substring bw bfs

all : $(EXAMPLES)

runall : $(EXAMPLES)
	./mcss -r 5 -n 100000000
	./wc -r 5 build_index.cpp
	./grep -r 5 main bw.cpp
	./build_index -r 5 build_index.cpp
	./primes -r 5 100000000
	./longest_repeated_substring -r 5 longest_repeated_substring
	./bw -r 5 bw.cpp

# object files
% : %.cpp ligra.h extra.h grep.h wc.h
	$(CC) $(CFLAGS) $(PFLAGS) $@.cpp -o $@ $(JEMALLOC)

clean:
	rm -f $(EXAMPLES)
