#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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


void send_info_message(int fd, const char* message){
  char message_template[4096];

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  sprintf(message_template, "<center>"
                            "<h1>Welcome to httpserver!</h1>"
                            "<hr>"
                            "<p>%s.</p>"
                            "</center>", message);
  http_send_string(fd, message_template);
}


void send_not_found(int fd, const char* requested_file){
  char message_template[4096];

  http_start_response(fd, 404);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  sprintf(message_template, "<center>"
                            "<h1>Welcome to httpserver!</h1>"
                            "<hr>"
                            "<p>Sorry, %s can not be found.</p>"
                            "</center>", requested_file);
  http_send_string(fd, message_template); 
}

void list_directory(int fd, const char* dir_name){
  DIR* cur_dir = opendir(dir_name);
  struct dirent* dir_entry = readdir(cur_dir);

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);

  while(dir_entry != NULL){
    char href_template[4096];

    sprintf(href_template, "<a href=%s>%s</a>\n", dir_entry->d_name, dir_entry->d_name);
    
    http_send_string(fd, href_template);
    dir_entry = readdir(cur_dir);
  }

  closedir(cur_dir);
}

void send_file(int fd, int requested_fd, const char* requested_file_name){
  char buffer[4096];
  ssize_t bytes_read;

  http_start_response(fd, 200);
  char length[1024];
  sprintf(length, "%d", (int)lseek(requested_fd, 0, SEEK_END));
  lseek(requested_fd, 0, SEEK_SET);
  http_send_header(fd, "Content-Type", http_get_mime_type((char*)requested_file_name));
  http_send_header(fd, "Content-Length", length);
  http_end_headers(fd);

  bytes_read = read(requested_fd, buffer, 4096);
  while(bytes_read > 0){
    http_send_data(fd, buffer, bytes_read);
    bytes_read = read(requested_fd, buffer, 4096);
  }
  
  close(requested_fd);
}


int is_a_directory(const char* path){
  struct stat path_stat;
  stat(path, &path_stat);
  return S_ISDIR(path_stat.st_mode);
}

int is_a_file(const char* path){
  struct stat path_stat;
  stat(path, &path_stat);
  return S_ISREG(path_stat.st_mode);
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
  int requested_fd;

  struct http_request *request = http_request_parse(fd);
  if(request == NULL) {
    close(fd);
    return;
  }

  if(strcmp(request->method, "GET") != 0){
    send_info_message(fd, "Currently only GET method is supported");
    close(fd);
    return;
  }

  char requested_path[strlen(server_files_directory) + strlen(request->path) + 1];
  strcpy(requested_path, server_files_directory);
  strcat(requested_path, request->path);
  
  if(is_a_directory((const char*)requested_path)){
    strcat(requested_path, "index.html");
    requested_fd = open(requested_path, O_RDONLY);
    if (requested_fd == -1){
      requested_path[strlen(requested_path) - strlen("index.html")] = '\0';

      strcat(requested_path, "/");
      list_directory(fd, requested_path);

      close(fd);
      return;
    }
    send_file(fd, requested_fd, requested_path);
  }else if (is_a_file(requested_path)){
    requested_fd = open(requested_path, O_RDONLY);
    if(requested_fd == -1){
      send_not_found(fd, request->path);
      close(fd);
      return;
    }
    send_file(fd, requested_fd, requested_path);
  }else{
    send_not_found(fd, request->path);
    close(fd);
    return;
  }

  close(fd);
}

int ends_with(const char* c1, const char* c2, int length1, int length2){
  if(length1 < length2) return 0;
  for(int i = 1; i <= length2; i++){
    if(c1[length1 - i] != c2[length2 - i]){
      return 0;
    }
  }
  return 1;
}

void* proxy_worker(void* aux){
  int from = *(int*)aux;
  int to = *((int*)aux + 1);

  char buffer[(1 << 16)];
  int bytes_read = read(from, buffer, (1 << 16));
  while(bytes_read > 0){
    int offset = 0;
    int bytes_written = write(to, buffer, bytes_read);
    if(bytes_written < 0){
      close(from);
      free(aux);

      return NULL;
    }
    offset += bytes_written;
    while(offset < bytes_read){
      bytes_written = write(to, buffer + offset, bytes_read - offset + 1);
      if(bytes_written < 0){
        close(from);
        free(aux);

        return NULL;
      }
      offset += bytes_written; 
    }

    bytes_read = read(from, buffer, (1 << 16));          
  }

  close(from);
  free(aux);

  return NULL;
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
  pthread_t* thread_a = malloc(sizeof(pthread_t));
  pthread_t* thread_b = malloc(sizeof(pthread_t));
  int* a_to_b = malloc(sizeof(int) * 2);
  int* b_to_a = malloc(sizeof(int) * 2);
  a_to_b[0] = fd;
  b_to_a[0] = client_socket_fd;
  a_to_b[1] = client_socket_fd;
  b_to_a[1] = fd;
  pthread_create(thread_a, NULL, proxy_worker, a_to_b);
  pthread_create(thread_b, NULL, proxy_worker, b_to_a);
}

void* worker_routine(void* aux){
    void(*request_handler)(int) = aux;

    while(1){
      int client_socket_number  = wq_pop(&work_queue);
      request_handler(client_socket_number);
    }
    
    return NULL;
}


void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
  for(int i = 0; i < num_threads; i++){
    pthread_t* thread = malloc(sizeof(pthread_t));
    pthread_create(thread, NULL, worker_routine, request_handler);
  }
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
    wq_push(&work_queue, client_socket_number);

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
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
  wq_init(&work_queue);
  num_threads = 1;

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
