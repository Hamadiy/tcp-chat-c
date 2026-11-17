CC       ?= cc
CFLAGS   ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS += -D_DEFAULT_SOURCE
BIN      := bin

all: $(BIN)/chat_server $(BIN)/chat_client

$(BIN)/chat_server: src/chat_server.c | $(BIN)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/chat_server.c

$(BIN)/chat_client: src/chat_client.c | $(BIN)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/chat_client.c

$(BIN):
	mkdir -p $@

test: all
	python3 tests/test_chat.py

clean:
	rm -rf $(BIN)

.PHONY: all test clean
