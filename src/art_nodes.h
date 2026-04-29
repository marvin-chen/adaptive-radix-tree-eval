#ifndef ART_NODES_H
#define ART_NODES_H

#include "art_internal.h"

typedef int (*art_child_cb)(art_node *child, void *data);

art_node* art_alloc_node(uint8_t type);
art_node** art_find_child(art_node *n, unsigned char c);
void art_add_child(art_node *n, art_node **ref, unsigned char c, void *child);
void art_remove_child(art_node *n, art_node **ref, unsigned char c, art_node **slot);
art_node* art_first_child(const art_node *n);
art_node* art_last_child(const art_node *n);
int art_for_each_child(art_node *n, art_child_cb cb, void *data);

art_node** art_node4_find_child(art_node4 *n, unsigned char c);
void art_node4_add_child(art_node4 *n, art_node **ref, unsigned char c, void *child);
void art_node4_remove_child(art_node4 *n, art_node **ref, art_node **slot);
art_node* art_node4_first_child(const art_node4 *n);
art_node* art_node4_last_child(const art_node4 *n);
int art_node4_for_each_child(art_node4 *n, art_child_cb cb, void *data);

art_node** art_node16_find_child(art_node16 *n, unsigned char c);
void art_node16_add_child(art_node16 *n, art_node **ref, unsigned char c, void *child);
void art_node16_remove_child(art_node16 *n, art_node **ref, art_node **slot);
art_node* art_node16_first_child(const art_node16 *n);
art_node* art_node16_last_child(const art_node16 *n);
int art_node16_for_each_child(art_node16 *n, art_child_cb cb, void *data);

art_node** art_node48_find_child(art_node48 *n, unsigned char c);
void art_node48_add_child(art_node48 *n, art_node **ref, unsigned char c, void *child);
void art_node48_remove_child(art_node48 *n, art_node **ref, unsigned char c);
art_node* art_node48_first_child(const art_node48 *n);
art_node* art_node48_last_child(const art_node48 *n);
int art_node48_for_each_child(art_node48 *n, art_child_cb cb, void *data);

art_node** art_node256_find_child(art_node256 *n, unsigned char c);
void art_node256_add_child(art_node256 *n, art_node **ref, unsigned char c, void *child);
void art_node256_remove_child(art_node256 *n, art_node **ref, unsigned char c);
art_node* art_node256_first_child(const art_node256 *n);
art_node* art_node256_last_child(const art_node256 *n);
int art_node256_for_each_child(art_node256 *n, art_child_cb cb, void *data);

#endif
