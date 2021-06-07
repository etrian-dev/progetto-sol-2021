// varie operazioni su binary search tree

// std lib headers
#include <stdio.h>
#include <stdlib.h>

// struttura per albero binario di ricerca
typedef struct node {
	int val;
	struct node* left;
	struct node* right;
} Node;
typedef Node* Tree;

// stats about the tree
void treestats(Tree root);

// inserisce il nodo v in un albero binario di ricerca
// se l'albero dato in input e' vuoto alloca la radice
void insert(Tree *root, const int v);
// cerca nodo con valore v nell'albero
Node* src(Tree root, const int v);
// rimuove nodo con valore n dall'albero
void delete(Tree *root, const int n);
// stampa l'albero con una visita ordinata
void in_order_print(Tree root);
// calcola la dimensione dell'albero in tempo lineare
int dimTree(Tree root);
// calcola l'altezza dell'albero in tempo lineare
int heightTree(Tree root);
// calcola se l'albero e' un ABCB (albero binario completo e bilanciato) in tempo lineare
void is_ABCB(Tree root, int *height, int *sub_ABCB);
// stampa i nodi cardine, ovvero i nodi per cui altezza == profondita'
int cardine(Tree root, const int depth);
// stampa i nodi centrali, ovvero i nodi per cui
// la somma dei valori di quelli tra n e la radice dell'albero (il percorso fatto)
// e' uguale alla dimensione del sottoalbero radicato in n
int centrali(Tree root, const int sum);

// calcola minimo delta bilanciamento per cui albero e' delta-bil (1-bil, 2-bil ...)
void min_delta_bil(Node *tree, int *delta, int *h);

// libera memoria albero
void free_tree(Tree* root);

int max(const int v1, const int v2) {
	if(v2 > v1)
		return v2;
	return v1;
}

int main(void) {
	int i, n, x;
	Tree root = 0;
	/* leggo numero nodi */
	scanf("%d", &n);
	/* leggo elementi che inserisco nell'albero binario di ricerca */
	for(i = 0; i < n; i++) {
		if(scanf("%d", &x)) {
			insert(&root, x);
		}
	}
	treestats(root);
	/* libero memoria */
	free_tree(&root);
	return 0;
}

void treestats(Tree root) {
	puts("TREE STATS:");
	if(root) {
		// prints keys with inorder visit
		puts("in order print");
		printf("< ");
		in_order_print(root);
		puts(">");
		// prints dimension and height
		printf("dim = %d\nheight = %d\n", dimTree(root), heightTree(root));
		// prints if completely balanced
		int abcb, h, mindelta;
		is_ABCB(root, &h, &abcb);
		if(abcb) {
			puts("Is an ABCB");
		}
		else {
			puts("Not an ABCB");
		}
		puts("nodi cardine");
		cardine(root, 0);
		puts("nodi centrali");
		centrali(root, 0);
		min_delta_bil(root, &mindelta, &h);
		printf("minimo delta-bil:\n%d\n", mindelta);
	}
}

/* inserisco in un albero binario di ricerca il valore v in modo ricorsivo */
void insert(Tree* root, const int v) {
	if(*root) {
		// non sono ammessi duplicati, e' un ABR
		if(v < (*root)->val) {
			/* inserisco nel sottoalbero sinistro */
			insert(&(*root)->left, v);
		}
		else if(v > (*root)->val) {
			/* inserisco nel sottoalbero destro */
			insert(&(*root)->right, v);
		}
		// l'albero contiene gia' v, non inserisco
		else {
			printf("%d gia\'presente nell\'albero\n", v);
		}
	}
	// albero vuoto, alloco radice
	else {
		*root = malloc(sizeof(Node));
		if(*root) {
			/* inizializzo radice */
			(*root)->val = v;
			(*root)->left = NULL;
			(*root)->right = NULL;
		}
		else {
			puts("impossibile allocare memoria");
			exit(1);
		}
	}
}

void in_order_print(Tree root) {
	if(root) {
		in_order_print(root->left);
		printf("%d ", root->val);
		in_order_print(root->right);
	}
}

// calcola la dimensione dell'albero radicato in root: O(n)
int dimTree(Tree root) {
	if(root) {
		// ritorno dim sottoalbero sx, dx e aggiungo la radice
		return 1 + dimTree(root->left) + dimTree(root->right);
	}
	// albero vuoto
	return 0;
}

// calcola l'altezza dell'albero ricorsivamente: O(n)
int heightTree(Tree root) {
	if(root) {
		// calcolo altezza sottoalberi sx e dx e ne prendo il max, poi sommo 1 (altezza albero di un nodo)
		return 1 + max(heightTree(root->left), heightTree(root->right));
	}
	// caso base: altezza albero vuoto e' -1
	return -1;
}

// determina se l'albero radicato in root e' un ABCB: O(n)
void is_ABCB(Tree root, int *height, int *sub_ABCB) {
	if(root) {
		// dichiaro flag per stabilire se i sottoalberi sono ABCB
		int l_ABCB, r_ABCB;
		// variabili che conterranno l'altezza dei sottoalberi
		int h_left, h_right;
		// calcolo se ABCB e loro altezza
		is_ABCB(root->left, &h_left, &l_ABCB);
		is_ABCB(root->right, &h_right, &r_ABCB);
		// se sono entrambi ABCB e altezza uguale, allora l'albero e' ABCB
		if(l_ABCB && r_ABCB && (h_left == h_right)) {
			*sub_ABCB = 1;
		}
		else {
			*sub_ABCB = 0;
		}
		// poi calcolo l'altezza dell'albero corrente come 1 + max{h_left, h_right};
		*height = 1 + (h_left > h_right ? h_left : h_right);
	}
	// albero vuoto, e' per definizione ABCB con h = -1;
	else {
		*height = -1;
		*sub_ABCB = 1;
	}
}

int cardine(Tree root, const int depth) {
	if(root) {
		// calcolo altezza del sottalbero radicato in root ed allo stesso tempo i nodi cardine dei sottoalberi
		int hleft = cardine(root->left, depth + 1);
		int hright = cardine(root->right, depth + 1);
		int h = 1 + (hleft > hright ? hleft : hright);
		if(h == depth) {
			printf("%d\n", root->val);
		}
		// ritorno l'altezza
		return h;
	}
	// se sono arrivato al termine dell'albero ho altezza -1 (per def)
	return -1;
}

// stampa i nodi centrali, ovvero i nodi per cui
// la somma dei nodi precedenti e' uguale alla dimensione del sottoalbero
int centrali(Tree root, const int sum) {
	if(root) {
		// calcolo dimensione dei sottoalberi e trovo nodi centrali in essi
		int dim = 1 + centrali(root->left, sum + root->val) + centrali(root->right, sum + root->val);
		// se trovo un nodo per cui vale lo stampo
		if(dim == sum + root->val) {
			printf("%d\n", root->val);
		}
		return dim;
	}
	// se sono arrivato in fondo all'albero ho dimensione 0
	return 0;
}

void min_delta_bil(Node *tree, int *delta, int *h) {
	// albero vuoto, delta 0 e altezza -1
	if(!tree) {
		*delta = 0;
		*h = -1;
	}
	else {
		int deltasx, deltadx, hsx, hdx;
		min_delta_bil(tree->left, &deltasx, &hsx);
		min_delta_bil(tree->right, &deltadx, &hdx);
		// minimo valore per cui albero e' deltabil e' il max tra questi
		*delta = max(abs(hsx - hdx), (deltasx > deltadx ? deltasx : deltadx));
		*h = 1 + (hsx > hdx ? hsx : hdx);
	}
}

// libero memoria occupata dall'albero
void free_tree(Tree* root) {
	if(*root) {
		/* libero sottoalbero sinistro e poi destro, poi la root */
		free_tree(&(*root)->left);
		free_tree(&(*root)->right);
		free(*root);
	}
}
