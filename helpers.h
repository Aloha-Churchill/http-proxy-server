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
		if(num_input_strings < 3){
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
int send_file(int fd, char* pathname, char* http_version){

    // get file length
	int file_length;
	FILE* fp;
    fp = fopen(pathname, "r");
	fseek(fp, 0L, SEEK_END);
	file_length = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	char number_string[10];
	bzero(number_string, 10);
	sprintf(number_string, "%d", file_length);

    char content_length[PATHNAME_SIZE];
    bzero(content_length, PATHNAME_SIZE);
    strcpy(content_length, "Content-Length: ");
	strcat(content_length, number_string);
	strcat(content_length, "\r\n\r");

    // get content type
    char content_type[PATHNAME_SIZE];
    bzero(content_type, PATHNAME_SIZE);
	const char delimiters[] = ".";
	char* element = strtok(strrev(pathname), delimiters);
	element = strrev(element);

	if(element == NULL){
		error("Not a valid file format\n");
	}
	else{
		if(strcmp(element, "html") == 0){
			strcpy(content_type, "Content-Type: text/html\r");
		}
		else if(strcmp(element, "txt") == 0){
			strcpy(content_type, "Content-Type: text/plain\r");	
		}
		else if(strcmp(element, "png") == 0){
			strcpy(content_type, "Content-Type: image/png\r");
		}
		else if(strcmp(element, "gif") == 0){
			strcpy(content_type, "Content-Type: image/gif\r");
		}
		else if(strcmp(element, "jpg") == 0){
			strcpy(content_type, "Content-Type: image/jpg\r");
		}
		else if(strcmp(element, "css") == 0){
			strcpy(content_type, "Content-Type: text/css\r");
		}
		else if(strcmp(element, "js") == 0){
			strcpy(content_type, "Content-Type: application/javascript\r");
		}
        else{
            strcpy(content_type, "Content-Type: unsupported\r");
        }
	}

    char status_header[PATHNAME_SIZE];
    bzero(status_header, PATHNAME_SIZE);
    strcpy(status_header, http_version);
    strcat(status_header, " 200 OK\r");

    dprintf(fd, "%s\n", status_header);
    dprintf(fd, "%s\n", content_type);
    dprintf(fd, "%s\n", content_length);

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
