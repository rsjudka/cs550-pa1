all: indexing_server peer env_dirs test_data

indexing_server: indexing_server.cpp
	g++ indexing_server.cpp -std=c++11 -pthread -o indexing_server

peer: peer.cpp
	g++ peer.cpp -std=c++11 -pthread -o peer

env_dirs:
	mkdir peers/
	mkdir peers/p1/
	mkdir peers/p2/
	mkdir peers/p3/

	mkdir logs/
	mkdir logs/peers/
	mkdir logs/indexing_server/

test_data:
	cp ../data/p1/* peers/p1/
	cp ../data/p2/* peers/p2/
	cp ../data/p3/* peers/p3/

clean:
	rm indexing_server peer
	rm -rf peers/
	rm -rf logs/