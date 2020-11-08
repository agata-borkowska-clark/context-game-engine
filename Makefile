.PHONY: default clean

TS=$(shell find src -name '*.ts')
JS=${TS:src/%.ts=build/scripts/%.js}
STATIC_IN=$(shell find static)
STATIC_OUT=${STATIC_IN:%=build/%}

default: build/Makefile ${JS} ${STATIC_OUT}
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

build/static: | build
	mkdir build/static

build/static/%: static/% | build/static
	cp $^ $@

clean:
	rm -r build
