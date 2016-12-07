#include "ftpClient.h"
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
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "tcpSocket.h"

std::string ftpClient::send_list(const std::string& hostName,const std::string& sdfsDir)
{
	std::stringstream ss;
	ss<<"list "<<sdfsDir<<"\n";
	int sockfd = Socket::connect_to_server((char*)hostName.c_str(),FTPPORTNUM);
	std::stringstream sstr;
	if(sockfd != -1)
	{
		int sent = write(sockfd,ss.str().c_str(),ss.str().size());
		int received = 0;
		
		do 
		{
			const int BUFFER_LEN = 4096;
			char buf[BUFFER_LEN];
      			received = recv(sockfd,buf,BUFFER_LEN,0);
			buf[received] = '\0';
			sstr<<buf;
		} while(received > 0);
		close(sockfd);
	}
	return sstr.str();
}

void ftpClient::send_get(const std::string& hostName,const std::string& sdfsFileName,const std::string& localFileName)
{
	std::stringstream ss;
	ss<<"getx "<<sdfsFileName<<"\n";
	int sockfd = Socket::connect_to_server((char*)hostName.c_str(),FTPPORTNUM);
	if(sockfd != -1)
	{
		int sent = write(sockfd,ss.str().c_str(),ss.str().size());
		int received = 0;
		std::ofstream outfile(localFileName.c_str(),std::ios::out|std::ios::binary);
		do 
		{
			const int BUFFER_LEN = 4096;
			char buf[BUFFER_LEN];
      			received = recv(sockfd,buf,BUFFER_LEN,0);
	      		outfile.write(buf,received);
			} while(received > 0);
		outfile.close();
		close(sockfd);
	}
}
int ftpClient::connect(const std::string& hostName)
{
	int sockfd = Socket::connect_to_server((char*)hostName.c_str(),FTPPORTNUM);
	return sockfd;
}
void ftpClient::send_put(int sockfd,const std::string& localFileName,const std::string& sdfsFileName)
{
	std::stringstream ss;
	ss<<"putx "<<sdfsFileName<<"\n";
	if(sockfd != -1)
	{
		int sent = send(sockfd,ss.str().c_str(),ss.str().size(),0);
		//dup2(sockfd,1); //redirect standard output of grep to client's socket
		//dup2(sockfd,2); //redirect standard error of grep to client's socket
		//close(sockfd); ////do not need duplicate socket descriptor
		/*std::ifstream localFile(localFileName.c_str(),std::ios::in|std::ios::binary|std::ios::ate);
		if (localFile.is_open())
		{
			int size = localFile.tellg();

			char *memblock = new char [size];
			localFile.seekg (0, std::ios::beg);
			localFile.read (memblock, size);
			localFile.close();

			for(int i = 0; i < size; ++i)
			{
				write(sockfd,memblocki]
				std::cout<<memblock[i];

			}
			delete[] memblock;
			}*/
		FILE *fp = fopen(localFileName.c_str(),"rb");
		int fd = fileno(fp);
		struct stat stat_buf;		
		fstat(fd, &stat_buf);
    		off_t  offset = 0;
		int remain_data = stat_buf.st_size;
		int sent_bytes = 0;
		while (((sent_bytes = sendfile (sockfd, fd, &offset, BUFSIZ)) > 0) && (remain_data > 0))
        	{
			remain_data -= sent_bytes;
		}
		close(fd);
		close(sockfd);
		/*while(1)
		{
			unsigned char buff[256];
			int nread = fread(buff,1,256,fp);
			std::cout<<"nread = "<<nread<<"\n";
			if(nread > 0)
			{
				write(sockfd, buff, nread);
			}
			if (nread < 256)
			{
				break;
			}
		}*/
		//close(sockfd);
		//sleep(1);
	}
}
void ftpClient::send_put(const std::string& hostName,const std::string& localFileName,const std::string& sdfsFileName)
{
	int sockfd = Socket::connect_to_server((char*)hostName.c_str(),FTPPORTNUM);
	if(sockfd != -1)
	{
		send_put(sockfd,localFileName,sdfsFileName);
		close(sockfd);
	}
}
void ftpClient::send_delete(const std::string& hostName,const std::string& sdfsFileName)
{
	std::stringstream ss;
	ss<<"delx "<<sdfsFileName<<"\n";
	int sockfd = Socket::connect_to_server((char*)hostName.c_str(),FTPPORTNUM);
	if(sockfd != -1)
	{
		int sent = write(sockfd,ss.str().c_str(),ss.str().size());
		close(sockfd);
	}
}

