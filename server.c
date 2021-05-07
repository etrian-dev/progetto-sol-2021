// header progetto
#include <utils.h>
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
	// effettua il parsing del file di configurazione riempiendo i campi della struttura
	struct serv_params run_params;
	memset(&run_params, 0, sizeof(struct serv_params)); // per sicurezza azzero tutto
	if(parse_config(&run_params) == -1) {
		// errore di parsing: lo riporto su standard output ed esco con tale codice di errore
		int errcode = errno;
		perror("Fallito il parsing del file di configurazione");
		return errcode;
	}

	// stampo tutti i parametri del server
	printf("thread pool size: %ld\n", run_params.thread_pool);
	printf("max memory size: %ld\n", run_params.max_memsz);
	printf("max file count: %ld\n", run_params.max_fcount);
	printf("socket path: %s\n", run_params.sock_path);
	printf("log file path: %s\n", run_params.log_path);


	int logfile_fd;
	// parsing completato con successo: apro il file di log
	// in scrittura (append), creandolo se non esiste
	if((logfile_fd = open(run_params.log_path, O_WRONLY|O_CREAT|O_APPEND)) == -1) {
		// errore nell'apertura o nella creazione del file di log. L'errore è riportato
		// su standard output, ma il server continua l'esecuzione
		perror("Impossibile creare o aprire il file di log");
	}


	return 0;
}
