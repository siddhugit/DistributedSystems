#ifndef TCP_SOCKET_H__
#define TCP_SOCKET_H__

#include <string>

#define FTPPORTNUM 2121


std::string getHostName(int id);
std::string getMasterHostname();
std::string getSDFSHostname();

class Socket
{
	public:
	static int make_server_socket( int portnum );
	//static void handle_request(int fd);
	static int connect_to_server( char *hostname, int portnum );
	static int send_request(const std::string& hostName,const std::string& message);
};

#endif //TCP_SOCKET_H__
