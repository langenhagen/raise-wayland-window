CFLAGS  ?= -O2 -Wall -Wextra
PKG     := gio-2.0

.PHONY: build
build: raise-window

raise-window: raise-window.c
	$(CC) $(CFLAGS) -o $@ $< $$(pkg-config --cflags --libs $(PKG))

.PHONY: clean
clean:
	rm -f raise-window

.PHONY: check
check:
	shellcheck -x --exclude SC2059 raise-window.sh
	shfmt --indent 4 --diff raise-window.sh
