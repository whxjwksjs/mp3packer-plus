CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c99
LDFLAGS = -pthread
# Exclude Windows-only GUI (built separately via windows-x64.yml)
SRCS = $(filter-out gui_main.c, $(wildcard *.c))
OBJS = $(SRCS:.c=.o)
TARGET = mp3packer-plus

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
