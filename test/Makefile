# Host-side unit tests: pure code from main/ and components/, built with the
# system compiler (no ESP-IDF). Needs a C/C++ compiler, libmbedtls-dev and libcjson-dev.
#   make -C test        build and run
CXX ?= g++
CC ?= gcc
FLAGS = -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
        -Wno-deprecated-declarations \
        -fsanitize=address,undefined -fno-sanitize-recover=all
INC = -Istubs -Igen -I../main -I../components/stratum_v2/include -I/usr/include/cjson
SRC_CPP = test_main.cpp ../main/mining/work.cpp ../main/mining/payout.cpp
SRC_C = ../components/stratum_v2/sv2_protocol.c ../components/stratum_v2/sv2_aead.c

run: unit_tests
	./unit_tests

# core/board_config.h is generated from the board file (the NerdQaxe++, the
# default board, here) as the firmware build does it
BOARD_FILES = $(wildcard ../boards/*.toml ../boards/families/*.toml)
gen/core/board_config.h: ../boards/nerdqaxe.toml $(BOARD_FILES) ../tools/gen_board.py
	python3 ../tools/gen_board.py ../boards/nerdqaxe.toml gen

unit_tests: gen/core/board_config.h $(SRC_CPP) $(SRC_C) $(wildcard *.h)
	$(CC) $(FLAGS) $(INC) -c ../components/stratum_v2/sv2_protocol.c -o sv2_protocol.o
	$(CC) $(FLAGS) $(INC) -c ../components/stratum_v2/sv2_aead.c -o sv2_aead.o
	$(CXX) -std=gnu++17 $(FLAGS) $(INC) $(SRC_CPP) sv2_protocol.o sv2_aead.o -lmbedcrypto -lcjson -lm -o $@

clean:
	rm -rf unit_tests *.o gen

.PHONY: run clean
