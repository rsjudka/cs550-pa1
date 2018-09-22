all: peer indexing_server

peer: peer.cpp
	g++ peer.cpp -std=c++11 -pthread -o peer

indexing_server: indexing_server.cpp
	g++ indexing_server.cpp -std=c++11 -pthread -o indexing_server