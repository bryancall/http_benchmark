#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <assert.h>
#include <string>
#include <sstream>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
using namespace std;

void *doprocessing(void *sock);

ostringstream request_keep_alive;
ostringstream request_close;
uint32_t keep_alive = 0;
struct sockaddr_in server_address;
atomic<uint64_t> total_number_of_requests = 0;
std::chrono::time_point<std::chrono::high_resolution_clock> start;

void usage(string_view program) {
  cout << "USAGE: " << program << " [-k 10] [-p 8080] [-s 127.0.0.1] [-t 30]" << endl
    << endl
    << "\t-k\tnumber of keep-alive response to do before closing connection" << endl
    << "\t-p\tport to connect to" << endl
    << "\t-h\thost to connect to" << endl
    << "\t-t\tnumber of threads" << endl;
}

int main(int argc, char **argv) {
  uint16_t portno = 8080;
  const char *server_ip = "127.0.0.1";
  uint32_t number_of_threads = 30;
  
  // get command line arguments
  int c;
  while ((c = getopt(argc, argv, "k:p:s:t:h")) != -1) {
    switch (c) {
    case 'k':
      keep_alive = (uint32_t)atoi(optarg);
      break;
    case 'p':
      portno = (uint16_t)atoi(optarg);
      break;
    case 's':
      server_ip = optarg;
      break;
    case 't':
      number_of_threads = atoi(optarg);
      break;
    case 'h':
    default:
      usage(argv[0]);
      exit(1);
    }
  }

  // print how it is running
  cout << "Running with server ip: " << server_ip << " and port: " << portno << endl;

  // initialize socket
  bzero((char *)&server_address, sizeof(server_address));

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(portno);

  if(inet_pton(AF_INET, server_ip, &server_address.sin_addr)<=0) {
    perror("error with inet_pton");
    exit(1);
  } 

  // create requests
  request_keep_alive << "GET /200_no_cache_0.php HTTP/1.1\r\n" << "Connection: Keep-Alive\r\n" << "\r\n";
  request_close << "HTTP/1.1 200 OK\r\n" << "Connection: Close\r\n" "\r\n";

  // set the starting time
  start = chrono::high_resolution_clock::now();

  // create threads
  pthread_t thread[number_of_threads];
  for (uint32_t i = 0; i < number_of_threads; ++i) {
    pthread_create(&thread[i], nullptr, doprocessing, (void*)&i);
  }
  for (uint32_t i = 0; i < number_of_threads; ++i) {
    pthread_join(thread[i], nullptr);
    cout << "thread exited number: " << i << endl;
  }
  cout << "exit" << endl;
  return(0);
}

void *doprocessing(void *ptr) {
  uint32_t thread_number = *(uint32_t*)ptr;
  cout << "thread: " << thread_number << endl;
  char buffer[4096];
  ssize_t request_length = 0;
  
  // create a socket
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }

  // connect to the server
  if (connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) != 0) {
    perror("ERROR connecting to server");
    exit(1);  
  }

  int64_t count = 0;
  while (1) {
    ++count;

    // set the respose keep-alive or not
    ostringstream *request = &request_keep_alive;
    if (keep_alive > 0 && count >= keep_alive) {
      request = &request_close;
    }

    // write the request
    ssize_t bytes_written = 0;
    int loop = 10;
    do {
      bytes_written += write(sockfd, request->str().c_str() + bytes_written, request->str().size() - bytes_written);
      if (--loop < 0) {
        cerr << "Couldn't write response in 10 tries" << endl;
        close(sockfd);
        return nullptr;
      }

      if (bytes_written < 0) {
        perror("ERROR writing to socket");
        close(sockfd);
        return nullptr;
      }
    } while (request->str().size() > (size_t)bytes_written);

    ssize_t bytes_read = read(sockfd, buffer, 4095);
    buffer[bytes_read] = 0;

    // error or client closed the connection
    if (bytes_read <= 0) {
      if (bytes_read < 0) {
        perror("ERROR reading from socket");
      }
      close(sockfd);
      return nullptr;
    }

    // simple parsing
    bool read_body = false;
    if (!(buffer[0] == 'H' && buffer[1] == 'T' && buffer[2] == 'T' && buffer[3] == 'P' &&
        buffer[4] == '/' && buffer[5] == '1')) {
      cerr << "Can't parse response" << buffer << endl;
      return nullptr;
    } else {
      while (read_body == false) {
        // read until terminating body
        if (buffer[bytes_read - 1] == 'r' && buffer[bytes_read - 2] == '@') {
          read_body = true;
        } else {
            bytes_read = read(sockfd, buffer, 4095);
        }
      }

      if (++total_number_of_requests % 1000000 == 0) {
        auto end = chrono::high_resolution_clock::now();
        auto diff = chrono::duration<double>(end - start).count();
        cout << " requests per second: " << (int)((double)1000000 / diff) << endl;
        start = end;
      }
    }

    if (request_length == 0) {
      request_length = bytes_read;
    } else if (request_length != bytes_read) {
      cout << "request_length != bytes_read - request_length: " << request_length << " bytes_read: " << bytes_read << endl;
      // assert(request_length == bytes_read);
    }

    if (keep_alive > 0 && count >= keep_alive) {
      close(sockfd);
      return nullptr;
    }
  }
}
