CC=gcc
CFLAGS=-Wall -I.
DEPS = remoteServer.h remoteClient.h
OBJ = remoteServer.o remoteClient.o 

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: remoteServer remoteClient

remoteServer: remoteServer.o
	$(CC) -o $@ $^ $(CFLAGS)

remoteClient: remoteClient.o
	$(CC) -o $@ $^ $(CFLAGS)

server: remoteServer
	./remoteServer 9002 10

client: remoteClient
	./remoteClient localhost 9002 2300 input

clean:
	$(RM) count *.o remoteServer remoteClient output*

fewclients: remoteClient
	./remoteClient localhost 9002 2300 input &
	./remoteClient localhost 9002 2301 input &
	./remoteClient localhost 9002 2302 input

manyclients: remoteClient
	./remoteClient localhost 9002 2300 input | grep Exiting &
	./remoteClient localhost 9002 2301 input | grep Exiting &
	./remoteClient localhost 9002 2302 input | grep Exiting &
	./remoteClient localhost 9002 2303 input | grep Exiting &
	./remoteClient localhost 9002 2304 input | grep Exiting &
	./remoteClient localhost 9002 2305 input | grep Exiting &
	./remoteClient localhost 9002 2306 input | grep Exiting &
	./remoteClient localhost 9002 2307 input | grep Exiting &
	./remoteClient localhost 9002 2308 input | grep Exiting &
	./remoteClient localhost 9002 2309 input | grep Exiting &
	./remoteClient localhost 9002 2310 input | grep Exiting &
	./remoteClient localhost 9002 2311 input | grep Exiting &
	./remoteClient localhost 9002 2312 input | grep Exiting &
	./remoteClient localhost 9002 2313 input | grep Exiting &
	./remoteClient localhost 9002 2314 input | grep Exiting &
	./remoteClient localhost 9002 2315 input | grep Exiting &
	./remoteClient localhost 9002 2316 input | grep Exiting &
	./remoteClient localhost 9002 2317 input | grep Exiting &
	./remoteClient localhost 9002 2318 input | grep Exiting &
	./remoteClient localhost 9002 2319 input | grep Exiting
