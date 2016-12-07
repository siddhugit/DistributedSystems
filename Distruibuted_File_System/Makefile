#
# makefile for MP - 3
#

####### Compiler and options
CPPC = g++
OPTIONS = -std=c++11
LIBS = -lpthread
####### Files

all: node sdfs

node: client-server.o tcpSocket.o ftpClient.o
	$(CPPC) $(OPTIONS) $(LIBS) -o node client-server.o ftpClient.o tcpSocket.o 

sdfs: sdfs.o tcpSocket.o ftpServer.o
	$(CPPC) $(OPTIONS) $(LIBS) -o sdfs sdfs.o tcpSocket.o ftpServer.o 

tcpSocket.o: tcpSocket.cpp
	$(CPPC) $(OPTIONS) -c tcpSocket.cpp

ftpClient.o: ftpClient.cpp
	$(CPPC) $(OPTIONS) -c ftpClient.cpp

client-server.o: client-server.cpp
	$(CPPC) $(OPTIONS) -c client-server.cpp

ftpServer.o: ftpServer.cpp
	$(CPPC) $(OPTIONS) -c ftpServer.cpp

sdfs.o: sdfs.cpp
	$(CPPC) $(OPTIONS) -c sdfs.cpp


clean:
	rm -r node sdfs sdfs.o client-server.o tcpSocket.o ftpClient.o ftpServer.o

