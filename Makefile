CC = gcc

CFLAGS = -g -std=c17 -D_POSIX_C_SOURCE=200809L \
		 -Wall -Werror -Wextra \
		 -Wcast-align -Wconversion -Wfloat-equal -Wformat=2 -Wnull-dereference -Wshadow -Wsign-conversion -Wswitch-enum -Wundef -Wunreachable-code -Wunused \
 		 -fsanitize=undefined -pthread

ifneq ($(shell uname -s),Darwin) # se não for macOS
	CFLAGS += -fmax-errors=5
endif

HEADERS = $(wildcard *.h)
OBJECTS = parser.o datacenter.o datacenter_utils.o resources.o filesystem.o

all: cloudIST

cloudIST: main.c $(HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) -o cloudIST main.c $(OBJECTS) -lm

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c ${@:.o=.c}

clean:
	rm -f *.o cloudIST

format:
	@which clang-format >/dev/null 2>&1 || echo "Please install clang-format to run this command"
	clang-format -i *.c *.h
