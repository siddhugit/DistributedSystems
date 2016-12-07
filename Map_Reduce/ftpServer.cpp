#include "ftpServer.h"
#include <iostream>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <cstring>
#include <libgen.h>
#include <stdlib.h>

const std::string ftpServer::SDFS_DIR("/tmp/sdfs/");

void ftpServer::createDir(const std::string& filaPath)
{
	char *dummy = strdup(filaPath.c_str());
	char *dname = dirname(dummy);
	bool exists = false;
	struct stat st;
	if(stat(dname,&st) == 0)
	{
    		if(st.st_mode & S_IFDIR != 0)
		{
			exists = true;
		}
	}
	if(!exists)
	{
		mkdir(dname, S_IRUSR | S_IWUSR | S_IXUSR);
	}
	free(dummy);
}

void ftpServer::receive_get(int sockfd,const std::string& sdfsFileName)
{
	dup2(sockfd,1); //redirect standard output of grep to client's socket
	dup2(sockfd,2); //redirect standard error of grep to client's socket
	close(sockfd); ////do not need duplicate socket descriptor
	std::string sdfsFileNamePath = 	SDFS_DIR + sdfsFileName;
	std::ifstream sdfsFile(sdfsFileNamePath.c_str(),std::ios::in|std::ios::binary|std::ios::ate);
	if (sdfsFile.is_open())
	{
	    int size = sdfsFile.tellg();
	    char *memblock = new char [size];
	    sdfsFile.seekg (0, std::ios::beg);
	    sdfsFile.read (memblock, size);
	    sdfsFile.close();
	    for(int i = 0; i < size; ++i)
	    {
		std::cout<<memblock[i];
	    }
	    delete[] memblock;
	}
}
void ftpServer::receive_list(int sockfd,const std::string& sdfsDir)
{
	dup2(sockfd,1); //redirect standard output of grep to client's socket
	dup2(sockfd,2); //redirect standard error of grep to client's socket
	close(sockfd); ////do not need duplicate socket descriptor
	std::string sdfsDirPath = SDFS_DIR + sdfsDir;
	DIR *d;
  	struct dirent *dir;
  	d = opendir(sdfsDirPath.c_str());
	if (d)
	{
		while ((dir = readdir(d)) != NULL)
		{
			std::string currentPath = sdfsDirPath + "/" + dir->d_name;
			struct stat statbuf;
			stat(currentPath.c_str(), &statbuf);

			if (S_ISREG(statbuf.st_mode)){
				std::cout<<dir->d_name<<" ";
			}
		}
		closedir(d);
	}

}
void ftpServer::receive_put(int sockfd,const std::string& sdfsFileName,const char *data,int size)
{
	std::string sdfsFileNamePath = 	SDFS_DIR + sdfsFileName;
	createDir(sdfsFileNamePath);
	std::ofstream outfile(sdfsFileNamePath.c_str(),std::ios::out|std::ios::binary);
	outfile.write(data,size);
	int received = 0;
	do 
	{
		const int BUFFER_LEN = 4096;
		char buf[BUFFER_LEN];
      		received = recv(sockfd,buf,BUFFER_LEN,0);
		//std::cout<<"received = "<<received<<"\n";
	      	outfile.write(buf,received);
        } while(received > 0);
	outfile.close();
	close(sockfd);
}
void ftpServer::receive_delete(int sockfd,const std::string& sdfsFileName)
{
	std::string sdfsFileNamePath = 	SDFS_DIR + sdfsFileName;
	remove(sdfsFileNamePath.c_str());
	close(sockfd);
}
