COMPILER=gcc
FLAGS=-g -Wall

run: proxy
	./proxy 8888 60

all: proxy.c
	$(COMPILER) $(FLAGS) -o proxy proxy.c

clean:
	rm proxy
	rm -rf cached/*