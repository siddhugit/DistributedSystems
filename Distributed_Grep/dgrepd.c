#include	<stdio.h>
#include	<stdlib.h>
#include	<strings.h>
#include	<sys/types.h>
#include 	<sys/wait.h>
#include	<sys/socket.h>
#include	<netinet/in.h>
#include	<netdb.h>
#include	<unistd.h>
#include	<string.h>
#include	<errno.h>
#include	<signal.h>

#define PORTNUM 10000
#define	LINELEN	1024
#define	ARGLEN	16
#define	HOSTNAMELEN	64

/*
*   SIGCHLD handler
*/
void child_handler(int signum)
{
    //cleans up all terminated child processes
    while(waitpid(-1,NULL,WNOHANG) != -1);
}

int make_server_socket( int portnum )
{
        struct  sockaddr_in   saddr;   /* build our address here */
	int	sock_id;	       /* line id, file desc     */
	int	on = 1;		       /* for sockopt		 */

	/*
	 *      step 1: build our network address
	 *               domain is internet, hostname is any address
	 *               of local host, port is some number
	 */

	memset(&saddr, 0, sizeof(saddr));          /* 0. zero all members   */

	saddr.sin_family = AF_INET;		   /* 1. set addr family    */
	saddr.sin_addr.s_addr = htonl(INADDR_ANY); /* 2. and IP addr	    */
	saddr.sin_port = htons(portnum);	   /* 3. and the port 	    */

	/*
	 *      step 2: get socket, set option, then then bind address
	 */

	sock_id = socket( PF_INET, SOCK_STREAM, 0 );    /* get a socket */
	if ( sock_id == -1 ) return -1;
	if ( setsockopt(sock_id,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)) == -1 )
		return -1;
	if ( bind(sock_id,(struct sockaddr*)&saddr, sizeof(saddr)) ==  -1 )
	       return -1;

	/*
	 *      step 3: tell kernel we want to listen for calls
	 */
	if ( listen(sock_id, 1) != 0 ) return -1;
	return sock_id;
}

char* getLogFileName() //returns log file name to grep (vmi.log)
{
	char hostname[HOSTNAMELEN];
	hostname[HOSTNAMELEN - 1] = '\0';
	gethostname(hostname, HOSTNAMELEN);
	char* fileName = "/tmp/vm1.log";
	char *logFileName = malloc(strlen(fileName) + 1);
	strcpy(logFileName,fileName);
	logFileName[7] =  hostname[16]; //set correct value of i
	return logFileName;
}

void handle_request(int fd)
{
	char *args[ARGLEN];
	char request[LINELEN];
	FILE *fpin  = fdopen(fd, "r");	
	fgets(request,LINELEN,fpin);//read request
	args[0] = "/bin/grep";//prepare grep options
	args[1] = "-Hn";//to get line number in grep output
	int index = 2;
	const char delim[]=" \t\r\n";
	char *buff = strtok(request,delim);
	while(buff!=NULL)//parse input request
	{
 		buff = strtok (NULL, delim);
		if(buff != NULL)
		{
			args[index] = buff;
			++index;
		}
	}	
	args[index] = getLogFileName(); //get log file name to grep (vmi.log)
	args[index + 1] = NULL;	//NULL to indicate end of argumets for execvp
	int pid = fork();
    	if ( pid == -1 ){
		perror("fork");
		return;
	}
	if(pid == 0){//run grep in child process
	    dup2(fd,1); //redirect standard output of grep to client's socket
	    dup2(fd,2); //redirect standard error of grep to client's socket
	    close(fd); ////do not need duplicate socket descriptor
	    execvp(args[0],args);//exec grep command
	    perror("grep error");
	}
	else{//parent process
	    close(fd); //Close the socket descriptor in parent
	}
}

int main(int argc,char *argv[])
{
	signal(SIGCHLD,child_handler);//collect child exit status to avoid zombies
	struct sockaddr_in cli_addr;
	
	int sock_id = make_server_socket(PORTNUM);
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


