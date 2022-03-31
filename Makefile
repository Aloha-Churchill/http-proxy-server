COMPILER=gcc
FLAGS=-g -Wall

run: proxy
	./proxy 8888

all: proxy.c
	$(COMPILER) $(FLAGS) -o proxy proxy.c

clean:
	rm proxy