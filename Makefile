# Makefile for IOS Project 2 - Roller Coaster synchronization (proj2)
# Author: Henrich Sobotovic xsoboth00
# Date: 24/04/2026
# Usage:
#   make        - build the binary
#   make clean  - remove generated files

# Compiler and flags
CC      = gcc
# Required flags 
CFLAGS  = -std=gnu99 -Wall -Wextra -Werror -pedantic
# Link against pthreads and real-time lib
LDFLAGS = -pthread -lrt

TARGET  = proj2
SRC     = proj2.c

.PHONY: all clean

# all: compile the project 
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

## clean: remove the compiled binary and output file
clean:
	rm -f $(TARGET) proj2.out
