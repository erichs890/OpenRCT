# 32-bit builds (msys2 mingw-w64-i686-gcc).
#   make test        differential tests, plain and with all hooks installed
#   make test-hooks  each hook alone, so every adapter runs under original asm callers (slow)
#   make launcher  build/OpenRCT.exe
#   make dist      dist/OpenRCT-v$(VERSION)-windows-portable-x86(.zip)
VERSION = 0.1
CC = /c/msys64/mingw32/bin/gcc
export PATH := /c/msys64/mingw32/bin:$(PATH)
export TMP := $(CURDIR)/build
export TEMP := $(TMP)
CFLAGS = -O1 -Wall -Wextra -Wno-missing-field-initializers -std=c11 -Isrc -Iruntime -Iharness
# Every exe that loads RCT spans the original image range itself (runtime/image_space.S); its own code
# starts at 0xC50000. runtime/globals.ld binds src/ globals to their original addresses.
LDFLAGS = -Wl,--image-base,0x400000 -Wl,--disable-dynamicbase -Wl,--section-start,.rctimg=0x401000 \
          -Wl,-Ttext,0xC50000 runtime/globals.ld
SRC = $(wildcard src/*.c)
RUNTIME = runtime/image.c runtime/image_space.S runtime/hooks.c runtime/hooks.S
DEPS = $(SRC) $(RUNTIME) $(wildcard src/*.h runtime/*.h) runtime/globals.ld
HOOKS = $(shell seq 0 42)   # one per entry of rct_hooks[] (runtime/hooks.c)

TESTS = build/test_platform_file.exe build/test_sawyer_io.exe build/test_paint_supports.exe build/test_platform_display.exe build/test_viewport.exe build/test_paint_add.exe build/test_paint_list.exe

test: $(TESTS) build/test_image.exe
	./build/test_image.exe
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done
	@for t in $(TESTS); do echo "== $$t --hook all"; ./$$t --hook all | tail -1; done

# Each hook alone, so every adapter runs under the original asm callers (~9 min).
test-hooks: $(TESTS)
	@for t in $(TESTS); do for h in $(HOOKS); do ./$$t --hook $$h | tail -1 | grep -q "^ok" \
	  || { echo "FAIL $$t --hook $$h"; exit 1; }; done; done; echo "each of $(words $(HOOKS)) hooks ok"

build/test_%.exe: tests/test_%.c harness/loader.c harness/rct.h $(DEPS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(filter %.c %.S,$^) $(LDFLAGS)

launcher: build/OpenRCT.exe
build/OpenRCT.exe: launcher/main.c launcher/skin.c launcher/skin.h runtime/display.c build/launcher_res.o $(DEPS)
	@mkdir -p build
	$(CC) $(CFLAGS) -mwindows -o $@ $(filter %.c %.S %.o,$^) $(LDFLAGS) -lbcrypt -lcomctl32 -lole32 -lgdi32

build/launcher_res.o: launcher/launcher.rc launcher/launcher.manifest launcher/openrct.ico
	@mkdir -p build
	cd launcher && windres launcher.rc -O coff -o ../$@

# Portable folder + zip, laid out like other portable game builds: the exe at the root, text files beside it.
DIST = dist/OpenRCT-v$(VERSION)-windows-portable-x86
dist: build/OpenRCT.exe LICENSE $(wildcard launcher/dist/*)
	rm -rf $(DIST) $(DIST).zip
	mkdir -p $(DIST)
	cp build/OpenRCT.exe $(DIST)/openrct.exe
	cp launcher/dist/changelog.txt launcher/dist/contributors.md launcher/dist/PRIVACY.md $(DIST)/
	cp LICENSE $(DIST)/licence.txt
	sed 's/{VERSION}/$(VERSION)/; s/{DATE}/'"$$(date +%F)"'/' launcher/dist/readme.txt > $(DIST)/readme.txt
	cd dist && python -c "import shutil; shutil.make_archive('$(notdir $(DIST))', 'zip', '$(notdir $(DIST))')"

.PHONY: test test-hooks launcher dist
