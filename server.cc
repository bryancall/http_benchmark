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

using namespace std;

void *doprocessing(void *sock);

ostringstream response_keep_alive;
ostringstream response_close;
int content_length = 1024;
uint32_t keep_alive = 0;

void usage(string_view program) {
  cout << "USAGE: " << program << " [-k 10] [-p 8080] [-s 1024]" << endl
    << endl
    << "\t-k\tnumber of keep-alive response to do before closing connection" << endl
    << "\t-p\tport to listen on" << endl
    << "\t-s\tcontent size of the response in bytes" << endl;
}

int main(int argc, char **argv) {
  int sockfd;
  uint16_t portno = 8080;
  socklen_t client_address_length = 0;
  struct sockaddr_in server_address, client_address;

  // get command line arguments
  int c;
  while ((c = getopt(argc, argv, "k:p:s:h")) != -1) {
    switch (c) {
    case 'k':
      keep_alive = (uint32_t)atoi(optarg);
      break;
    case 'p':
      portno = (uint16_t)atoi(optarg);
      break;
    case 's':
      content_length = atoi(optarg);
      break;
    case 'h':
    default:
      usage(argv[0]);
      exit(1);
    }
  }

  // print how it is running
  printf("\nRunning with port: %d and content length: %d\n", portno,
         content_length);

  // create a socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if (sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }

  // set SO_REUSEADDR
  int enable = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
  }

  // initialize socket
  bzero((char *)&server_address, sizeof(server_address));

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(portno);

  // bind to host adddress
  if (bind(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
    perror("ERROR on binding");
    exit(1);
  }

  // create responses
  response_keep_alive << "HTTP/1.1 200 OK\r\n" << "Connection: Keep-Alive\r\n" << "Content-Length: " << content_length << "\r\n\r\n";
  for (auto i = content_length - 2; i > 0; --i) {
    response_keep_alive << 'x';
  }
  response_keep_alive << "@r";
  response_close << "HTTP/1.1 200 OK\r\n" << "Connection: Close\r\n" << "Content-Length: " << content_length << "\r\n\r\n";
  for (auto i = content_length - 2; i > 0; --i) {
    response_close << 'x';
  }
  response_close << "@r";

  // listen for clients
  listen(sockfd, 5);
  client_address_length = sizeof(client_address);

  signal(SIGPIPE, SIG_IGN);

  while (1) {
    int newsockfd = accept(sockfd, (struct sockaddr *)&client_address, &client_address_length);

    if (newsockfd < 0) {
      perror("ERROR on accept");
      exit(1);
    }

    // create thread
    pthread_t thread;
    pthread_create(&thread, nullptr, doprocessing, (void *)(long)newsockfd);
    pthread_detach(thread);
  }
}

void *doprocessing(void *ptr) {
  int sock = (int)(long)ptr;
  char buffer[4096];
  ssize_t request_length = 0;
  
  int64_t count = 0;
  while (1) {
    ++count;

    // set the respose keep-alive or not
    ostringstream *response = &response_keep_alive;
    if (keep_alive > 0 && count >= keep_alive) {
      response = &response_close;
    }

    ssize_t bytes_read = read(sock, buffer, 4095);
    buffer[bytes_read] = 0;

    // error or client closed the connection
    if (bytes_read <= 0) {
      if (bytes_read < 0) {
        perror("ERROR reading from socket");
      }
      close(sock);
      return nullptr;
    }

    // simple parsing
    if (!(buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T' && buffer[bytes_read-1] == '\n' &&
        buffer[bytes_read-2] == '\r' && buffer[bytes_read-3] == '\n' && buffer[bytes_read-4] == '\r')) {
      cerr << "Can't parse request" << endl;
    }

    if (request_length == 0) {
      request_length = bytes_read;
    } else {
      assert(request_length == bytes_read);
    }

    ssize_t bytes_written = 0;
    int loop = 10;
    do {
      bytes_written += write(sock, response->str().c_str() + bytes_written, response->str().size() - bytes_written);
      if (--loop < 0) {
        cerr << "Couldn't write response in 10 tries" << endl;
        close(sock);
        return nullptr;
      }

      if (bytes_written < 0) {
        perror("ERROR writing to socket");
        close(sock);
        return nullptr;
      }
    } while (response->str().size() > (size_t)bytes_written);

    if (keep_alive > 0 && count >= keep_alive) {
      close(sock);
      return nullptr;
    }
  }
}
