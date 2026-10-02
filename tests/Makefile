# Host unit/integration tests for the Plugin Manager core.
#   sudo apt-get install libcjson-dev libunarr-dev libmbedtls-dev zlib1g-dev libcurl4-openssl-dev
#   make -C PluginManager/tests            # unit tests
#   make -C PluginManager/tests install PKG_DIR=/path/to/downloaded/archives
#   make -C PluginManager/tests live       # downloads every store package (network)

CC      ?= gcc
CFLAGS  ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu99 -fsanitize=address,undefined
LDLIBS   = -lcjson -lunarr -lmbedcrypto -lz

CORE     = ../src/util.c ../src/fs.c ../src/store.c ../src/pluginstxt.c ../src/db.c \
           ../src/archive.c ../src/installer.c ../src/transaction.c ../src/resume.c ../src/tlsdiag.c
SEED     = $(abspath ../store/store.json)
WORK     = $(CURDIR)/work

.PHONY: all check install live clean

all: check

test_core: test_core.c $(CORE) test.h
	$(CC) $(CFLAGS) -o $@ test_core.c $(CORE) $(LDLIBS)

test_install: test_install.c $(CORE) test.h
	$(CC) $(CFLAGS) -o $@ test_install.c $(CORE) $(LDLIBS)

test_transaction: test_transaction.c $(CORE) test.h
	$(CC) $(CFLAGS) -DPM_FS_TESTING -o $@ test_transaction.c $(CORE) $(LDLIBS)

test_resume: test_resume.c $(CORE) test.h
	$(CC) $(CFLAGS) -DPM_FS_TESTING -o $@ test_resume.c $(CORE) $(LDLIBS)

resume_http: resume_http.c ../src/resume.c ../src/fs.c ../src/util.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl

test_worker: test_worker.c ../src/worker.c $(CORE) test.h
	$(CC) $(CFLAGS) -Ipsp -o $@ test_worker.c ../src/worker.c $(CORE) $(LDLIBS)

transfer_http: transfer_http.c ../src/transfer.c ../src/http_policy.c
	$(CC) $(CFLAGS) -o $@ $^ -lcurl

check: test_core test_transaction test_resume test_worker resume_http transfer_http
	rm -rf $(WORK)/core && mkdir -p $(WORK)/core/ms0 $(WORK)/core/ef0
	PM_FS_ROOT=$(WORK)/core PM_SEED_STORE=$(SEED) ./test_core
	rm -rf $(WORK)/transaction && mkdir -p $(WORK)/transaction/ms0 $(WORK)/transaction/ef0
	PM_FS_ROOT=$(WORK)/transaction ./test_transaction
	rm -rf $(WORK)/resume && mkdir -p $(WORK)/resume/ms0 $(WORK)/resume/ef0
	PM_FS_ROOT=$(WORK)/resume ./test_resume
	./test_worker
	python3 test_http_resume.py
	python3 test_transfer.py

install: test_install
	rm -rf $(WORK)/install && mkdir -p $(WORK)/install/ms0 $(WORK)/install/ef0
	PM_FS_ROOT=$(WORK)/install PM_SEED_STORE=$(SEED) PM_PKG_DIR=$(PKG_DIR) ./test_install

live: test_install
	rm -rf $(WORK)/live && mkdir -p $(WORK)/live/ms0 $(WORK)/live/ef0
	PM_FS_ROOT=$(WORK)/live PM_SEED_STORE=$(SEED) PM_LIVE=1 ./test_install

clean:
	rm -rf test_core test_transaction test_resume test_worker resume_http transfer_http test_install hostinstall $(WORK)

hostinstall: hostinstall.c $(CORE)
	$(CC) -O1 -g -Wall -std=gnu99 -o $@ hostinstall.c $(CORE) $(LDLIBS)
