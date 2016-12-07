#
# makefile for MP - 4
#

####### Compiler and options
CPPC = g++
OPTIONS = -std=c++11
LIBS = -lpthread
####### Files

all: master worker maple juice maple_wordcount juice_wordcount sdfs

master: master.cpp ftpClient.cpp tcpSocket.cpp sdfsClient.cpp common.cpp
	$(CPPC) master.cpp ftpClient.cpp tcpSocket.cpp sdfsClient.cpp common.cpp $(LIBS) -o master

worker: worker.cpp ftpClient.cpp tcpSocket.cpp sdfsClient.cpp common.cpp
	$(CPPC) -o worker worker.cpp ftpClient.cpp tcpSocket.cpp sdfsClient.cpp common.cpp 

maple: maple.cpp tcpSocket.cpp
	$(CPPC) maple.cpp tcpSocket.cpp -o maple 

juice: maple.cpp tcpSocket.cpp
	$(CPPC) juice.cpp tcpSocket.cpp -o juice 

maple_wordcount: maple_wordcount.cpp
	$(CPPC) maple_wordcount.cpp  -o maple_wordcount

juice_wordcount: juice_wordcount.cpp
	$(CPPC) juice_wordcount.cpp  -o juice_wordcount

sdfs: ftpServer.cpp tcpSocket.cpp sdfs.cpp
	$(CPPC) ftpServer.cpp tcpSocket.cpp sdfs.cpp -o sdfs

ftpServer.o: ftpServer.cpp
	$(CPPC) $(OPTIONS) -c ftpServer.cpp

sdfs.o: sdfs.cpp
	$(CPPC) $(OPTIONS) -c sdfs.cpp


clean:
	rm -r master worker maple juice maple_wordcount juice_wordcount sdfs

