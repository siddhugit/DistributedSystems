#
# Makefile for MP - 2
#

####### Compiler and options

CPPC            = g++

####### Files

node: node.o  udp.o
	$(CPPC) -o node node.o udp.o -lpthread

node.o: node.cpp
	$(CPPC) -c node.cpp

udp.o: udp.cpp
	$(CPPC) -c udp.cpp


clean:
	rm -r node node.o udp.o
