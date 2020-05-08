#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h> 
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <assert.h> 
#include <ctype.h>

//Server
#define SERVER_PORT 9002

//Client
#define CONSECUTIVE_MESSAGES 10
#define MAX_LINE_LEN 5000
#define ALLOWED_CMD_LEN 101
#define TRANSFERRED_CMD_LEN (ALLOWED_CMD_LEN + 1)
#define MAX_UDP_LEN 512

//Instruction Data
#define INSTR_SEMICOLON ';'
#define INSTR_PIPE '|'
#define INSTR_END '\0'

char* SUPPORTED_INSTRUCTIONS[] = {"ls", "cat", "cut", "grep", "tr"};
char* END_INSTR = "end";
char* STOP_INSTR = "timeToStop";

//Structure of TCP packets
struct send_info {
    int order;
    int receive_port;
    char data[TRANSFERRED_CMD_LEN];
};

//Structure of UDP packets
struct receive_info {
    int order;
    int sub_order;
    char data[MAX_UDP_LEN - 2 * sizeof(int) - sizeof(bool)];
    bool sub_last;
};

//Structure of Pipe packets
struct pipe_info {
    struct in_addr client_address;
    struct send_info si;
};

//Functions

/**
 * Prints information and exits the program
 */
void perror_exit(const char *message, int exit_code){
    fprintf(stderr, "%s (%d)\n", message, exit_code);
    exit(exit_code);
}

/**
 * Checks if given instruction is contained in the SUPPORTED INSTRUCTIONS array
 */ 
bool instr_valid(char *instr){
    for (int i = 0; i < 5; i++){
        if (strncmp(instr, SUPPORTED_INSTRUCTIONS[i], strlen(SUPPORTED_INSTRUCTIONS[i])) == 0 && (instr[strlen(SUPPORTED_INSTRUCTIONS[i])] == ' ' || instr[strlen(SUPPORTED_INSTRUCTIONS[i])] == '\0' || instr[strlen(SUPPORTED_INSTRUCTIONS[i])] == ';'))
            return true;
    }
    return false;
}