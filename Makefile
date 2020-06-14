GCC=clang
GLAGS=-g

src = $(wildcard *.cpp)
obj = $(src:.cpp=.o)

watcher: $(obj)
	$(GCC) $(GLAGS) -o $@ $^

.PHONY: clean
clean:
	rm watcher
	rm *.o
	
