/** algo per invertire abr */
#include <stdio.h>
#include <stdlib.h>

typedef struct node_t {
	int key;
	struct node_t *left;
	struct node_t *right;
} Node;

void abr_insert(Node **t, const int x);
Node* invert(Node **t);
void print(Node *t);
void free_t(Node **t);

int main(void) {
	int n;
	scanf("%d", &n);
	int i, x;
	// leggo abr di n nodi
	Node *tree = 0;
	for(i = 0; i < n; i++) {
		scanf("%d", &x);
		abr_insert(&tree, x);
	}
	puts("abr:");
	print(tree);
	// inverto abr: scambio ricorsivamente sottoalberi destro e sinistro
	tree = invert(&tree);
	// stampo albero invertito
	puts("abr invertito:");
	print(tree);
	free_t(&tree);
	return 0;
}

void abr_insert(Node **t, const int x) {
    if(*t) {
        if((*t)->key >= x) {
            abr_insert(&(*t)->left, x);
        }
        else {
            abr_insert(&(*t)->right, x);
        }
    }
    else {
        *t = (Node*) malloc(sizeof(Node));
        if(*t) {
        	(*t)->key = x;
        	(*t)->left = NULL;
        	(*t)->right = NULL;
        }
        else {
        	puts("Failed alloc");
        	exit(1);
        }
    }
}
Node* invert(Node **t) {
	if(!(*t)) {
		return NULL;
	}
	else {
		Node *x = invert(&(*t)->left);
		Node *y = invert(&(*t)->right);
		(*t)->left = y;
		(*t)->right = x;
		return *t;
	}
}
void print(Node *t) {
	if(t) {
		print(t->left);
		printf("%d\n", t->key);
		print(t->right);
	}
}
void free_t(Node **t) {
	if(*t) {
		free_t(&(*t)->left);
		free_t(&(*t)->right);
		free(*t);
	}
}


