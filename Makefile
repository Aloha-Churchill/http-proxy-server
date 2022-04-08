COMPILER=gcc
FLAGS=-g -Wall
#RM_COMMAND := $(shell cd cached/ && rm -v !(README.md) && cd ..)

run: proxy
	./proxy 8080 10

all: proxy.c
	$(COMPILER) $(FLAGS) -o proxy proxy.c -lcrypto -lssl

clean:
	rm proxy
	rm -rf cached/*
	touch cached/README.md
	echo "## Cached directory" > cached/README.md
	echo "For this code to work, there must be cached directory folder." >> cached/README.md

	