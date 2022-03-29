/*
Simple HTTP server implemented with fork()
*/

#include "helpers.h"

/*
Function to send HTTP response to client.
*/
void handle_client(int fd) {
    char recvbuf[REQUEST_SIZE];
    bzero(recvbuf, REQUEST_SIZE);

    //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
    if(recv(fd, recvbuf, REQUEST_SIZE, 0) < 0){
        error("Recieve failed\n");
    }

    // parse request into 3 parts [[method],[url],[http version]]
    char* parsed_commands[3];
    int num_parsed = parse_commands(recvbuf, parsed_commands);

    // get full pathname of file
    char pathname[PATHNAME_SIZE];
    bzero(pathname, PATHNAME_SIZE);

    // check if request is okay, if so, then we check if file is valid
    if(check_request(fd, parsed_commands, num_parsed) != -1){

        //create full path name
        strcpy(pathname, "www");
        strcat(pathname, parsed_commands[1]);

        // check if file is valid, if so, then we send the file
        if(check_file(fd, pathname, parsed_commands[2]) != -1){
            send_file(fd, pathname, parsed_commands[2]);
        }  
    }

}


/*
Function to initialize variables, start server, and accept client connections
Code modified from Beej's Guide to Network Programming
*/
void start_server(int *server_socket, int port) {

    // get socket file descriptor
    *server_socket = socket(PF_INET, SOCK_STREAM, 0);
 
    // Set up socket so can re-use address
    int socket_option = 1;
    if(setsockopt(*server_socket, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option)) == -1){
        error("Could not set socket option\n");
    }
 
    // Set server address structure 
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET; //internet domain
    server_address.sin_addr.s_addr = INADDR_ANY; 
    server_address.sin_port = htons(port); //use host to network so that encoding is correct (big vs little endian)
 
    // bind to socket to associate it with port
    if(bind(*server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
        close(*server_socket);
        error("Could not bind to port\n");
    }
 
    // Start listening. Accept as many as BACKLOG connections
    if(listen(*server_socket, BACKLOG) == -1){
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
 
    // initializing client socket variables
    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_socket;
 
    // Server continues to accept connections until Ctrl+C is pressed
    while (1) {
        // Accept the client socket.
        client_socket = accept(*server_socket, (struct sockaddr *) &client_address, (socklen_t *) &client_address_length);
        if(client_socket == -1){
            error("Refused to accept connection\n");
        }

        // create child process to handle request and let parent continue to listen for new connections
        if(!fork()){
            
            close(*server_socket);
            
            // function to send response
            handle_client(client_socket);
            close(client_socket);
            exit(0);
        }
        
        close(client_socket);
    }
 
    // Shut down the server.
    shutdown(*server_socket, SHUT_RDWR);
    close(*server_socket);
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