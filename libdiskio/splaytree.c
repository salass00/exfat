/**
 * Copyright (c) 2015 Fredrik Wikstrom <fredrik@a500.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "splaytree.h"
#include <proto/exec.h>
#include <proto/utility.h>

struct SplayNode {
	struct SplayNode *parent;
	struct SplayNode *left;
	struct SplayNode *right;
	APTR key;
	APTR data;
};

struct SplayTree {
	APTR mempool;
	struct Hook *hook;
	struct SplayNode *root;
};

struct SplayTree *CreateSplayTree(struct Hook *hook) {
	struct SplayTree *tree;

	tree = AllocMem(sizeof(*tree), MEMF_PUBLIC);
	if (tree == NULL)
		return NULL;

	tree->mempool = CreatePool(MEMF_PUBLIC, 4096, 1024);
	if (tree->mempool == NULL) {
		FreeMem(tree, sizeof(*tree));
	}

	tree->hook = hook;
	tree->root = NULL;

	return tree;
}

void DeleteSplayTree(struct SplayTree *tree) {
	if (tree != NULL) {
		DeletePool(tree->mempool);
		FreeMem(tree, sizeof(*tree));
	}
}

static void left_rotate(struct SplayTree *tree, struct SplayNode *sn) {
	struct SplayNode *s2 = sn->right;

	if (s2 != NULL) {
		sn->right = s2->left;
		if (s2->left != NULL)
			s2->left->parent = sn;
		s2->parent = sn->parent;
	}

	if (sn->parent == NULL)
		tree->root = s2;
	else if (sn == sn->parent->left)
		sn->parent->left = s2;
	else
		sn->parent->right = s2;

	if (s2 != NULL)
		s2->left = sn;

	sn->parent = s2;
}

static void right_rotate(struct SplayTree *tree, struct SplayNode *sn) {
	struct SplayNode *s2 = sn->left;

	if (s2 != NULL) {
		sn->left = s2->right;
		if (s2->right != NULL)
			s2->right->parent = sn;
		s2->parent = sn->parent;
	}

	if (sn->parent == NULL)
		tree->root = s2;
	else if (sn == sn->parent->left)
		sn->parent->left = s2;
	else
		sn->parent->right = s2;

	if (s2 != NULL)
		s2->right = sn;

	sn->parent = s2;
}

static void splay(struct SplayTree *tree, struct SplayNode *sn) {
	while (sn->parent != NULL) {
		if (sn->parent->parent == NULL) {
			if (sn == sn->parent->left)
				right_rotate(tree, sn->parent);
			else
				left_rotate(tree, sn->parent);
		} else if (sn->parent == sn->parent->parent->left) {
			if (sn == sn->parent->left)
				right_rotate(tree, sn->parent->parent);
			else
				left_rotate(tree, sn->parent);
			right_rotate(tree, sn->parent);
		} else {
			if (sn == sn->parent->left)
				right_rotate(tree, sn->parent);
			else
				left_rotate(tree, sn->parent->parent);
			left_rotate(tree, sn->parent);
		}
	}
}

static struct SplayNode *find(struct SplayTree *tree, APTR key) {
	struct SplayNode *sn = tree->root;
	int res;

	while (sn != NULL) {
		res = CallHookPkt(tree->hook, key, sn->key);
		if (res > 0)
			sn = sn->right;
		else if (res < 0)
			sn = sn->left;
		else
			break;
	}

	return sn;
}

static void replace(struct SplayTree *tree, struct SplayNode *sn, struct SplayNode *s2) {
	if (sn->parent == NULL)
		tree->root = s2;
	else if (sn == sn->parent->left)
		sn->parent->left = s2;
	else
		sn->parent->right = s2;

	if (s2 != NULL)
		s2->parent = sn->parent;
}

BOOL InsertSplayNode(struct SplayTree *tree, APTR key, APTR data) {
	struct SplayNode *sn = tree->root;
	struct SplayNode *parent = NULL;
	int res;

	if (data == NULL)
		return FALSE;

	while (sn != NULL) {
		parent = sn;
		res = CallHookPkt(tree->hook, key, sn->key);
		if (res > 0)
			sn = sn->right;
		else
			sn = sn->left;
	}

	sn = AllocPooled(tree->mempool, sizeof(*sn));
	if (sn == NULL)
		return FALSE;

	sn->parent = parent;
	sn->left = NULL;
	sn->right = NULL;
	sn->key = key;
	sn->data = data;

	if (parent == NULL)
		tree->root = sn;
	else {
		res = CallHookPkt(tree->hook, sn->key, parent->key);
		if (res > 0)
			parent->right = sn;
		else
			parent->left = sn;
	}

	splay(tree, sn);

	return TRUE;
}

APTR FindSplayNode(struct SplayTree *tree, APTR key) {
	struct SplayNode *sn;

	sn = find(tree, key);
	if (sn == NULL)
		return NULL;

	splay(tree, sn);

	return sn->data;
}

BOOL RemoveSplayNode(struct SplayTree *tree, APTR key) {
	struct SplayNode *sn;

	sn = find(tree, key);
	if (sn == NULL)
		return FALSE;

	splay(tree, sn);

	if (sn->left == NULL)
		replace(tree, sn, sn->right);
	else if (sn->right == NULL)
		replace(tree, sn, sn->left);
	else {
		struct SplayNode *s2 = sn->right;

		while (s2->left != NULL)
			s2 = s2->left;

		if (sn != s2->parent) {
			replace(tree, s2, s2->right);
			s2->right = sn->right;
			s2->right->parent = s2;
		}

		replace(tree, sn, s2);
		s2->left = sn->left;
		s2->left->parent = s2;
	}

	FreePooled(tree->mempool, sn, sizeof(*sn));

	return TRUE;
}

