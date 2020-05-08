#include <remoteServer.h>

void perror_exit(const char *message, int exit_code);
void serverParent(int pipe_write, int serverPort);
void serverChild(int pipe_read);

//Stop handler (when "timeToStop")
bool stop_requested = false;
void stop_handler(int sig){
    stop_requested = true;
}

//End handler (when "end")
void end_handler(int sig){
    pid_t id;
    while ((id = waitpid(-1, NULL, WNOHANG)) > 0);
    if (id < 0) stop_requested = true;
}

//Term handler (when parent kills children)
bool child_terminate = false;
void term_handler(){
    child_terminate = true;
}

int main(int argc, char *argv[]){ //server_port,  num_of_children
    //Checking parameters
    if (argc != 3) perror_exit("usage: server_port num_of_children!", ERR_S_PARAM);

    //Creating Pipe for children communication
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) perror_exit("Could not create pipe!", ERR_S_PIPE_CREATE); 

    //Create num_of_children children processes
    int children_number = atoi(argv[2]);
    for (int i = 0; i < children_number; i++){
        pid_t pid;
        switch (pid = fork()) {
        case 0:
            close(pipe_fd[1]);
            serverChild(pipe_fd[0]); //pipe_fd[0] -> pipe_read
            close(pipe_fd[0]);
            exit(0);
        case -1:
            fprintf(stderr, "Could not initiate child server!\n");
            continue;
        }
    }

    //Begin the server
    close(pipe_fd[0]);
    serverParent(pipe_fd[1], atoi(argv[1])); //pipe_fd[1] -> pipe_write
    close(pipe_fd[1]);

    //Waiting for children to terminate
    while(wait(NULL) >= 0);
    fprintf(stderr, "[Parent %d] : Children Exited. Killing Parent\n", getpid());
    return 0;
}

/**
 * This is the child process of the server. It reads the instruction from the pipe,
 * parses it, executes it and sends an answer to its client.
 */ 
void serverChild(int pipe_read){
    printf("[Child %d] : Server child created. Pipe read: %d\n", getpid(), pipe_read);

    //Assign handler for SIGTERM, SIGPIPE signal
    struct sigaction term = {.sa_handler = term_handler, .sa_flags = 0};
    sigaction(SIGTERM, &term, NULL);
    signal(SIGPIPE, SIG_IGN);
    printf("[Child %d] : Assigned signal handlers\n", getpid());

    //Reading from pipe and responding to client
    struct pipe_info pi;
    while(1){ //Child server always running
        //Checks if termination requested from parent
        if (child_terminate){
            printf("[Child %d] : Child terminating (end received)\n", getpid());
            break;
        }

        //Reads pipe packet
        int r = read(pipe_read, &pi, sizeof(pi));
        if (r < sizeof(pi)){
            printf("[Child %d] : Pipe closed, terminating\n", getpid());
            break;
        }
        printf("[Child %d] : ===== RECEIVED PACKET FROM PIPE (INSTRUCTION %d OF CLIENT %s:%d) =====\n\"%s\"\n[Child %d] : ==========================================================================\n", getpid(), pi.si.order, inet_ntoa(pi.client_address), pi.si.receive_port, pi.si.data, getpid());
        
        //Essential Variables for parsing
        char original_data[TRANSFERRED_CMD_LEN];
        char temp_instruction[TRANSFERRED_CMD_LEN];
        char processed_data[TRANSFERRED_CMD_LEN];
        int start_index = 0, end_index = 0, last_pipe_index = 0;
        strncpy(original_data, pi.si.data, sizeof(pi.si.data));
        memset(temp_instruction, 0, sizeof(temp_instruction)); //Clearing char arrays
        memset(processed_data, 0, sizeof(processed_data)); //Clearing char arrays

        //Parsing instructions
        while (1) { //Loops and examine each instruction seperately (ls /etc/ | rm -i) -> (ls /etc/) and (rm -i) 
            while(isspace(original_data[start_index])) start_index++; //Trimming leading space
            end_index = start_index;
            while(original_data[end_index] != ';' && original_data[end_index] != '|' && original_data[end_index] != '\0') end_index++;
            strncpy(temp_instruction, original_data + start_index, end_index - start_index + 1); //copy to temp with offset and length

            //Examining if temp instruction is terminating instruction 
            if (strcmp(temp_instruction, END_INSTR) == 0){ // (end)
                child_terminate = true;
                break;
            } else if (strcmp(temp_instruction, STOP_INSTR) == 0) // (timeToStop)
                kill(getppid(), SIGUSR1); //send signal to parent
            
            //Examining if temp instruction is supported instruction 
            if (instr_valid(temp_instruction))
                strncat(processed_data,temp_instruction, end_index - start_index + 1); //copies temp_instruction to processed_data
            else {
                processed_data[last_pipe_index] = '\0'; //removes pipe from string if it's been added by the previous instruction
                break;
            }
            
            //Examining the cause of the end index
            if (original_data[end_index] == INSTR_SEMICOLON || original_data[end_index] == INSTR_END)
                break;
            else if (original_data[end_index] == INSTR_PIPE){   //space needed after pipe
                last_pipe_index = end_index; //in case the next instruction is invalid
                start_index = end_index + 1;
                continue;
            }

        }
        processed_data[end_index] = '\0';
        printf("[Child %d] : Parsed instruction: (\"%s\"->\"%s\")\n", getpid(), original_data, processed_data);

        //Gathering Client info
        struct sockaddr_in client_info;
        client_info.sin_addr = pi.client_address;
        client_info.sin_family = AF_INET;
        client_info.sin_port = htons(pi.si.receive_port);
        printf("[Child %d] : Found client %s:%d \n", getpid(), inet_ntoa(client_info.sin_addr), client_info.sin_port);

        //Connecting to client
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) perror_exit("Socket Creation Error!", ERR_SC_SOCK_CREATE);
        if (connect(s, (struct sockaddr*)&client_info, sizeof(client_info)) < 0) 
            fprintf(stderr, "Could not connect to client %s:%d", inet_ntoa(pi.client_address), pi.si.receive_port);
        printf("[Child %d] : Connected to client %s:%d \n", getpid(), inet_ntoa(client_info.sin_addr), client_info.sin_port);

        //Executing command and sending udp packet
        FILE *file = popen(processed_data, "r");
        printf("[Child %d] : Executing instruction and exporting to file\n", getpid());
        int counter =  0;
        while (1){
            //Creating UDP packet
            struct receive_info ri;
            memset(ri.data, 0, sizeof(ri.data));
            int fr = fread(ri.data, sizeof(char), sizeof(ri.data), file);
            ri.order = pi.si.order;
            ri.sub_order = counter++;
            ri.sub_last = (fr < sizeof(ri.data));

            //Sending UDP packet
            sendto(s, &ri, sizeof(struct receive_info), MSG_CONFIRM, (struct sockaddr*)NULL, sizeof(client_info));
            printf("[Child %d] : ===== SENT UDP PACKET %d (PART %d) =====\n\"%s\"\n[Child %d] : ========================================\n", getpid(), ri.order, ri.sub_order, ri.data, getpid());
            usleep(20000); //small delay helps to avoid packet loss
            
            //Terminating condition
            if (child_terminate){
                printf("[Child %d] : End requested. Leaving this cruel world\n", getpid());
                break;
            } else if (ri.sub_last) break;
        }
        //Closing fds and terminating
        fprintf(stderr, "[Child %d] : Terminating connection with client %s:%d \n", getpid(), inet_ntoa(client_info.sin_addr), ntohs(client_info.sin_port));
        pclose(file);
        close(s);
    }
}

/**
 * This is the parent process. It receives the TCP packet containing the client's instruction,
 * checks if length is valid, then proceeds to transfer it to the pipe. The first available child
 * will read from pipe.
 */ 
void serverParent(int pipe_write, int serverPort) {
    printf("[Parent %d] : Server parent created on port %d. Pipe write: %d.\n", getpid(), serverPort, pipe_write);

    //Assign handlers for SIGEND, SIGSTOP, SIG_CHLD, SIGPIPE signals
    struct sigaction end = {.sa_handler = end_handler};
    struct sigaction stop = {.sa_handler = stop_handler};
    sigaction(SIGUSR1, &stop, NULL);
    sigaction(SIGCHLD, &end, NULL);
    signal(SIGPIPE, SIG_IGN);

    //Creating reusable TCP socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) perror_exit("Socket Creation Error!", ERR_SP_SOCK_CREATE);
    printf("[Parent %d] : Server parent TCP socket created on port %d.\n", getpid(), serverPort);
    int reuse = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        fprintf(stderr, "setsockopt failed\n");
        return;
    }

    //Acquiring server info
    struct sockaddr_in server_info;
    server_info.sin_family = AF_INET;
    server_info.sin_addr.s_addr = INADDR_ANY; //receive from any address
    server_info.sin_port = htons(serverPort);
    
    //Binding socket
    int b = bind(s, (const struct sockaddr *) (&server_info), sizeof(server_info));
    if (b < 0) perror_exit("Socket Bind Error!", ERR_SP_SOCK_BIND);
    printf("[Parent %d] : Parent server bound TCP socket!\n", getpid());

    //Listening for connections
    int l = listen(s, SOMAXCONN);
    if (l < 0) perror_exit("Socket Listen Error!", ERR_SP_SOCK_LISTEN);
    printf("[Parent %d] : Parent server listening in TCP socket!\n", getpid());

    //Accept connections and transfer data
    struct in_addr client_addresses[FD_SETSIZE]; //Keep track of client addresses
    fd_set active_set, read_set;
    FD_ZERO(&active_set); //initializing set of active sockets
    FD_SET(s, &active_set); //initializing set of active sockets

    while(1) {
        //Checks if (timeToStop)
        if (stop_requested) break;
        
        read_set = active_set;
        int sel = select(FD_SETSIZE, &read_set, NULL, NULL, NULL);
        if (sel <= 0) continue;
        
        printf("[Parent %d] : %d file descriptors are ready.\n" , getpid(), sel);
        for (int i = 0; i < FD_SETSIZE; i++){
            if (FD_ISSET(i, &read_set)){
                if (i == s){ //Branch when new connection shows up
                    struct sockaddr_in client_info;
                    socklen_t size = sizeof(client_info);
                    int a = accept(s, (struct sockaddr *) &client_info, &size);
                    if (a < 0) {
                        fprintf(stderr, "Could not accept connection!\n");
                        continue;
                    } else printf("[Parent %d] : Server parent accepted new connection from client %s:%ud\n", getpid(), inet_ntoa(client_info.sin_addr), ntohs(client_info.sin_port));
                    assert(a <= FD_SETSIZE);
                    FD_SET(a, &active_set);
                    client_addresses[a] = client_info.sin_addr; //updaes ip table
                } else{ //Branch for existing connection
                    struct send_info si; //receiving data
                    struct pipe_info pi; //data to be transfered through pipe
                    
                    //Reading received data from client
                    int r = read(i, (void *) &si, sizeof(si)); 
                    if (r == 0){
                        printf("[Parent %d] : Server parent terminating connection with client\n", getpid());
                        FD_CLR(i, &active_set);
                        close(i);
                        continue;
                    }
                    printf("[Parent %d] : ===== RECEIVED TCP PACKET (INSTRUCTION %d OF CLIENT %s:%d) =====\n\"%s\"\n[Parent %d] : ================================================================\n", getpid(), si.order, inet_ntoa(client_addresses[i]), si.receive_port, si.data, getpid());

                    //Writing data to pipe
                    memcpy(&pi.si, &si, sizeof(pi.si));
                    pi.client_address = client_addresses[i];
                    if (pi.si.data[ALLOWED_CMD_LEN - 1] != 0) bzero(&pi.si.data, sizeof(pi.si.data)); //data read but ignored (emptying data string)
                    write(pipe_write, &pi, sizeof(pi));
                    printf("[Parent %d] : ===== SENT PACKET TO PIPE (INSTRUCTION %d OF CLIENT %s:%d) =====\n\"%s\"\n[Parent %d] : ================================================================\n", getpid(), pi.si.order, inet_ntoa(pi.client_address), pi.si.receive_port, pi.si.data, getpid());
                }
            }
        }
    }

    //Closing active sets
    for (int i = 0; i < FD_SETSIZE; i++){
        if (FD_ISSET(i, &active_set)) close(i);
    }

    //Kill Children at the end
    printf("[Parent %d] : Killing all children\n", getpid());
    signal(SIGTERM, SIG_IGN);
    kill(0, SIGTERM);
}