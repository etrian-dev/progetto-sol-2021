// sockets headers
#include <sys/un.h>
#include <sys/socket.h>
// I/O calls
#include <stdio.h>
#include <string.h>

// Funzione usata per inizializzare il socket su cui il server ascolta le richieste di connessione

// creates a socket, binds it to and address (passed as a parameter) and makes it
// listen for connections (up to SOMAXCONN queued)
// If some step fails returns -1, otherwise the socket's file descriptor is returned
int sock_init(const char *addr, const size_t len) {
    // first create a socket and get a fd for it
    int sock_fd = -1;
    // creates a socket to communicate between local processes with a stream type
    // "reliable, two-way, connection-based byte stream" (see man 2 socket)
    if((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        //perror("Cannot create socket");
        return -1;
    }

    // the socket then must be binded to an address. in this case the address is
    // passed in as a string (with its associated lenght)
    // creates a sockaddr struct used by the AF_UNIX socket (see man 7 unix)
    struct sockaddr_un address;
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, addr, len + 1);

    if(bind(sock_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        //printf("Cannot bind socket %d to address \"%s\": %s\n", sock_fd, addr, strerror(errno));
        return -1;
    }

    // now that the socket is binded to some file, make it listen for incoming connections
    if(listen(sock_fd, SOMAXCONN) == -1) {
        //printf("Cannot listen on socket %d: %s\n", sock_fd, strerror(errno));
        return -1;
    }

    // socket created and listening: ready to accept connections
    return sock_fd;
}
