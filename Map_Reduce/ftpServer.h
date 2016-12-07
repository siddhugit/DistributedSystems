#ifndef FTP_SERVER_H__
#define FTP_SERVER_H__

#include <string>

class ftpServer
{
	private:
	static const std::string SDFS_DIR;
	static void createDir(const std::string& filaPath);
	public:
	static void receive_get(int sockfd,const std::string& sdfsFileName);
	static void receive_list(int sockfd,const std::string& sdfsDir);
	static void receive_put(int sockfd,const std::string& sdfsFileName,const char *data,int fileSize);
	static void receive_delete(int sockfd,const std::string& sdfsFileName);
};

#endif //FTP_SERVER_H__
