CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS ?=
LDFLAGS ?=

TARGET := soralink
TARGET_TLS := soralink_tls
SRC := soralink.c

LDLIBS_COMMON :=
LDLIBS_TLS := -lssl -lcrypto

ifeq ($(OS),Windows_NT)
TARGET := soralink.exe
TARGET_TLS := soralink_tls.exe
CPPFLAGS += -D_WIN32_WINNT=0x0601
LDLIBS_COMMON += -lws2_32
endif

.PHONY: all clean debug

all: $(TARGET_TLS) $(TARGET)

$(TARGET_TLS): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSORALINK_USE_OPENSSL $(LDFLAGS) -o $@ $< $(LDLIBS_TLS) $(LDLIBS_COMMON)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS_COMMON)

debug: CFLAGS := -O0 -g3 -Wall -Wextra -Wpedantic -std=c11
debug: clean all

clean:
	$(RM) soralink_tls soralink soralink_tls.exe soralink.exe
