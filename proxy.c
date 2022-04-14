/*
HTTP proxy server

Test with http://nginx.org/en/
*/

#include "helpers.h"


/*
Function for proxy to get response from server and then send HTTP response to client.
*/
void handle_client(int fd, int port, int timeout) {
    char http_buf[REQUEST_SIZE];
    bzero(http_buf, REQUEST_SIZE);

    // recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
    if(recv(fd, http_buf, REQUEST_SIZE, 0) < 0){
        error("Recieve failed\n");
    }

    // the http_buf gets modified by parsed commands, so need preserved_buffer to forward to client
    char preserved_http_buf[REQUEST_SIZE];
    bzero(preserved_http_buf, REQUEST_SIZE);
    memcpy(preserved_http_buf, http_buf, REQUEST_SIZE);
    
    printf("Recieved: %s\n", http_buf);

    // parse request into 4 parts [[method],[uri],[http version],[requested host]]
    char* parsed_commands[5];
    int num_parsed = parse_commands_V2(http_buf, parsed_commands);

    // need copies of the uri because passing to future functions changes the original
    char* copy_uri = strdup(parsed_commands[1]);
    char* copy_copy_uri = strdup(parsed_commands[1]);

    for(int i=0; i< 5; i++){
        printf("Parsed commands: %s\n", parsed_commands[i]);
    }

    // get full pathname of file
    char pathname[PATHNAME_SIZE];
    bzero(pathname, PATHNAME_SIZE);

    // check if request is ok
    int hostname_exists = -1;
    struct addrinfo *res = NULL;
    struct addrinfo server_hints;

    // Set host/server address structure 
    memset(&server_hints, 0, sizeof(struct addrinfo));
    server_hints.ai_family = AF_UNSPEC;
    server_hints.ai_socktype = SOCK_STREAM;
    
    // check if server hostname was found using getaddrinfo
    int ret;

    // if a port number is not specified
    if(parsed_commands[4] == NULL){
        ret = getaddrinfo(parsed_commands[3], "http", &server_hints, &res);
    }
    // if a port number was specified
    else{
        ret = getaddrinfo(parsed_commands[3], parsed_commands[4], &server_hints, &res);
    }

    // if host name is invalid instead this should be NULL
    if(ret != 0){
        printf("Address was INVALID\n");
        dprintf(fd, "HTTP/1.1 404 Not Found\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
    }
    else{
        hostname_exists = 0;
    }

    // check if request is okay, if so, then we check if file is valid, else send back HTTP error to client
    if(check_request(fd, parsed_commands, num_parsed) != -1){

        // if the host to forward to could be resolved
        if(hostname_exists == 0){

            // transformed name is where hash of url is stored
            char transformed_name[33+7];
            bzero(transformed_name, 33+7);
            strcpy(transformed_name, "cached/");
            md5_hash(copy_copy_uri, transformed_name+7);  

            // if file is not in cache
            if(check_cache(transformed_name, timeout, fd) == -1){
                printf("DID NOT FIND NAME IN CACHE, SENDING FILE AND CREATING CACHE ENTRY\n");
                
                // creating socket for host
                int sockfd_host = -1;
                sockfd_host = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

                if(sockfd_host == -1){
                    error("Could not create socket");
                }

                if(connect(sockfd_host, res->ai_addr, res->ai_addrlen) == -1){
                    close(sockfd_host);
                    error("Could not connect to host\n");
                }

                // create HTTP GET request to send to host
                char http_transformed_request[REQUEST_SIZE];
                bzero(http_transformed_request, REQUEST_SIZE);
                strcpy(http_transformed_request, "GET ");
                strcat(http_transformed_request, copy_uri);
                strcat(http_transformed_request, " ");
                //strcat(http_transformed_request, parsed_commands[2]);
                //printf("parsed commands 2: %s\n", parsed_commands[2]);
                strcat(http_transformed_request, "HTTP/1.0");
                strcat(http_transformed_request, "\r\n");
                strcat(http_transformed_request, "Host: ");
                strcat(http_transformed_request, parsed_commands[3]);
                strcat(http_transformed_request, "\r\n");

                char content_type[PATHNAME_SIZE];
                bzero(content_type, PATHNAME_SIZE);
                get_content_type(copy_uri, content_type);
                strncat(http_transformed_request, content_type, strlen(content_type));
                strcat(http_transformed_request, "\r\n\r\n");

                // send http request
                send(sockfd_host, http_transformed_request, strlen(http_transformed_request), 0);
               
                // creating file for cache
                FILE* create_fp;
                create_fp = fopen(transformed_name, "w");

                if(create_fp == NULL){
                    error("Could not create file to write cached document to\n");
                }

                char recv_header[REQUEST_SIZE];
                bzero(recv_header, REQUEST_SIZE);

                char recvbuf[REQUEST_SIZE];
                bzero(recvbuf, REQUEST_SIZE);

                int num_written;
                int n = 0;
                int header_terminated = 0;

                // recieve just header from host first
                while(header_terminated == 0){
                    recv(sockfd_host, recv_header + n, 1, 0);
                    n += 1;
                    if(strstr(recv_header, "\r\n\r\n") || strstr(recv_header, "\n\n")){
                        header_terminated = 1;  
                    }
                }

                // write header
                send(fd, recv_header, n, 0);
                fwrite(recv_header, 1, n, create_fp);

                // get content length of response
                char* content_length_string = strstr(recv_header, "Content-Length: ");
                content_length_string[strcspn(content_length_string, "\n")] = 0;
                int content_length = atoi(content_length_string + 16);

 
                //taking the ceiling of file_length/FILE_SIZE_PART to find how many sends
                int num_sends = content_length/FILE_SIZE_PART + ((content_length % FILE_SIZE_PART) != 0); 
                char file_contents[FILE_SIZE_PART];

                for(int i=0; i < num_sends; i++){
                    bzero(file_contents, FILE_SIZE_PART);

                    int n = recv(sockfd_host, file_contents, FILE_SIZE_PART, 0);
                    if(n < 0){
                        error("Error on recv\n");
                    }

                    if(send(fd, file_contents, n, 0) < 0){
                        error("Send to client failed\n");
                    }

                    num_written = fwrite(file_contents, 1, n, create_fp);
                    if(num_written < 0){
                        error("Could not write to cached file\n");
                    }

                }

                fclose(create_fp);
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