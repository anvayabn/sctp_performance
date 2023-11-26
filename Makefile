# Makefile for compiling a network application

CC=gcc
CFLAGS=-Wall -g
LDFLAGS=
LIBS=-lsctp

# Name of the final executable
TARGET=sctp_client

# Source and object files
SRC=sctp_client.c
OBJ=$(SRC:.c=.o)

# Default target
all: $(TARGET)

# Linking the final executable
$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# Compiling source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Cleaning up
clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
