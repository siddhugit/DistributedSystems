#ifndef SDFS_CLIENT_H__
#define SDFS_CLIENT_H__

#include <vector>
#include <string>

class SDFSClient
{
public:
	std::vector<std::string> list(const std::string& sdfsDir);
	void getLocalFileName(const std::string& sdfsFilename, const std::string& localFilename);
	void putLocaltoSdfs(const std::string& localFilename, const std::string& sdfsFilename);
	void putLocaltoSdfs(int sockfd,const std::string& localFilename, const std::string& sdfsFilename);
	void deleteSdfsFile(const std::string& sdfsFilename);
	int connect();
};

#endif //SDFS_CLIENT_H__
