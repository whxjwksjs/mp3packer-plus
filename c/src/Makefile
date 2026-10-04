CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c99
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)
TARGET = mp3packer-plus

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
