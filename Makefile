TARGET := soralink
SRC := soralink.c

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

ifeq ($(OS),Windows_NT)
TARGET := $(TARGET).exe
CPPFLAGS += -D_WIN32_WINNT=0x0601
LDLIBS += -lws2_32
endif

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

debug: CFLAGS := -O0 -g3 -Wall -Wextra -Wpedantic -std=c11
debug: clean all

clean:
	$(RM) soralink soralink.exe
