CC = gcc
CFLAGS = -Wall -Wextra -g

all: elfinfo elfinstrument profiler.so

elfinfo: elfinfo.c
	$(CC) $(CFLAGS) -o elfinfo elfinfo.c

elfinstrument: elfinstrument.c
	$(CC) $(CFLAGS) -o elfinstrument elfinstrument.c

profiler.so: profiler.c
	$(CC) -shared -fPIC $(CFLAGS) -o profiler.so profiler.c -ldl

clean:
	rm -f elfinfo elfinstrument profiler.so

.PHONY: all clean
