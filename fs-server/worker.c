// header progetto
#include <utils.h>
#include <icl_hash.h> // per hashtable
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// worker thread

void *work(void *queue) {
    struct Queue *elem;

    while(1) {
        // prendo mutex sulla coda di richieste
        while() {
            pthread_cond_wait(&
        }
    }
}
