/*
HTTP proxy server

Questions:
- Testing when browser defaults to https vs http
- Do we only want to cache plain text?

Testing website
http://www.testingmcafeesites.com/index.html
*/

#include "helpers.h"


void transform_cached_name(char* original_name, char* transformed_name){

    strcpy(transformed_name, "cached/");

    for(int i=0; i<strlen(original_name); i++){
        if(original_name[i] == '/'){
            transformed_name[i+7] = '\\';
        }
        else{
            transformed_name[i+7] = original_name[i];
        }
    }

}


/*
Function to send HTTP response to client.
*/
void handle_client(int fd, int port, int timeout) {
    char http_buf[REQUEST_SIZE];
    bzero(http_buf, REQUEST_SIZE);

    //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
    if(recv(fd, http_buf, REQUEST_SIZE, 0) < 0){
        error("Recieve failed\n");
    }

    // the http_buf gets modified by parsed commands, so need preserved_buffer
    char preserved_http_buf[REQUEST_SIZE];
    bzero(preserved_http_buf, REQUEST_SIZE);

    memcpy(preserved_http_buf, http_buf, REQUEST_SIZE);
    
    printf("Recieved: %s\n", http_buf);

    // parse request into 4 parts [[method],[url],[http version],[Host:],[requested host]]
    char* parsed_commands[5];
    int num_parsed = parse_commands(http_buf, parsed_commands);

    // get full pathname of file
    char pathname[PATHNAME_SIZE];
    bzero(pathname, PATHNAME_SIZE);

    // check if request is okay, if so, then we check if file is valid
    if(check_request(fd, parsed_commands, num_parsed) != -1){

        printf("Host to forward to: %s\n", parsed_commands[4]);
        struct addrinfo server_hints;
        struct addrinfo *res;

        // Set host/server address structure 
        memset(&server_hints, 0, sizeof(struct addrinfo));
        server_hints.ai_family = AF_UNSPEC;
        server_hints.ai_socktype = SOCK_STREAM;
        
        //check if server hostname was found using getaddrinfo
        int ret = getaddrinfo(parsed_commands[4], "80", &server_hints, &res);

        // if host name is invalid
        if(ret != 0){
            printf("Address was INVALID\n");
            dprintf(fd, "HTTP/1.1 404 Not Found\r\n");
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
        }

        // if host name is valid
        else{

            // if we already have requested page in cache
            FILE* cache_fp;
            char path[PATHNAME_SIZE];
            bzero(path, PATHNAME_SIZE);

            cache_fp = popen("ls cached/", "r");
            if (cache_fp == NULL){
                error("Could not open cache\n");
            }

            char transformed_name[PATHNAME_SIZE];
            bzero(transformed_name, PATHNAME_SIZE);

            transform_cached_name(parsed_commands[1], transformed_name);
            printf("TRANSFORMED NAME: %s\n", transformed_name);

            int found_match = 0;

            // loop through cached directory to find hit, delete files that are expired
            while(fgets(path, PATHNAME_SIZE, cache_fp) != NULL){
                // if there is a cache hit

                printf("REQUESTED NAME: %s \t\t PATH NAME: %s\n", transformed_name + 7, path);
                path[strcspn(path, "\n")] = 0;

                // now we actually need to read and then send file
                if(strcmp(path, transformed_name + 7) == 0){
                    // return the cached file and update timestamp
                    printf("FOUND MATCH IN CACHE, NOW CHECKING TO SEE IF EXPIRED\n");
                    
                    struct stat last_accessed;
                    stat(transformed_name, &last_accessed);
                    time_t time_last_accessed = mktime(localtime(&last_accessed.st_atime));
                    time_t current_time = time(NULL);

                    printf("Time file accessed: %ld\t\tCurrent time: %ld\n", time_last_accessed, current_time);
                    int time_difference = current_time-time_last_accessed;
                    printf("TIME DIFFERENCE IS: %d\n", time_difference);
                    if(time_difference > timeout){
                        printf("THIS FILE IS EXPIRED\n");
                        // we then want to remove the file from cached
                        if(remove(transformed_name) != 0){
                            error("Could not remove expired file from cache\n");
                        }

                    }

                    else{
                        printf("THIS FILE IS NOT EXPIRED, SERVING FILE\n");
                        found_match = 1;

                        FILE* send_cached_fp;
                        send_cached_fp = fopen(transformed_name, "r");
                        
                        fseek(send_cached_fp, 0L, SEEK_END);
                        int filesize = ftell(send_cached_fp);
                        fseek(send_cached_fp, 0L, SEEK_SET);

                        int num_sends = filesize/FILE_SIZE_PART + ((filesize % FILE_SIZE_PART) != 0); 
                        char file_contents[FILE_SIZE_PART];

                        for(int i=0; i < num_sends; i++){
                            bzero(file_contents, FILE_SIZE_PART);
                            int n = fread(file_contents, FILE_SIZE_PART, 1, send_cached_fp);
                            if(n < 0){
                                error("Error on reading file into buffer\n");
                            }
                            // just send remaining bytes
                            if(i == num_sends-1){
                                send(fd, file_contents, filesize % REQUEST_SIZE, 0);
                            }
                            else{
                                send(fd, file_contents, REQUEST_SIZE, 0);
                            } 
                        }
                        fclose(send_cached_fp);
                    }
                }
                

            }
            fclose(cache_fp);

            if(found_match == 0){

                printf("DID NOT FIND NAME IN CACHE, SENDING FILE AND CREATING CACHE ENTRY\n");
                // creating socket for host
                int sockfd_host = -1;

                sockfd_host = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
                // sockfd_host = 3
                if(sockfd_host == -1){
                    error("Could not create socket");
                }
                printf("Created Socket\n");


                if(connect(sockfd_host, res->ai_addr, res->ai_addrlen) == -1){
                    close(sockfd_host);
                    error("Could not connect to host\n");
                }

                printf("Connected\n");

                // need to also put in way to make sure it has sent correct number of bytes
                printf("HTTP Sending: %s\n", preserved_http_buf);
                if(send(sockfd_host, preserved_http_buf, REQUEST_SIZE, 0) < 0){
                    error("Send to host failed\n");
                }
                
                char recvbuf[REQUEST_SIZE];
                bzero(recvbuf, REQUEST_SIZE);

                // creating file for cache
                FILE* create_fp;

                create_fp = fopen(transformed_name, "w");

                if(create_fp == NULL){
                    error("Could not create file to write cached document to\n");
                }

                //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
                int recv_res = recv(sockfd_host, recvbuf, REQUEST_SIZE, 0) < 0;
                printf("RECIEVED FROM HOST result: %s\n", recvbuf);
                if(recv_res < 0){
                    error("Recieve from host failed\n");
                }

                if(send(fd, recvbuf, REQUEST_SIZE, 0) < 0){
                    error("Send to client failed\n");
                }

                if(fwrite(recvbuf, REQUEST_SIZE, 1, create_fp) < 0){
                    error("Could not write to cached file\n");
                }

                
                fclose(create_fp);
                // then we want to send what we just recieved in the proxy to the original client
                freeaddrinfo(res);
                close(sockfd_host);
            }

        }

    }

}

/*
Function to initialize variables, start server, and accept client connections
Code modified from Beej's Guide to Network Programming
*/
void start_server(int *proxy_socket, int port, int timeout) {

    // get socket file descriptor
    *proxy_socket = socket(PF_INET, SOCK_STREAM, 0);
 
    // Set up socket so can re-use address
    int socket_option = 1;
    if(setsockopt(*proxy_socket, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option)) == -1){
        error("Could not set socket option\n");
    }
 
    // Set proxy address structure 
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET; //internet domain
    proxy_address.sin_addr.s_addr = INADDR_ANY; 
    proxy_address.sin_port = htons(port); //use host to network so that encoding is correct (big vs little endian)
 
    // bind to socket to associate it with port
    if(bind(*proxy_socket, (struct sockaddr *) &proxy_address, sizeof(proxy_address)) == -1){
        close(*proxy_socket);
        error("Could not bind to port\n");
    }
 
    // Proxy starts listening on port. Accept as many as BACKLOG connections
    if(listen(*proxy_socket, BACKLOG) == -1){
        error("Listen failed\n");
    }

    // registering the SIGCHLD handler to reap children processes when they exit
    struct sigaction sa;
	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	// handle SIGCHLD, which is signal that child sends to parent when it terminates
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
 
    // initializing client (who calls proxy) socket variables
    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_socket;
 
    // Proxy continues to accept connections until Ctrl+C is pressed
    while (1) {
        // Accept the client socket.
        client_socket = accept(*proxy_socket, (struct sockaddr *) &client_address, (socklen_t *) &client_address_length);
        if(client_socket == -1){
            error("Refused to accept connection\n");
        }

        // create child process to handle request and let parent continue to listen for new connections
        if(!fork()){
            
            close(*proxy_socket);
            
            // function to send response
            handle_client(client_socket, port, timeout);
            close(client_socket);
            exit(0);
        }
        
        close(client_socket);
    }
 
    // Shut down the server.
    shutdown(*proxy_socket, SHUT_RDWR);
    close(*proxy_socket);
}


int main(int argc, char **argv) {

    if(argc !=3){
        error("Incorrect number of arguments\t Correct format: ./server [PORTNO]\n");
    }
    int port = atoi(argv[1]);
    int timeout = atoi(argv[2]);

    // if user types Ctrl+C, server shuts down
    signal(SIGINT, exit_handler);
    start_server(&server_fd, port, timeout);

}