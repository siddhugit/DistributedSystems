#
# makefile for MP - 1
#

####### Compiler and options

CC            = gcc
CPPC            = g++

####### Files

all: dgrep dgrepd

dgrep: dgrep.o 
	$(CPPC) -o dgrep dgrep.o -lpthread

dgrepd: dgrepd.o 
	$(CC) -o dgrepd dgrepd.o

dgrep.o: dgrep.cpp
	$(CPPC) -c dgrep.cpp

dgrepd.o: dgrepd.c
	$(CC) -c dgrepd.c


clean:
	rm -r dgrepd dgrep dgrepd.o dgrep.o