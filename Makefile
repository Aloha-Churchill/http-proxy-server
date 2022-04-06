COMPILER=gcc
FLAGS=-g -Wall

run: proxy
	./proxy 8888 10

all: proxy.c
	$(COMPILER) $(FLAGS) -o proxy proxy.c -lcrypto -lssl

clean:
	rm proxy
	rm -rf cached/*