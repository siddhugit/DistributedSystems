/* pseudocode:
  contains other_nodes list(ids of all other entries)
  contains waiting_nodes queue(ids of those we are waiting for, timestamp of ping sent), represented as list, because we will require traversal
  contains list_counter (index in other_nodes list to ping) 

   set current_time = local_time_lookup
   if ! id.has_current_time
      set id.current_time = current_time
   if hostname = "fa16-cs425-g40-01.cs.illinois.edu" // if we are the initiator
      look for config.file
      if config.file
         update other_nodes list based on config.file
   else 
      while(true) // try to connect to existing group
         ping "fa16-cs425-g40-01.cs.illinois.edu" (intro_message)
         if ack
            update other_nodes list based on ack
            break
   main_loop:
      on 1sec timer:
         ping other_nodes[list_counter] // how does this work?
         waiting_nodes.push_back((current_time,other_nodes[list_counter]))
         if hostname is "fa16-cs425-g40-01.cs.illinois.edu" // do this in actual loop to prevent loss of ring on failure of initiator
            write other_nodes to /tmp/existing.txt
         
      receive_pings() // and update other_nodes list with failures / joins
      respond_to_pings() // and push back the ack with our other_nodes data
      
      while waiting_nodes.peek().second not in other_nodes OR current_time - waiting_nodes.peek().first > 3 seconds   // lost something we were waiting for, or got a timeout
         if waiting_nodes.peek().second not in other_nodes
            waiting_nodes.pop()
         else
            // waiting_nodes.peek().second is timed out
            timeout_node = waiting_nodes.pop().second
            remove timeout_node from other_nodes
            multicast timeout_node to all in other_nodes (timeout_message) // there's probably a better way to do this, probably the same way as we detect failure in 3 seconds.
      if leave_command //  catch ctrl-c?
         multicast leave to all in other_nodes (leave_message)
         exit_cleanly
            
            
   // create class to maintain other_nodes list
   // on add or remove or leave, write this with timestamps to known logfile (/tmp/vm*.log)
*/

#include <iostream>
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <string.h> // strtok
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h> // for debug output
#include <unistd.h>
#include <vector>
#include <list>
#include <ctime> // for timestamp
#include "udp.h"
//#include <pthread.h>
#include <algorithm> // for count, random_shuffle
#include <cstdlib> // std::rand, srand

#include <signal.h>
#include <stdlib.h> // to catch ctrl-C for leave rather than fail

using namespace std;

enum Message_Type {
   PING = 0,
   INTRO = 1,
   ACK = 2,
   INTRO_ACK = 3,
   TIMEOUT = 4,
   LEAVE = 5
};

class ID
{
public:
   ID(string aName, int aTime) {hostname = aName; create_timestamp = aTime; waiting_timestamp = 0;}
   ID() {hostname = ""; create_timestamp = 0; waiting_timestamp = 0;}

   bool operator==(const ID& other) { return hostname.compare(other.hostname)==0 && create_timestamp == other.create_timestamp; }
   string hostname;
   int create_timestamp;
   int waiting_timestamp;
};
class Message
{
public:
   Message(ID aID, Message_Type aType ,string aData) 
   {
      mID = aID;
      mType = aType;
      char * each;
      char data[aData.size()+1];
      int i;
      for(i = 0; i < aData.size(); ++i)
         data[i] = aData[i];
      data[i] = '\0';
      each = strtok(data," :\n");
      while(each != NULL)
      {
         string newHostname(each);
         each = strtok(NULL," :\n");
         if(each == NULL) break;
         int newTimestamp = atoi(each);
         ID newID(newHostname,newTimestamp);
         mData.push_back(newID);
         each = strtok(NULL," :\n");
      }      
   }
   ID mID; // who message is from
   Message_Type mType; // type of message
   vector<ID> mData; // representation of data in message
};


vector<ID> other_nodes; // nodes get added when ACK received, removed only on TIMEOUT or LEAVE message receipt
ID myID;
list<ID> waiting_nodes;

string getHostName()
{
   char hostname[64];
   hostname[64 - 1] = '\0';
   gethostname(hostname, 64);
   string retVal(hostname);
   return hostname;
}

void writeToExistingFile(vector<ID> other_nodes)
{
   ofstream outfile;
   outfile.open("/tmp/existing.txt");
   for(vector<ID>::iterator i = other_nodes.begin();i!=other_nodes.end();++i)
      outfile << i->hostname<<" "<<i->create_timestamp<<endl;
   outfile.close();
}


void appendToLogfile(ID myID,string aStr)
{
  ofstream outfile;
  string filename = "/tmp/vm1.log";
  filename[7] = getHostName()[16];
  outfile.open(filename.c_str(), ios_base::app);
  outfile<<aStr<<endl;
  cout<<aStr<<endl; // for screen debugging
  outfile.close();
}

bool loadConfigFile(vector<ID>& other_nodes)
{
   ifstream infile("/tmp/existing.txt");
   if(!infile.good()) return false;
   string hostname;
   string garbage;
   int timestamp;
   stringstream out;
   while(getline(infile,hostname,' ')) // delimited by ' ' character
   {
      infile >> timestamp;
      getline(infile,garbage,'\n');      
      out.str("");
      ID newID = ID(hostname,timestamp);
      out<<"loading "<<hostname<<" "<<timestamp<<" from /tmp/existing.txt file";
      appendToLogfile(myID,out.str());
      other_nodes.push_back(newID);
   }
   return true;
}

void processMessages(vector<Message> aMessages, vector<ID>& other_nodes)
{
   vector<Message>::iterator message;
   for(message = aMessages.begin();message!=aMessages.end();++message)
   {
      switch (message->mType) {
      case PING:
      { 
         ID newID = ID(message->mID.hostname,message->mID.create_timestamp);
         if(find(other_nodes.begin(),other_nodes.end(),newID) == other_nodes.end()) 
         { // if we get a ping from someone not in our list, add them to our list
            stringstream a;
            a<<message->mID.hostname<<" "<<message->mID.create_timestamp<<" pinged us but was not yet in list, adding to local list";
            appendToLogfile(myID, a.str());
            other_nodes.push_back(newID);
         }
         UDP::send(message->mID.hostname,"",ACK,myID.create_timestamp);
         break;      
      }
      case INTRO:
      { 
         ID newID = ID(message->mID.hostname,message->mID.create_timestamp);
         if(find(other_nodes.begin(),other_nodes.end(),newID) == other_nodes.end()) 
         { // if we get a ping from someone not in our list, add them to our list
            stringstream a;
            a<<message->mID.hostname<<" "<<message->mID.create_timestamp<<" is looking to be introduced, adding to local list";
            appendToLogfile(myID, a.str());
            other_nodes.push_back(newID);
         }
         stringstream data;
         for(vector<ID>::iterator it = other_nodes.begin();it != other_nodes.end();++it)
         {
            data << it->hostname<<" "<<it->create_timestamp<<":"; // add data to ack message
         }
         UDP::send(message->mID.hostname,data.str(),INTRO_ACK,myID.create_timestamp);
         break;      
      }
      case ACK:
      {
         // cull this node from the waiting_nodes list: it ack-ed
         list<ID>::iterator i = waiting_nodes.begin();
         for(i; i!=waiting_nodes.end();++i)
         {
            if(*i == message->mID){
               waiting_nodes.erase(i);
               break;}
         }
         ID newID = ID(message->mID.hostname,message->mID.create_timestamp);
         if(find(other_nodes.begin(),other_nodes.end(),newID) == other_nodes.end()) 
         { // if we get an ack from someone not in our list, add them to our list
            stringstream a;
            a<<message->mID.hostname<<" "<<message->mID.create_timestamp<<" responded to ping but was not yet in list, adding to local list";
            appendToLogfile(myID, a.str());
            other_nodes.push_back(newID);
         }
         break;
      }
      case INTRO_ACK:
      {
         ID newID = ID(message->mID.hostname,message->mID.create_timestamp);
         if(find(other_nodes.begin(),other_nodes.end(),newID) == other_nodes.end()) 
         { // if we get an ack from someone not in our list, add them to our list
            stringstream a;
            a<<message->mID.hostname<<" "<<message->mID.create_timestamp<<" responded to ping but was not yet in list, adding to local list";
            appendToLogfile(myID, a.str());
            other_nodes.push_back(newID);
         }
         for(vector<ID>::iterator it = message->mData.begin(); it != message->mData.end();++it)
         {
            if(*it == myID) continue; // don't add ourselves to our other_nodes list
            newID = ID(it->hostname,it->create_timestamp);
            if(find(other_nodes.begin(),other_nodes.end(),newID) != other_nodes.end()) continue; // don't add repeat nodes to our list
            if(newID == myID) continue; // don't add an old version of ourself to our nodes list
            other_nodes.push_back(newID);
            stringstream a;
            a<<message->mID.hostname<<" "<<message->mID.create_timestamp<<" communicated that "<<it->hostname<<" "<<it->create_timestamp<<" is a node in the pool, adding to local list";
            appendToLogfile(myID, a.str());
         }
         break;
      }
      case TIMEOUT:
      {
         // remove node from other_nodes list
         vector<ID>::iterator it = other_nodes.begin();
         for(it; it!=other_nodes.end();++it)
         {
            if(*it == message->mData[0])
            {
               other_nodes.erase(it);
               break;
               }
         }
         stringstream a;
         a<<message->mID.hostname<<" "<<message->mID.create_timestamp<<" communicated that "<<message->mData[0].hostname<<" "<<message->mData[0].create_timestamp<<" has timed out: removing from local list";
         appendToLogfile(myID, a.str());
         break;
      }
      case LEAVE:
      {
         // remove node from other_nodes list
         vector<ID>::iterator it = other_nodes.begin();
         for(it; it!=other_nodes.end();++it)
         {
            if(*it == message->mID){
               other_nodes.erase(it);
               break;}
         }
         stringstream a;
         a<<message->mID.hostname<<" "<<message->mID.create_timestamp<<" communicated that it has left the pool: removing from local list";
         appendToLogfile(myID, a.str());
         break;
      }
      default:
      {
         cout<<"received bad message somehow"<<endl;
         break;
      }  }
   }
}

void catchLeave(int s)
{
   vector<ID>::iterator iter = other_nodes.begin();
   for(iter; iter!=other_nodes.end();++iter)
   {
      UDP::send(iter->hostname, "",LEAVE, myID.create_timestamp);
   }
   exit(1);
}

vector<Message> receiveMessages()
{
   vector<Message> retVal;
   std::vector<UdpMessage> udpMessages;
   if(UDP::receive(udpMessages) <= 0) return retVal; // if no messages received, return empty vector
   for(vector<UdpMessage>::iterator iter = udpMessages.begin(); iter != udpMessages.end(); ++iter)
   {
      ID tmpID(iter->senderHostName, iter->timeStamp);
      Message tmpMessage(tmpID, (Message_Type)iter->type, iter->message);
      retVal.push_back(tmpMessage);
   }
   return retVal;
}

void printIdAndMembers()
{
cout<<"self: "<<myID.hostname<<":"<<myID.create_timestamp<<endl;
for(vector<ID>::iterator iter = other_nodes.begin();iter != other_nodes.end();++iter)
{
  cout<<"  "<<iter->hostname<<":"<<iter->create_timestamp<<endl;
}
}

void *commandHadler(void *threadid)
{
   while(true)
   {
	   std::string command;
	   std::cin>>command;
	   if(command == "list")
	   {
		   printIdAndMembers();
	   }
	   else if(command == "exit")
	   {
		   catchLeave(0);
	   }
	   else
	   {
		   std::cerr<<"Unknown Command: "<<command<<"\n";
	   }
   }
}

void handleCommands()
{
	pthread_t thread;
	int rc = pthread_create(&thread, NULL, commandHadler, NULL);
    if (rc)
	{
		std:cerr<<"handleCommands(): could not create command handler thread\n";
     }
}

int main(int argc, char *argv[])
{
	handleCommands();
	UDP::init();
   int list_counter = 0;// guarantee detect in 3 seconds:
   // -- ordered list of all entries, traverse in round-robin fashion
   // -- order determined by arrival order of nodes
   // -- new member is inserted into list at last position
   // -- requires traversal every 3 seconds. send pings at scaled rate
   int current_time = time(0);
   string myName = getHostName();
   myID = ID(myName,current_time);
   stringstream initial;
   initial<<"(self) "<<myID.hostname<<" "<<myID.create_timestamp<<" joining pool...";
   appendToLogfile(myID, initial.str());
   if(getHostName().compare("fa16-cs425-g40-01.cs.illinois.edu") == 0)
   {
      if(loadConfigFile(other_nodes))
      {
         cout<<"I am introducer, and found existing_nodes file"<<endl;
      }
      else
      {
         cout<<"I am introducer, but existing_nodes file not found. Waiting for connections"<<endl;
      }
   }
   else // if must join existing node ring
   {
      while(true) // try to connect to existing group
      { 
         cout<<"attempting to join existing node ring. contacting introducer."<<endl;
         // ping "fa16-cs425-g40-01.cs.illinois.edu" (intro)
			UDP::send("fa16-cs425-g40-01.cs.illinois.edu","",INTRO,myID.create_timestamp);
         usleep(500000);
         vector<Message> received_list;
         received_list = receiveMessages();
         if (received_list.size()>0)
         {
            processMessages(received_list,other_nodes);
            break;
         }
         
         usleep(500000);
      }
   }
   // catch ctrl-c for clean leave, but only after in group already
   //signal(SIGINT,catchLeave);
   // main_loop:
   while(true)
   {
      if(list_counter >= other_nodes.size()) list_counter = 0;
      current_time = time(0);
      if (other_nodes.size() > 0) // don't want to break if we don't have a connection
      {
         // Ping other_nodes[list_counter]
			UDP::send(other_nodes[list_counter].hostname,"",PING,myID.create_timestamp);
         other_nodes[list_counter].waiting_timestamp = current_time;
         if(find(waiting_nodes.begin(),waiting_nodes.end(),other_nodes[list_counter])==waiting_nodes.end())
            waiting_nodes.push_back(other_nodes[list_counter]); // only add to queue if it's not in queue already
      }
      if(getHostName().compare("fa16-cs425-g40-01.cs.illinois.edu")==0)
         writeToExistingFile(other_nodes);
      vector<Message> received_list;
      received_list = receiveMessages();
      processMessages(received_list,other_nodes);
      while(waiting_nodes.size()>0) // check to cull receive queue
      {
         bool breakOut = true;
         // if front not in other_nodes, pop, set breakOut = false;
         vector<ID>::iterator iter = other_nodes.begin();
         for(iter; iter!=other_nodes.end();++iter)
         {
            if(*iter == waiting_nodes.front())
               break;
         }
         if(iter == other_nodes.end())
         {
            waiting_nodes.pop_front();
            breakOut = false;
            continue;
         }
         
         if(current_time - waiting_nodes.front().waiting_timestamp >= 2) // if front is timed out
         {
            ID toReport(waiting_nodes.front().hostname,waiting_nodes.front().create_timestamp);
            waiting_nodes.pop_front(); // remove it
            stringstream ss;
            ss << toReport.hostname<<" "<<toReport.create_timestamp;
            vector<ID>::iterator iter = other_nodes.begin();
            for(iter; iter!=other_nodes.end();++iter)
            {
               if(*iter == toReport)
               {
                  iter = other_nodes.erase(iter); // remove timed out node from our other_nodes list
                  --iter; // decrement because iterator now pointing to next node
               }
               else { // send timeout message to all other nodes
                  UDP::send(iter->hostname,ss.str(),TIMEOUT,myID.create_timestamp);
               }
            }
            breakOut = false;
            ss << " has been detected as timed-out";
            appendToLogfile(myID, ss.str());
         }
         if(breakOut) break;
      }        
      list_counter++;
      if(list_counter >= other_nodes.size()) list_counter = 0;
      usleep(1000000/((other_nodes.size()+2)));//for now just soft guarantee we'll ping everyone every 1 second at least
   }
}

