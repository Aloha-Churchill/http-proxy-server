/*
Simple HTTP server implemented with fork()
*/

#include "helpers.h"

/*
Function to send HTTP response to client.
*/
void handle_client(int fd, int port) {
    char http_buf[REQUEST_SIZE];
    bzero(http_buf, REQUEST_SIZE);

    char preserved_http_buf[REQUEST_SIZE];
    bzero(preserved_http_buf, REQUEST_SIZE);

    //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
    if(recv(fd, http_buf, REQUEST_SIZE, 0) < 0){
        error("Recieve failed\n");
    }

    memcpy(preserved_http_buf, http_buf, REQUEST_SIZE);
    

    printf("Recieved: %s\n", http_buf);
    // parse request into 4 parts [[method],[url],[http version],[requested host]]
    char* parsed_commands[5];
    int num_parsed = parse_commands(http_buf, parsed_commands);

    // get full pathname of file
    char pathname[PATHNAME_SIZE];
    bzero(pathname, PATHNAME_SIZE);

    // check if request is okay, if so, then we check if file is valid
    if(check_request(fd, parsed_commands, num_parsed) != -1){

        //if user enters /, then send index.html

        //create a second socket and send request to host_ip
        //check if server hostname was found using getaddrinfo
        printf("Host to forward to: %s\n", parsed_commands[4]);
        struct addrinfo server_hints;
        struct addrinfo *res;

        // Set host/server address structure 
        memset(&server_hints, 0, sizeof(struct addrinfo));
        server_hints.ai_family = AF_UNSPEC;
        server_hints.ai_socktype = SOCK_STREAM;
        //server_hints.ai_flags = AI_PASSIVE;

        // checking if hostname exists
        //int ret = getaddrinfo(parsed_commands[4], NULL, &server_hints, &res);
        int ret = getaddrinfo(parsed_commands[4], "80", &server_hints, &res);
        
        //return values was zero

        // if host name is invalid
        if(ret != 0){
            printf("Address was INVALID\n");
            dprintf(fd, "HTTP/1.1 404 Not Found\r\n");
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
        }

        // if host name is valid, then open socket  for communicating from proxy to host
        else{
            printf("Address was valid\n");
            // creating socket for host
            int sockfd_host = -1;

            sockfd_host = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            // sockfd_host = 3
            if(sockfd_host == -1){
                error("Could not create socket");
            }
            printf("Created Socket\n");

            //int socket_option = 1;
            //if(setsockopt(sockfd_host, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option)) == -1){
            //    error("Could not set socket option\n");
            //}
            //printf("Set Socket Option\n");
            // could not connect to sock host.
            // using connect instead of listen because we know who client and server are
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

            //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
            int recv_res = recv(sockfd_host, recvbuf, REQUEST_SIZE, 0) < 0;
            printf("RECIEVED FROM HOST result: %s\n", recvbuf);
            if(recv_res < 0){
                error("Recieve from host failed\n");
            }
            //while(recv_res != 0){
            if(send(fd, recvbuf, REQUEST_SIZE, 0) < 0){
                error("Send to client failed\n");
            }
            //}

            // then we want to send what we just recieved in the proxy to the original client
            freeaddrinfo(res);
            close(sockfd_host);
            
        }

    }

}

/*
Function to initialize variables, start server, and accept client connections
Code modified from Beej's Guide to Network Programming
*/
void start_server(int *proxy_socket, int port) {

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
            handle_client(client_socket, port);
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

    if(argc !=2){
        error("Incorrect number of arguments\t Correct format: ./server [PORTNO]\n");
    }
    int port = atoi(argv[1]);

    // if user types Ctrl+C, server shuts down
    signal(SIGINT, exit_handler);
    start_server(&server_fd, port);
}