#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <openssl/md5.h>

#define REQUEST_SIZE 4000 // handle first 4000 bytes of request
#define PATHNAME_SIZE 512
#define FILE_SIZE_PART 1024 // send files in 1024 size increments
#define BACKLOG 1024

// global server file descriptor variable
int server_fd;

/*
Wrapper function for error messages
*/
void error(char* message){
    perror(message);
    exit(1);
}

/*
Terminate server upon Ctrl+C
*/
void exit_handler(int signal) {
    printf("Recieved shutdown signal\n");
    close(server_fd);
    exit(0);
}


/*
Reap terminated child processes
*/
void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning
	int saved_errno = errno;

    // WNOHANG: return immediately if no child has exited
	while(waitpid(-1, NULL, WNOHANG) > 0);
	errno = saved_errno;
}

/*
Wrapper function for sending file. This ensures that all the byes are sent, and does partial resends if necessary.
*/
int send_all(int fd, char* send_buf, int size){
	int n = -1;

	n = send(fd, send_buf, size, 0);
	if(n < 0){
		error("Send failed\n"); 
	}

	if(n != size){
		while(n < size){
			n += send(fd, send_buf + n, size - n, 0);
		}
	}

    if(n == -1){
        error("Could not send\n");
    }
	return n;	
}

/*
Parses recieved string into 3 distinct parts
*/
int parse_commands_V2(char* recvbuf, char* parsed_commands[]){

    // [GET] [URI] [HTTP_VERSION] [HOST] [PORT]

	const char delimiters[] = " \r\n";
	char* element = strtok(recvbuf, delimiters);
	int num_input_strings = 0;
    int host_index = -1;
    char* full_hostname;
    char* address;

	while(element != NULL){
        if(num_input_strings < 3){
            if(num_input_strings == 1){
                address = element;
            }
            else{
                parsed_commands[num_input_strings] = element;
            }
            
        }
        if(strcmp(element, "Host:") == 0){
            host_index = num_input_strings + 1;
        }
        if(num_input_strings == host_index){
            full_hostname = element;
        }
		element = strtok(NULL, delimiters);
		num_input_strings += 1;
	}

    // identify host name and port number
	char* i = strtok(full_hostname, ":");
    int n = 3;
    
	while(i != NULL){
        parsed_commands[n] = i;
		i = strtok(NULL, delimiters);
        n += 1;
	}

    // if a port number is specified
    if(n == 5){
        char modified_hostname[PATHNAME_SIZE];
        bzero(modified_hostname, PATHNAME_SIZE);

        int offset = strlen("http://") + strlen(parsed_commands[3]) + strlen(parsed_commands[4])+ 1;
        strcpy(modified_hostname, address + offset);
        parsed_commands[1] = modified_hostname;
        printf("Modified host name: %s\n", parsed_commands[1]);  

    }
    else{
        parsed_commands[1] = address;
        parsed_commands[4] = NULL;
    }

    return num_input_strings;

}

/*
Converts string hostname to IP address
code from https://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/
*/
int hostname_to_ip(char * hostname , char* ip)
{
	struct hostent *he;
	struct in_addr **addr_list;
	int i;
		
	if ( (he = gethostbyname( hostname ) ) == NULL) 
	{
		// get the host info
		herror("gethostbyname");
		return 1;
	}

	addr_list = (struct in_addr **) he->h_addr_list;
	
	for(i = 0; addr_list[i] != NULL; i++) 
	{
		//Return the first one;
		strcpy(ip , inet_ntoa(*addr_list[i]) );
		return 0;
	}
	
	return 1;
}

/*
Checks if user entered in valid request. Default to HTTP/1.1 upon malformed request.
dprintf writes to fd. Also checks if hostname is in blocklist.
*/
int check_request(int fd, char* parsed_commands[], int num_input_strings){

    // malformed request, user did not enter in enough commands
    if(num_input_strings < 3){
        dprintf(fd, "HTTP/1.1 400 Bad Request\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
        return -1;
    }


    // user entered in incorrect method
    if(strcmp(parsed_commands[0], "GET") != 0){
        dprintf(fd, "HTTP/1.1 405 Method Not Allowed\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
        return -1;
	}

	// user entered in incorrect HTTP version
	if(strncmp(parsed_commands[2], "HTTP/1.0", 8) != 0 && strncmp(parsed_commands[2], "HTTP/1.1", 8) != 0){
        dprintf(fd, "HTTP/1.1 505 HTTP Version Not Supported\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
        return -1;		
	}


    // check if request is in blocklist --> need to make sure this works with wget
    FILE* blocklist_fp;
    char* entry = NULL;
    size_t len = 0;
    ssize_t read;

    printf("Checking blocklist\n");

    blocklist_fp = fopen("blocklist", "r");
    if(blocklist_fp == NULL){
        printf("No such blocklist file exits\n");
        return 0;
    }


    while((read = getline(&entry, &len, blocklist_fp)) != -1){

        entry[strcspn(entry, "\n")] = 0;
        // convert entry and parsed_commands to IP Address and then compare
        char current_entry[PATHNAME_SIZE];
        bzero(current_entry, PATHNAME_SIZE);

        char search_entry[PATHNAME_SIZE];
        bzero(search_entry, PATHNAME_SIZE);

        hostname_to_ip(entry, current_entry);
        hostname_to_ip(parsed_commands[3], search_entry);

        
        if(strcmp(current_entry, search_entry) == 0){
            printf("RECIEVED BLOCKED COMMAND\n");
            dprintf(fd, "HTTP/1.1 403 Forbidden\r\n");
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
            return -1;	
        }

    }

    fclose(blocklist_fp);
    return 0;
}


/*
Helper function from stack overflow to reverse strings. Used to get file content type.
*/
char *strrev(char *str)
{
      char *p1, *p2;

      if (! str || ! *str)
            return str;
      for (p1 = str, p2 = str + strlen(str) - 1; p2 > p1; ++p1, --p2)
      {
            *p1 ^= *p2;
            *p2 ^= *p1;
            *p1 ^= *p2;
      }
      return str;
}

/*
Takes the md5 hash of the URI and converts it to a string representation
*/
void md5_hash(char* original_name, char* transformed_name){
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

/*
Gets content type given the URI
*/
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

/*
Checks the cache for the requested file. If there is a cache hit, then sends file back to client.
This function also takes care of deleting expired files in cache.
*/
int check_cache(char* transformed_name, int timeout, int fd){
    int file_found = -1;
    
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

        struct stat last_accessed;
        struct utimbuf new_times;

        stat(full_cached_path_name, &last_accessed);
        time_t time_last_accessed = mktime(localtime(&last_accessed.st_atime));
        time_t current_time = time(NULL);
        int time_difference = current_time-time_last_accessed;

        // if there is a timeout then remove the file from cache
        if(time_difference > timeout){   
            if(remove(full_cached_path_name) != 0){
                error("Could not remove expired file from cache\n");
            }
        }
        // otherwise check if the names match
        else{
            // if cache hit, send file and update accessed time
            if(strcmp(transformed_name, full_cached_path_name) == 0){
                printf("FOUND UNEXPIRED MATCH IN CACHE\n");
                file_found = 0;

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

                fclose(send_cached_fp);    
            }
        }       
    }
    fclose(cache_fp);

    return file_found;
}



