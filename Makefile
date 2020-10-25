.PHONY: default clean

default: build/Makefile
	cd build && $(MAKE)

build/Makefile: CMakeLists.txt | build
	cd build && cmake ..

build:
	mkdir build

clean:
	rm -r build
