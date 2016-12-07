# Failure Detector used from assigned MP2
This submission implements a group membership protocol based on SWIM (i.e. ping-ack style of failure detection).

## Design
In this project, we implemented a distributed group membership protocol which maintains at each machine a list of all machines that are connected and up. We have a fixed contact machine that all other members know about. We call it the “introducer”. Every machine runs a code in which there is a server thread that continuously waits for incoming messages. There is one client thread that begins by sending a JOIN request to the introducer node and in turn receives a message containing its ID and current connected members list. The members keep on sending PING messages to maintain their membership list. Whenever they detect a timeout on another machine, they delete that node from their list. They also broadcast this information to all other nodes through FAIL messages so they can also delete the node from their lists. Also whenever a new node joins the group, the introducer assigns it an ID. It also sends NEWJ (new join) messages to nodes in its membership list so that they all can add the new node in their lists. Now, they can ping each other to maintain the lists. Whenever a node wants to leave the group, it sends a LEAV message to other nodes so that they can delete it from their list. It astos pinging other machine nodes. The algorithm that we use algorithm uses a small bandwidth (messages per second). We use UDP messages to send these messages. 

Although we use a special introducer node in our implementation it is fault tolerant in the way that, even if the introducer fails, the rest of the processes can continue failure detector module without any problem, and introducer failure only limits joining new processes in the group. Also, if the introducer voluntarily leaves, and joins back, it can begin its operation of letting other machines join. 
 
## Algorithm Used
The nodes in every nodes’ membership list are sorted by timestamps. Every node pings the machine in a different order. Each node starts from their next node. For e.g. there are 7 machines, let us call them 1,2,3,4,5,6,7. 
Node 1 pings machines in the order: 2->3->4->5->6->7
Node 2 pings machines in the order: 3->4->5->6->7->1
We have kept our ping interval to be 1.5 seconds. At any instant time (assuming all start pinging together) each node should be pinged. Since, we have been given that at most 2 machines can fail simultaneously, with a ping interval of 1.5 s, we can say for sure that within 3 seconds machine failure would be detected in at least one membership list. 

Our application is fast (detects failure within 3 seconds and disseminates within 6 sec), complete and consistent with very low false positive rate. We took into account various cases involving introducer failure, high network latencies and scalability while implementing the MP.

## Instructions
### Step 1 - Set up code in VM's
The following are the steps to get code and compile in application in a VM:
1. ssh into the vm machine : Eg - ```ssh <NETID>@fa15-cs425-gNN-XX.cs.illinois.edu```
2. Type ```git clone https://gitlab-beta.engr.illinois.edu/jfetsch2/CS425_MPs.git```
3. cd into the project root directory

### Step 2 - Run Introducer
1. ssh into the first vm machine: ```ssh <NETID>@fa15-cs425-gNN-XX.cs.illinois.edu```
2. compile code using "make"
3. ./sdfs & 
4. ./node introducer_vm introducer_port_no. introducer_vm introducer_port_no.
5. Type: "join" to make the introducer start

### Step 3 - Run Normal processes
You can any any number of processes you want in the group
1. ssh into the vm machine: ```ssh <NETID>@fa15-cs425-gNN-XX.cs.illinois.edu```
2. compile code using "make"
3. ./sdfs &
4. ./node introducer_vm introducer_port_no. node_vm node_port_no.
5. Type: "join" to start the process and join it with the introducer.

### Step 4 - Run FTP commands
From any running and connected node, commands are as follows:
- list: shows all nodes connected to the network
- ls: display a full list of all nodes and all files stored on each node
- ls <arg>: show all nodes who have a copy of <arg> stored locally
- back: show all nodes who are backup nodes (runnable from master or backups only)
- store: show all files held locally on that node
- id: display the id of the current node
- get <sdfsFilename> <localFilename>: put file from sdfs to local path
- put <localFilename> <sdfsFilename>: put file from local path to sdfs
- delete <sdfsFilename>: remove all instances of sdfsFilename from sdfs
