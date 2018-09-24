all: indexing_server peer logging env_dirs test_data

indexing_server: indexing_server.cpp
	g++ indexing_server.cpp -std=c++11 -pthread -o indexing_server

peer: peer.cpp
	g++ peer.cpp -std=c++11 -pthread -o peer

logging:
	mkdir logs/
	mkdir logs/peers/
	mkdir logs/indexing_server/

env_dirs:
	mkdir peers/
	mkdir peers/p1/
	mkdir peers/p2/
	mkdir peers/p3/

eval_env_dirs:
	mkdir peers/p4/
	mkdir peers/p5/
	mkdir peers/p6/
	mkdir peers/p7/
	mkdir peers/p8/
	mkdir peers/p9/
	mkdir peers/p10/

test_data:
	cp ../data/p1/* peers/p1/
	cp ../data/p2/* peers/p2/
	cp ../data/p3/* peers/p3/

eval_test_data:
	cp ../data/p4/* peers/p4/
	cp ../data/p5/* peers/p5/
	cp ../data/p6/* peers/p6/
	cp ../data/p7/* peers/p7/
	cp ../data/p8/* peers/p8/
	cp ../data/p9/* peers/p9/
	cp ../data/p10/* peers/p10/

clean:
	rm indexing_server peer
	rm -rf peers/
	rm -rf logs/