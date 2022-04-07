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

#define REQUEST_SIZE 1000 // handle first 4000 bytes of request
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

        
        /*
        int offset = strlen("http://") + strlen(parsed_commands[3]);
        int port_len = strlen(parsed_commands[4]);
        strncpy(modified_hostname, address, offset);
        strcat(modified_hostname, address + offset + port_len + 1);
        
        parsed_commands[1] = modified_hostname;
        */

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
Checks if user entered in valid request. Default to HTTP/1.1 upon malformed request.
dprintf writes to fd

We also need to check if hostname is ok, use getaddrinfo and it should return an IP addr, if not then this is not correctly implemented
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


    // check if request is in blocklist
    /*
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
        printf("Blocklist entry: %s\n", entry);
        printf("Parsed commands entry: %s\n", parsed_commands[3]);

        if(strncmp(entry, parsed_commands[3], strlen(parsed_commands[3])-1) == 0){
            printf("RECIEVED BLOCKED COMMAND\n");
            dprintf(fd, "HTTP/1.1 403 Forbidden\r\n");
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
            return -1;	
        }

    }

    printf("5 PARSED COMMANDS 1: %s\n", parsed_commands[1]);
    fclose(blocklist_fp);
    */

    return 0;
}

/*
Checks file status and sends proper header.
*/
int check_file(int fd, char* pathname, char* http_version){
    FILE* fp = fopen(pathname, "r");

    char status_header[PATHNAME_SIZE];
    bzero(status_header, PATHNAME_SIZE);
    strcpy(status_header, http_version);

	if(fp == NULL){

        // file permission is denied
		if(errno == 13){
            strcat(status_header, " 403 Forbidden\r");
            dprintf(fd, "%s\n", status_header);
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
            return -1;	
		}

        // file was not found
		if(errno == 2){
            strcat(status_header, " 404 Not Found\r");
            dprintf(fd, "%s\n", status_header);
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
            return -1;	
		}
		else{
			error("Unexpected error while trying to open file\n");
		}	
	}

	fclose(fp);
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



