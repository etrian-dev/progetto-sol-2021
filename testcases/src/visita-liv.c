// algortimo di visita per livelli di un albero (binario)
// usa una coda, iterativo
#include <stdio.h>
#include <stdlib.h>

// struct per AB
typedef struct node_t {
	int key;
	struct node_t *left;
	struct node_t *right;
} Node;

// struct per coda
typedef struct elem_t {
	Node* node;
	struct elem_t *next;
} Elem;
typedef Elem* Queue;

// prototipi
void insert_ABR(Node* *t, const int val);
void enqueue(Queue *head, Queue *tail, Node *el);
Node* dequeue(Queue *head, Queue *tail);
void free_q(Queue *head);
void free_t(Node* *t);

int main(void) {
	// legge n nodi e li inserisce in un ABR (per comodita')
	int i, x, n;
	Node* t;
	scanf("%d", &n);
	for(i = 0; i < n; i++) {
		scanf("%d", &x);
		insert_ABR(&t, x);
	}
	// esegue una visita per livelli dalla radice alle foglie
	// algoritmo iterativo che usa una coda nella quale inserisce i nodi al livello k esaminando quelli a k-1
	Queue head = 0, tail = 0;
	Node *elem = 0;
	// inizializzo con la radice
	enqueue(&head, &tail, t);
	// finche' non e' vuota proseguo
	while(head) {
		elem = dequeue(&head, &tail);
		// prima stampa x
		printf("%d\n", elem->key);
		// poi inserisce eventuali figli nella coda
		if(elem->left) {
			enqueue(&head, &tail, elem->left);
		}
		if(elem->right) {
			enqueue(&head, &tail, elem->right);
		}
	}
	// posso liberare coda e albero
	//free_q(&head);
	//free_t(&t);
	return 0;
}

void insert_ABR(Node* *t, const int val) {
	if(!(*t)) {
		// creo nuovo nodo
		*t = (Node*) malloc(sizeof(*t));
		if(*t) {
			(*t)->key = val;
			(*t)->left = NULL;
			(*t)->right = NULL;
		}
		else {
			puts("failed alloc");
			exit(1);
		}
	}
	else {
		if((*t)->key >= val) {
			insert_ABR(&(*t)->left, val);
		}
		else {
			insert_ABR(&(*t)->right, val);
		}
	}
}
void enqueue(Queue *head, Queue *tail, Node *el) {
	// el e' gia allocato, mantengo solo una copia
	Elem *new = (Elem*) malloc(sizeof(new));
	if(!new) {
		puts("failed alloc");
		exit(1);
	}
	else {
		printf("enqueue %d\n", el->key);
		new->node = el;
		new->next = NULL;
		if(*head) {
			// allora collego la tail a new
			(*tail)->next = new;
		}
		else {
			*head = new;
		}
		*tail = new;
	}
}
Node* dequeue(Queue *head, Queue *tail) {
	Node* cp = 0;
	if(*head) {
		printf("dequeue %d\n", (*head)->node->key);
		// prima copio il nodo in testa
		cp = (*head)->node;
		// poi sovrascrivo la head con il suo campo next
		*head = (*head)->next;
		// se tale campo e' NULL allora anche la coda e' vuota
		if(*head == NULL) {
			*tail = NULL;
		}
	}
	// infine ritorno cp
	return cp;
}
void free_q(Queue *head) {
	Elem *e = 0;
	while(*head) {
		e = *head;
		*head = (*head)->next;
		free(e);
	}
}
void free_t(Node* *t) {
	free_t(&(*t)->left);
	free_t(&(*t)->right);
	free(*t);
}
