# Host-side unit tests: pure code from main/ and components/, built with the
# system compiler (no ESP-IDF). Needs a C/C++ compiler, libmbedtls-dev and libcjson-dev.
#   make -C test        build and run
CXX ?= g++
CC ?= gcc
FLAGS = -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
        -Wno-deprecated-declarations \
        -fsanitize=address,undefined -fno-sanitize-recover=all
INC = -Istubs -I../main -I../components/stratum_v2/include -I/usr/include/cjson
SRC_CPP = test_main.cpp ../main/mining/work.cpp ../main/mining/payout.cpp
SRC_C = ../components/stratum_v2/sv2_protocol.c ../components/stratum_v2/sv2_aead.c

run: unit_tests
	./unit_tests

unit_tests: $(SRC_CPP) $(SRC_C) $(wildcard *.h)
	$(CC) $(FLAGS) $(INC) -c ../components/stratum_v2/sv2_protocol.c -o sv2_protocol.o
	$(CC) $(FLAGS) $(INC) -c ../components/stratum_v2/sv2_aead.c -o sv2_aead.o
	$(CXX) -std=gnu++17 $(FLAGS) $(INC) $(SRC_CPP) sv2_protocol.o sv2_aead.o -lmbedcrypto -lcjson -lm -o $@

clean:
	rm -f unit_tests *.o

.PHONY: run clean
