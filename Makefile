# goldhen plugin build. ci builds this on linux with the openorbis toolchain + goldhen sdk.
# OO_PS4_TOOLCHAIN must point at the toolchain with libGoldHEN_Hook.a, crtprx.o and the
# GoldHEN headers under it (ci does `make install` of the sdk then copies crtprx.o)
TARGET    := cowboy_menu.prx
TOOLCHAIN := $(OO_PS4_TOOLCHAIN)
PROJDIR   := src
INTDIR    := build

LIBS   := -lSceLibcInternal -lkernel -lSceSysmodule -lGoldHEN_Hook
CFILES := $(wildcard $(PROJDIR)/*.c)
OBJS   := $(patsubst $(PROJDIR)/%.c,$(INTDIR)/%.o,$(CFILES))

CFLAGS  := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -O2 \
           -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -I$(PROJDIR)
LDFLAGS := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x -e _init --eh-frame-hdr \
           -L$(TOOLCHAIN)/lib $(LIBS)
CRTPRX  := $(TOOLCHAIN)/lib/crtprx.o
HOST_CC ?= cc
DOCKER_IMAGE ?= cowboy-menu-build

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CC   := clang
  LD   := ld.lld
  CDIR := linux
endif
ifeq ($(UNAME_S),Darwin)
  CC   := /opt/homebrew/opt/llvm/bin/clang
  LD   := /opt/homebrew/opt/llvm/bin/ld.lld
  CDIR := macos
endif

all: $(TARGET)

$(INTDIR):
	@mkdir -p $(INTDIR)

$(INTDIR)/%.o: $(PROJDIR)/%.c | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET): $(OBJS)
	$(LD) $(OBJS) $(CRTPRX) -o $(INTDIR)/cowboy_menu.elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(INTDIR)/cowboy_menu.elf -out=$(INTDIR)/cowboy_menu.oelf --lib=$(TARGET) --paid 0x3800000000000011

clean:
	rm -rf $(INTDIR) $(TARGET)

test: | $(INTDIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tests/protection.c -o $(INTDIR)/protection_test
	./$(INTDIR)/protection_test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tests/ui_selectors.c -o $(INTDIR)/ui_selectors_test
	./$(INTDIR)/ui_selectors_test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tests/config.c -o $(INTDIR)/config_test
	./$(INTDIR)/config_test

docker:
	docker build --platform linux/amd64 -t $(DOCKER_IMAGE) .
	docker run --rm --platform linux/amd64 --network none \
	  --user "$(shell id -u):$(shell id -g)" \
	  --mount 'type=bind,src=$(CURDIR),dst=/workspace' $(DOCKER_IMAGE)
	mkdir -p dist
	cp $(TARGET) dist/$(TARGET)

.PHONY: all clean test docker
