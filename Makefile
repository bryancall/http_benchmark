all:
	g++ -std=c++17 -O3 -g -lpthread client.cc -o client
	g++ -std=c++17 -O3 -g -lpthread server.cc -o server

clean:
	rm -f client server
