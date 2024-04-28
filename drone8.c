// CSCI 3762 NETWORK PROGRAMMING
// MILES DIXON
// LAB7 / fixing lab6 / lab8???

/*
TO DO
--------
FIRST FIX LAB6 STUFF
- get seq num working properly, modify sendpath to work correctly
idea: create struct to store tp, fp, and seqnum, check if tp/fp inter exists, if so seqnum+=1
	- check messages after they are typed for the tp fp inter, start seqnum at 1


THEN GET LAB8 STUFF GOING
- modify forward to store message(s) and forward every 20s to all in config file (see timeout code)
- only forward 5 times, then delete message(s) and stop, (probalbly can't use a simple forloop here)
- Do not store duplicate messages, check new against old to verify (seqnum, toport, fromport)
- if you move to new square, send out all stored msgs before moving, 

- IF THIS GETS DONE: check acks that pass through and delete stored messages related to ack




UPDATE 4/23/24
- tpfpinter not quite working right, but seq num seems to be increasing 
- need to implement the func that changes tokens to string before msg sent like I do w acks/fwds
- 
*/

#include <arpa/inet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define MAX_LOCATIONS 100
#define BUFFER_SIZE 1024
#define MAX_PORTS 30
#define MAX_INTERACTIONS 20 //may need to change this depending on size of config file


struct ServerConfig {
    char serverIP[16];
    int port;
    int location;
};


struct tpFpInter { // toPort fromPort interaction, used to track seq num
	int toPort;
	int fromPort;
	int seqNum;
}; //did i define this twice? am i blind?

/*struct Message {
    char msg[BUFFER_SIZE];
    int toPort;
    int fromPort;
};*/ //come back to this later if needed???

struct _tokens {
    char key[100];
    char value[100];
};

struct tpfpInter {
	//store toport fromport interactions to determine seqnum
	int tp, fp, seq;
};

//find tokens in the message from professor Ogle
int findTokens(char *buffer, struct _tokens *tokens) {
    int counter = 0;
    char *ptr;

    //tokenize first on the space ' '
    ptr = strtok(buffer, " ");
    while (ptr != NULL) {
        memset(tokens[counter].key, 0, 100);
        memset(tokens[counter].value, 0, 100);

        int i = 0;
        int flag = 0;
        int k = 0; 
        for (i = 0; i < strlen(ptr); i++) {
            if (flag == 0) { 
                if (ptr[i] != ':')
                    tokens[counter].key[i] = ptr[i];
                else
                    flag = 1;
            } else {                  
                if (ptr[i] == '^') {
                    ptr[i] = ' ';
                }
                tokens[counter].value[k] = ptr[i];
                k++;
            }
        }
        ptr = strtok(NULL, " ");
        counter++;
    }
    return counter;
}

// parse the config file
int parseConfigFile(const char *filename, struct ServerConfig *servers,
                    int *num_servers) {
    FILE *config_file = fopen(filename, "r");
    if (config_file == NULL) {
        perror("Error opening config file");
        return -1;
    }
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), config_file) != NULL) {
        if (*num_servers >= MAX_LOCATIONS) {
            fprintf(stderr, "Maximum number of servers reached.\n");
            break;
        }
        sscanf(line, "%s %d %d", servers[*num_servers].serverIP,
               &servers[*num_servers].port, &servers[*num_servers].location);
        (*num_servers)++;
    }
    fclose(config_file);
    return 0;
}

// euclidean distance calculator
double distance(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    printf("dist b4 truncate: %f\n", sqrt(dx*dx + dy*dy));
    return abs(sqrt(dx * dx + dy * dy)); //use abs to truncate val, ex: 1 -> 7 = success, 1-> 
}

//create new msg from extracted tokens from msg
void createStringFromTokens(struct _tokens *tokens, int num_tokens, char *output) {
    output[0] = '\0';
    for (int i = 0; i < num_tokens; i++) {
        //key:value
        strcat(output, tokens[i].key);
        strcat(output, ":");
        strcat(output, tokens[i].value);
        //add space if not last token
        if (i != num_tokens - 1) {
            strcat(output, " ");
        }
    }
}

//swap toPort and fromPort in msg, used for sending ACK
void swapPorts(struct _tokens *tokens, int num_tokens) {
    char temp[100];
    for (int i = 0; i < num_tokens; i++) {
        if (strcmp(tokens[i].key, "toPort") == 0) {
            // swap toPort and fromPort values
            for (int j = 0; j < num_tokens; j++) {
                if (strcmp(tokens[j].key, "fromPort") == 0) {
                    strcpy(temp, tokens[i].value);
                    strcpy(tokens[i].value, tokens[j].value);
                    strcpy(tokens[j].value, temp);
                    break; //fromPort found
                }
            }
            break; //toPort found
        }
    }
}

//find ports in received sendpath
void extractPorts(const char *sendPath, int *ports, int *numPorts) {
    *numPorts = 0;
    //tokenize on comma
    char *token = strtok((char *)sendPath, ",");
    while (token != NULL) {
        //convert to int, store
        ports[*numPorts] = atoi(token);
        (*numPorts)++;
        token = strtok(NULL, ",");
    }
}

//omit forwarding to those in sendpath
bool isPortInArray(int port, const int *ports, int numPorts) {
    for (int i = 0; i < numPorts; i++) {
        if (ports[i] == port) {
            return true; //port found in arr
        }
    }
    return false; //port not in arr
}



int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <config_file> <port> <gridsize>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[2]);
    char myPortStr[5]; //used for adding port to sendpath
    sprintf(myPortStr, "%d", port);
    int gridSize = atoi(argv[3]);
    if (port <= 0 || port > 65535 || gridSize <= 0) {
        fprintf(stderr, "Invalid port number or grid size.\n");
        exit(1);
    }

    struct ServerConfig servers[MAX_LOCATIONS];
    int num_servers = 0;

    // parse config and store info
    if (parseConfigFile(argv[1], servers, &num_servers) != 0) {
        exit(1);
    }

    // create / bind socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) ==
        -1) {
        perror("bind");
        close(sockfd);
        exit(1);
    }

    // get selected port
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(sockfd, (struct sockaddr *)&addr, &len);

    printf("Server listening on port %d...\n", ntohs(addr.sin_port));
    char buffer[BUFFER_SIZE];
    // struct Message userMessage;

    fd_set read_fds;

    //char sendPath[BUFFER_SIZE];  /////////////////////// SEND PATH ARRAY
                                 ///////////////////////////////////////
    int myLocation;
    for (int i = 0; i < num_servers; ++i) {
    	if (servers[i].port == port) {
        	myLocation = servers[i].location;
        }
    }
    printf("Drone positioned at location: %d\n", myLocation);


	//BEGIN STRUCTURE TO STORE TP/FP INTERACTIONS, USED TO DETERMINE SEQ NUM
	struct tpfpInter interactions[MAX_INTERACTIONS];

    while (1) {
    
        printf("Enter a message: \n");
        fflush(stdout);  //force output
        //printf("MY LOCATION: %d\n", myLocation);

        //reset read_fds, add file descriptors
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd, &read_fds);

        //wait for activity on stdin/socket
        int activity = select(sockfd + 1, &read_fds, NULL, NULL, NULL);
        if (activity == -1) {
            perror("select");
            exit(1);
        }
		//int locTest = -1;
        char addLoc[] = " location:";
        char addSendpath[] = " send-path:";
        char addSeqNum[] = " seqNumber:1"; //starts at 1 //DELETE THIS
        char pathStr[100];
        char locStr[4];
        int sendpathPorts[MAX_PORTS];
        int numSPPorts; // num ports in sendpath
        
        
        
        // Handle standard input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
            	char tempBuff[BUFFER_SIZE];
            	strcpy(tempBuff, buffer);
            	printf("%s\n%s\n", buffer, tempBuff);
            	struct _tokens inputTokens[10];
            	//struct tpfpInter interactions[MAX_INTERACTIONS];
            	int iptp, ipfp; //user inputted toport and fromport
            	int numIpTok = findTokens(tempBuff, inputTokens); //num input tokens
            	printf("TOKENS FROM TYPED MSG:\n");
                for(int i=0; i<numIpTok; i++){
                	if(strcmp(inputTokens[i].key, "toPort") == 0){
                		iptp = atoi(inputTokens[i].value);
                		printf("%s: %d\n", inputTokens[i].key, iptp);
                	}else if(strcmp(inputTokens[i].key, "fromPort") == 0){	
                		ipfp = atoi(inputTokens[i].value);
                		printf("%s: %d\n", inputTokens[i].key, ipfp);
                	}
                	//printf("key: %s  val: %s\n", inputTokens[i].key, inputTokens[i].value);
                	/*if (strcmp(inputTokens[i].key, "toPort") == 0){
                		interactions[i].tp = atoi(inputTokens[i].value);
                	}
                	else if (strcmp(inputTokens[i].key, "fromPort") == 0){
                		interactions[i].fp = atoi(inputTokens[i].value);
                	}*/
                }
                /*for(int i=0; i<MAX_INTERACTIONS; i++){
                	if(iptp == interactions[i].tp){
                		printf("iptp found in interactions: iter {%d}\n", i);
                		if(ipfp == interactions[i].fp){
                			printf("ipfp also found in interactions: iter {%d}\n", i);
                		}
                	}else{
                		interactions[i].tp = iptp;
                		printf("inter i %d\n", interactions[i].tp);
                	}
                		
                }*/
                //for(int i=0; i<MAX_INTERACTIONS; i++){
                bool lit;
                int superlit;
                	for(int i=0; i<MAX_INTERACTIONS; i++){
                		if(interactions[i].tp == iptp && interactions[i].fp == ipfp){
                			printf("interaction found in array iter, seq num += 1\n");
                			lit = false;
                			superlit = i;
                			interactions[i].seq += 1;
                			printf("seq: %d\n", interactions[i].seq);
                		}else{
                			printf("interaction NOT found in array, seq num = 1\n");
                			lit = true;
                			interactions[i].seq = 1;
                			interactions[i].tp = iptp;
                			interactions[i].fp = ipfp;
                			printf("tp: %d	fp: %d	seq: %d\n", interactions[i].tp, interactions[i].fp, interactions[i].seq);
                		}
                	}
                	
                if(lit == true){
                	interactions[superlit].tp = iptp;
                	interactions[superlit].fp = ipfp;
                	interactions[superlit].seq = 1;
                	printf("tp/fp/seq:%d/%d/%d\n", interactions[superlit].tp, interactions[superlit].fp, interactions[superlit].seq);
                }else if(lit ==false){
                	interactions[superlit].seq += 1;
                	printf("tp/fp/seq:%d/%d/%d\n", interactions[superlit].tp, interactions[superlit].fp, interactions[superlit].seq);
                }
                
                
                printf("\n\n////////////////////\n\n");
                for(int i=0; i<MAX_INTERACTIONS; i++){
                	printf("%d %d %d\n", interactions[i].tp, interactions[i].fp, interactions[i].seq);
                }
                printf("\n\n////////////////////\n\n");
                //}
                
                /*for(int i=0; i< MAX_INTERACTIONS; i++){
                	printf("tpfpinter%d: tp:%d  fp:%d\n", i, interactions[i].tp, interactions[i].fp);
                }*/
               	
                
                //for(int i=0; i<numIpTok;
                
                //char seqnumStr[4];
                size_t bufLen = strlen(buffer);
                
                sprintf(locStr, "%d", myLocation);
                sprintf(pathStr, "%d", port);
                
                if (bufLen + strlen(addLoc) + strlen(locStr) <
                    BUFFER_SIZE - 1) {
                    if (buffer[bufLen - 1] == '\n') {
                        buffer[bufLen - 1] = '\0';
                    }
                    strcat(buffer, addLoc);
                    // printf("New buff: %s\n", buffer);
                    strcat(buffer, locStr); //add loc to buffer
                    strcat(buffer, addSendpath); //add sendpath:
                    strcat(buffer, pathStr); //add sendpath (x,x,x)
                    strcat(buffer, addSeqNum); //add seqnum
                    printf("New Buff: %s\n", buffer);
                } else {
                    printf("Not enough space in buffer to append\n");
                }
                // Send message to all servers
                for (int i = 0; i < num_servers; ++i) {
                    struct sockaddr_in destAddr;
                    memset(&destAddr, 0, sizeof(destAddr));
                    destAddr.sin_family = AF_INET;
                    destAddr.sin_addr.s_addr = inet_addr(servers[i].serverIP);
                    destAddr.sin_port = htons(servers[i].port);
                    ssize_t bytesSent = sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
                    if (bytesSent == -1) {
                        perror("sendto");
                        continue;
                    }
                    printf("Sent message to location %d (IP: %s, Port: %d)\n", servers[i].location, servers[i].serverIP, servers[i].port);                
                }
            }
        }

        // Handle incoming messages
        if (FD_ISSET(sockfd, &read_fds)) {
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            ssize_t bytesRead = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&clientAddr, &clientLen);
            if (bytesRead == -1) {
                perror("recvfrom");
                continue;
            }

            // Print the received buffer
            printf("Received Buffer 1: %s\n", buffer);
            char buffTest[BUFFER_SIZE * 2];
            strcpy(buffTest, buffer);
            // printf("copy buffer: %s\n", buffTest);
            buffer[bytesRead] = '\0';  //null terminate
            // Check if the message is intended for this server
            int receivedToPort = -1;
            int receivedFromPort = -1;
            int receivedTTL = -1;
            int receivedLoc = -1;
            int receivedVersion = 7;  // default lab7
            int receivedSeqNum = -1;
            char receivedType[4];
            char receivedMsg[40];
            char receivedSendpath[100];
            int moveToLoc = -1;
            time_t receivedTime = 0;

            struct _tokens tokens[10];
            int num_tokens = findTokens(buffer, tokens);
            // int tokennum = sizeof(tokens) / sizeof(tokens[0]);
            // printf("numtokens: %d  tokennum: %d\n", num_tokens, tokennum);
            // printf("Received Buffer2: %s\n", buffer);
            // printf("copy buffer: %s\n", buffTest);
            for (int i = 0; i < num_tokens; ++i) {
                if (strcmp(tokens[i].key, "msg") == 0) {
                    strncpy(receivedMsg, tokens[i].value, BUFFER_SIZE);
                    receivedMsg[39] = '\0';  // GOTTA FIGURE OUT HOW TO GET MESSASGES w/ SPACES TO BE HANDLED, THIS DOESN'T WORK
                } else if (strcmp(tokens[i].key, "toPort") == 0) {
                    receivedToPort = atoi(tokens[i].value);
                    // printf("receivedToPort: %d ", receivedToPort);
                } else if (strcmp(tokens[i].key, "fromPort") == 0) {
                    receivedFromPort = atoi(tokens[i].value);
                    // printf(" receivedFromPort: %d\n", receivedToPort);
                } else if (strcmp(tokens[i].key, "TTL") == 0) {
                    receivedTTL = atoi(tokens[i].value);
                } else if (strcmp(tokens[i].key, "location") == 0) {
                    receivedLoc = atoi(tokens[i].value);
                } else if (strcmp(tokens[i].key, "version") == 0) {
                    receivedVersion = atoi(tokens[i].value);
                } else if (strcmp(tokens[i].key, "seqNumber") == 0) {
                    receivedSeqNum = atoi(tokens[i].value);
                } else if (strcmp(tokens[i].key, "type") == 0) {
                    strncpy(receivedType, tokens[i].value, 4);
                } else if (strcmp(tokens[i].key, "send-path") == 0) {
                    strncpy(receivedSendpath, tokens[i].value, BUFFER_SIZE);
                    //printf("sendpath token: %s\n", tokens[i].value);
                    //printf("copy token: %s\n", receivedSendpath);
                } else if (strcmp(tokens[i].key, "move") == 0){
                	moveToLoc = atoi(tokens[i].value);
                } else if (strcmp(tokens[i].key, "time") == 0){
                	receivedTime = atoi(tokens[i].value);
                	printf("received time: %ld\n", receivedTime);
                }
            }
            
            //CHANGE MY LOCATION IF MSG FOR ME
            if(receivedToPort == port){
            	if(moveToLoc <= gridSize*gridSize && moveToLoc > 0){ //if location is within my gridsize, change location, else reject move command
            		for (int i = 0; i < 60; i++) {printf("*");}
            		printf("\nRECEIVED LOCATION MOVE COMMAND\nChanging my location to: %d\n", moveToLoc);
            		printf("Time: %ld\n", time(NULL));
			for (int i = 0; i < 60; i++) {printf("*");} printf("\n");
            		myLocation = moveToLoc;
			continue;
            	} else if(moveToLoc == -1){
            		//DO NOTHING, default moveToLoc value
            	} else {
            		for (int i = 0; i < 60; i++) {printf("*");}
            		printf("\nRECEIVED LOCATION MOVE COMMAND OUT OF RANGE\nMAINTAINING LOCATION: %d\n", myLocation);
            		for (int i = 0; i < 60; i++) {printf("*");} printf("\n");
            	}
            }
            
            
            char portStr[5];
            char copySendpath[100]; //create copy of received sendpath, extractPorts func messes up full sendpath
            sprintf(portStr, "%d", receivedToPort);
			printf("received sendpath1: %s\n", receivedSendpath);
			strcpy(copySendpath, receivedSendpath);
			extractPorts(receivedSendpath, sendpathPorts, &numSPPorts);
			printf("copySendpath: %s\n", copySendpath);
			printf("Extracted port numbers:\n");
    		for (int i = 0; i < numSPPorts; i++) {
        		printf("%d\t", sendpathPorts[i]);
    		} printf("\n");

			//FIND DISTANCE
            int senderX = (receivedLoc - 1) % gridSize;  // changed senderLocation to receivedLoc, now fetch from msg, not from config file
            int senderY = (receivedLoc - 1) / gridSize;  // also changed ^^^^^^
            int myX = (myLocation - 1) % gridSize;
            int myY = (myLocation - 1) / gridSize;
	    senderX++; senderY++; myX++; myY++;
	    printf("senderX: %d, senderY:%d, myX:%d, myY:%d\n", senderX, senderY, myX, myY);
            double dist = distance(myX, myY, senderX, senderY); printf("dist: %f\n", dist);
            
            
            if (receivedToPort != port) {
                printf("Received message not for me: receivedToPort:%d  receivedLoc:%d  dist:%f\n", receivedToPort, receivedLoc, dist);

                if (dist <= 2) {
                    if (receivedTTL > 0) {
                        //decrement ttl and forward msg
                        char *ttlPtr = strstr(buffTest, "TTL:"); //find ttl in str to decrement
                        if (ttlPtr != NULL) {
                            ttlPtr += strlen("TTL:");
                            receivedTTL -= 1;
                            sprintf(ttlPtr, "%d", receivedTTL);
                            // printf("New Buffer: %s\n", buffTest);
                        } else {
                            printf("TTL not found in buffer\n");
                        }
                        //int locTest = -1;
                        char additions[] = " location:";
                        char locStr[4];
                        char timeStr[10];
                        size_t bufLen = strlen(buffTest);
                     
                        sprintf(locStr, "%d", myLocation);
                        createStringFromTokens(tokens, num_tokens, buffer);
			printf("Buffer after tokenstr: %s\n", buffer); 
                        //add my location, revise sendpath
                        if (bufLen + strlen(additions) + strlen(locStr) + strlen(addSendpath) + strlen(myPortStr) < BUFFER_SIZE - 1) {
                            if (buffer[bufLen - 1] == '\n') {
                                buffer[bufLen - 1] = '\0';
                            }
                            strcat(buffTest, additions);
                            // printf("New buff: %s\n", buffer);
                            strcat(buffTest, locStr);
                            strcat(buffTest, addSendpath);
                            strcat(buffTest, receivedSendpath);
                            strcat(buffTest, ",");
                            strcat(buffTest, myPortStr);
                            strcat(buffTest, " time:");
                            sprintf(timeStr, "%ld", time(NULL));
                            strcat(buffTest, timeStr);
                            
                            // printf("New Buff: %s\n", buffer);
                        } else {
                            printf("Not enough space in buffer to append location/sendpath\n");
                        }

                      
                        for (int i = 0; i < 60; i++) {printf("*");} printf("\n");
                        printf("Forwarding message: %s\n", buffTest);
                        printf("receivedType: %s\n", receivedType);
                        for (int i = 0; i < num_servers; ++i) {
                        	//if type ack do not forward
                        	if(strcmp(receivedType, "ACK") == 0){
                        		printf("Not forwarding ACK\n");
                        		continue;
                        	}
                        	//if port in sendpath, do not forward, else forward
                        	if(isPortInArray(servers[i].port, sendpathPorts, numSPPorts)){
                        		printf("Omitted sending to port: %d\n", servers[i].port);
                        		continue;
                        	}
				struct sockaddr_in destAddr;
				memset(&destAddr, 0, sizeof(destAddr));
			        destAddr.sin_family = AF_INET;
		                destAddr.sin_addr.s_addr = inet_addr(servers[i].serverIP);
		                destAddr.sin_port = htons(servers[i].port);
		                ssize_t bytesSent = sendto(sockfd, buffTest, strlen(buffTest), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
				if (bytesSent == -1) {
	                            perror("sendto");
				    continue;
				}
				printf("Msg forwarded to port: %d\n", servers[i].port);
                        }
                        for (int i = 0; i < 60; i++) {printf("*");} printf("\n");
                        
                    } else {
                        // printf("Received message w/ TTL expired: %d\n",
                        // receivedTTL); IGNORE MSG IF TTL <= 0
                    }
                }

            } else {
                if (dist <= 2) {
                    if (receivedTTL > 0) {
                    	//printf("BEFORE RECEIVING ACK: port: %d  receivedToPort: %d\n", port, receivedToPort);
                        if (strcmp(receivedType, "ACK") == 0 && receivedToPort == port) {
                			for (int i = 0; i < 60; i++) {printf("*");}
                			printf("\nGOT ACK\n");
                			printf("\nReceived Buffer: %s\n", buffTest);
                			printf("Received ACK:\nversion:\t%d\nmsg:\t\t%s\ntoPort:\t\t%d"
                    			"\nfromPort:\t%d\nTTL:\t\t%d\ntype:\t\t%s\nlocation:"
                    			"\t%d\nmyLocation:\t%d\nseqNumber:\t%d\nsendPath\t%s\ntime:\t\t%ld\n",
                    			receivedVersion, receivedMsg, receivedToPort,
                    			receivedFromPort, receivedTTL, receivedType,
                    			receivedLoc, myLocation, receivedSeqNum, copySendpath, time(NULL));
                			for (int i = 0; i < 60; i++) {printf("*");} printf("\n");
                			//break;
            			} else {
                        for (int i = 0; i < 60; i++) {printf("*");}
                        printf("\nReceived Buffer: %s\n", buffTest);
                        printf("\nReceived message:\nversion:\t%d\nmsg:\t\t%s\ntoPort:\t\t%d"
                            "\nfromPort:\t%d\nTTL:\t\t%d\ntype:\t\t%s"
                            "\nlocation:\t%d\nmyLocation:\t%d\nseqNumber:\t%d\nsendPath\t%s\ntime:\t\t%ld\n",
                            receivedVersion, receivedMsg, receivedToPort,
                            receivedFromPort, receivedTTL, receivedType,
                            receivedLoc, myLocation, receivedSeqNum, copySendpath, time(NULL));
                        for (int i = 0; i < 60; i++) {printf("*");} printf("\n");}

                        ///////////////ACK MESSAGE/////////////////
                        ////add type to msg//////
                        char addType[] = " type:ACK";
                        swapPorts(tokens, num_tokens); //swap to/from ports when sending ack
                        printf("receivedSendpath2: %s\n", receivedSendpath);
                        for (int i = 0; i < num_tokens; ++i) {
                            if (strcmp(tokens[i].key, "TTL") == 0) {
                                int ttl = atoi(tokens[i].value);
                                printf("ttl before dec: %d\n", ttl);
                                ttl -= 1;
                                sprintf(tokens[i].value, "%d", ttl);
                                printf("new ttl: %d\n", ttl);
                            }
                            if (strcmp(tokens[i].key, "send-path") == 0){
                            	//sprintf(tokens[i].value, "%s", //
                            }
                        }
                        char tokenStr[BUFFER_SIZE];
                        tokenStr[BUFFER_SIZE - 1] = '\0';
                        
                        printf("BEFORE SP TOKEN: receivedToPort: %d  port: %d\n", receivedToPort, port);
                        if(receivedToPort != port){
		                    for (int i = 0; i < num_tokens; ++i) {
		            			if (strcmp(tokens[i].key, "send-path") == 0) {
		            				strcpy(tokens[i].value, receivedSendpath);
		            				printf("new sp token: %s\n", tokens[i].value);
		            			}
		            			/*else if (strcmp(tokens[i].key, "TTL") == 0){
		            				strcpy(tokens[i].value, "1"); //CHANGE THIS LATER
		            				printf("new ttl token: %d\n", tokens[i].value);
		            			}*/
		            		}
			}
			printf("TOKENS\n");			
			for(int i=0; i< num_tokens; i++){ 
				//printf("key: %s, value: %s\n", tokens[i].key, tokens[i].value);
                        	if(strcmp(tokens[i].value, copySendpath) == 0){
					printf("copySP matches tokenval: orig:%s copy:%s\n", tokens[i].value, copySendpath);
				}
			}
			printf("END TOKENS\n");
			/*for(int i=0; i< num_tokens; i++){
				if(strcmp(tokens[i].key, "move" == 0){
					printf("deleting move from tokens\n");
			*/		////////////////////
			createStringFromTokens(tokens, num_tokens, tokenStr);
                        if(strcmp(receivedType, "ACK") == 0){
                        	printf("ACK type already in msg, not sending ACK\n");
                        	continue;
                        } else {
                        	printf("adding ACK to tokenstr\n");
                        	strcat(tokenStr, addType);
                       }
                        printf("Token String: %s\n", tokenStr);
                        //printf("receivedFromPort: %d\n", receivedFromPort);
                        
                        ///////SEND ACK///////////
                        struct sockaddr_in destAddr;
                        memset(&destAddr, 0, sizeof(destAddr));
                        destAddr.sin_family = AF_INET;
                        destAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); //assuming loopback addr, may change later if needed
                        destAddr.sin_port = htons(receivedFromPort);
                        ssize_t bytesSent = sendto(sockfd, tokenStr, strlen(tokenStr), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));  // buffTest changed to tokenStr
                        if (bytesSent == -1) {
                            perror("sendto");
                            continue;
                        }
                        printf("SENT ACK\n");
                    }

                    else {
                        // printf("Received message w/ TTL expired: %d\n",
                        // receivedTTL);
                    }
                } else {
                    printf("Received message out of range\n"); //do nothing / discard msg
                }
            }
        }
    }  // endwhile

    close(sockfd);
    return 0;
}
