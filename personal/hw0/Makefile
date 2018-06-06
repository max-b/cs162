TARGETS = wc main map
LIBS = 
CC = gcc
CFLAGS = -g
# CFLAGS = -std=gnu11 -Wall -Wextra -Werror -Werror-implicit-function-declaration
BUILD_DIR = ./build/
BINS = $(addprefix $(BUILD_DIR), $(TARGETS))

.PHONY: default all clean check

default: $(TARGETS)
all: default

check: all
	./scripts/test.sh 

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
		$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS) $(TARGETS)

$(TARGETS): $(OBJECTS)
		$(CC) $@.o $(CFLAGS) $(LIBS) -o $(addprefix $(BUILD_DIR), $@)

clean:
		-rm -f *.o
		-rm -f $(BINS)
