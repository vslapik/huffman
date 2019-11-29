DOTOPT = -Gratio=fill
CFLAGS = -Ofast -march=native -g -std=c11 -Wall -Iugeneric/
#CFLAGS = -O0 -march=native -g -std=c11 -Wall -Iugeneric/
TARGET = huff
LIBUGENERIC = ugeneric/libugeneric.a

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS) Makefile
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(LIBUGENERIC) $(OBJECTS)
	$(CC) $(OBJECTS) -g -rdynamic $(LIBUGENERIC) -o $@

define check_file
    ./huff $(1) -c arch -v $(CLI_AUX)
    ./huff arch -x extracted -v $(CLI_AUX)
    md5sum $(1) extracted
    #@-rm -rf extracted
    #@-rm -rf arch
endef

ctest: huff large.test
	./huff large.test -c arch -v --dump-table
	md5sum arch

xtest: huff large.test
	./huff arch -x extracted -v --dump-table
	md5sum extracted

large.txt:
	python large.py

anomaly.txt:
	python anomaly.py

clean:
	rm -rf huff *.o *.dot core* *log *.i *.s callgrind.out.* cachegrind.out.* arch extracted vgcore*
	make -C ugeneric clean > /dev/null

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
