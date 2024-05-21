#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
#define MAXRCVLEN 200
#define STRLEN 200
#define PEER_PORT 27429

char name[STRLEN];   // name used to join the server
char opponent[STRLEN]; // name of the peer (Opponent)
pthread_t tid;      // Thread ID
int yours = 0;       // User's score
int oppos = 0;       // Peer's (Opponent) Score
int serversocket, peersocket, mysocket, consocket; // All socket descriptors
int ingame, turn; // Game state and turn indicator

void *server_thread(void *parm);
void *peer_thread(void *arg);
void play_game(int socket, char *buffer, int playerID);

int main(int argc, char *argv[]) {
    printf("Enter Server's IP: ");
    char server[30]; // To store IP address of server
    scanf("%s", server);

    // Establish connection with the server
    struct sockaddr_in dest;
    serversocket = socket(AF_INET, SOCK_STREAM, 0); // Use a TCP connection
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(server); // Convert IP address to network byte order
    dest.sin_port = htons(SERVER_PORT);

    if (connect(serversocket, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        printf("Connection to server failed\n");
        return -1;
    }
    
    // Start thread to handle communication with the server
    pthread_create(&tid, NULL, server_thread, (void *)&serversocket);

    // Set up peer connection for invitations
    struct sockaddr_in peer, serv;
    socklen_t socksize = sizeof(struct sockaddr_in);
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(PEER_PORT);
    mysocket = socket(AF_INET, SOCK_STREAM, 0); // Use a TCP connection
    bind(mysocket, (struct sockaddr *)&serv, sizeof(serv));
    listen(mysocket, 1); // Only accept one connection
    consocket = accept(mysocket, (struct sockaddr *)&peer, &socksize);

    char buffer[MAXRCVLEN]; // Buffer to hold data exchanged
    int len;

    // Handle peer connections
    while (consocket) {
        pthread_cancel(tid); // Cancel server thread to redirect input to peer
        len = recv(consocket, buffer, MAXRCVLEN, 0); 
        buffer[len] = '\0';
        strcpy(opponent, buffer);
        printf("\n%s has challenged you. Accept challenge? (y/n) ", opponent);
        
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strlen(buffer)-1] = '\0';
        send(consocket, buffer, strlen(buffer), 0);

        if (strcmp(buffer, "y") == 0) {
            yours = 0;
            oppos = 0;
            printf("\nYour Score: %d", yours);
            printf("\nOpponent's Score: %d", oppos);
            ingame = 1;
        } else {
            yours = 0;
            oppos = 0;
            printf("\nYou declined...\n");
            ingame = 0;
        }

        turn = 0;
        int playerID = 2;
        while (ingame) {
            play_game(consocket, buffer, playerID);
        }

        yours = 0;
        oppos = 0;
        opponent[0] = '\0';
        pthread_create(&tid, NULL, server_thread, (void *)&serversocket);
        consocket = accept(mysocket, (struct sockaddr *)&peer, &socksize);
    }

    // Close all sockets before the program exits
    close(mysocket);
    close(consocket);
    close(peersocket);
    close(serversocket);

    return EXIT_SUCCESS;
}

void *server_thread(void *obj) {
    bool checkflag = false;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    char buffer[MAXRCVLEN + 1];
    int len;

    printf("\nUsage: \n\n join {name}\n list\n invite {player}\n leave\n\n");

    while (1) {
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strlen(buffer)-1] = '\0';
        char arg1[STRLEN], arg2[STRLEN];
        int n = sscanf(buffer, "%s %s", arg1, arg2);

        if (strcmp(buffer, "list") == 0) {
            send(serversocket, buffer, strlen(buffer), 0);
            len = recv(serversocket, buffer, MAXRCVLEN, 0);
            buffer[len] = '\0';
            printf("\n%s\n", buffer);
        } else if ((strcmp(arg1, "join") == 0) && n > 1) {
            if (strlen(name) < 1) {
                send(serversocket, buffer, strlen(buffer), 0);
                len = recv(serversocket, buffer, MAXRCVLEN, 0);
                buffer[len] = '\0';
                printf("\n%s\n", buffer);
                strcpy(name, arg2);
            } else {
                printf("\nYou are already joined by the following name: %s\n", name);
            }
        } else if (strcmp(arg1, "invite") == 0 && n > 1) {
            if (strlen(name) < 1) {
                printf("\nYou must join before inviting...\n");
            } else {
                strcpy(opponent, arg2);
                send(serversocket, buffer, strlen(buffer), 0);
                len = recv(serversocket, buffer, MAXRCVLEN, 0);
                buffer[len] = '\0';
                pthread_create(&tid, NULL, peer_thread, buffer);
                pthread_exit(0);
            }
        } else if (strcmp(buffer, "leave") == 0) {
            name[0] = '\0';
            printf("\nGoodbye!\n");
            close(mysocket);
            close(consocket);
            close(peersocket);
            close(serversocket);
            exit(1);
        } else {
            if (!checkflag) {
                checkflag = true;
            } else {
                printf("\nUsage: \n\n join {name}\n list\n invite {player}\n leave\n\n");
            }
        }
    }
}

void *peer_thread(void *ip) {
    char buffer[MAXRCVLEN + 1];
    int len;

    struct sockaddr_in dest;
    peersocket = socket(AF_INET, SOCK_STREAM, 0);
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr((char *)ip);
    dest.sin_port = htons(PEER_PORT);
    printf("\nSending invite...\n");
    connect(peersocket, (struct sockaddr *)&dest, sizeof(dest));

    send(peersocket, name, strlen(name), 0);

    len = recv(peersocket, buffer, MAXRCVLEN, 0);
    buffer[len] = '\0';
    if (strcmp(buffer, "y") == 0) {
        yours = 0;
        oppos = 0;
        printf("\nYour Score: %d", yours);
        printf("\nOpponent's Score: %d", oppos);
        ingame = 1;
    } else {
        yours = 0;
        oppos = 0;
        ingame = 0;
        printf("\n%s declined...\n", opponent);
    }

    turn = 0;
    int playerID = 1;
    while (ingame) {
        play_game(peersocket, buffer, playerID);
    }

    yours = 0;
    oppos = 0;
    opponent[0] = '\0';
    close(peersocket);
    pthread_create(&tid, NULL, server_thread, (void *)&serversocket);
    pthread_exit(0);
}

void play_game(int socket, char *buffer, int playerID) {
    int len, datasocket = socket;

    printf("\nSTARTING GAME\n");

    int i = 0;
    int player = 0;
    int go = 0;
    int row = 0;
    int column = 0;
    int line = 0;
    int winner = 0;
    char board[3][3] = {
        {'1','2','3'},
        {'4','5','6'},
        {'7','8','9'}
    };

    for (i = (0 + turn); i < (9 + turn) && winner == 0; i++) {
        printf("\n\n");
        printf(" %c | %c | %c\n", board[0][0], board[0][1], board[0][2]);
        printf("---+---+---\n");
        printf(" %c | %c | %c\n", board[1][0], board[1][1], board[1][2]);
        printf("---+---+---\n");
        printf(" %c | %c | %c\n", board[2][0], board[2][1], board[2][2]);

        player = i % 2 + 1;

        if (player == playerID) {
            printf("Player %d, enter a number: ", player);
            scanf("%d", &go);

            row = --go/3;
            column = go%3;

            if (board[row][column] != 'X' && board[row][column] != 'O') {
                if (player == 1) {
                    board[row][column] = 'X';
                } else {
                    board[row][column] = 'O';
                }
            } else {
                printf("Invalid move\n");
                i--;
            }
            sprintf(buffer, "%d", go);
            send(datasocket, buffer, strlen(buffer), 0);
        } else {
            printf("Waiting for opponent's move...\n");
            len = recv(datasocket, buffer, MAXRCVLEN, 0);
            buffer[len] = '\0';
            go = atoi(buffer);
            row = --go/3;
            column = go%3;
            if (board[row][column] != 'X' && board[row][column] != 'O') {
                if (player == 1) {
                    board[row][column] = 'X';
                } else {
                    board[row][column] = 'O';
                }
            }
        }

        for (line = 0; line <= 2; line++) {
            if ((board[line][0] == board[line][1] && board[line][1] == board[line][2]) ||
                (board[0][line] == board[1][line] && board[1][line] == board[2][line])) {
                winner = player;
            }
        }

        if ((board[0][0] == board[1][1] && board[1][1] == board[2][2]) ||
            (board[0][2] == board[1][1] && board[1][1] == board[2][0])) {
            winner = player;
        }
    }

    printf("\n\n");
    printf(" %c | %c | %c\n", board[0][0], board[0][1], board[0][2]);
    printf("---+---+---\n");
    printf(" %c | %c | %c\n", board[1][0], board[1][1], board[1][2]);
    printf("---+---+---\n");
    printf(" %c | %c | %c\n", board[2][0], board[2][1], board[2][2]);

    if (winner == 0) {
        printf("It's a draw\n");
    } else {
        printf("Player %d is the winner\n", winner);
        if (winner == playerID) {
            yours++;
        } else {
            oppos++;
        }
    }

    printf("\nYour Score: %d", yours);
    printf("\nOpponent's Score: %d", oppos);

    // Reset the game state
    ingame = 0;
    turn = (turn + 1) % 2;  // Alternate who starts the game
}

