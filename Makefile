all: client server

client: client.cpp
	g++ client.cpp -std=c++11 -pthread -o client

server: server.cpp
	g++ server.cpp -std=c++11 -pthread -o server