#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;


/*
 * Takes a stream (fd), and writes 404 HTTP response
 */
void send_not_found_response(int fd) {
  http_start_response(fd, 404);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  http_send_string(fd, "<center><h1>404 Not Found</h1><hr></center>");
}

/*
 * Takes a stream (fd), and writes 500 HTTP response
 */
void send_error_response(int fd) {
  http_start_response(fd, 500);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  http_send_string(fd, "<center><h1>Server Error</h1><hr></center>");
}

/*
 * Takes a stream (fd), and a valid file path of a directory
 * and responds with a file listing
 */
void send_dir_listing(int fd, char *path) {
  struct dirent *d;
  int ret;
  DIR *dir = opendir(path);
  char formatted_path[strlen(path) + 1024];
  if (!dir) {
    send_error_response(fd);
    return;
  }

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  http_send_string(fd, "<center><h1>Directory Listing</h1><hr></center>");

  sprintf(formatted_path, "<h2>contents of %s </h2>", path);
  http_send_string(fd, formatted_path);

  while((d = readdir(dir))) {

    sprintf(formatted_path, "<h3><a href=\"%s\">%s</a></h3>", d->d_name, d->d_name);
    http_send_string(fd, formatted_path);
  }


  ret = closedir(dir);
  if (ret) {
    fprintf(stderr, "Error in closedir: error %d: %s\n", errno, strerror(errno));
  }
}

/*
 * Takes a stream (fd), and a valid file path
 * and responds with file data and mimetype
 */
void send_file_response(int fd, char *path) {
  struct stat sb;
  int ret;
  FILE *file;
  int READ_SIZE = 1024;
  char buf[READ_SIZE];
  char file_length[64]; // File size must be less than 10^64 bytes (seems safe...)
  int nr;
  char *mimetype = http_get_mime_type(path);

  ret = stat(path, &sb);
  if (ret) {
    fprintf(stderr, "Error in stat: error %d: %s\n", errno, strerror(errno));
    send_error_response(fd);
    return;
  }

  fprintf(stdout, "File size: %d\n", (int) sb.st_size);
  sprintf(file_length, "%ld", sb.st_size);

  file = fopen(path, "r");
  if (!file) {
    fprintf(stderr, "Error in fopen: error %d: %s\n", errno, strerror(errno));
    send_error_response(fd);
    return;
  }

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", mimetype);
  http_send_header(fd, "Content-Length", file_length);
  http_end_headers(fd);

  while ((nr = fread(buf, sizeof(char), READ_SIZE, file)) > 0) {
    http_send_data(fd, buf, nr * sizeof(char));
  }

  fclose(file);
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {
  struct stat sb;
  int ret;

  struct http_request *request = http_request_parse(fd);

  char relative_path[PATH_MAX] = "./files";
  char index_path[PATH_MAX];

  strncat(relative_path, request->path, PATH_MAX - 19); // 19 to support 8 bytes from  "./files" and 11 bytes from "/index.html"

  ret = stat(relative_path, &sb);

  if (ret) {
    if (errno == ENOENT) {
      // Respond with 404
      fprintf(stdout, "Sending not found response for path: %s\n", relative_path);
      send_not_found_response(fd);
    } else {
      fprintf(stderr, "Cannot stat path: error %d: %s\n", errno, strerror(errno));
      send_error_response(fd);
    }
  } else if (S_ISDIR(sb.st_mode)) {
    fprintf(stdout, "directory\n");
    fprintf(stdout, "relative_path: %s\n", relative_path);

    strcpy(index_path, relative_path);
    strcat(index_path, "/index.html");

    fprintf(stdout, "index path to check is: %s\n", index_path);
    ret = stat(index_path, &sb);

    if (ret) {
      if (errno == ENOENT) {
        send_dir_listing(fd, relative_path);
      } else {
        fprintf(stderr, "Cannot stat path: error %d: %s\n", errno, strerror(errno));
        send_error_response(fd);
      }
    } else {
      if (S_ISREG(sb.st_mode)) {
        fprintf(stdout, "Sending file response for path: %s\n", index_path);
        send_file_response(fd, index_path);
      }
    }

  } else if (S_ISREG(sb.st_mode)) {
    // respond with file from relative_path
    fprintf(stdout, "Regular file\n");
    fprintf(stdout, "Sending file response for path: %s\n", relative_path);
    send_file_response(fd, relative_path);
  }

  free(request->path);
  free(request->method);
  free(request);
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;

  }

  /* 
  * TODO: Your solution for task 3 belongs here! 
  */
}


void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    // TODO: Change me?
    request_handler(client_socket_number);
    close(client_socket_number);

  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
