#include "udp.h"
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <cstdlib>
#define HOSTNAMESIZE 256
#define BUFFSIZE 4096

int UDP::PORTNUM = 15000;
int UDP::ServerSockFd = -1;


int UDP::getSocketAddress(const char *hostname, int port, struct sockaddr_in *addrp)
{
	struct hostent *hp;
	bzero((void*)addrp, sizeof(struct sockaddr_in));
	hp = gethostbyname(hostname);
	if(hp == NULL) return -1;
	bcopy((void *)hp->h_addr, (void *)&addrp->sin_addr, hp->h_length);
	addrp->sin_port = htons(port);
	addrp->sin_family = AF_INET;
	return 0;
}

int UDP::getServerSocket()
{
	sockaddr_in serverAddr;
	int sockFd = socket(PF_INET, SOCK_DGRAM, 0);
	if(sockFd == -1)
	{ 
		return -1;
	}
	char hostname[HOSTNAMESIZE];	
	gethostname(hostname, HOSTNAMESIZE);
	getSocketAddress(hostname, PORTNUM, &serverAddr);
	if(bind(sockFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0)
		return -1;
	return sockFd;
}

void UDP::send_ip(const std::string& ipv4,const std::string& message,int msgType,int timeStamp)
{
	struct in_addr ipv4addr;
	inet_pton(AF_INET, ipv4.c_str() , &ipv4addr);
	hostent *he = gethostbyaddr(&ipv4addr, sizeof ipv4addr, AF_INET);
	send(he->h_name,message,msgType,timeStamp);
}

void UDP::send(const std::string& host,const std::string& message,int msgType,int timeStamp)
{
   if(rand()%100 <= -1) return;
	int sockFd = socket(PF_INET, SOCK_DGRAM, 0);
	if(sockFd == -1)
	{
		std::cerr<<"send():could not create socket\n";
		return;
	}
	sockaddr_in serverAddr;
	getSocketAddress(host.c_str(), PORTNUM, &serverAddr);
	char msgToSend[BUFFSIZE];
	uint32_t hostInt = msgType;
	uint32_t networkInt = htonl(hostInt);
	memcpy(msgToSend, &networkInt, sizeof(uint32_t));
	hostInt = timeStamp;
	networkInt = htonl(hostInt);
	memcpy(msgToSend + sizeof(uint32_t), &networkInt, sizeof(uint32_t));
	memcpy(msgToSend + 2*sizeof(uint32_t),message.c_str(), message.size());
	if(sendto(sockFd, msgToSend, message.size() + 2*sizeof(uint32_t),0,(const sockaddr*)&serverAddr, sizeof(serverAddr)) == -1)
	{
        	std::cerr<<"send():could not send message\n";
	}
	close(sockFd);
}

int UDP::receive(std::vector<UdpMessage>& udpMessages)//caller should pass an empty vector
{
	if(ServerSockFd != -1)
	{
		int buffLen;
		sockaddr_in serverAddr;
		socklen_t serverAddrLen;
		char buff[BUFFSIZE];
		int bytesRead = 0;
		while((buffLen = recvfrom(ServerSockFd, buff, BUFFSIZE, 0, (sockaddr*)&serverAddr, &serverAddrLen)) > 0)
		{
			buff[buffLen] = '\0';
			bytesRead += buffLen;
			std::string message = buff + 2*sizeof(uint32_t);
			uint32_t *networkIntPtr = (uint32_t *)buff;
			int msgType = ntohl(*networkIntPtr);
			networkIntPtr = (uint32_t *)(buff + sizeof(uint32_t));
			int tStamp = ntohl(*networkIntPtr);
			char ipAddress[HOSTNAMESIZE];
			strncpy(ipAddress, inet_ntoa(serverAddr.sin_addr), HOSTNAMESIZE);
			struct in_addr ipv4addr;			
			inet_pton(AF_INET, ipAddress, &ipv4addr);
			hostent *he = gethostbyaddr(&ipv4addr, sizeof(ipv4addr), AF_INET);
			std::string hName;
			if(he)
				hName = he->h_name;
			UdpMessage udpMessage(hName,ipAddress,message,msgType,tStamp);
			udpMessages.push_back(udpMessage);
	    	}
		return bytesRead;
	}
	return -1;
}

std::string UDP::getIpv4FromHost(const std::string& host)
{
	struct in_addr ipv4addr;			

	inet_pton(AF_INET, host.c_str(), &ipv4addr);
	hostent *he = gethostbyaddr(&ipv4addr, sizeof(ipv4addr), AF_INET);

	std::string hName = he->h_name;
	return hName;
}

void UDP::init()
{	
	int sockFd;
	if((sockFd = getServerSocket()) == -1)
	{
        	std::cerr<<"init():cannot make server socket\n";
		return;
	}
	ServerSockFd = sockFd;
	int flags = fcntl(ServerSockFd, F_GETFL, 0);
	fcntl(ServerSockFd, F_SETFL, flags | O_NONBLOCK);
}
/*
int main(int argc,char **argv)
{
	if(argc != 2)
	{
		return 1;
	}
	UDP::init();
	if(strcmp("send",argv[1]) == 0)//call send()
	{
		while(true)
		{		
			std::string message = "Ping Message";
			int msgType = 0;
			int timeStamp = 42342;
			std::string host = "fa16-cs425-g40-01.cs.illinois.edu"; //send to vm-1 from let's say vm-2
			UDP::send(host,message,msgType,timeStamp);
			message = "Ack Message";
			msgType = 1;
			timeStamp = 57436;
			UDP::send(host,message,msgType,timeStamp);
			message = "";
			msgType = 3;
			timeStamp = 9679345;
			UDP::send(host,message,msgType,timeStamp);
			message = "Timeout Message";
			msgType = 2;
			timeStamp = 63687;
			UDP::send(host,message,msgType,timeStamp);
			sleep(1);
		}
	}
	else //call receive()
	{
		std::string message,host;
		int msgType;
		while(true)
		{
			std::vector<UdpMessage> udpMessages;
			int bytesRead = UDP::receive(udpMessages);
			std::vector<UdpMessage>::const_iterator it = udpMessages.begin();
			for(; it != udpMessages.end(); ++it)
			{
				std::cout<<"Sender Hostname : "<< it->senderHostName <<"\n";
				std::cout<<"Message Type : "<< it->type <<"\n";
				std::cout<<"Timestamp : "<< it->timeStamp <<"\n";
				std::cout<<"Message : "<< it->message <<"\n\n";
			}
			sleep(2);
		}
	}
	return 0;
}

*/
