/*
HTTP proxy server

Questions:
- Testing when browser defaults to https vs http
- Do we only want to cache plain text?
- If we already have name in cache, do we prefetch again? Let's assume that only if the requested
page is not in the cache, then we have to prefetch everything, otherwise things are already prefetched.
- file access time is not being modified by opening and reading from it


Testing website
***********************************Need to test blocklist a little bit more also**********************
Also want to clean up code, modularize after finished.

Currently, problem is in caching file --> not writing to file correctly



NEED TO PROCESS PORT NUMBER CORRECTLY
Recieved: GET http://localhost:8888/index.html HTTP/1.1 --> need to take off port number
User-Agent: Wget/1.20.3 (linux-gnu)
Accept:
Accept-Encoding: identity
Host: localhost:8888 --> need to rip off port number here

============ TESTING =============
http_proxy=localhost:8080 wget -m http://localhost:8888/index.html
mv localhost\:8888/ localhost.old
http_proxy=localhost:8080 wget -m http://localhost:8888/index.html
diff -r localhost.old localhost\:8888
diff localhost\:8888/ University/2022Spring/Networks/PA2/www


URGENT: Still need to fix blocklist to convert everything to ip or domain name



*/
#include "helpers.h"

void md5hash(char* original_name, char* transformed_name){
    MD5_CTX c;

    unsigned char out[MD5_DIGEST_LENGTH];

    MD5_Init(&c);
    MD5_Update(&c, original_name, strlen(original_name));
    MD5_Final(out, &c);

    int n;
    for(n=0; n<MD5_DIGEST_LENGTH; ++n){
        sprintf(&transformed_name[n*2], "%02x", (unsigned int)out[n]);
    }


}

void get_content_type(char* pathname, char* content_type){

	const char delimiters[] = ".";
	char* element = strtok(strrev(pathname), delimiters);
	element = strrev(element);

	if(element == NULL){
		error("Not a valid file format\n");
	}
	else{
		if(strcmp(element, "html") == 0){
			strcpy(content_type, "Content-Type: text/html");
		}
		else if(strcmp(element, "txt") == 0){
			strcpy(content_type, "Content-Type: text/plain");	
		}
		else if(strcmp(element, "png") == 0){
			strcpy(content_type, "Content-Type: image/png");
		}
		else if(strcmp(element, "gif") == 0){
			strcpy(content_type, "Content-Type: image/gif");
		}
		else if(strcmp(element, "jpg") == 0){
			strcpy(content_type, "Content-Type: image/jpg");
		}
		else if(strcmp(element, "css") == 0){
			strcpy(content_type, "Content-Type: text/css");
		}
		else if(strcmp(element, "js") == 0){
			strcpy(content_type, "Content-Type: application/javascript");
		}
        else{
            strcpy(content_type, "Content-Type: unsupported");
        }
    }
}


int checkCache(char* transformed_name, int timeout, int fd){
    // if we already have requested page in cache
    FILE* cache_fp;
    char path[PATHNAME_SIZE];
    bzero(path, PATHNAME_SIZE);

    cache_fp = popen("ls cached/", "r");
    if (cache_fp == NULL){
        error("Could not open cache\n");
    }

    // loop through cached directory to find hit, delete files that are expired
    while(fgets(path, PATHNAME_SIZE, cache_fp) != NULL){

        // creating full cached name
        char full_cached_path_name[PATHNAME_SIZE];
        bzero(full_cached_path_name, PATHNAME_SIZE);
        path[strcspn(path, "\n")] = 0;
        strcpy(full_cached_path_name, "cached/");
        strcat(full_cached_path_name, path);

        printf("REQUESTED NAME: %s\nPATH NAME: %s\n", transformed_name, full_cached_path_name);
        struct stat last_accessed;
        struct utimbuf new_times;

        stat(full_cached_path_name, &last_accessed);
        time_t time_last_accessed = mktime(localtime(&last_accessed.st_atime));
        time_t current_time = time(NULL);

        printf("Time file accessed: %ld\t\tCurrent time: %ld\n", time_last_accessed, current_time);
        int time_difference = current_time-time_last_accessed;
        printf("TIME DIFFERENCE IS: %d\n", time_difference);

        if(time_difference > timeout){
            printf("THIS FILE IS EXPIRED\n");
            // we then want to remove the file from cached
            if(remove(full_cached_path_name) != 0){
                error("Could not remove expired file from cache\n");
            }
            fclose(cache_fp);
            return -1;
        }
        // now we actually need to read and then send file
        if(strcmp(transformed_name, full_cached_path_name) == 0){
            printf("FOUND UNEXPIRED MATCH IN CACHE\n");

            // update cache expiration
            new_times.actime = time(NULL);
            utime(transformed_name, &new_times);

            // send file back to client
            FILE* send_cached_fp;
            send_cached_fp = fopen(transformed_name, "r");
            
            fseek(send_cached_fp, 0L, SEEK_END);
            int filesize = ftell(send_cached_fp);
            fseek(send_cached_fp, 0L, SEEK_SET);


            //taking the ceiling of file_length/FILE_SIZE_PART to find how many sends
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
                    send_all(fd, file_contents, filesize % FILE_SIZE_PART);
                }
                else{
                    send_all(fd, file_contents, FILE_SIZE_PART);
                } 
            }

            /*
            char* file_contents = (char*) malloc(filesize);
            int n = fread(file_contents, filesize, 1, send_cached_fp);

            printf("SENDING BACK TO CLIENT: \n %s \n", file_contents);

            if(n < 0){
                error("Error on reading file into buffer\n");
            }

            send(fd, file_contents, filesize, 0);
            free(file_contents);
            */

            printf("DONE SENDING FILE\n");
            fclose(send_cached_fp);
            fclose(cache_fp);
            return 0;
            
        }
            
    }
    fclose(cache_fp);

    return -1;

}


/*
Function to send HTTP response to client.
*/
void handle_client(int fd, int port, int timeout) {
    char http_buf[REQUEST_SIZE];
    bzero(http_buf, REQUEST_SIZE);


    // CHANGE THIS TO ONLY RECV ONE BYTE AT A TIME
    //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
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


    for(int i = 0; i<5; i++){
        printf("ENTRY: %s\n", parsed_commands[i]);
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
    if(parsed_commands[4] == NULL){
        printf("Port number unspecified\n");
        ret = getaddrinfo(parsed_commands[3], "http", &server_hints, &res);
    }
    else{
        printf("Port number specified: %s\n", parsed_commands[4]);
        ret = getaddrinfo(parsed_commands[3], parsed_commands[4], &server_hints, &res);
    }

    printf("IP ADDR: %s\n", res->ai_addr->sa_data);
    
    
    // if host name is invalid
    if(ret != 0){
        printf("Address was INVALID\n");
        dprintf(fd, "HTTP/1.1 404 Not Found\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
    }
    else{
        hostname_exists = 0;
    }

    printf("PARSED COMMANDS 1: %s\n", parsed_commands[1]);

    // check if request is okay, if so, then we check if file is valid
    
    if(check_request(fd, parsed_commands, num_parsed) != -1){

        printf("PARSED COMMANDS 1: %s\n", parsed_commands[1]);

        if(hostname_exists == 0){

            // transformed name is where hash of url is stored
            char* copy_uri = strdup(parsed_commands[1]);
            char transformed_name[33+7];
            bzero(transformed_name, 33+7);
            strcpy(transformed_name, "cached/");
            md5hash(parsed_commands[1], transformed_name+7);
            printf("TRANSFORMED NAME: %s\n", transformed_name);
            printf("POST HASH PARSED COMMANDS 1: %s\n", copy_uri);  

            // if file is not in cache
            if(checkCache(transformed_name, timeout, fd) == -1){
                printf("DID NOT FIND NAME IN CACHE, SENDING FILE AND CREATING CACHE ENTRY\n");
                
                // creating socket for host
                int sockfd_host = -1;
                sockfd_host = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
                // sockfd_host = 3
                if(sockfd_host == -1){
                    error("Could not create socket");
                }

                if(connect(sockfd_host, res->ai_addr, res->ai_addrlen) == -1){
                    close(sockfd_host);
                    error("Could not connect to host\n");
                }


                char http_transformed_request[REQUEST_SIZE];
                bzero(http_transformed_request, REQUEST_SIZE);
                strcpy(http_transformed_request, "GET ");
                strcat(http_transformed_request, copy_uri);
                strcat(http_transformed_request, " ");
                strcat(http_transformed_request, parsed_commands[2]);
                strcat(http_transformed_request, "\r\n");
                strcat(http_transformed_request, "Host: ");
                strcat(http_transformed_request, parsed_commands[3]);
                strcat(http_transformed_request, "\r\n");

    
                // this part is not currently working
                char content_type[PATHNAME_SIZE];
                bzero(content_type, PATHNAME_SIZE);

                printf("Before get content type\n");
                get_content_type(copy_uri, content_type);
                printf("Content type: %s\n", content_type);

                strncat(http_transformed_request, content_type, strlen(content_type)); //modify this to also accept other types of content
                //strcat(http_transformed_request, "Content-Type: text/html"); 
                strcat(http_transformed_request, "\r\n\r\n");

                printf("HTTP REQUEST SENDING: %s\n", http_transformed_request);
                send(sockfd_host, http_transformed_request, strlen(http_transformed_request), 0);
               
                // creating file for cache
                FILE* create_fp;
                create_fp = fopen(transformed_name, "w");

                if(create_fp == NULL){
                    error("Could not create file to write cached document to\n");
                }

                //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
                // we first need to recieve return header, write this into file, and then get the content length and do the proper number of recvs based on this
                

                char recv_header[REQUEST_SIZE];
                bzero(recv_header, REQUEST_SIZE);

                char recvbuf[REQUEST_SIZE];
                bzero(recvbuf, REQUEST_SIZE);

                //int num_bytes;
                int num_written;

                int n = 0;
                int header_terminated = 0;
                while(header_terminated == 0){
                    recv(sockfd_host, recv_header + n, 1, 0);
                    n += 1;
                    if(strstr(recv_header, "\r\n\r\n") || strstr(recv_header, "\n\n")){
                        header_terminated = 1;  
                    }
                }

                char* content_length_string = strstr(recv_header, "Content-Length: ");
                content_length_string[strcspn(content_length_string, "\n")] = 0;
                int content_length = atoi(content_length_string + 16);

                printf("CONTENT LENGTH: %s\n", content_length_string);
                printf("CONTENT LENGTH INT: %d\n", content_length);


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

                    // maybe need while loop again
                    num_written = fwrite(file_contents, 1, n, create_fp);
                    if(num_written < 0){
                        error("Could not write to cached file\n");
                    }

                }

                fclose(create_fp);
                freeaddrinfo(res);
                close(sockfd_host);

                /*
                num_bytes = recv(sockfd_host, recvbuf, REQUEST_SIZE-1, 0);
                if(num_bytes < 0){
                    error("Recv failed\n");
                } 

                recvbuf[num_bytes] = 0;
                printf("RECIEVED %d BYTES FROM HOST\n", num_bytes);
                printf("RECIEVED FROM HOST result: \n%s\n", recvbuf);
                
                if(send(fd, recvbuf, num_bytes, 0) < 0){
                    error("Send to client failed\n");
                }

                // maybe need while loop again
                num_written = fwrite(recvbuf, 1, num_bytes, create_fp);
                if(num_written < 0){
                    error("Could not write to cached file\n");
                }
                printf("NUMBER OF BYTES WRITTEN TO FILE: %d\n", num_written);
                bzero(recvbuf, REQUEST_SIZE);
 

                if(num_bytes < 0){
                    error("Recv failed\n");
                }
                */



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


                /*
                if(!fork()){
                    // parsing document to find urls and then fetching webpages
                    printf("INSIDE OF CHILD PROCESS\n");
                    FILE* url_fp;
                    url_fp = fopen(transformed_name, "r");

                    fseek(url_fp, 0L, SEEK_END);
                    int filesize = ftell(url_fp);
                    fseek(url_fp, 0L, SEEK_SET);

                    char* file_buf = (char*) malloc(filesize);
                    fread(file_buf, filesize, 1, url_fp);
                    fclose(url_fp);

                    printf("Contents: %s\n", file_buf);

                    char* href = "href";
                    char* occurrence = strstr(file_buf, href);
                    int position = occurrence - href;

                    printf("Position: %d\n", position);
                    printf("Occurrence: %s\n", occurrence);

                    free(file_buf);
                    
                    exit(0);
                }
                */