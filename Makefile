CC = gcc
CFLAGS = -g -Wall -Wuninitialized -O1 -std=c11
ifdef SANATIZE_ADDRESS
	CFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif
BINARY = rt

all: $(BINARY)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINARY): $(wildcard src/*.c)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	$(RM) */*.o *.o $(BINARY)
