// connecting on port 10000
// log files in /tmp/vm*.log
// threads
// receive "args" in command line
// exec("grep args")
// in servers, exec hostname, receive which vm we are
//fa16-cs425-g40-XX.cs.illinois.edu
// where XX is 01 - 07

#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h> // for debug output
#include <unistd.h>
#include <vector>
#include <pthread.h>
#include <algorithm> // for count

using namespace std;
static int ProcessNumber = 7;
static string MagicPortNumber = "10000";

struct ThreadData{
   int threadId;
   string log;
   int socketNum;
};

string ServerNames[7] = 
                        {"fa16-cs425-g40-01.cs.illinois.edu",
                         "fa16-cs425-g40-02.cs.illinois.edu",
                         "fa16-cs425-g40-03.cs.illinois.edu",
                         "fa16-cs425-g40-04.cs.illinois.edu",
                         "fa16-cs425-g40-05.cs.illinois.edu",
                         "fa16-cs425-g40-06.cs.illinois.edu",
                         "fa16-cs425-g40-07.cs.illinois.edu"};


void* Receive(void* data)
{
   const unsigned int BUFFER_LEN = 4096;
   char buf[BUFFER_LEN];
   int received = 0;
   ThreadData* myData = (ThreadData*) data;
   int myProcess = myData->threadId;
   do {
      received = recv(myData->socketNum,buf,BUFFER_LEN,0);
      if(received == -1)
      {
         myData->log = "";
         printf("Issue with receive from socket %d with error %d\n",myProcess+1,errno);
         pthread_exit((void*)&myData->log); // should be fail-safe because threaded
      }
      else
      {
		  myData->log += string(buf,received);
      }
   } while(received > 0);//while(received == BUFFER_LEN); // while we are still receiving from the socket
   pthread_exit((void*)&myData->log);
   
}

int connect_to_server( char *hostname, int portnum )
{
	struct sockaddr_in  servadd;        /* the number to call */
	struct hostent      *hp;            /* used to get number */
	int    sock_id;			    /* returned to caller */

        /*
         *  build the network address of where we want to call
         */

       memset( &servadd, 0, sizeof( servadd ) );   /* 0. zero the address   */
       servadd.sin_family = AF_INET ;              /* 1. fill in addr type  */

       hp = gethostbyname( hostname );		   /* 2. and host addr      */
       if ( hp == NULL ) return -1;
       memcpy( &servadd.sin_addr, hp->h_addr, hp->h_length);
       servadd.sin_port = htons(portnum);          /* 3. and port number    */

       /*
        *        make the connection
        */

       sock_id = socket( PF_INET, SOCK_STREAM, 0 );    /* get a line   */
       if ( sock_id == -1 ) return -1;                 /* or fail      */
                                                       /* now dial     */
       if ( connect(sock_id,(struct sockaddr*)&servadd, sizeof(servadd)) !=0 )  
               return -1;
       return sock_id;
}

int main(int argc, char *argv[])
{
   int i;
   stringstream ss;
   for(i=0;i<argc;++i)
      ss<<argv[i]<<" ";
   ss<<"\n";
   ss.seekg(0,ios::end);
   // everything in ss goes to the socket (to each of the machines)
   bool run[ProcessNumber];
   int sockets[ProcessNumber];
   struct addrinfo hints[ProcessNumber], *results;
   
    for(i=0; i < ProcessNumber; ++i)
	{
		run[i] = false; //initialize
	}

   for(i=0; i < ProcessNumber; ++i)
   {
	  sockets[i] = connect_to_server((char*)ServerNames[i].c_str(),10000);
	  if(sockets[i] < 0)
      {
         cerr<<"Failure to connect to socket "<<i+1<<" with error "<<errno<<endl;
         continue;
      }
	  int sent = write(sockets[i],ss.str().c_str(),ss.str().size());
      if(sent < 0)
      {
         cerr<<"Failure to send to socket "<<i+1<<" with error "<<errno<<endl;
         continue;
      }
      else if(sent < ss.tellg())
      {
         cerr<<"couldn't send all of input in one chunk. please play nice."<<endl;
         return 1;
      }
      run[i]=true; // if we make it this far, we should listen for a response
   }  
   pthread_t threads[ProcessNumber];
   pthread_attr_t attr;
   pthread_attr_init(&attr);
   pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
   ThreadData threadData[ProcessNumber]; 
   string resultStrings[ProcessNumber];
   int lineCount[ProcessNumber];
   for(i=0; i<ProcessNumber; ++i) // ensure we have one process dedicated to each socket
   {
      resultStrings[i] = "";
      lineCount[i] = 0;
      if(!run[i]) // don't run processes that don't have valid sockets
         continue;
      threadData[i].threadId = i;
      threadData[i].log = "";
      threadData[i].socketNum = sockets[i];
      pthread_create(&threads[i], NULL, Receive, (void *)(&threadData[i]));
   }
   pthread_attr_destroy(&attr);
   void* retVal;
   for(i=0; i<ProcessNumber; ++i)
   {
      if(!run[i]) continue; // don't join threads that we didn't create
      pthread_join(threads[i], &retVal);
      resultStrings[i] = *((string *) retVal); // receive results from threads
   }
   int totalLines = 0;
   for(i=0;i<ProcessNumber;++i) // for each result we got:
   {
      if(resultStrings[i].size()==0)
      {
         continue;
      }
      lineCount[i] = std::count(resultStrings[i].begin(),resultStrings[i].end(),'\n');
      // process and print it (and its relevant data) out
      printf("From %s:\n%s\n",ServerNames[i].c_str(),resultStrings[i].c_str());
   }  
    for(i=0;i<ProcessNumber;++i) // for each result we got:
   {
      if(resultStrings[i].size()==0)
      {
         if(run[i])
            printf("No results from logfile vm%d.log\n",i + 1); // there was no match
         else
            printf("No results received from logfile vm%d.log: process failed.\n",i + 1); // the process failed
         continue;
      }      
      totalLines += lineCount[i];
      // process and print it (and its relevant data) out
      printf("%d lines matched from logfile vm%d.log\n",lineCount[i],i+1,resultStrings[i].c_str());
   }  
   printf("%d lines matched in total from all non-failing processes.\n",totalLines); // don't forget the summary
   for(i=0;i<ProcessNumber;++i)
   {
		close(sockets[i]); // cleanup
   }
}

