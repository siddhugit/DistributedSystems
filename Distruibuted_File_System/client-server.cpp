#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <iostream>
#include <algorithm>
#include <vector>
#include <sstream>
#include <map>
#include <mutex>
#include <chrono>
#include "ftpClient.h"
#include "ftpServer.h"

using namespace std;
/*
Each file needs to be replicated on at least 3 servers (because we must support up to 2 simultaneous failures)
 - we need to come up with a file transfer system.  Probably just via tcp or something - likely need to create separate method for transfer (maybe pull from MP1?)
 - this file transfer system must support put, get, delete operations.  We do not need to worry about changing files beyond this though.
 - will probably want to store these files in /tmp, along with the logs.  We should test with just putting files in our home directories though, to prevent write conflicts in /tmp
 - assuming we will copy whole files.  Sharding is not necessary: upon a failure, the same amount of data will be replicated either way, and this way is easier to manage.
 - on put, get, delete, inform master. master updates list, sends to other master-list holders. acks, on receive ack, begin file operation
   - put ack will contain information on who should get this file (which 3 machines should be replicas (machines with fewest files so far?), send files via separate tcp connection)
   - get ack will contain information on who has that file, who to get from (use tcp get, probably)
   - delete ack happens, and then other nodes will receive a message to delete
 - if file transfer operation fails for some reason, notify master as soon as possible (requires master-ack to ensure safe with 2 failures)
Each node should know of all other nodes and all files stored locally
 - this will support the 'store' command - this part will be really easy if we do things this way.
 - each node should know who the master node is as well.
 - master needs to be queried for 'ls' command
There should be one master node determining where files are sent
 - master nodes should have full list of all other nodes, all files
 - master-list (file names, locations) should be replicated like any other file, except not written to disk, and excluded from ls and store operations
 - 3 master-lists makes election easy.  Only nodes with master-lists can become master (if two remain, bully protocol between the two)
 - nodes behave as if they have very low ids if they aren't a master-list holder
 - broadcast master election information after decision made
 - master list should be replicated first after leader elected.  Most fail-safe.
 - master then sends commands to file transfer for re-balancing after failures (both master or other node failures require rebalancing)
log files should be used
 - each node should log on any failure, any join, election notification, file transfer
   - log file transfer to, from that node if not master
   - log file transfer to, from any node if master
   */

#define MAXBUFLEN 4096
#define PING_INTERVAL 1500

//GETM,PUTM and DELM handler function declarations
static void get_m(const std::string& message,int sockfd,sockaddr_storage their_addr,int addr_len);
static void put_m(const std::string& message,int sockfd,sockaddr_storage their_addr,int addr_len);
static void del_m(const std::string& message,int sockfd,sockaddr_storage their_addr,int addr_len);


/*
* Helper function of split method below
**/
void split(const std::string &s, char delim, vector<std::string> &elems) {
	stringstream ss;
	ss.str(s);
	string item;
	while (getline(ss, item, delim)) {
		elems.push_back(item);
	}
}

/*
* Parse the received message
*/
vector<std::string> split(const std::string &s, char delim) {
	vector<std::string> elems;
	split(s, delim, elems);
	return elems;
}

struct ID {
	std::pair<unsigned long, std::string> values;
	bool isMaster = false;
	bool isBackup = false;

	bool operator==(const ID &b) const { return values == b.values; }
	bool operator!=(const ID &b) const { return values != b.values; }
	bool operator<(const ID &b) const { return values < b.values; }
	ID& operator=(const ID &b) { if (this != &b) { values = std::make_pair(b.values.first,b.values.second); isMaster = b.isMaster; isBackup = b.isBackup; }return *this; }

	unsigned long timestamp() const { return values.first; }
	std::string ip() const { return values.second; }
	std::string Serialize() const {
		return std::to_string(timestamp()) + ":" + ip() + (isMaster ? "M":isBackup ? "B":"N");
	}
	static ID Deserialize(const std::string &ids) {
		ID id;
		std::vector<std::string> after_split = split(ids, ':');
		id.isMaster = after_split[1].back() == 'M';
		id.isBackup = after_split[1].back() == 'B';
		if(id.isMaster || id.isBackup || after_split[1].back() == 'N') after_split[1].pop_back(); // don't pop if we don't know what they are
		id.values = std::make_pair(std::stoul(after_split[0]), after_split[1]);
		return id;
	}
};

vector<string> localFiles;
map<ID, vector<string> > fileList;
vector<ID> backupVec;

std::mutex ele;
bool START_ELECTION = false;

std::mutex m;
std::mutex b;
std::string introducer_IP;
std::string introducer_port;
std::string node_IP;
std::string node_port;
ID node_id;
double leaving_time;
double joining_back_time;
int file_transfer_port;
// Maintaining a bool to keep track of whether a machine has left. If it has
// left it should not ping others.
bool hasLeft = false;

// Map from ID to port number.
std::map<ID, std::string> mapping;
// Membership list containing the IDs. This list is always in sync with
// @mapping above.
std::vector<ID> members;

/*
 * Convert the mappting information to one string of the following format:
 * nodeNum1:IP1:port1;nodeNum2:IP2:port2 ...
*/
std::string serialize_map() {
  std::string serialized;
	  for (auto it = members.begin(); it != members.end(); ++it)
	  {
			  serialized += it->Serialize() + ":" + mapping[*it] + ";";
	  }
  return serialized;
}

/*
* Print the mapping content
*/
void printing_the_map() {
  printf("Mapping:\n");
  for (const auto &elem : mapping) {
    printf("<%s> \n", elem.first.Serialize().c_str());
  }
}

/*
* Print the membership list
*/
void printing_members() {
	printf("Members:\n");
	for (const auto &elem : members) {
		if (elem == node_id)
			printf("%s \n", node_id.Serialize().c_str());
		else
			printf("%s \n", elem.Serialize().c_str());
	}
}

/*
* It converts the serialized string message to the vector structured membership
* list
* Input: string in the format:
* "node_id_current_node#memberID1:memberIP1:memberPort1;memberID2:memberIP2:memberPort2;"
*/
void deserialize_string_mapping_map(const string &to_deserialize) {
  const string node_id_string = to_deserialize.substr(0, to_deserialize.find('#'));
  node_id = ID::Deserialize(node_id_string);
  string members_list = to_deserialize.substr(to_deserialize.find('#') + 1);
  printf("The list received from introducer is -> %s\n", members_list.c_str());
  std::vector<std::string> member_node = split(members_list, ';');
  {
    std::lock_guard<std::mutex> lk(m);
    for (int i = 0; i < member_node.size(); i++) {
      std::vector<std::string> after_split = split(member_node[i], ':');
      const ID member_node_id(ID::Deserialize(member_node[i]));
	  members.push_back(member_node_id);
	 // vector<string> tmpVec;
	  //fileList[member_node_id] = tmpVec;
      string member_node_port = after_split[2];
      mapping[member_node_id] = member_node_port;
    }
  }
}

/**
* Iterate through the map and send all nodes except the newly joint node
*(new_node_id) information about the new node.
* Every message starts with a 4 byte message type, NEWJ, PING, JOIN etc.
**/
void sending_info_to_all_nodes(ID new_or_failed_node_id, string message,
                               bool sendtoself) {
  std::map<ID, std::string> mapping_copy;
  {
    std::lock_guard<std::mutex> lk(m);
    mapping_copy = mapping;
  }
  for (auto &elem : mapping_copy) {
    if (elem.first != new_or_failed_node_id &&
        (sendtoself || elem.first != node_id)) {
      int sockfd;
      struct addrinfo hints, *servinfo, *p;
      int rv, numbytes;
      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
      hints.ai_socktype = SOCK_DGRAM;
      hints.ai_flags = AI_PASSIVE; // use my IP
      //printf("id= %s, port = %s \n", elem.first.Serialize().c_str(),
      //       elem.second.c_str());
      //printf("new_or_failed_id = <%s> %s \n",
      //       new_or_failed_node_id.Serialize().c_str(),
      //       mapping_copy[new_or_failed_node_id].c_str());
      if ((rv = getaddrinfo(elem.first.ip().c_str(), elem.second.c_str(),
                            &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        continue;
      }
      // loop through all the results and bind to the first we can
      for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
          perror("node: socket for sending other nodes information about the newly-joined node");
          continue;
        }
        break;
      }
	  if (p == NULL) {
        fprintf(stderr, "node: failed to bind socket for sending other nodes "
                        "information about the newly-joined node\n");
        return;
      }
      // Sending message to that node
      // Message has the following format: TYPE#node_id:IP:port
      //printf("Send message = %s\n", message.c_str());
      if ((numbytes = sendto(sockfd, message.c_str(), message.length(), 0,
                             p->ai_addr, p->ai_addrlen)) == -1) {
        printf("Sending %s from %s to %s\n", message.substr(0,4).c_str(), mapping_copy[new_or_failed_node_id].c_str(), elem.second.c_str());
        perror("Sending failed");
      }
      freeaddrinfo(servinfo);
      close(sockfd);
    }
  }
}

/*
* This method sends the join message to the INTRODUCER node, and asks to join
* the membership community
* Then it receives the membership message from the INTRODUCER node, and parses
* it the vectorized membership list.
*/
void sending_join_receiving_list_making_map() {
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;
  int numbytes2;
  char buf[MAXBUFLEN];
  string message;
  int bytesReceived = 0;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  if ((rv = getaddrinfo(introducer_IP.c_str(), introducer_port.c_str(), &hints,
                        &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return;
  }
  // loop through all the results and make a socket
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("node: socket");
      continue;
    }
    break;
  }
  if (p == NULL) {
    fprintf(stderr, "node: failed to create socket\n");
    return;
  }
  hasLeft = false; // Now it has joined back or is alive
  printf("sending join request   \n");
  message = "JOIN:" + node_IP + ":" + node_port;
  // sent "JOIN" request to introducer node
  if ((numbytes = sendto(sockfd, message.c_str(), message.length(), 0,
                         p->ai_addr, p->ai_addrlen)) == -1) {
    perror("node: sendto");
    exit(1);
  }
  printf("Sent JOIN request from %s to introducer node\n", node_IP.c_str());

  // it receives the node number ID along with membership list in the following format:
  // node_id#nodeNum1:IP1:port1;nodeNum2:IP2:port2 ...
  if ((numbytes2 = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, p->ai_addr,
                            &p->ai_addrlen)) == -1) {
    perror("recvfrom");
    exit(1);
  }

  buf[numbytes2] = '\0';
  //printf("Join approval packet contains \"%s\"\n", buf);
  string bufst = string(buf);
  // We will first delete the vector and map if it has been filled already 
  // (if node was alive once and it left and is joining back)
  // We will fill the map again by the list given by the introducer

  {
    std::lock_guard<std::mutex> lk(m);
    members.clear(); // empty vector
    mapping.clear(); // empty map
    printing_members();
    // cleared the vector and map. Now it can be filled again by the latest list
  }
  deserialize_string_mapping_map(bufst); 
  // it will create the map from information given b the introducer
  // The vector of members and the mapping has been initialized with the list given by
  // introducer
  freeaddrinfo(servinfo);
  close(sockfd);
}

/**
* It sends one PING message to other normal node for every interval
* If it does not receive the ACK from other node in a specific time interval,
* we treat the node as a failed node and
* asks other nodes to delete it from the membership list.
*/
string sending_one_ping_message_per_interval(const ID &member_id, string port) {
	struct addrinfo hints, *servinfo, *p;
	int sockfd, rv, numbytes, numbytes2;
	string message = "";
	char buf[MAXBUFLEN];
	buf[0] = '\0';
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 500000;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(member_id.ip().c_str(), port.c_str(), &hints,
		&servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return "";
	}

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("node: socket for sending other nodes information");
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "node: failed to bind socket for sending other nodes information\n");
		return "";
	}

	message = "PING#" + node_id.Serialize();
	if ((numbytes = sendto(sockfd, message.c_str(), message.length(), 0,
		p->ai_addr, p->ai_addrlen)) == -1) {
		perror("node: Failed to send PING message.");
		exit(1);
	}
	// sent "PING" ping request to a normal node
	//printf("Sent %d bytes of PING message from %s to %s port \n", numbytes,
	//	node_id.Serialize().c_str(), member_id.Serialize().c_str());
	//printf("Sent the message: %s\n", message.c_str());

	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	// If the node does not receive the message from the node within the time
	if ((numbytes2 = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, p->ai_addr,
		&p->ai_addrlen)) < 0) {
		// On timeout, we will send a message to all other machines to remove this node from
		// their list. The message will be of the format: "FAIL#node_id"
		printf("TIMEOUT occured of member_id = %s and port = %s\n",
			member_id.Serialize().c_str(), port.c_str());
		string message2 = "FAIL#" + member_id.Serialize();
		sending_info_to_all_nodes(member_id, message2, true);
	}
	else {
		// it receives the ACKK
		//printf("Acknowledgement packet received %d bytes long on sending "
		//	"PING from %s to %s \n", numbytes2, node_id.Serialize().c_str(), member_id.Serialize().c_str());
		buf[numbytes2] = '\0';
	}
	close(sockfd);
	string str(buf);
	return str;
}

/**
* It sends one message to one other node.
* If it does not receive the ACK from other node in a specific time interval,
* we treat the node as a failed node and
* asks other nodes to delete it from the membership list.
*/
bool send_message(const ID &member_id, string port, string message) {
	bool found = false;
	for (auto it = members.begin(); it != members.end(); ++it)
		if (member_id == *it)
			found = true;
	if (!found)
		return false;
	struct addrinfo hints, *servinfo, *p;
	int sockfd, rv, numbytes, numbytes2;
	char buf[MAXBUFLEN];
	buf[0] = '\0';
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 500000;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	if ((rv = getaddrinfo(member_id.ip().c_str(), port.c_str(), &hints,
		&servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return false;
	}
	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("node: socket for sending other nodes information");
			continue;
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "node: failed to bind socket for sending other nodes information\n");
		return false;
	}
	if ((numbytes = sendto(sockfd, message.c_str(), message.length(), 0,
		p->ai_addr, p->ai_addrlen)) == -1) {
		perror("node: Failed to send message.");
		exit(1);
	}
	// sent request to a normal node
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	// If the node does not receive the message from the node within the time
	if ((numbytes2 = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, p->ai_addr,&p->ai_addrlen)) < 0) {
		// On timeout, we will send a message to all other machines to remove this node from
		// their list. The message will be of the format: "FAIL#node_id"
		printf("TIMEOUT occured of member_id = %s and port = %s\n",
			member_id.Serialize().c_str(), port.c_str());
		string message2 = "FAIL#" + member_id.Serialize();
		sending_info_to_all_nodes(member_id, message2, true);
	}
	else {
		// it receives the ACKK
		buf[numbytes2] = '\0';
	}
	close(sockfd);
	return numbytes2>0;
}

/**
* It sends one message to other node, no questions asked.
*/
void send_message_no_ack(const ID &member_id, string port, string message) {
	struct addrinfo hints, *servinfo, *p;
	int sockfd, rv, numbytes, numbytes2;
	char buf[MAXBUFLEN];
	buf[0] = '\0';
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	if ((rv = getaddrinfo(member_id.ip().c_str(), port.c_str(), &hints,&servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}
	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("node: socket for sending other nodes information");
			continue;
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "node: failed to bind socket for sending other nodes information\n");
		return;
	}
	if ((numbytes = sendto(sockfd, message.c_str(), message.length(), 0,
		p->ai_addr, p->ai_addrlen)) == -1) {
		perror("node: Failed to send message.");
		exit(1);
	}
	close(sockfd);
	return;
}

/*
* We iterate through vector<ID> to find index of the node_id whose timestamp is just
* greater than the current node id.
* Vector<ID> members will be of the form: [<1, IP1>,<2, IP2>,<3, IP3> --------]
* In other words,
* Node 0: pings in the order 1->2->3->4->5->6->7
* Node 1: pings in the order 2->3->4->5->6->7->0
* Node 2: pings in the order 3->4->5->6->7->0->1
* Node 3: pings in the order 4->5->6->7->0->1->2
*/
void sending_pings() {

  int pointer = 0;
  {
    std::lock_guard<std::mutex> lk(m);
    for (int i = 0; i < members.size(); i++) {
      if (members[i].timestamp() > node_id.timestamp()) {
        pointer = i;
        break;
      }
    }
  }

  while (1) {
    string ip, port;
    ID member_id;
    bool member_size_one;
    bool member_size_zero;
    {
      std::lock_guard<std::mutex> lk(m);
      member_size_one = members.size() == 1;
      member_size_zero = members.size() == 0;
      if (!member_size_zero) {
        int index = pointer % members.size();
        pointer++;
        member_id = members[index];
        port = mapping[member_id];
      }
    }

    // if the node is left, then do not ping. Sleep for sometime and check again
    if (hasLeft) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    // if member list is zero, sleep for a while and check later
    if (member_size_zero) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    // To prevent the main introducer node from sending messages to himself
    if (member_id == node_id) {
      // Since it does not need to send PING to itself, it can sleep for a while
      if (member_size_one) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      continue;
    }
    std::thread first(sending_one_ping_message_per_interval, member_id, port);
    first.detach();

    // printf("elem.first = %d, elem.second.first=%s, elem.second.second=%s\n",
    // elem.first, elem.second.first.c_str(), elem.second.second.c_str());
    // printf("id = %d, mapping[member_id].first=%s,
    // mapping[member_id].second=%s\n", member_id,
    // mapping[member_id].first.c_str(), mapping[member_id].second.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(PING_INTERVAL));
  }
}

/**
* It first sleeps, then executes the following steps to leave the membership
* community
* It sends LEAV#node_id message to all nodes except itself; all nodes will
* remove this node from their list
* We also empty its map so that it doesn't ping others
*/
void leaving() {
	backupVec.clear();
	for (auto it = fileList.begin(); it != fileList.end(); ++it) 
		it->second.clear(); 
	fileList.clear();
	hasLeft = true; // Instead of emptying the map, we set this variable.
	string message = "LEAV#" + node_id.Serialize();
	node_id.isBackup = false;
	node_id.isMaster = false;
	// Sends LEAV#node_id message to all nodes except itself
	// all nodes will remove this node from their list
	sending_info_to_all_nodes(node_id, message, false);
	printf("Sent LEAV message to all other nodes\n");
	// we will remove its entry from the map, so that it can pick it up from
	// here on joining back
	{
		std::lock_guard<std::mutex> lk(m);
		for (auto it = members.begin(); it != members.end(); ++it) {
			if (*it == node_id) {
				members.erase(it);
				break;
			}
		}
		mapping.erase(node_id);
		printing_members();
	}
}

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// places local-file list into a string
string file_list()
{
	stringstream ss;
	ss << "Local files:\n";
	for (auto it = localFiles.begin(); it != localFiles.end(); ++it) {
		ss << *it << '\n';
	}
	return ss.str();
}

// places full master-file list into a string
string full_list()
{
	stringstream ss;
	ss << "All files on sdfs:\n";
	for (auto it = fileList.begin(); it != fileList.end(); ++it) {
		ss << "\t" << it->first.Serialize() << "\n";
		for (auto i = it->second.begin(); i != it->second.end(); ++i) {
			ss << "\t\t" << *i << '\n';
		}
	}
	return ss.str();
}

// places local list into string
string flist(ID node)
{
	stringstream ss;
	ss << "All local files on sdfs:\n";
	for (auto it = fileList.begin(); it != fileList.end(); ++it) {
		if (node != it->first)
			continue;
		for (auto i = it->second.begin(); i != it->second.end(); ++i) {
			ss << "\t" << *i << '\n';
		}
	}
	return ss.str();
}

// returns string list of IDs that contain a given file
string specificList(string aName)
{
	stringstream ss;
	ss << "Nodes that contain "<<aName<<":\n";
	for (auto it = fileList.begin(); it != fileList.end(); ++it) {
		for (auto i = it->second.begin(); i != it->second.end(); ++i) {
			if(aName.compare(*i)==0)
			ss << "\t"<< it->first.Serialize() << '\n';
		}
	}
	return ss.str();
}

// helper function to set an ID to the current master ID
bool get_master_node(ID& aID) {

	auto it = members.begin();
	for (; it != members.end(); ++it)
	{
		if (it->isMaster) break;
	}
	if (it == members.end()) return false;
	aID = *it;
	return true;
}

// helper function to send a simple request to master: given a prefix and a message
bool send_request_to_master(string prefix, string message) {
	bool res = false;
	ID master;
	if(!get_master_node(master))
		cout << "No master currently in list... please wait to make this call again" << endl;
	else {
		res = send_message(master, mapping[master], prefix + "#" + message);
		if (!res)
			cout << "Could not reach master node... please wait to make this call again" << endl;
	}
	return res;
}

// function for master node to remove evidence of the other node from fileList and friends
void leader_remove_node(ID to_remove)
{
	for (auto it = backupVec.begin(); it != backupVec.end(); ++it) {
		if (*it == to_remove) {
			backupVec.erase(it);
			break;
		}
	}
	for (auto it = fileList.begin(); it != fileList.end(); ++it) {
		if (it->first == to_remove) {
			it->second.clear();
			fileList.erase(it);
			break;
		}
	}
}

// appoint new replacement backup if one needs to be added
void appoint_new_helper()
{
	if (members.size() == 0 || members.size() == backupVec.size() || backupVec.size() == 2)
		return;
	auto candidate = members.begin();
	for (auto it = members.begin(); it != members.end(); ++it)
	{
		if (*candidate < *it)
			candidate = it;
	}
	candidate->isMaster = false;
	candidate->isBackup = true;
	backupVec.push_back(*candidate);
	printf("Added %s to backup list\n", candidate->Serialize().c_str());
}

// takes help message in some format, opposite of build_help_message function
void process_help_message(string message)
{
	{
		std::lock_guard<std::mutex> lk(b);
		backupVec.clear();
		for (auto it = fileList.begin(); it != fileList.end(); ++it)
			it->second.clear();
		fileList.clear();
		vector<string> IDS = split(message, '#');
		for (auto it = IDS.begin(); it != IDS.end(); ++it)
		{
			if (it->size() < 3) continue; // empty initial split
			vector<string> value = split(*it, '!');
			ID tmpID(ID::Deserialize(value[0]));
			fileList[tmpID] = vector<string>();
			for (int i = 1; i < value.size(); ++i)
			{
				if (value[i].size() < 3) continue; // empty last filename before next ID
				fileList[tmpID].push_back(value[i]);
			}
			if (tmpID.isBackup)
				backupVec.push_back(tmpID);
		}
	}
}

// assemble the fileList into a usable string
string build_help_message()
{
	stringstream ss;
	{
		for (auto it = fileList.begin(); it != fileList.end(); ++it) // iterate over IDs with files
		{
			ss << "#" << it->first.Serialize();
			for (auto i = it->second.begin(); i != it->second.end(); ++i) // iterate over files owned by each ID
			{
				ss << "!" << *i;
			}
			ss << "!";
		}
	}
	return ss.str();
}

// send help messages to all available backups
void send_help_messages()
{
	string message = "HELP#"+build_help_message();
	for (auto it = backupVec.begin(); it != backupVec.end(); ++it)
	{
		printf("Sending help message to %s\n",it->Serialize().c_str());
		send_message(*it, mapping[*it], message);
	}
}

// helper struct to contain rebalanceing information
struct balance {
	string name;
	int count;
	ID source;
};

// gets all files in fileList that exist in fewer than 3 locations
vector<balance> get_unbalanced_files()
{
	map<string,balance> countMap;
	for (auto it = fileList.begin(); it != fileList.end(); ++it)
	{
		for (auto i = it->second.begin(); i != it->second.end(); ++i)
		{
			if (countMap.find(*i) == countMap.end())
			{
				balance tmpBalance;
				tmpBalance.name = *i;
				tmpBalance.count = 0;
				tmpBalance.source = it->first;
				countMap[*i] = tmpBalance;
			}
			++countMap[*i].count;
		}
	}
	vector<balance> retVal;
	for (auto it = countMap.begin(); it != countMap.end(); ++it)
		if (it->second.count < 3)
		{
			retVal.push_back(it->second);
		}
	return retVal;
}

// simple helper: returns true if aVal exists in aVec; false otherwise
bool in_vec(vector<string> aVec, string aVal)
{
	for (auto it = aVec.begin(); it != aVec.end(); ++it)
		if (aVal.compare(*it) == 0)
			return true;
	return false;
}

// returns one or two IDs that do not have the filename passed in
vector<ID> getIDsToPutFilesToByFile(bool two, string filename)
{
	vector<ID> retVal;
	if (members.size() <= 3)
	{
		for (auto it = members.begin(); it != members.end(); ++it)
			if (!in_vec(fileList[*it], filename))
				retVal.push_back(*it);
		return retVal;
	}
	ID first;
	ID second;
	int size = 65536;
	for (auto it = fileList.begin(); it != fileList.end(); ++it)
	{
		if (it->second.size() < size && !in_vec(it->second, filename))
		{
			size = it->second.size();
			first = it->first;
		}
	}
	retVal.push_back(first);
	size = 65536; 
	if (two)
	{
		for (auto it = fileList.begin(); it != fileList.end(); ++it)
		{
			if (it->first != first && it->second.size() < size && !in_vec(it->second, filename))
			{
				size = it->second.size();
				second = it->first;
			}
		}
		retVal.push_back(second);
	}
	return retVal;
}

// rebalances files in fileList among existing nodes
// returns true if a change was made
bool rebalance_files()
{
	std::lock_guard<std::mutex> lk(b);
	bool changed = false;
	vector<balance> unbalancedFiles = get_unbalanced_files();
	for (auto it = unbalancedFiles.begin(); it != unbalancedFiles.end(); ++it)
	{
		vector<ID> recipients = getIDsToPutFilesToByFile(it->count == 1, it->name);
		if (recipients.size() == 0)
			continue;
		changed = true;
		// PUTM#ID_to_put_to_1#ID_to_put_to_2#ID_to_put_to_3#sdfsfilename#localfilename
		stringstream ss;
		ss << "PUTM";
		for (auto iter = recipients.begin(); iter != recipients.end(); ++iter)
		{
			ss << "#" << iter->Serialize();
			fileList[*iter].push_back(it->name);
		}
		ss << "#" << it->name << "#" << it->name;
		send_message_no_ack(it->source, mapping[it->source], ss.str());
	}
	return changed;
}

// rebalances files in fileList among existing nodes
// returns true if a change was made
bool rebalance_files_quietly()
{
	std::lock_guard<std::mutex> lk(b);
	bool changed = false;
	vector<balance> unbalancedFiles = get_unbalanced_files();
	for (auto it = unbalancedFiles.begin(); it != unbalancedFiles.end(); ++it)
	{
		vector<ID> recipients = getIDsToPutFilesToByFile(it->count == 1, it->name);
		if (recipients.size() == 0)
			continue;
		changed = true;
		// PUTM#ID_to_put_to_1#ID_to_put_to_2#ID_to_put_to_3#sdfsfilename#localfilename
		stringstream ss;
		ss << "PUTM";
		for (auto iter = recipients.begin(); iter != recipients.end(); ++iter)
		{
			ss << "#" << iter->Serialize();
			fileList[*iter].push_back(it->name);
		}
	}
	return changed;
}

/*
runs leader update tasks:
  keeps number of backups up to 2
  fork! to allow parent process to continue
     sends help messages to keep backups informed
     rebalances files on node entry or release
*/
void run_leader_tasks()
{
	appoint_new_helper(); // appoint new replacement backup
	if (!fork()) // return to other process
	{
		send_help_messages(); // send help messages to backups
		if (rebalance_files()) // re-balance files to account for dead nodes
			send_help_messages(); // send help updates again; keep these backups up to date
		exit(0); // exit forked process
	}
	else
	{
		rebalance_files_quietly();
	}
}

void add_backup(ID aID)
{
	bool found = false;
	for (auto it = backupVec.begin(); it != backupVec.end(); ++it)
	{
		if (aID == *it)
			found = true;
	}
	if (!found)
		backupVec.push_back(aID);
}

// sets node_id and isMaster in members list
void set_self_leader()
{
	printf("Setting self as leader: %s\n", node_id.Serialize().c_str());
	node_id.isBackup = false;
	node_id.isMaster = true;
	for (auto it = members.begin(); it != members.end(); ++it)
		if (node_id == *it)
		{
			it->isBackup = false;
			it->isMaster = true;
			break;
		}
}

// runs necessary leader-assuming function
void announce_self_as_leader()
{
	set_self_leader();
	sending_info_to_all_nodes(node_id, "LEDR#" + node_id.Serialize(), false);
	run_leader_tasks();
}

bool can_find_backup(ID& aID)
{
	bool retVal = false;
	for (auto it = members.begin(); it != members.end(); ++it)
	{
		if (it->isBackup && node_id != *it)
		{
			aID = *it;
			retVal = true;
			break;
		}
	}
	return retVal;
}

// sends an election message to the other backup: if no response received, announces self as leader
int start_election()
{
	ID backup;
	if (!can_find_backup(backup))
	{
		announce_self_as_leader();
		return 0;
	}
	if (backup.values < node_id.values)
		return 0;// send_message_no_ack(backup, mapping[backup], "ELEC#" + node_id.Serialize());// tell other node to elect itself
	else
		announce_self_as_leader();
	return 0;
}

// creates a vector of up to 3 IDs as locations to put files. first is an ID where the request is being made
vector<ID> getIDsToPutFilesTo(ID first)
{
	vector<ID> retVal;
	if (members.size() <= 3)
	{
		for (auto it = members.begin(); it != members.end(); ++it)
			retVal.push_back(*it);
		return retVal;
	}
	retVal.push_back(first);
	ID second;
	ID third;
	int size = 65536;
	for (auto it = fileList.begin(); it != fileList.end(); ++it)
	{
		if (it->first != first && it->second.size() < size)
		{
			size = it->second.size();
			second = it->first;
		}
	}
	size = 65536;
	for (auto it = fileList.begin(); it != fileList.end(); ++it) 
	{
		if (it->first != first && it->first != second && it->second.size() < size)
		{
			size = it->second.size();
			third = it->first;
		}
	}
	retVal.push_back(second);
	retVal.push_back(third);
	return retVal;
}

int demo_action(){
  string demo_action = "";
  // Thread waiting for user input 
  while(1){
  	//cin >> demo_action;
	getline(cin, demo_action);
	vector<string> result = split(demo_action, ' ');
	if (result[0].compare("list") == 0) {
		// list the membership
		printing_members();
	} else if (result[0].compare("back") == 0) {
		// list backup members
		for(auto it = backupVec.begin(); it != backupVec.end();++it)
		printf("%s\n", it->Serialize().c_str());
	} else if (result[0].compare("id") == 0) {
		// list self's id
		printf("Self's id =%s\n", node_id.Serialize().c_str());
  	} else if ((result[0].compare("join"))== 0){
  		// join the membership group
  		sending_join_receiving_list_making_map();
  	} else if ((result[0].compare("leave")) == 0) {
		// leave the membership group
		leaving();
	} else if ((result[0].compare("ls")) == 0) {
		if (result.size() != 2 && result.size() != 1) {
			cout << "USAGE: ls OPTIONAL:sdfsfilename" << endl;
			continue;
		}
		if (result.size() == 1)
		{
			// send LSRQ request to master, print out results when received
			if (node_id.isMaster || node_id.isBackup) { // unless we are the master or a backup node and have the full list
				printf("%s\n", full_list().c_str());
			}
			else {
				// fork here before sending, as sending waits for an ack for half second.
				if (!fork()) {
					if (send_request_to_master("LSRQ", node_id.Serialize()))
					{
						printf("LS message sent to master node\n");
					}
					exit(0);
				}
			}
		} else if (result.size() == 2)
		{
			// send LSRQ request to master, print out results when received
			if (node_id.isMaster || node_id.isBackup) { // unless we are the master or a backup node and have the full list
				printf("%s\n", specificList(result[1]).c_str());
			}
			else {
				// fork here before sending, as sending waits for an ack for half second.
				if (!fork()) {
					if (send_request_to_master("LSRQ", node_id.Serialize() + "#" + result[1]))
					{
						printf("LS message sent to master node\n");
					}
					exit(0);
				}
			}
		}
	} else if ((result[0].compare("store")) == 0) {

		// send LSRQ request to master, print out results when received
		if (node_id.isMaster || node_id.isBackup) { // unless we are the master or a backup node and have the full list
			printf("%s\n", flist(node_id).c_str());
		}
		else {
			// fork here before sending, as sending waits for an ack for half second.
			if (!fork()) {
				send_request_to_master("LSR1", node_id.Serialize());
				exit(0);
			}
		}
	}
	else {
		// here go the filesystem command-line options
		if ((result[0].compare("get")) == 0) {
			if (result.size() != 3) {
				cout << "USAGE: get sdfsfilename localfilename" << endl;
				continue;
			}
			if (find(localFiles.begin(),localFiles.end(),result[1]) == localFiles.end()) {// if not in localFiles:
				/* send GETN notice to master, master returns ack
				 * master subsequently sends who to get from, filename (everything needed for that message and sdfs transfer) */
				// fork here before sending, as sending waits for a half second.
				if (!fork()) {
					if (send_request_to_master("GETN", node_id.Serialize() + "#" + result[1] + "#" + result[2]))
					{
						printf("GET message sent to master node\n");
					}
					exit(0);
				}
			}
			else { // file is local
				//TODO copy file from distributed system location (still on local machine) to local system (wherever file is supposed to end up)
				ftpClient::send_get(node_id.ip(), result[1], result[2]);


			}
		}
		else if ((result[0].compare("put")) == 0) {
			if (result.size() != 3) {
				cout << "USAGE: put localfilename sdfsfilename" << endl;
				continue;
			}
			/* send PUTN notice to master, master returns ack
			* master subsequently sends who to put to, filename (everything needed for that message and sdfs transfer) */
			// fork here before sending, as sending waits for a half second.
			if (!fork()) {
				if (send_request_to_master("PUTN", node_id.Serialize() + "#" + result[1] + "#" + result[2]))
				{
					printf("PUT message sent to master node\n");
				}
				exit(0);
			}
		} 
		else if ((result[0].compare("delete")) == 0) {
			if (result.size() != 2) {
				cout << "USAGE: delete sdfsfilename" << endl;
				continue;
			}
			/* send DELN notice to master, master returns ack
			* master subsequently sends notices to delete to other nodes, with filename to delete */
			// fork here before sending, as sending waits for a half second.
			if (!fork()) {
				if (send_request_to_master("DELN", node_id.Serialize() + "#" + result[1]))
				{
					printf("DEL message sent to master node\n");
				}
				exit(0);
			}
		} 
		else {
			perror("Invalid action.");
		}
	}
  }
  return 0;
}


/*
*  Client side testing function
* It first leave, then ask to join, and send pings.
*/
int client_talker() 
{
  sending_pings();
  return 0;
}

/*
*  This function starts an election in a new thread
*  Sends a ELEC message to the other backup
*/

/*
* Node that receives and execute correpsonding message
*/
int server_listening()
{
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	int numbytes2;
	struct sockaddr_storage their_addr;
	char buf[MAXBUFLEN];
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];
	string message = "ack";
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	if ((rv = getaddrinfo(node_IP.c_str(), node_port.c_str(), &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			perror("listener: socket");
			continue;
		}
		if (::bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);
			perror("listener: bind");
			continue;
		}
		break;
	}
	if (p == NULL)
	{
		fprintf(stderr, "listener: failed to bind socket\n");
		return 2;
	}
	printf("listener: waiting to recvfrom...\n");
	while (1)
	{ // main accept() loop
		addr_len = sizeof their_addr;
		if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
		{
			perror("recvfrom");
			exit(1);
		}
		buf[numbytes] = '\0';
		string bufst = string(buf);
		// getting the first four characters of the received packet
		string prefix = bufst.substr(0, 4);
		if (prefix.compare("PING") != 0 && prefix.compare("LSRP") != 0 && prefix.compare("HELP") != 0)
		   printf("%s\n", bufst.c_str());
		if (prefix.compare("JOIN") == 0)
		{
			if (members.size() <= 1) // if we are introducer and nodes just started coming in
				set_self_leader();
			// If the node receives the "JOIN" message, then it is an INTRODUCER NODE
			// message in the format "JOIN:IP:Port Number"

			printf("Received a JOIN request: \"%s\"\n", bufst.c_str());

			// Get the current timestamp
			auto milliseconds_nodeid_time_stamp =
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count();

			// Parse the message
			std::vector<std::string> after_split = split(bufst, ':');

			ID new_id;
			new_id.values = std::make_pair(milliseconds_nodeid_time_stamp, after_split[1]);
			const std::string new_port = after_split[2];

			// sending message in the format node_id#membership_list
			// node_id#nodeNum1:IP1:port1;nodeNum2:IP2:port2 .......
			// e.g. on 2nd machine's JOIN request, it sends "2#1:IP1:port1"

			message = new_id.Serialize();

			// If the introducer node has left and the JOIN request is from some other
			// node (other than itself) it must be ignored
			// So, when introducer node has left, it should only take into account its
			// own join request
			if (hasLeft && new_id.ip() != introducer_IP) {
				continue;
			}
			{
				std::lock_guard<std::mutex> lk(m);
				members.push_back(new_id);
				vector<string> tmpVec;
				fileList[new_id] = tmpVec;
				mapping[new_id] = new_port; // filling map
				message += "#" + serialize_map();
			}
			printf("Updated map \n");
			if (node_id.isMaster) run_leader_tasks(); // if we are master, and new node comes in, make sure we are all good

			// this is the child process
			if (!fork()) {

				// Send the newly joined node information to all other nodes.
				if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
					(struct sockaddr *)&their_addr, addr_len)) == -1) {
					perror("server sending error");
					close(sockfd);
					exit(1);
				}

				string message2 = "NEWJ#" + new_id.Serialize() + ":" + new_port;
				// calling a function to send other nodes information about the new node
				sending_info_to_all_nodes(new_id, message2, false);
				exit(0);
			}
		}

		// If a node receives a PING message
		// then it sends back a ACKK message to the sender
		else if (prefix.compare("PING") == 0) {
			// If the node has left, it should not take account of these messages
			if (hasLeft) {
				continue;
			}
			// A NORMAL NODE receives a PING request
			//printf("Received a PING message: \"%s\"\n", bufst.c_str());
			const ID pinged_id(ID::Deserialize(split(bufst, '#')[1]));
			if (pinged_id == node_id) {
				continue;
			}
			{
				std::lock_guard<std::mutex> lk(m); // keep map up to date
				auto it = members.begin();
				for (; it != members.end(); ++it)
				{
					if (pinged_id == *it)
					{
						it->isMaster = pinged_id.isMaster;
						it->isBackup = pinged_id.isBackup;
						if (it->isBackup)
							add_backup(*it);
						break;
					}
				}
				if (it == members.end())
				{
					members.push_back(pinged_id);
					vector<string> tmpVec;
					fileList[pinged_id] = tmpVec;
				}
			}
			message = "ACKK";
			if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
				(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("Server sending error: acknowledgement on ping");
				close(sockfd);
				exit(1);
			}
		}

		// NORMAL NODE
		// Receiving message about a new node having joined the group
		else if (prefix.compare("NEWJ") == 0) {
			// If the node has left, it should not take account of these messages
			if (hasLeft) {
				continue;
			}

			printf("Received a new node request: \"%s\"\n", bufst.c_str());
			// the node must be added to the membership list
			// (i.e. a vector) and an entry of it must be added
			// The received message is in the following format: "NEWJ#node_id:IP:port"
			// "node_id:IP:port"
			string nodeIdIpPort = bufst.substr(5);
			// printf("new_node_id_port = %s\n", nodeIdIpPort.c_str());

			// Parse the received message
			// Output is in the following foramt: <node_id, IP, port are now split>
			std::vector<std::string> after_split = split(nodeIdIpPort, ':');
			const ID new_node_id(ID::Deserialize(nodeIdIpPort));
			//printf("new_node_id = %s\n", new_node_id.Serialize().c_str());
			string new_node_port = after_split[2];
			//printf("new_node_port = %s\n", new_node_port.c_str());

			{
				std::lock_guard<std::mutex> lk(m);
				printf("BEFORE Adding the new node:\n");
				printing_members();
				members.push_back(new_node_id);
				vector<string> tmpVec;
				fileList[new_node_id] = tmpVec;
				mapping[new_node_id] = new_node_port;
				printf("AFTER Adding the new node:\n");
				printing_members();
			}
			if (node_id.isMaster) run_leader_tasks(); // if we are master, and new node comes in, make sure we are all good
		}

		// NORMAL NODE receiving message about a node failing/leaving
		// Then the node must be removed from the members vector and from the
		// mapping
		else if (prefix.compare("FAIL") == 0 || prefix.compare("LEAV") == 0) {
			// If the node has left, it should not take account of these messages
			if (hasLeft) continue;
			// message is in the format: "FAIL#failed_node_id" or
			// "LEAV#leaving_node_id"
			std::string nodeIdIpPort = bufst.substr(5);
			bool removed = false;
			const ID remove_node_id(ID::Deserialize(nodeIdIpPort));
			{
				std::lock_guard<std::mutex> lk(m);
				for (auto it = members.begin(); it != members.end(); ++it) {
					if (*it == remove_node_id) {
						members.erase(it);
						removed = true;
						break;
					}
				}
				mapping.erase(remove_node_id);
			}
			if (!removed) continue;
			// printf("failed or leaving node_id = %s\n", remove_node_id.Serialize().c_str());
			if (node_id.isBackup && remove_node_id.isBackup) // if we are backup and they are backup, remove from backupVec
			{
				for (auto it = backupVec.begin(); it != backupVec.end();++it)
					if (*it == remove_node_id) {
						backupVec.erase(it);
						break;
					}
			}
			else if (remove_node_id.isMaster && node_id.isBackup) // if we are a backup, and the leader just failed, election!
			{
				// send elec to other backup
				printf("starting election for new master - old one is gone\n");
				leader_remove_node(remove_node_id);
				start_election();
			}
			else if (node_id.isMaster) // if we were master, run the leader tasks to maintain the sdfs
			{
				leader_remove_node(remove_node_id);
				run_leader_tasks(); // if we are master, keep things up to date
			}
		}

		// LS request: send back message containing full file_list
		else if (prefix.compare("LSR1") == 0) {
			vector<string> results = split(bufst, '#');
			const ID requesting_id(ID::Deserialize(results[1]));
			message = "ACKK";
			if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
				(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("Server sending error: acknowledgement on ls");
				close(sockfd);
				exit(1);
			}
			if (results.size() == 2)
				message = "LSRP#" + flist(requesting_id);
			// send response message to requesting_id()
			send_message_no_ack(requesting_id, mapping[requesting_id], message);
		}

		// LS request: send back message containing full file_list
		else if (prefix.compare("LSRQ") == 0) {
			vector<string> results = split(bufst, '#');
			const ID requesting_id(ID::Deserialize(results[1]));
			message = "ACKK";
			if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
				(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("Server sending error: acknowledgement on ls");
				close(sockfd);
				exit(1);
			}
			if(results.size()==2)
				message = "LSRP#" + full_list();
			if (results.size() == 3)
				message = "LSRP#" + specificList(results[2]);
			// send response message to requesting_id()
			send_message_no_ack(requesting_id, mapping[requesting_id], message);
		}
		
		// LS reply: print out ls reply; it was asked for
		else if (prefix.compare("LSRP") == 0) {
			string res = split(bufst, '#')[1];
			printf("%s\n",res.c_str());
		}
		
		// PUT (from not master): master determines who to put file to
		else if (prefix.compare("PUTN") == 0) {
			// don't forget to ackk!
			message = "ACKK";
			if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
				(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("Server sending error: acknowledgement on put");
				close(sockfd);
				exit(1);
			}
			vector<string> splitResults = split(bufst, '#'); //PUTN#ID#local#sdfs
			{
				std::lock_guard<std::mutex> lk(b);
				// PUTM#ID_to_put_to_1#ID_to_put_to_2#ID_to_put_to_3#sdfsfilename#localfilename
				vector<ID> IDs_to_put_to = getIDsToPutFilesTo(ID::Deserialize(splitResults[1]));
				stringstream ss;
				ss << "PUTM";
				for (auto it = IDs_to_put_to.begin(); it != IDs_to_put_to.end(); ++it)
				{
					ss << "#" << it->Serialize();
					fileList[*it].push_back(splitResults[3]); // could do this with reply message too
				}
				ss << "#" << splitResults[3] << "#" << splitResults[2];
				ID send_to_id(ID::Deserialize(splitResults[1]));
				send_message_no_ack(send_to_id, mapping[send_to_id], ss.str());
			}
			if (!fork())
			{
				send_help_messages();
				exit(0);
			}
		}
		
		// PUT (from master): contains locations of 3 nodes that the file should go to (use ftpClient to distribute)
		else if (prefix.compare("PUTM") == 0) {
			if(!fork())
			{
				put_m(bufst,sockfd,their_addr,addr_len);
				exit(0);
			}
		}
		
		// GET (from not master): send a GETM message with node_id to request from for given file
		else if (prefix.compare("GETN") == 0) {
			// don't forget to ackk!
			vector<string> splitResults = split(bufst, '#'); // GETN#ID_TO_SEND_TO#SDFS_FILENAME#LOCAL_FILENAME_TO_PUT_AS
			message = "ACKK";
			if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
				(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("Server sending error: acknowledgement on get");
				close(sockfd);
				exit(1);
			}
			if (!fork())
			{
				for (auto it = fileList.begin(); it != fileList.end(); ++it)
				{
					// GETM#ID_to_get_from#sdfsname#localname
					bool found = false;
					for (auto i = it->second.begin(); i != it->second.end(); ++i)
					{
						if (i->compare(splitResults[2]) == 0)
						{
							found = true;
							break;
						}
					}
					if (found)
					{
						message = "GETM#" + it->first.Serialize() + "#" + splitResults[2] + "#" + splitResults[3];
						ID send_to_id(ID::Deserialize(splitResults[1]));
						send_message_no_ack(send_to_id, mapping[send_to_id], message);// could do this with reply message too
						break;
					}
				}
				exit(0);
			}
		}
		
		// GET (from master): contains locations of node that the file should come from (use ftpClient to get)
		else if (prefix.compare("GETM") == 0) {
			if (!fork())
			{
				get_m(bufst,sockfd,their_addr,addr_len);
				exit(0);
			}
		}
		
		// DELETE (from not master): send a DELM message with filename to all holders of file given in DELN
		else if (prefix.compare("DELN") == 0) {
			string sdfsfilename = split(bufst, '#')[2]; // DELN#SENDER_ID#SDFS_FILENAME
			// don't forget to ackk!
			message = "ACKK";
			if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
				(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("Server sending error: acknowledgement on del");
				close(sockfd);
				exit(1);
			}
			{
				std::lock_guard<std::mutex> lk(b);
				for (auto it = fileList.begin(); it != fileList.end(); ++it)
				{
					message = "DELM#" + it->first.Serialize() + "#" + sdfsfilename;
					for (auto i = it->second.begin(); i != it->second.end(); ++i)
					{
						if (i->compare(sdfsfilename) == 0)
						{
							send_message_no_ack(it->first, mapping[it->first], message); // could do this with reply message too
							it->second.erase(i); // remove file from fileList if we got a reply
							break;
						}
					}
				}
			}
			if (!fork())
			{
				send_help_messages();
				exit(0);
			}
		}
		
		// DELETE (from master): contains name of file to delete
		else if (prefix.compare("DELM") == 0) {
			if (!fork())
			{
				del_m(bufst,sockfd,their_addr,addr_len);
				exit(0);
			}
		}
		
		// HELPER message from master: contains file list in processable form (sent after file delete or put, or after new election and transfers)
		else if (prefix.compare("HELP") == 0) {
			{
				if (node_id.isMaster)
					continue;
				std::lock_guard<std::mutex> lk(m);
				node_id.isBackup = true;
				node_id.isMaster = false;
				for(auto it = members.begin(); it != members.end(); ++it)
					if (node_id == *it)
					{
						it->isBackup = true;
						it->isMaster = false;
						break;
					}
			}
			// don't forget to ackk back to master
			message = "ACKK";
			if ((numbytes2 = sendto(sockfd, message.c_str(), message.length(), 0,
				(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("Server sending error: acknowledgement on elec");
				close(sockfd);
				exit(1);
			}
			process_help_message(bufst.substr(5));
		}
		
		// ELEC message means the sending node wants to be leader (super-simple bully protocol)
		else if (prefix.compare("ELEC") == 0) {
			const ID requesting_id(ID::Deserialize(split(bufst, '#')[1]));
			// if we are a backup, and a worse choice than the other node, do nothing
			if (requesting_id.values.first < node_id.values.first)
			{
				printf("received elec message, deferring to other backup (%s) as leader\n",requesting_id.Serialize().c_str());
				continue;
			}
			printf("received elec message, bullying other backup (%s), anointing self as leader\n", requesting_id.Serialize().c_str());
			// appoint self leader, send that info to all nodes
			announce_self_as_leader();
		}
		
		// LEDR message means that the following node has declared itself leader
		else if (prefix.compare("LEDR") == 0) {
			// set the node in the message as our leader
			const ID requesting_id(ID::Deserialize(split(bufst, '#')[1]));
			for (auto it = members.begin(); it != members.end(); ++it) {
				if (*it == requesting_id)
					it->isMaster = true;
				else
					it->isMaster = false;
			}
		}
		else
		{
			printf("received unrecognized prefix: %s\n", prefix.c_str());
		}
	}
	freeaddrinfo(servinfo);
	close(sockfd);
	return 0;
}

/**
 Input: the vm_number
 Output: the corresponding remote host
**/
std::string generate_ip(string vm_number) {
  std::string node_vmname;
  std::string vmnumber;
  int vm = std::stoi(vm_number);
  if (vm < 10) {
    vmnumber = "0" + std::to_string(vm);
  } else {
    vmnumber = "10";
  }
  node_vmname = "fa16-cs425-g40-" + vmnumber + ".cs.illinois.edu";
  return node_vmname;
}

/* 
takes message as GETM#ID_to_get_from#sdfsfilename#localfilename
receives file from ID_to_get_from called sdfsfilename, places it in localfilename
*/
static void get_m(const std::string& message,int sockfd,sockaddr_storage their_addr,int addr_len)
{
    std::string restOfMsg = message.substr(5);
    std::vector<std::string> msgParams = split(restOfMsg, '#');
    const ID id(ID::Deserialize(msgParams[0]));
    std::string sdfsFileName = msgParams[1];
    std::string localFileName = msgParams[2];
    std::string hostName = id.ip();
    ftpClient::send_get(hostName, sdfsFileName, localFileName);
	printf("got %s from sdfs: placed as %s\n", sdfsFileName.c_str(), localFileName.c_str());
}

/*
takes message as PUTM#ID_to_put_to_1#ID_to_put_to_2#ID_to_put_to_3#sdfsfilename#localfilename
receives file from ID_to_get_from called sdfsfilename, places it in localfilename
*/
static void put_m(const std::string& message,int sockfd,sockaddr_storage their_addr,int addr_len)
{
    std::string restOfMsg = message.substr(5);
    std::vector<std::string> msgParams = split(restOfMsg, '#');
	int numIds = msgParams.size() - 2;
	vector<ID> nodeIds;
	for(int i = 0;i < numIds;++i)
	{
		nodeIds.push_back(ID::Deserialize(msgParams[i]));
	}
    std::string sdfsFileName = msgParams[numIds];
    std::string localFileName = msgParams[numIds + 1];
	for(int i = 0;i < numIds;++i)
	{
		const ID node_id = nodeIds[i];
		std::string hostName = node_id.ip();
	}
	for(int i = 0;i < numIds;++i)
	{
		const ID node_id = nodeIds[i];
		std::string hostName = node_id.ip();
		ftpClient::send_put(hostName, localFileName, sdfsFileName);
	}
	printf("put %s to sdfs as %s\n", localFileName.c_str(),sdfsFileName.c_str());
}

/*
takes message as DELM#sdfsfilename
removes sdfsfilename from sdfs
*/
static void del_m(const std::string& message,int sockfd,sockaddr_storage their_addr,int addr_len)
{
    std::string restOfMsg = message.substr(5);
    std::vector<std::string> msgParams = split(restOfMsg, '#');
    const ID id(ID::Deserialize(msgParams[0]));
    std::string sdfsFileName = msgParams[1];
    std::string hostName = id.ip();
    ftpClient::send_delete(hostName, sdfsFileName);
	printf("deleted %s from sdfs\n", sdfsFileName.c_str());
}

int main(int argc, char *argv[]) {

	if (argc != 5) {
		fprintf(stderr, "usage: ./node introducer_vm introducer_port_no. node_vm node_port_no.\n");
		exit(1);
	}

	// Parse the arguments and store to the corresponing global variables.
	std::string vm_introducer = argv[1];
	introducer_IP = generate_ip(vm_introducer);
	introducer_port = argv[2];
	std::string vm_node = argv[3];
	node_IP = generate_ip(vm_node);
	node_port = argv[4];
	//leaving_time = std::stoi(argv[5]);
	//joining_back_time = std::stoi(argv[6]);

	// Output to the log
	/**
	string temp = "machine." + vm_node + ".log";
	const char* file_name = temp.c_str();
	freopen(file_name, "w", stdout);
    **/

    //TODO add ftpServer to a thread so we are always ready for file transfer
	file_transfer_port = 10001;
	std::thread third(demo_action);
	// Multi thread
	std::thread first(server_listening);
	std::thread second(client_talker);
	first.join();
	second.join();
	third.join();
	return 0;
}
