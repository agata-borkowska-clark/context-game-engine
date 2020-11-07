.PHONY: default clean

TS=$(shell find src -name '*.ts')
JS=${TS:src/%.ts=build/scripts/%.js}

default: build/Makefile ${JS}
	cd build && $(MAKE)

.PHONY: build/Makefile
build/Makefile: CMakeLists.txt | build
	cd build && cmake ..

${JS} &: ${TS} | build/scripts
	tsc --incremental | src/format_errors.sh

build:
	mkdir build

build/scripts: | build
	mkdir build/scripts

clean:
	rm -r build
