// header client
#include <utils.h>
#include <client.h>
// system call headers
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>


// Funzione main del processo client
int main(int argc, char **argv) {
    // processing degli argomenti riceuti da riga di comando
    // riempe i campi della struttura se ha successo
    struct client_opts options;
    memset(&options, 0, sizeof(struct client_opts)); // azzero per sicurezza
    if(get_client_options(argc, argv, &client_opts) == -1) {
	// errore nel processare la lista di argomenti
    }

    return 0;
}
