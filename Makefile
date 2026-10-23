CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
DEPS_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libcurl json-c)
DEPS_LIBS ?= $(shell $(PKG_CONFIG) --libs libcurl json-c)
CFLAGS ?= -O2 -g
# Kept separate so that overriding CFLAGS cannot drop warnings or include paths.
BUILD_CFLAGS = -Isrc -Ibuild $(DEPS_CFLAGS) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror $(CFLAGS)
SOURCES = src/main.c src/api.c src/tools.c

.PHONY: all clean test install
all: newrelic-mcp-c

build/catalog.h: tools.json
	mkdir -p build
	python3 -c 'import json,pathlib; s=pathlib.Path("tools.json").read_text(); json.loads(s); pathlib.Path("$@").write_text("static const char tool_catalog[] = " + json.dumps(s) + ";\n")'

newrelic-mcp-c: $(SOURCES) src/nr.h build/catalog.h
	$(CC) $(BUILD_CFLAGS) $(LDFLAGS) $(SOURCES) $(DEPS_LIBS) $(LDLIBS) -o $@

build/newrelic-mcp-test: $(SOURCES) src/nr.h build/catalog.h
	$(CC) $(BUILD_CFLAGS) $(LDFLAGS) -DNR_TEST $(SOURCES) $(DEPS_LIBS) $(LDLIBS) -o $@

test: all build/newrelic-mcp-test
	python3 tests/exercise.py ./newrelic-mcp-c ./build/newrelic-mcp-test

install: newrelic-mcp-c
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 newrelic-mcp-c $(DESTDIR)$(PREFIX)/bin/newrelic-mcp-c

clean:
	rm -rf build newrelic-mcp-c
