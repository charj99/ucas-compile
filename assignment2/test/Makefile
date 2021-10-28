SRC = $(wildcard *.c)
BC = $(patsubst %.c, %.bc, $(SRC))
LL = $(patsubst %.c, %.ll, $(SRC))

bc: $(BC)

%.bc: %.c
	clang -emit-llvm -c -g $< -o $@

ll: $(LL)

%.ll: %.c
	clang -emit-llvm -S -g $< -o $@


.PONHY: clean echo ll bc

clean:
	rm -rf $(BC)
	rm -rf $(LL)

echo:
	@echo "SRC = $(SRC)"
	@echo "BC = $(BC)"
	@echo "LL = $(LL)"
