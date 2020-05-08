#include <remoteClient.h>

void perror_exit(const char *message, int exit_code);
void clientSender(char* serverName, int serverPort, int receivePort, char* fileName);
void clientReceiver(int receivePort, char* fileName);

int main(int argc, char *argv[]){ //server_name, server_port, receive_port, input_file
    //Checking Parameters
    if (argc != 5) perror_exit("usage: server_name server_port receive_port input_file!", ERR_C_PARAM);

    //Initiating receiver-side of client
    switch (fork()){
        case 0: clientReceiver(atoi(argv[3]), argv[4]);
        case -1: fprintf(stderr, "Could not initiate receiver-side of client!\n");
    }
    //Initiating sender-side of client
    switch (fork()){
        case 0: clientSender(argv[1], atoi(argv[2]), atoi(argv[3]), argv[4]);
        case -1: fprintf(stderr, "Could not initiate sender-side of client!\n");
    }
    printf("[Parent %d] : Client Sender and Receiver created successfully!\n", getpid());
    
    //Waiting for children to terminate
    wait(NULL);
    wait(NULL);
    printf("[Parent %d] : Exiting Client %d!\n", getpid(), atoi(argv[3]));
    return 0;
}

/**
 * This is the sender side of the client. It extracts instructions from an input file
 * so that it can create and send TCP packets to the server.
 */
void clientSender(char* serverName, int serverPort, int receivePort, char* fileName){
    printf("[Sender %d] : Sender side of client created on server port %d, with a receiving port %d\n", getpid(), serverPort, receivePort);
    //Creating TCP socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) perror_exit("Socket Creation Error!", ERR_CS_SOCK_CREATE);
    printf("[Sender %d] : Sender-End of client TCP socket created\n", getpid());

    //Gathering server info
    struct hostent *host = gethostbyname(serverName); //Finding server address
    if (host == NULL) {
        perror_exit("Could not get server by name!", ERR_CS_HOST_NAME);
        exit(1);
    }
    printf("[Sender %d] : Found Server %s-%s:%d)\n", getpid(), host->h_name, host->h_addr, serverPort);
    struct sockaddr_in server_info;
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(serverPort);
    memcpy(&server_info.sin_addr, host->h_addr_list[0], host->h_length);

    //Connecting to server
    int c = connect(s, (struct sockaddr *)&server_info, sizeof(server_info));
    if (c < 0) perror_exit("Could not connect to server!", ERR_CS_SOCK_CONN);
    printf("[Sender %d] : Connected to server\n", getpid());

    //Reading instructions from input file and sending them to server parent
    char line_buffer[MAX_LINE_LEN]; //each line is stored in this buffer
    FILE *file = fopen(fileName, "r");
    if (file == NULL) perror_exit("Could not open file!", ERR_CS_FILE_OPEN);
    int line_counter = 0;
    //Reading input file line by line and sending data
    while(fgets(line_buffer, MAX_LINE_LEN, file)){ //for each line
        //Constructing TCP Packet
        struct send_info si;
        si.order = line_counter;
        si.receive_port = receivePort;
        memcpy(si.data, line_buffer, sizeof(si.data));
        si.data[strcspn(si.data, "\n")] = 0;

        //Sending data to socket
        if (write(s, (void*)(&si), sizeof(si)) < 0) perror_exit("Write Error!", ERR_CS_WRITE);
        printf("[Sender %d] : ===== SENT TCP PACKET (INSTRUCTION %d) =====\n\"%s\"\n[Sender %d] : ============================================\n", getpid(), si.order, si.data, getpid());
        
        //Sleeping. This is for having a small delay between packets. 
        //It helps eliminating the number of packets dropped.
        usleep(10000);

        //waiting 5 seconds every 10 packets
        if (line_counter % CONSECUTIVE_MESSAGES == 0 && line_counter != 0){
            printf("[Sender %d] : Waiting for 5 seconds\n", getpid());
            sleep(5);
        }
        line_counter++;
    }
    //Terminating connection
    printf("[Sender %d] : Terminated connection with server %s:%d \n", getpid(), inet_ntoa(server_info.sin_addr), ntohs(server_info.sin_port));
    close(s);
    exit(0);
}

/**
 * This is the receiver side of the client. It receives UDP packets from the server
 * containing executed instructions and it outputs them to files.
 */
void clientReceiver(int receivePort, char* fileName){
    printf("[Receiver %d] : Receiver side of client created with a receiving port %d\n", getpid(), receivePort);

    //Creating UDP socket
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) perror_exit("Socket Creation Error!", ERR_CR_SOCK_CREATE);
    printf("[Receiver %d] : Receiver-End of client UDP socket created\n", getpid());

    //Acquiring client info and bind socket
    struct sockaddr_in client_info;
    client_info.sin_family = AF_INET;
    client_info.sin_addr.s_addr = INADDR_ANY;  //receive from any address
    client_info.sin_port = htons(receivePort);

    int b = bind(s, (const struct sockaddr *) (&client_info), sizeof(client_info));
    if (b < 0) perror_exit("Socket Bind Error!", ERR_CR_SOCK_BIND);
    printf("[Receiver %d] : Receiver-End of UDP socket bound\n", getpid());
    
    //Estimating number of instructions in input file. 
    //We need this so that we know how many "answers" we are waiting for.
    FILE *input = fopen(fileName, "r");
    char ch; int files = 1, char_since_last_new_line = 0;
    while ((ch = fgetc(input)) != EOF){
        if (ch == '\n') {
            files++;
            char_since_last_new_line = 0;
        } else char_since_last_new_line++;
    }
    if (char_since_last_new_line == 0)
        files--;

    printf("[Receiver %d] : %d instructions found from %s\n", getpid(), files, fileName);

    //Creating output files (keeps file fds open until writing is finished)
    FILE *fds[files];
    for (int i = 0; i < files; i++) {
        char fileName[30];
        sprintf(fileName, "output.%d.%d", receivePort, (i+1));
        fds[i] = fopen(fileName, "w");
    }

    //Receiving UDP Packets
    while (1){
        struct sockaddr_in server_info;
        struct receive_info ri;
        socklen_t length = sizeof(server_info);
        
        //Receiving data
        int rcv = recvfrom(s, (char*)&ri, sizeof(struct receive_info), MSG_WAITALL, (struct sockaddr *) (&server_info), &length);
        if (rcv != sizeof(ri)) {
            fprintf(stderr, "Receive problem: %d.\n", rcv);
            continue;
        }
	    //printf("files: %d, order: %d, part: %d", files, ri.order, ri.sub_order);
        printf("[Receiver %d] : ===== RECEIVED UDP PACKET %d (PART %d, IS LAST: %d) =====\n\"%s\"\n[Receiver %d] : =============================================\n", getpid(), ri.order, ri.sub_order, ri.sub_last, ri.data, getpid());
        
        //Writing received data to output files
        FILE *f = fds[ri.order];
        fseek(f, sizeof(ri.data) * ri.sub_order, SEEK_SET);
        fwrite(ri.data, strnlen(ri.data, sizeof(ri.data)), 1, f);

        //This receiver is done when it receives the final part of the last executed instruction
        if (ri.order == files - 1 && ri.sub_last) break;
    }
    //Closing file fds and exiting
    printf("[Receiver %d] : Shutting down!\n", getpid());
    for (size_t i = 0; i < files; i++) fclose(fds[i]);
    exit(0);
}
