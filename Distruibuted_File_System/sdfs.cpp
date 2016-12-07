#include <iostream>
#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include "tcpSocket.h"
#include "ftpServer.h"

#define	LINELEN	1024
#define	ARGLEN	16
#define	HOSTNAMELEN 64

/*
*   SIGCHLD handler
*/
void child_handler(int signum)
{
    //cleans up all terminated child processes
    while(waitpid(-1,NULL,WNOHANG) != -1);
}

void handle_request(int fd)
{
	char *args[ARGLEN];
	char request[LINELEN];
	FILE *fpin  = fdopen(fd, "r");	
	fgets(request,LINELEN,fpin);//read request
	int index = 0;
	const char delim[]=" \t\r\n";
	char *buff = strtok(request,delim);
	while(buff!=NULL)//parse input request
	{
		args[index] = buff;
		++index;
 		buff = strtok (NULL, delim);
	}	
	int pid = fork();
    	if ( pid == -1 ){
		perror("fork");
		return;
	}
	if(pid == 0){//run grep in child process
	    std::string command(args[0]);
	    if(command == "get")
	    {
		ftpServer::receive_get(fd,args[1]);
	    }
	    else if(command == "put")
	    {
		ftpServer::receive_put(fd,args[1]);
	    }
	    else if(command == "delete")
	    {
		ftpServer::receive_delete(fd,args[1]);
	    }
	   exit(0);		/* child is done	*/
	}
	else{//parent process
	    close(fd); //Close the socket descriptor in parent
	}
}

int main()
{
	signal(SIGCHLD,child_handler);//collect child exit status to avoid zombies
	struct sockaddr_in cli_addr;
	
	int sock_id = Socket::make_server_socket(FTPPORTNUM);
	if ( sock_id == -1 ) {
		fprintf(stderr, "error in making socket");
		exit(1);
	}
	int clilen = sizeof(cli_addr);
	while(1)
	{
		int fd    = accept( sock_id, (struct sockaddr *)&cli_addr 
		                         , (socklen_t *)&clilen );	/* take a call	*/
		if ( fd == -1 )
		{
		    //continue when accept returns due to interruption
		    if(errno == EINTR)
		        continue;
		    else //other error
			    perror("accept");
	 	}
		else
		{
			handle_request(fd);
		}	
	}
	return 0;
}
