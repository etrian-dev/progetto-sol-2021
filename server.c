// header progetto
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
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

// TODO: add brief description of the function
int main(int argc, char **argv) {
	// effettua il parsing del file di configurazione
	parse_config(CONFIG_FILE);
	return 0;
}
