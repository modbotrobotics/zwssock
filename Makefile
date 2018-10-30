# Copyright 2018 Modbot Inc.
.PHONY: all clean install-dependencies build c-library-build c-library-install c-library-uninstall \
	python-library-build python-install python python-uninstall python-library-update-version

BUILD = cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. -G "Unix Makefiles" && make

NPROCS:=1
OS:=$(shell uname -s)

ifeq ($(OS),Linux)
	NPROCS := $(shell grep -c ^processor /proc/cpuinfo)
else ifeq ($(OS),Darwin)
	NPROCS := $(shell sysctl hw.ncpu | awk '{print $$2}')
endif

all: clean install-dependencies build

build:
	mkdir -p build
	cd build && $(BUILD) -j$(NPROCS)

clean:
	rm -rf build bin

install:
	cd build && sudo make install

install-dependencies:
	mkdir -p build
	cd build && conan install .. --build=outdated

test: install-dependencies build
	build/bin/c_test

uninstall:
	sudo rm -rf /usr/local/lib/libzwssock.*
	sudo rm -rf /usr/local/include/zwssock
