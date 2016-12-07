#include <iostream>
#include <cstdio>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include "sdfsClient.h"
#include "tcpSocket.h"
#include "common.h"

#define	LINELEN	8192
#define	ARGLEN	16
#define	HOSTNAMELEN 64
#define WORKERPORTNUM 1983
#define BUFF_LEN 4096

const std::string LOCAL_DIR = getLocalTempDir();
const std::string INPUT_DIR = getHomeDir();

/*
*   SIGCHLD handler
*/
void child_handler(int signum)
{
    //cleans up all terminated child processes
    while(waitpid(-1,NULL,WNOHANG) != -1);
}

SDFSClient* getSDFSObj()
{
	static SDFSClient obj;
	return &obj;
}

std::string getLocalFileName(const std::string& sdfsFileName)
{
	return sdfsFileName;
}

void getLocalFilesFromSdfs(const std::vector<std::string>& sdfsFiles)
{
	SDFSClient* sdfsObjPtr = getSDFSObj();
	for(int i = 0; i < sdfsFiles.size(); ++i)
	{
		std::string localFileName = INPUT_DIR + sdfsFiles[i];
		sdfsObjPtr->getLocalFileName(sdfsFiles[i],localFileName);
	}
}

std::vector<std::string> getsdfsFileNames(const std::string& catFileNames)
{
	std::vector<std::string> result;
    	std::istringstream f(catFileNames);
    	std::string s;    
    	while (std::getline(f, s, ';')) 
	{
        	result.push_back(s);
    	}
	return result;
}

std::string readPipeOuput(int fd)
{
	char buff[BUFF_LEN];
	std::stringstream sstr;
	while(1)
	{
		int rd = read(fd,buff,BUFF_LEN);
		if(rd <= 0)
		{
			break;
		}
		buff[rd] = '\0';
		sstr<<buff;
		if(rd < BUFF_LEN)
		{
			break;
		}
	}
	return sstr.str();
}

void handle_maple(int argc,char **args,int masterFd)
{
	std::string mapleExe = args[1];
	std::string dirPrefix = args[2];
	std::string sdfsDir = args[3];
	std::string catFileNames = args[4];
	std::vector<std::string> localFiles = getsdfsFileNames(catFileNames);
	getLocalFilesFromSdfs(localFiles);
	std::string commandLine = mapleExe;
	for(int i = 0; i < localFiles.size(); ++i)
	{
		commandLine += " " + INPUT_DIR + localFiles[i];
	}
	FILE* fp = popen(commandLine.c_str(),"r");
	std::string outputStr = readPipeOuput(fileno(fp));
	int sent = write(masterFd,outputStr.c_str(),outputStr.size());
	for(int i = 0; i < localFiles.size(); ++i)
	{
		std::string fileName =  INPUT_DIR + localFiles[i];
		remove(fileName.c_str());
		std::cout<<"Maple: Handling "<<fileName<< "\n";
	}
}

void handle_juice(int argc,char **args,int masterFd)
{
	std::string juiceExe = args[1];
	std::string dirPrefix = args[2];
	std::string catKeys = args[3];
	std::vector<std::string> keys = getsdfsFileNames(catKeys);
	
	std::string commandLine = juiceExe + " " + LOCAL_DIR + " " + dirPrefix;
	SDFSClient* sdfsObjPtr = getSDFSObj();
	for(int i = 0; i < keys.size(); ++i)
	{
		std::string sdfsFileName = dirPrefix + "_" + keys[i];
		sdfsObjPtr->getLocalFileName(sdfsFileName,LOCAL_DIR + sdfsFileName);
		commandLine += " " + keys[i];
		std::cout<<"Juice: Handling "<<keys[i]<< "\n";
	}
	FILE* fp = popen(commandLine.c_str(),"r");
	std::string outputStr = readPipeOuput(fileno(fp));
	int sent = write(masterFd,outputStr.c_str(),outputStr.size());
	for(int i = 0; i < keys.size(); ++i)
	{
		std::string sdfsFileName = dirPrefix + "_" + keys[i];
		std::string fileName = LOCAL_DIR + sdfsFileName;
		remove(fileName.c_str());
	}
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
	if(pid == 0){
	    std::string command(args[0]);
	    if(command == "maple")
	    {
		handle_maple(index,args,fd);
	    }
	    else if(command == "juice")
	    {
		handle_juice(index,args,fd);
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
	
	int sock_id = Socket::make_server_socket(WORKERPORTNUM);
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
