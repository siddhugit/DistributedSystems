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
#include "tcpSocket.h"

#define MASTERPORTNUM 1982

int main(int argc,char** argv)
{
	std::string hostName = getMasterHostname();
	int sockfd = Socket::connect_to_server((char*)hostName.c_str(),MASTERPORTNUM);
	std::stringstream ss;
	int numMaples = atoi(argv[2]);
	std::string mapleExe = argv[1];
	std::string sdfsDir = argv[4];
	std::string dirPrefix = argv[3];
	ss<<"maple "<<mapleExe<<" "<<numMaples<<" "<<dirPrefix<<" "<<sdfsDir<<"\n";
	if(sockfd != -1)
	{
		int sent = write(sockfd,ss.str().c_str(),ss.str().size());
	}
}
