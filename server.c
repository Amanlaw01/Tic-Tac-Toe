#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>
#include "strmap.h" // Make sure to include the header file for the hashmap

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define PROTOPORT 27428 // default protocol port number
#define QLEN 10 // size of request queue
#define MAXRCVLEN 200 // Maximum size of buffer
#define STRLEN 200 // Maximum String length

sqlite3 *db;
int size = 0;

typedef struct {
    int socket;
    int game_id;
    char username[50];
} client_t;

client_t *clients[MAX_CLIENTS];
StrMap *players; // HashMap<Player's name, IP address>
pthread_mutex_t mut; // Mutex to prevent race conditions.

void *handle_client(void *arg);
void broadcast_message(int game_id, const char *message, const char *sender);
void *serverthread(void *parm); // Thread function to handle communication with clients
void getPlayers(const void *key, const void *value, void *obj); // Callback for hashmap enumerator

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_mutex_init(&mut, NULL);
    players = sm_new(100); // Initialize hashmap

    // Initialize SQLite database
    if (sqlite3_open("tictactoe.db", &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, QLEN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server started. Waiting for connections...\n");

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        printf("New connection...\n");

        pthread_t thread;
        client_t *client = malloc(sizeof(client_t));
        client->socket = new_socket;

        pthread_create(&thread, NULL, handle_client, (void *)client);
    }

    sqlite3_close(db);
    return 0;
}

void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    int n;

    // Receive username
    if ((n = recv(client->socket, buffer, BUFFER_SIZE, 0)) <= 0) {
        close(client->socket);
        free(client);
        pthread_exit(NULL);
    }
    buffer[n] = '\0';
    strcpy(client->username, buffer);

    // Game and chat loop
    while ((n = recv(client->socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[n] = '\0';
        printf("Message from %s: %s\n", client->username, buffer);
        broadcast_message(client->game_id, buffer, client->username);
    }

    close(client->socket);
    free(client);
    pthread_exit(NULL);
}

void broadcast_message(int game_id, const char *message, const char *sender) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->game_id == game_id) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "%s: %s", sender, message);
            send(clients[i]->socket, buffer, strlen(buffer), 0);
        }
    }
}

void *serverthread(void *parm) {
    int tsd, len;
    tsd = (int)parm; // Thread Socket descriptor
    
    // Get client's IP address
    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in peeraddr;
    socklen_t peeraddrlen = sizeof(peeraddr);
    getpeername(tsd, &peeraddr, &peeraddrlen); // Retrieves address of the peer to which a socket is connected
    inet_ntop(AF_INET, &(peeraddr.sin_addr), ip, INET_ADDRSTRLEN); // Binary to text string

    char buf[MAXRCVLEN + 1]; // buffer for data exchange
    char name[STRLEN + 1]; // Variable to store current client's name

    // Run loop until client closes connection.
    while (len = recv(tsd, buf, MAXRCVLEN, 0)) {
        // Split received query into two arguments
        buf[len] = '\0';
        char arg1[STRLEN], arg2[STRLEN];
        int n = sscanf(buf, "%s %s", arg1, arg2);

        // Handle 'join' query
        if (strcmp(arg1, "join") == 0 && arg2 != NULL) {
            // Put the player in hashmap if the player doesn't already exist.
            pthread_mutex_lock(&mut);
            if (sm_exists(players, arg2) == 0) {
                sm_put(players, arg2, ip);
                size++;
                strcpy(name, arg2);
                sprintf(buf, "Player %s added to the player's list\n", arg2);
                printf("Player %s added to the player's list\n", arg2);
            } else {
                sprintf(buf, "Player %s is already in the player's list\n", arg2);
            }
            pthread_mutex_unlock(&mut);
            send(tsd, buf, strlen(buf), 0);
        }
        // Handle 'invite {playername}' query.
        else if (strcmp(arg1, "invite") == 0 && arg2 != NULL) {
            // If invited player exists then send player's IP address.
            pthread_mutex_lock(&mut);
            if ((size > 0) && (sm_exists(players, arg2) == 1)) {
                sm_get(players, arg2, buf, sizeof(buf));
            } else {
                sprintf(buf, "Player %s not found\n", arg2);
            }
            pthread_mutex_unlock(&mut);
            send(tsd, buf, strlen(buf), 0);
        }
        // Handle 'list' query.
        else if (strcmp(arg1, "list") == 0) {
            // Iterate over the hashmap and form a list of players to send.
            pthread_mutex_lock(&mut);
            if (size > 0) {
                sprintf(buf, "Players: ");
                sm_enum(players, getPlayers, buf);
            } else {
                sprintf(buf, "There are no players online\n", arg2);
            }
            pthread_mutex_unlock(&mut);
            send(tsd, buf, strlen(buf), 0);
        }
    }

    // If client closes connection then remove the entry from the hashmap and
    // close the socket and thread.
    pthread_mutex_lock(&mut);
    if (name != NULL && strlen(name) > 1) {
        sm_get(players, name, buf, sizeof(buf));
        if (strcmp(buf, ip) == 0) {
            // Important note: Player is not really removed from hashmap,
            // only the value is removed while the key is kept. This is
            // a workaround function as the original hashmap implementation
            // did not come with a remove function and I did not have
            // time to implement it myself.
            sm_remove(players, name);
            size--;
            printf("Player %s removed from the player's list\n", name);
            name[0] = '\0';
        }
    }
    pthread_mutex_unlock(&mut);
    close(tsd);
    pthread_exit(0);
}

void getPlayers(const void *key, const void *value, void *obj) {
    if (value != NULL) {
        strcat(obj, key);
        strcat(obj, ", ");
    }
}
