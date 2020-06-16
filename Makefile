GCC=clang
GFLAGS=-g

src = $(wildcard *.cpp)

watcher: $(src)
	$(GCC) $(GFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm watcher
	rm *.o
	
