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

// funzione di cleanup_conf: libera memoria e chiude il file di configurazione
void cleanup_conf(FILE *fp, char *buf); // entrambi gli argomenti possono essere NULL

// Questo file contiene l'implementazione della funzione che effettua il parsing del file
// di configurazione "config.txt" e riempe una struttura contenente tutti i parametri
// del server settati opportunamente
int parse_config(struct serv_params *params, const char *conf_fpath) {
	// Se è stato fornito un path per il file di configurazione lo leggo
	// Altrimenti apro quello di default
	FILE *conf_fp = NULL;
	if(conf_fpath) {
		conf_fp = fopen(conf_fpath, "r");
		if(!conf_fp) {
			// errore nell'apertura del file: tento di aprire il file di configurazione di default
			conf_fp = fopen(CONF_PATH_DFL, "r");
		}
	}
	else {
		conf_fp = fopen(CONF_PATH_DFL, "r");
	}

	if(!conf_fp) {
		// errore nell'apertura del file, settato errno
		return -1;
	}

	// alloco un buffer per la lettura
	char *buf = malloc(BUF_BASESZ * sizeof(char));
	if(!buf) {
		// errore nell'allocazione di memoria, settato errno
		// salvo errno perché potrebbe essere alterato da errori di cleanup_conf
		int errno_saved = errno;
		// chiusura del file di configurazione prima di ritornare
		cleanup_conf(conf_fp, NULL);
		// ripristino errno prima di ritornare
		errno = errno_saved;
		return -1;
	}
	int buf_sz = BUF_BASESZ; // questa variabile manterrà la dimensione del buffer

	// parsing del file formattato secondo come specificato nella relazione
	while(feof(conf_fp) == 0) {
		// leggo nel buffer una riga del file
		if(fgets(buf, buf_sz, conf_fp) == NULL) {
			if(feof(conf_fp) == 0) {
				// errore nella lettura del file di config, serttato errno
				// salvo errno perché potrebbe essere alterato da errori di cleanup_conf
				int errno_saved = errno;
				// libero buffer e chiudo il file prima di ritornare
				cleanup_conf(conf_fp, buf);
				// ripristino errno prima di ritornare
				errno = errno_saved;
				return -1;
			}
			// altrimenti vuol dire che è stato raggiunto EOF, per cui uscirà dal ciclo
			// testando di nuovo la condizione
			break;
		}

		// ho ottenuto la linea: la tokenizzo
		char *stat = NULL;
		char *token = strtok_r(buf, "\t", &stat); // i token sono separati da un tab
		char *value = strtok_r(NULL, "\n", &stat);
		if(value && strcmp(token, TPOOLSIZE) == 0) {
			// converto la stringa in un numero usando una funzione di utilità
			long tpool;
			if(isNumber(value, &tpool) == 0 && tpool > 0) {
				// la conversione ha avuto successo, quindi scrivo il valore del parametro
				params->thread_pool = tpool;
			}
			else {
				// errore di conversione, setto ad un valore di default
				params->thread_pool = TPOOL_DFL;
			}
		}
		else if(value && strcmp(token, MAXMEM) == 0) {
			// converto la stringa in un numero usando una funzione di utilità
			long memsz;
			if(isNumber(value, &memsz) == 0 && memsz > 0) {
				// la conversione ha avuto successo, quindi scrivo il valore del parametro
				params->max_memsz = memsz;
			}
			else {
				// errore di conversione, setto ad un valore di default
				params->max_memsz = MAXMEM_DFL;
			}
		}
		else if(value && strcmp(token, MAXFILES) == 0) {
			// converto la stringa in un numero usando una funzione di utilità
			long fcount;
			if(isNumber(value, &fcount) == 0 && fcount > 0) {
				// la conversione ha avuto successo, quindi scrivo il valore del parametro
				params->max_fcount = fcount;
			}
			else {
				// errore di conversione, setto ad un valore di default
				params->max_fcount = MAXFILES_DFL;
			}
		}
		else if(value && strcmp(token, SOCK_PATH) == 0) {
			// non devo convertire niente, ma devo duplicare la stringa perché verrà sovrascritta
			params->sock_path = strndup(value, strlen(value));
			if(!(params->sock_path)) {
				// errore nella duplicazione della stringa, scrivo il valore di default
				strncpy(params->sock_path, SOCK_PATH_DFL, DFL_PATHLEN);
				// safe, se assumo di aver definito bene le macro
			}
		}
		else if(value && strcmp(token, LOG_PATH) == 0) {
			// non devo convertire niente, ma devo duplicare la stringa perché verrà sovrascritta
			params->log_path = strndup(value, strlen(value));
			if(!(params->log_path)) {
				// errore nella duplicazione della stringa, scrivo il valore di default
				strncpy(params->log_path, LOG_PATH_DFL, DFL_PATHLEN);
				// safe, se assumo di aver definito bene le macro
			}
		}
		else {
			// campo non riconosciuto o valore non valido
			// non ritorno un errore perché voglio continuare a leggere altri eventuali
			// campi validi del file di configurazione
			// TODO: scrittura nel file di log
			;
		}
	}
	// file di configurazione parsato senza errori e i campi sono stati scritti nella struttura

	// chiudo il file e libero il buffer
	cleanup_conf(conf_fp, buf);

	return 0;
}

// funzione di cleanup_conf: libera memoria e chiude il file di configurazione
void cleanup_conf(FILE *fp, char *buf) {
	if(buf) {
		free(buf);
	}
	if(fp && fclose(fp) != 0) {
		// errore nella chiusura del file
		// TODO: scrittura nel file di log
		;
	}
}
