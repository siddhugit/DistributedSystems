#ifndef __UDP_H_INCLUDED__
#define __UDP_H_INCLUDED__

#include <string>
#include <vector>

struct UdpMessage
{
	std::string senderHostName;
	std::string senderIpAddress;
	std::string message;
	int type;
	int timeStamp;
	UdpMessage(const std::string& hName,const std::string& ip
		,const std::string& msg,int mtype,int tstamp):senderHostName(hName),
		senderIpAddress(ip),message(msg),type(mtype),timeStamp(tstamp){}
};

class UDP
{
	private:
		static int getSocketAddress(const char *hostname, int port, struct sockaddr_in *addrp);
		static int getServerSocket();
		
		static int PORTNUM;
		static int ServerSockFd;
	public:
		static std::string getIpv4FromHost(const std::string& host);
		static void send_ip(const std::string& ipv4,const std::string& message,int msgType,int timeStamp);
		static void send(const std::string& host,const std::string& message,int msgType,int timeStamp);
		static int receive(std::vector<UdpMessage>& udpMessages);
		static void init();
};

#endif // __UDP_H_INCLUDED__


