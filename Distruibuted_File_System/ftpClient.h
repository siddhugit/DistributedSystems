#ifndef FTP_CLIENT_H__
#define FTP_CLIENT_H__

#include <string>

class ftpClient
{
	public:
	static void send_get(const std::string& hostName,const std::string& sdfsFileName,const std::string& localFileName);
	static std::string send_list(const std::string& hostName,const std::string& sdfsDir);
	static void send_put(const std::string& hostName,const std::string& localFileName,const std::string& sdfsFileName);
	static void send_delete(const std::string& hostName,const std::string& sdfsFileName);
	static int connect(const std::string& hostName);
	static void send_put(int sockfd,const std::string& localFileName,const std::string& sdfsFileName);
};

#endif //FTP_CLIENT_H__
