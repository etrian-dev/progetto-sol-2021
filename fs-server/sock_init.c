/**
 * \file sock_init.c
 * \brief File contenente l'implementazione della funzione che crea un socket
 * su cui il server ascolta le richieste di connessione
 */

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
/**
 * \brief La funzione crea un socket, legandolo all'indirizzo dato
 *
 * La funzione crea un socket di tipo AF_UNIX (locale). Successivamente viene effettuato
 * il binding all'indirizzo addr fornito e si mette in ascolto di richieste di connessione
 * su di esso da parte dei client. I client che vogliono connettersi devono effettuare
 * la syscall connect (man 2 connect) dando come path quello indicato in addr
 * \param [in] addr Path del file a cui il socket viene legato
 * \return Ritorna il descrittore del socket creato se ha successo (> 0), -1 altrimenti
 */
int sock_init(const char *addr) {
    if(!addr) {
        return -1;
    }
    size_t len = strlen(addr);
    // first create a socket and get a fd for it
    int sock_fd = -1;
    // creates a socket to communicate between local processes with a stream type
    // "reliable, two-way, connection-based byte stream" (see man 2 socket)
    if((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        return -1;
    }
    // the socket then must be binded to an address. in this case the address is
    // passed in as a string (with its associated lenght)
    // creates a sockaddr struct used by the AF_UNIX socket (see man 7 unix)
    struct sockaddr_un address;
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, addr, len + 1);
    if(bind(sock_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        return -1;
    }
    // now that the socket is binded to some file, make it listen for incoming connections
    if(listen(sock_fd, SOMAXCONN) == -1) {
        return -1;
    }

    // socket created and listening: ready to accept connections
    return sock_fd;
}
