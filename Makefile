CXXFLAGS = -std=c++11 -Wall -fno-rtti

# Determine the plugin-dir and add it to the flags
PLUGINDIR=$(shell $(CXX) -print-file-name=plugin)
CXXFLAGS += -I$(PLUGINDIR)/include

all: cph.so

cph.so: cph.o
	$(CXX) -shared -o $@ $<

cph.o : cph.cc debug-utils.h
	$(CXX) $(CXXFLAGS) -fPIC -c -o $@ $<

clean:
	rm -f cph.o cph.so test.out

test: cph.so test.c
	$(CC) -fplugin=./cph.so test.c -o test.out

.PHONY: all clean check
