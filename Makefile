
HEADERS=src/common.hh src/utils.hh src/value.hh src/core.hh src/runtime.hh src/iterator.hh src/promise.hh src/environment.hh src/modules.hh src/builtins.hh src/debugger.hh src/execute.hh src/ast.hh src/parser.hh src/readline.hh src/library.hh src/napi.hh out/intrinsics.hh

CCFILES=src/main.cc src/value.cc src/core.cc src/runtime.cc src/iterator.cc src/promise.cc src/environment.cc src/modules.cc src/builtins.cc src/ast.cc src/parser.cc src/execute.cc src/support.cc src/readline.cc src/library.cc src/napi.cc out/intrinsics.cc

OBJFILES=$(patsubst %.cc, out/$(OUTPUT)_obj/%.o, $(CCFILES))

CFLAGS+=-std=gnu++20 -isystem /usr/include/node26 -DENABLE_DEBUG=1 -g -Wall -Wno-reorder -Wextra

PLATFORM?=UNIX

ifeq ($(PLATFORM),Windows)
	LDFLAGS+=-Wl,--export-all-symbols
else
	LDFLAGS+=-rdynamic
endif

all:
	echo With ECMAScript 4
	OUTPUT=jsrun CFLAGS="-DENABLE_ES4=1 -DENABLE_DEC128=1" CXX=g++ make build
	echo Without ECMAScript 4
	OUTPUT=jsno4 CFLAGS="" CXX=g++ make build
	make napi

build: bin/$(OUTPUT)

bin/$(OUTPUT): $(OBJFILES) $(HEADERS)
	mkdir -p bin
	$(CXX) -o $@ $(OBJFILES) -licuuc -lgmpxx -lgmp -lgccpp -lgc $(CFLAGS) $(LDFLAGS)

out/$(OUTPUT)_obj/%.o: %.cc $(HEADERS)
	mkdir -p `dirname $@`
	$(CXX) -c -o $@ $< $(CFLAGS)

out/$(OUTPUT)_obj/src/main.o: src/main.cc out/tests.gen.cc $(HEADERS)
	mkdir -p `dirname $@`
	$(CXX) -c -o $@ $< $(CFLAGS)

clean:
	rm -rf bin out
	make -C napi clean

distclean: clean
	rm -rf *~ src/*~ test/*~ html latex
	make -C test/es4/test1 distclean
	make -C test/es4/test2 distclean
	make -C test/es4/test3 distclean

docs:
	doxygen Doxyfile

out/intrinsics.hh out/intrinsics.cc: src/intrinsics.py src/intrinsics.dat
	mkdir -p out
	python3 $^

out/tests.gen.cc: test/gentests.py test/tests.json
	mkdir -p out
	python3 $^ > $@

tests: napi
	echo Syntax tests
	sh test/runtests.sh
	echo Semantics tests
	python3 test/dotests.py

napi:
	make -C napi

.PHONY: all build tests napi clean distclean docs

