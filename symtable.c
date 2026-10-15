/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   symtable.c - implementace AVL stromu jako tabulka symbolu
 *
 * Autor: xpechja00
 */

#include "symtable.h"
#include <stdlib.h>
#include <string.h>

// uzel
typedef struct node {
    char* key;
    tData data;
    int height;
    struct node* left;
    struct node* right;
} tNode;

// tabulka symbolů
struct tSymTable {
    tNode* root;
};

// vytvori uzel a priradi data podle var/func
static tNode* node_create(const char* key, tData data)
{
    tNode* new_node = malloc(sizeof(tNode));
    if (new_node == NULL)
        return NULL;
    new_node->height = 0;
    new_node->key = malloc(sizeof(char) * (strlen(key) + 1));
    if (new_node->key == NULL) {
        free(new_node);
        return NULL;
    }
    strcpy(new_node->key, key);

    new_node->data = data;
    if (data.kind == SYM_FUNC) {
        if (data.attribute.func.paramCount > 0) {
            new_node->data.attribute.func.params = malloc(sizeof(tTypeInfo) * data.attribute.func.paramCount);
            if (new_node->data.attribute.func.params == NULL) {
                free(new_node->key);
                free(new_node);
                return NULL;
            }

            if (data.attribute.func.params == NULL) {
                free(new_node->key);
                free(new_node->data.attribute.func.params);
                free(new_node);
                return NULL;
            }
            memcpy(new_node->data.attribute.func.params, data.attribute.func.params,
                sizeof(tTypeInfo) * data.attribute.func.paramCount);
        } else {
            new_node->data.attribute.func.params = NULL;
        }
    }

    new_node->left = NULL;
    new_node->right = NULL;
    return new_node;
} // node_create

// inicializace prazdne tabulky
tSymTable* symtable_create(void)
{
    tSymTable* table = malloc(sizeof(tSymTable));
    if (table == NULL)
        return NULL;

    table->root = NULL;
    return table;
}

static int node_height(const tNode* node)
{
    if (node == NULL)
        return -1;
    return node->height;
} // node_height

static int max_int(int a, int b) { return a > b ? a : b; }
static void update_node_height(tNode* node)
{
    if (node != NULL) {
        node->height = max_int(node_height(node->left), node_height(node->right)) + 1;
    }
} // update_node_height

static int node_balance(tNode* node)
{
    if (node == NULL)
        return 0;
    return node_height(node->left) - node_height(node->right);
}

// avl rotace doleva
static tNode* rotate_left(tNode* old_root)
{
    if (old_root == NULL || old_root->right == NULL)
        return old_root;
    tNode* new_root = old_root->right;
    old_root->right = new_root->left;
    new_root->left = old_root;
    update_node_height(old_root);
    update_node_height(new_root);
    return new_root;
} // rotate_left

// avl rotace doprava
static tNode* rotate_right(tNode* old_root)
{
    if (old_root == NULL || old_root->left == NULL)
        return old_root;
    tNode* new_root = old_root->left;
    old_root->left = new_root->right;
    new_root->right = old_root;
    update_node_height(old_root);
    update_node_height(new_root);
    return new_root;
} // rotate_right

static tNode* rebalance(tNode* node)
{
    if (node == NULL)
        return NULL;

    update_node_height(node);
    int balance = node_balance(node);

    if (balance > 1) {
        if (node_balance(node->left) < 0) {
            node->left = rotate_left(node->left);
        }
        return rotate_right(node);
    } else if (balance < -1) {
        if (node_balance(node->right) > 0) {
            node->right = rotate_right(node->right);
        }
        return rotate_left(node);
    }
    return node;
} // rebalance

// vnitrni funkce na praci s ADT
static tData* tree_search(tNode* node, const char* key)
{
    if (node == NULL)
        return NULL;

    int compare = strcmp(key, node->key);

    if (compare == 0) {
        return &node->data;
    } else if (compare < 0) {
        return tree_search(node->left, key);
    }
    return tree_search(node->right, key);
} // tree_search

// funkce pro praci s tabulkou symbolu
tData* symtable_search(tSymTable* table, const char* key)
{
    if (table == NULL || key == NULL)
        return NULL;
    return tree_search(table->root, key);

} // symtable_search

// pomocna rekurzivni funkce na vlozeni noveho prvku
static tNode* tree_insert(tNode* node, const char* key, tData data,
    tInsertResult* status)
{
    tNode* temp;
    if (node == NULL) {
        temp = node_create(key, data);
        if (temp == NULL)
            *status = INSERT_ERROR;
        return temp;
    }

    int compare = strcmp(key, node->key);
    if (compare < 0) {
        node->left = tree_insert(node->left, key, data, status);
    } else if (compare > 0) {
        node->right = tree_insert(node->right, key, data, status);
    } else {
        *status = INSERT_DUPLICATE;
    }
    if (*status == INSERT_OK) {
        return rebalance(node);
    }
    return node;
} // tree_insert

// verejna funkce na vlozeni noveho prvku
// vraci enum kvuli rozliseni chyby s duplikatem a malloc errorem
tInsertResult symtable_insert(tSymTable* table, const char* key, tData data)
{
    if (table == NULL || key == NULL)
        return INSERT_ERROR;

    tInsertResult result = INSERT_OK;
    table->root = tree_insert(table->root, key, data, &result);

    return result;
} // symtable_insert

static void tree_free(tNode* node)
{
    if (node != NULL) {
        tree_free(node->left);
        tree_free(node->right);
        free(node->key);
        if (node->data.kind == SYM_FUNC) {
            free(node->data.attribute.func.params);
        }
        free(node);
    }
} // tree_free

void symtable_free(tSymTable* table)
{
    if (table == NULL)
        return;
    tree_free(table->root);
    free(table);
} // symtable_free
