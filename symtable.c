#include <stdio.h>
#include "symtable.h"
#include <string.h>
#include <stdlib.h>

// uzel
typedef struct node {
    char *key;           
    tData data;          
    int height;          
    struct node *left;   
    struct node *right;  
} tNode;

// tabulka symbolů
struct tSymTable{
    tNode *root;   
};

// vytvori uzel a priradi data podle var/func 
static tNode *node_create(const char *key, tData data){
    tNode *new_node = malloc(sizeof(tNode));
    if(new_node == NULL) return NULL;
    new_node->height = 0;
    new_node->key = malloc(sizeof(char)*(strlen(key)+1));
    if(new_node->key == NULL){
        free(new_node);
        return NULL;
    }
    strcpy(new_node->key, key);
    
    new_node->data = data;
    if(data.kind == SYM_FUNC){
        if(data.attribute.func.paramCount > 0){
            new_node->data.attribute.func.params = malloc(sizeof(tTypeInfo)*data.attribute.func.paramCount);
            if(new_node->data.attribute.func.params == NULL){
                free(new_node->key);
                free(new_node);
                return NULL;
            }

            if(data.attribute.func.params == NULL){
                free(new_node->key);
                free(new_node->data.attribute.func.params);
                free(new_node);
                return NULL;
            }
            memcpy(new_node->data.attribute.func.params, data.attribute.func.params, sizeof(tTypeInfo)*data.attribute.func.paramCount);
        } else {
            new_node->data.attribute.func.params = NULL;
        }
    }

    new_node->left = NULL;
    new_node->right = NULL;
    return new_node;
} //node_create

// inicializace prazdne tabulky
tSymTable *symtable_create(void){
    tSymTable *table = malloc(sizeof(tSymTable));
    if(table == NULL) return NULL;

    table->root = NULL;
    return table;
}

// funkce pro praci s tabulkou symbolu
tData *symtable_search(tSymTable *table, const char *key){
    if(table == NULL || key == NULL) return NULL;
    return tree_search(table->root, key);

}

static int node_height(const tNode *node){
    if(node == NULL) return -1;
    return node->height;
} //node_height

static int max_int(int a, int b){ 
    return a > b ? a : b; 
}
static void update_node_height(tNode *node){
    if(node != NULL){
        node->height = max_int(node_height(node->left), node_height(node->right)) + 1;  
    }
}

static int node_balance(tNode *node){
    if(node == NULL) return 0;
    return node_height(node->left) - node_height(node->right);
}

// avl rotace doleva
static tNode *rotate_left(tNode *old_root){
    if(old_root == NULL || old_root->right == NULL) return old_root;
    tNode *new_root = old_root->right;
    old_root->right = new_root->left;
    new_root->left = old_root;
    update_node_height(old_root);
    update_node_height(new_root);
    return new_root;
}

// avl rotace doprava
static tNode *rotate_right(tNode *old_root){
    if(old_root == NULL || old_root->left == NULL) return old_root;
    tNode *new_root = old_root->left;
    old_root->left = new_root->right;
    new_root->right = old_root;
    update_node_height(old_root);
    update_node_height(new_root);
    return new_root;
}


static rebalance(tNode *node){

}

// vnitrni funkce na praci s ADT 
tData *tree_search(tNode *node, const char *key){
    if(node == NULL) return NULL;

    int compare = strcmp(key, node->key);
    tData *temp;

    if(compare == 0){
        return &node->data;
    } else if(compare < 0){
        temp = tree_search(node->left, key);
    } else {
        temp = tree_search(node->right, key);
    }

    if(temp == NULL) return NULL;
    return temp;
} //tree_search

bool symtable_insert(tSymTable *table, const char *key, tData data){
    if(table == NULL || key == NULL) return false;
    if(symtable_search(table, key) != NULL) return false;

    tNode *new_node = node_create();
    if(new_node == NULL) return false;
    new_node->data = data;

    new_node->key = malloc(sizeof(char) * strlen(key) + 1);
    if(new_node->key == NULL) return false;
    strcpy(new_node->key, key);

    
} //symtable_insert


