#include "sdfsClient.h"
#include "ftpClient.h"
#include "tcpSocket.h"
#include <iostream>
#include <sstream>

int SDFSClient::connect()
{
	return ftpClient::connect(getSDFSHostname());
}

std::vector<std::string> SDFSClient::list(const std::string& sdfsDir)
{
	std::vector<std::string> result;
	std::string outputStr = ftpClient::send_list(getSDFSHostname(),sdfsDir);
	std::istringstream f(outputStr);
    	std::string s;    
    	while (std::getline(f, s, ' ')) 
	{
        	result.push_back(sdfsDir + "/" + s);
    	}
	return result;
}
void SDFSClient::getLocalFileName(const std::string& sdfsFilename, const std::string& localFilename)
{
	ftpClient::send_get(getSDFSHostname(),sdfsFilename,localFilename);
}
void SDFSClient::putLocaltoSdfs(int sockfd,const std::string& localFilename, const std::string& sdfsFilename)
{
	ftpClient::send_put(sockfd,localFilename,sdfsFilename);
}
void SDFSClient::putLocaltoSdfs(const std::string& localFilename, const std::string& sdfsFilename)
{
	ftpClient::send_put(getSDFSHostname(),localFilename,sdfsFilename);
}
void SDFSClient::deleteSdfsFile(const std::string& sdfsFilename)
{
	ftpClient::send_delete(getSDFSHostname(),sdfsFilename);
}

/*int main()
{
	SDFSClient client;
	client.list("dir");
}*/
