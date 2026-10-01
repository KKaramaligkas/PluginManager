TARGET = pluginmanager
OBJS = src/main.o src/app.o src/worker.o src/ui.o src/gfx.o src/text.o src/image.o \
       src/input.o src/net.o src/http_policy.o src/tlsdiag.o src/clock.o src/stubs.o src/entropy.o \
       src/util.o src/fs.o src/store.o src/pluginstxt.o src/db.o src/archive.o src/installer.o src/transaction.o src/resume.o

CFLAGS = -O2 -G0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -std=gnu99 $(EXTRA_CFLAGS)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PSP_FW_VERSION = 660

LIBDIR =
# sceUtility, sceRtc, sceNetInet and sceNetResolver are deliberately not listed:
# the toolchain adds them after libc, which calls them too (sockets, DNS...).
# Listing them here as well makes the linker pull each library's stubs in two
# separate chunks, and psp-fixup-imports then builds import tables that bind
# the wrong functions (tools/check_imports.py verifies this after linking).
LIBS = -lintrafont -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lcjson -lunarr -llzma -lbz2 -lpng -lz \
       -lpspgu -lpspgum -lpsppower -lpspwlan -lpspnet -lpspnet_apctl -lpspkubridge -lpspsystemctrl_user -lm

EXTRA_TARGETS = check-imports EBOOT.PBP
PSP_EBOOT_TITLE = Plugin Manager
PSP_EBOOT_ICON = res/ICON0.PNG
PSP_EBOOT_PIC1 = res/PIC1.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# Fails the build when an import table came out broken (see the note on LIBS)
.PHONY: check-imports
check-imports: $(TARGET).elf
	python3 tools/check_imports.py $(TARGET).elf

# Folder layout installed on the memory stick (PSP/APPS/PluginManager)
.PHONY: package
package: check-imports EBOOT.PBP
	rm -rf dist
	mkdir -p dist/PSP/APPS/PluginManager
	cp EBOOT.PBP dist/PSP/APPS/PluginManager/
	cp res/cacert.pem dist/PSP/APPS/PluginManager/
	cp store/store.json dist/PSP/APPS/PluginManager/
	cd dist && zip -q -r PluginManager.zip PSP
