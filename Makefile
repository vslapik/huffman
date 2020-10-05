DOTOPT := -Gratio=fill
CFLAGS := -Ofast -g -std=c11 -Wall -Iugeneric/include
#CFLAGS = -O0 -march=native -g -std=c11 -Wall -Iugeneric/
TARGET := huff
LIBUGENERIC := ugeneric/libugeneric.a

VALGRIND := $(shell command -v valgrind 2> /dev/null)

OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS := $(wildcard *.h)

%.o: %.c $(HEADERS) Makefile
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(LIBUGENERIC) $(OBJECTS)
	$(CC) $(OBJECTS) -g -rdynamic $(LIBUGENERIC) -o $@

define check_file
    ./huff $(1) -c arch -v --dump-table $(CLI_AUX)
    ./huff arch -x extracted -v --dump-table $(CLI_AUX)
    md5sum $(1) extracted
    #@-rm -rf extracted
    #@-rm -rf arch
endef

ctest: huff large.txt
	./huff large.txt -c arch -v --dump-table
	md5sum arch

xtest: huff arch ctest
	./huff arch -x extracted -v --dump-table
	md5sum extracted

atest: huff anomaly.txt
	$(call check_file,anomaly.txt)

ltest: huff large.txt
	$(call check_file,large.txt)

stest: huff sfile
	$(call check_file,sfile)

etest: huff efile
	$(call check_file,efile)

large.txt:
	python large.py

anomaly.txt:
	python anomaly.py

sfile:
	echo "123" > sfile

efile:
	touch efile

.PHONY: clean tests
clean:
	rm -rf huff *.o *.dot core* *log *.i *.s callgrind.out.* cachegrind.out.* arch extracted vgcore*
	make -C ugeneric clean > /dev/null

tests: atest ltest stest

tree:
	ccomps -x tree.dot | dot | gvpack | neato $(DOTOPT) -n2 -s -Tpng -o tree.png

tags:
	ctags -R .

$(LIBUGENERIC): ugeneric
	make -C ugeneric lib

ugeneric:
	git clone https://github.com/vslapik/ugeneric.git

print-%:
	@echo $* = $($*)

test-%: huff $*
	$(call check_file,$*)

.PHONY: tags clean tree
