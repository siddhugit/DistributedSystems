#include <iostream>
#include <cstdio>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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
#include <map>
#include <set>
#include <limits.h>
#include <sys/time.h>
#include "sdfsClient.h"
#include "tcpSocket.h"
#include "common.h"

#define	LINELEN	1024
#define	ARGLEN	16
#define	HOSTNAMELEN 64
#define MASTERPORTNUM 1982
#define WORKERPORTNUM 1983
#define BUFF_LEN 4096

pthread_mutex_t maple_juice_output_mutex;
std::vector<std::string> mapleJuiceOutputStrings;

const std::string LOCAL_DIR = getLocalTempDir();
const std::string LOCAL_DIR_JUICE = getHomeDir();
const std::string KEYS_FILE_NAME = getKeyFileName();

std::map<std::string,int> FileHandleMap;
std::set<std::string> KeysSet;
const unsigned int BIG_PRIME = 2654435769u;


SDFSClient* getSDFSObj()
{
	static SDFSClient obj;
	return &obj;
}

int hash(unsigned int key, int m)
{
    int result = ((BIG_PRIME*key)>>(sizeof(int)*CHAR_BIT - 10));
    return (result%m);
}
int hash(const std::string& str, int m)
{
	unsigned int result = 0;
	for(int i = 0;i < str.size(); ++i)
	{
		char c = str[i];
		result += c;
	}
	return hash(result,m);
}

FILE* getFileHandle(const std::string& prefix,const std::string& key)
{
	/*std::map<std::string,int>::const_iterator it;
	if((it = FileHandleMap.find(key)) != FileHandleMap.end())
	{
		if(key == "america")
		{
			std::cout<<"returning from 1\n";
		}
		return it->second;
	}
	else
	{
		mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
		std::string fileName = LOCAL_DIR + prefix + "_" + key;
		int fd getAllFiles= creat(fileName.c_str(), mode);
		FileHandleMap.insert(std::make_pair(key,fd));
		if(key == "america")
		{
			std::cout<<"returning from 2\n";
		}
		return fd;
	}*/
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH ;
	std::string fileName = LOCAL_DIR + prefix + "_" + key;
	FILE* fp = fopen(fileName.c_str(), "a+");
	return fp;
}

void closeFileHandles()
{
	std::map<std::string,int>::const_iterator it = FileHandleMap.begin();
	for(;it != FileHandleMap.end(); ++it)
	{
		close(it->second);
	}
	FileHandleMap.clear();
}
/*
*   SIGCHLD handler
*/
void child_handler(int signum)
{
    //cleans up all terminated child processes
    while(waitpid(-1,NULL,WNOHANG) != -1);
}

std::vector<std::string> getSdfsFileNames(const std::string& sdfsDir)
{
	SDFSClient* sdfsObjPtr = getSDFSObj();
	std::vector<std::string> fileNames = sdfsObjPtr->list(sdfsDir);
	return fileNames;
}

std::vector<std::vector<int> > partition(int numMaples,const std::vector<std::string>& fileNames)
{
	std::vector<std::vector<int> > result;
	int numFiles = fileNames.size();
	int filePerMaple = numFiles/numMaples;
	int remainingFiles = numFiles%numMaples;
	for(int i = 0; i < numMaples; ++i)
	{
		std::vector<int> temp;
		for(int j = 0; j < filePerMaple; ++j)
		{
			temp.push_back(i*filePerMaple + j);
		}
		result.push_back(temp);
	}
	for(int i = 0; i < remainingFiles; ++i)
	{
		result[i].push_back(filePerMaple*numMaples + i);
	}
	return result;
}

std::string getCatFileNames(const std::vector<int>& indexes,const std::vector<std::string>& fileNames)
{
	std::string commandLine;
	std::vector<int>::const_iterator it = indexes.begin();
	for(;it != indexes.end(); ++it)
	{
		if(it == indexes.begin())
		{
			commandLine += fileNames[*it];
		}
		else
		{
			commandLine += ( ";" + fileNames[*it] ); 
		}
	}
	return commandLine;
}

void *readOutput(void *fd)
{
   	int sockfd;
	sockfd = (long)(fd);
   	char buff[BUFF_LEN];
	std::stringstream sstr;
	while(1)
	{
		int rd = read(sockfd,buff,BUFF_LEN);
		if(rd <= 0)
		{
			break;
		}
		buff[rd] = '\0';
		sstr<<buff;
	}
	std::string outputStr = sstr.str();
	pthread_mutex_lock(&maple_juice_output_mutex);
	mapleJuiceOutputStrings.push_back(outputStr);
	pthread_mutex_unlock(&maple_juice_output_mutex);
   	pthread_exit(NULL);
}



void send_to_server(const std::string& message,pthread_t *thread,int hostId)
{
	std::string hostName = getHostName(hostId);
	int sockfd = Socket::connect_to_server((char*)hostName.c_str(),WORKERPORTNUM);
	if(sockfd != -1)
	{
		int sent = write(sockfd,message.c_str(),message.size());
		
		pthread_create(thread, NULL, readOutput, (void *)((long)sockfd));
	}
}
void processOutput(const std::string& outputStr,const std::string& prefix)
{
	std::istringstream sstr(outputStr);
	std::string line;    
    	while (std::getline(sstr, line)) 
	{
		std::string::size_type pos = line.find_first_of(',');
		std::string key = line.substr(0, pos);
		std::string val = line.substr(pos + 1, line.size());	
		FILE* fp = getFileHandle(prefix,key);
		int fd = fileno(fp);
		std::string valStr = key + "," + val + "\n"; 
		write(fd,valStr.c_str(),valStr.size());
		fclose(fp);
		KeysSet.insert(key);
    	}
}

void outputKeys(const std::string& prefix)
{
	std::ofstream outFile(KEYS_FILE_NAME.c_str());
	std::set<std::string>::const_iterator it = KeysSet.begin();
	SDFSClient* sdfsObjPtr = getSDFSObj(); 
	
	for(;it != KeysSet.end(); ++it)
	{
		std::string sdfsFileName = prefix + "_" + *it;
		std::string fileName = LOCAL_DIR + sdfsFileName;
		int sockfd = sdfsObjPtr->connect();
		sdfsObjPtr->putLocaltoSdfs(sockfd,fileName,sdfsFileName);
		close(sockfd);
		remove(fileName.c_str());
		outFile<<*it<<"\n";
		std::cout<<"Maple: "<<*it<< " Processed\n";
	}
	outFile.close();
	std::string sdfsDestFileName = prefix + "_maple_output_keys";
	//sdfsObjPtr->putLocaltoSdfs(KEYS_FILE_NAME,sdfsDestFileName);
}

void handle_maple(int argc,char **args)
{	
	struct timeval start, end;
	gettimeofday(&start, NULL);
	std::string mapleExe = args[1];
	int numMaples = atoi(args[2]);
	std::string dirPrefix = args[3];
	std::string sdfsDir = args[4];
	std::vector<std::string> localFiles = getSdfsFileNames(sdfsDir);
	std::vector<std::vector<int> > inputs = partition(numMaples,localFiles);
	std::vector<int> readFds;
	std::vector<FILE*> FPs;
	std::vector<pthread_t> outputThreads;
	mapleJuiceOutputStrings.clear();
	KeysSet.clear();
	for(int i = 0; i < numMaples;++i)
	{
		std::vector<int> indexes = inputs[i];
		if(indexes.size() > 0)
		{
			pthread_t thread;
			outputThreads.push_back(thread);
		}
	}
	int threadIndex = 0;
	int hostId = 2;
	for(int i = 0; i < numMaples;++i)
	{
		std::vector<int> indexes = inputs[i];
		if(indexes.size() > 0)
		{
			std::string catFileNames = getCatFileNames(indexes,localFiles);
			std::stringstream ss;
			ss<<"maple "<<mapleExe<<" "<<dirPrefix<<" "<<sdfsDir<<" "<<catFileNames<<"\n";
			send_to_server(ss.str(),&outputThreads[threadIndex],hostId);
			++threadIndex;++hostId;
		}
	}
	for(int i = 0; i < outputThreads.size();++i)
	{
		pthread_join(outputThreads[i],NULL);
	}
	for(int i = 0; i < mapleJuiceOutputStrings.size();++i)
	{
		processOutput(mapleJuiceOutputStrings[i],dirPrefix);
	}
	closeFileHandles();
	outputKeys(dirPrefix);
	KeysSet.clear();
	gettimeofday(&end, NULL);
	std::cout<<"Maple Completed in "<<end.tv_sec - start.tv_sec<<" seconds\n";
}



//juice starts
std::string getCatKeys(const std::vector<std::string>& keysToHandle)
{
	std::string commandLine;
	std::vector<std::string>::const_iterator it = keysToHandle.begin();
	for(;it != keysToHandle.end(); ++it)
	{
		if(it == keysToHandle.begin())
		{
			commandLine += *it;
		}
		else
		{
			commandLine += ( ";" + *it ); 
		}
	}
	return commandLine;
}
std::vector<std::string> getKeys(const std::string& intFilePrefix)
{
	//prefix_maple_output_keys
	//std::string sdfsFileName = intFilePrefix + "_maple_output_keys";
	//SDFSClient* sdfsObjPtr = getSDFSObj();
	//sdfsObjPtr->getLocalFileName(sdfsFileName,KEYS_FILE_NAME);
	std::vector<std::string> result;
	std::ifstream inFile(KEYS_FILE_NAME.c_str());
	std::string line; 
	while(std::getline(inFile,line))
	{
		result.push_back(line);
	}
	remove(KEYS_FILE_NAME.c_str());
	return result;
}
void processJuiceOutput(const std::string& destFilename)
{
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	std::string fileName = LOCAL_DIR_JUICE + destFilename;
	int fd = creat(fileName.c_str(), mode);
	for(int i = 0; i < mapleJuiceOutputStrings.size();++i)
	{
		write(fd,mapleJuiceOutputStrings[i].c_str(),mapleJuiceOutputStrings[i].size());
	}
	close(fd);
	SDFSClient* sdfsObjPtr = getSDFSObj(); 
	sdfsObjPtr->putLocaltoSdfs(fileName,destFilename);
	remove(fileName.c_str());
}
std::vector<std::vector<std::string> > hashPartition(const std::vector<std::string>& keys,int numJuices)
{
	std::vector<std::vector<std::string> > juiceTasks(numJuices,std::vector<std::string>());
	for(int i = 0; i < keys.size();++i)
	{
		int hashVal = hash(keys[i],numJuices);
		juiceTasks[hashVal].push_back(keys[i]);
	}
	return juiceTasks;
}
std::vector<std::vector<std::string> > rangePartition(const std::vector<std::string>& keys,int numJuices)
{
	std::vector<std::vector<std::string> > juiceTasks(numJuices,std::vector<std::string>());
	int slots = 26/numJuices;
	for(int i = 0; i < keys.size();++i)
	{
		int hashVal = 0;
		if(keys[i][0] >= '0' && keys[i][0] <='9')
		{
			hashVal = 0;
		}
		else
		{
			hashVal = (keys[i][0] - 'a')/slots;
			if(hashVal == numJuices)
				--hashVal;
		}
		juiceTasks[hashVal].push_back(keys[i]);
	}
	return juiceTasks;
}
void handle_juice(int argc,char **args)
{
	struct timeval start, end;
	gettimeofday(&start, NULL);
	std::string juiceExe = args[1];
	int numJuices = atoi(args[2]);
	std::string intFilePrefix = args[3];
	std::string sdfsDestFilename = args[4];
	std::vector<std::string> keys = getKeys(intFilePrefix);
	std::vector<std::vector<std::string> > juiceTasks;
	if(argc == 6 && std::string(args[5]) == "r")
	{
		juiceTasks = rangePartition(keys,numJuices);
	}
	else
	{
		juiceTasks = hashPartition(keys,numJuices);
	}
	std::vector<pthread_t> outputThreads;
	mapleJuiceOutputStrings.clear();
	for(int i = 0; i < numJuices;++i)
	{
		std::vector<std::string> keysToHandle = juiceTasks[i];
		if(keysToHandle.size() > 0)
		{
			pthread_t thread;
			outputThreads.push_back(thread);
		}
	}
	int hostId = 2;
	int threadIndex = 0;
	for(int i = 0; i < numJuices;++i)
	{
		std::vector<std::string> keysToHandle = juiceTasks[i];
		if(keysToHandle.size() > 0)
		{
			std::stringstream ss;
			std::string catKeys = getCatKeys(keysToHandle);
			ss<<"juice "<<juiceExe<<" "<<intFilePrefix<<" "<<catKeys<<"\n";
			send_to_server(ss.str(),&outputThreads[threadIndex],hostId);
			//std::cout<<"Juice: VM-"<<hostId<<"handles "<<catKeys<<"\n";
			++threadIndex;++hostId;
		}
	}
	for(int i = 0; i < outputThreads.size();++i)
	{
		pthread_join(outputThreads[i],NULL);
	}
	processJuiceOutput(sdfsDestFilename);
	gettimeofday(&end, NULL);
	std::cout<<"Juice Completed in "<<end.tv_sec - start.tv_sec<<" seconds\n";
}
//juice ends

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
	    if(command == "maple")
	    {
		handle_maple(index,args);
	    }
	    else if(command == "juice")
	    {
		handle_juice(index,args);
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
	
	int sock_id = Socket::make_server_socket(MASTERPORTNUM);
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
