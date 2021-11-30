SRC = $(wildcard *.c)
LL = $(patsubst %.c, %.ll, $(SRC))
BC = $(patsubst %.c, %.bc, $(SRC))
CFLAGS = -g -O0

all: $(BC)

%.bc: %.c
	clang -emit-llvm $(CFALGS) -c $< -o $@ 

ll: $(LL)

%.ll: %.c
	clang -emit-llvm $(CFLAGS) -S $< -o $@

.PHONY: clean ll echo

clean:
	rm -rf $(BC) $(LL)

echo:
	@echo SRC = $(SRC)
	@echo LL = $(LL)
	@echo BC = $(BC)
