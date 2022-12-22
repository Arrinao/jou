LLVM_CONFIG ?= llvm-config-11

SRC := $(wildcard src/*.c)

CFLAGS += -Wall -Wextra -Wpedantic -std=c11
CFLAGS += -g
CFLAGS += $(shell $(LLVM_CONFIG) --cflags)
LDFLAGS += $(shell $(LLVM_CONFIG) --ldflags --libs)

obj/%.o: src/%.c $(wildcard src/*.h)
	mkdir -vp obj && $(CC) -c $(CFLAGS) $< -o $@

all: newlangc compile_flags.txt

# point clangd to the right include folder so i don't get red squiggles in my editor
compile_flags.txt:
	echo "-I$(shell $(LLVM_CONFIG) --includedir)"  > compile_flags.txt

newlangc: $(SRC:src/%.c=obj/%.o)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rvf obj newlangc
