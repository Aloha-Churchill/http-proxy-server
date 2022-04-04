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

#define REQUEST_SIZE 4000 // handle first 4000 bytes of request
#define PATHNAME_SIZE 256
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
int parse_commands(char* recvbuf, char* parsed_commands[]){

	//strtok replaces delimiter with null terminator then continues to search for next string
	const char delimiters[] = " \r\n";
	char* element = strtok(recvbuf, delimiters);
	int num_input_strings = 0;

	while(element != NULL){
		if(num_input_strings < 5){
			parsed_commands[num_input_strings] = element;
		}
		element = strtok(NULL, delimiters);
		num_input_strings += 1;
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
        printf("Parsed commands entry: %s\n", parsed_commands[4]);

        if(strncmp(entry, parsed_commands[4], strlen(parsed_commands[4])-1) == 0){
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


/*
Sends the file.
1. Gets file length
2. Gets file type
3. Sends file in increments
*/

/*
int send_file(int fd, char* pathname, char* http_version){

    // get file length
	int file_length;
	FILE* fp;

    fseek(fp, 0, SEEK_SET);

    //taking the ceiling of file_length/FILE_SIZE_PART to find how many sends
    int num_sends = file_length/FILE_SIZE_PART + ((file_length % FILE_SIZE_PART) != 0); 
    char file_contents[FILE_SIZE_PART];

    for(int i=0; i < num_sends; i++){
        bzero(file_contents, FILE_SIZE_PART);
        int n = fread(file_contents, FILE_SIZE_PART, 1, fp);
        if(n < 0){
            error("Error on reading file into buffer\n");
        }
        // just send remaining bytes
        if(i == num_sends-1){
            send_all(fd, file_contents, file_length % FILE_SIZE_PART);
        }
        else{
            send_all(fd, file_contents, FILE_SIZE_PART);
        } 
    }

    fclose(fp);
    return 0;
}
*/
