COMPILER=gcc
FLAGS=-g -Wall

run: server
	./server 8888

all: server.c
	$(COMPILER) $(FLAGS) -o server server.c

clean:
	rm server