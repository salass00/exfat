/**
 * Copyright (c) 2014-2016 Fredrik Wikstrom <fredrik@a500.org>
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

#include "splay.h"

static void left_rotate(struct Splay **root, struct Splay *sn) {
	struct Splay *s2 = sn->right;

	if (s2 != NULL) {
		sn->right = s2->left;
		if (s2->left != NULL)
			s2->left->parent = sn;
		s2->parent = sn->parent;
	}

	if (sn->parent == NULL)
		*root = s2;
	else if (sn == sn->parent->left)
		sn->parent->left = s2;
	else
		sn->parent->right = s2;

	if (s2 != NULL)
		s2->left = sn;

	sn->parent = s2;
}

static void right_rotate(struct Splay **root, struct Splay *sn) {
	struct Splay *s2 = sn->left;

	if (s2 != NULL) {
		sn->left = s2->right;
		if (s2->right != NULL)
			s2->right->parent = sn;
		s2->parent = sn->parent;
	}

	if (sn->parent == NULL)
		*root = s2;
	else if (sn == sn->parent->left)
		sn->parent->left = s2;
	else
		sn->parent->right = s2;

	if (s2 != NULL)
		s2->right = sn;

	sn->parent = s2;
}

static void splay(struct Splay **root, struct Splay *sn) {
	while (sn->parent != NULL) {
		if (sn->parent->parent == NULL) {
			if (sn == sn->parent->left)
				right_rotate(root, sn->parent);
			else
				left_rotate(root, sn->parent);
		} else if (sn->parent == sn->parent->parent->left) {
			if (sn == sn->parent->left)
				right_rotate(root, sn->parent->parent);
			else
				left_rotate(root, sn->parent);
			right_rotate(root, sn->parent);
		} else {
			if (sn == sn->parent->left)
				right_rotate(root, sn->parent);
			else
				left_rotate(root, sn->parent->parent);
			left_rotate(root, sn->parent);
		}
	}
}

static struct Splay *find(struct Splay **root, SplayCmpFunc cmpfunc, CONST_APTR key) {
	struct Splay *sn = *root;
	int res;

	while (sn != NULL) {
		res = cmpfunc(key, sn->key);
		if (res > 0)
			sn = sn->right;
		else if (res < 0)
			sn = sn->left;
		else
			break;
	}

	return sn;
}

static void replace(struct Splay **root, struct Splay *sn, struct Splay *s2) {
	if (sn->parent == NULL)
		*root = s2;
	else if (sn == sn->parent->left)
		sn->parent->left = s2;
	else
		sn->parent->right = s2;

	if (s2 != NULL)
		s2->parent = sn->parent;
}

void InsertSplay(struct Splay **root, SplayCmpFunc cmpfunc, struct Splay *sn, CONST_APTR key) {
	struct Splay *s2 = *root;
	struct Splay *parent = NULL;
	int res;

	if (sn == NULL)
		return;

	while (s2 != NULL) {
		parent = s2;
		res = cmpfunc(key, s2->key);
		if (res > 0)
			s2 = s2->right;
		else
			s2 = s2->left;
	}

	sn->parent = parent;
	sn->left   = NULL;
	sn->right  = NULL;
	sn->key    = key;

	if (parent == NULL)
		*root = sn;
	else {
		res = cmpfunc(key, parent->key);
		if (res > 0)
			parent->right = sn;
		else
			parent->left = sn;
	}

	splay(root, sn);
}

struct Splay *FindSplay(struct Splay **root, SplayCmpFunc cmpfunc, CONST_APTR key) {
	struct Splay *sn;

	sn = find(root, cmpfunc, key);
	if (sn != NULL)
		splay(root, sn);

	return sn;
}

struct Splay *FirstSplay(struct Splay **root) {
	struct Splay *sn = *root;

	if (sn == NULL)
		return NULL;

	while (sn->left != NULL) {
		sn = sn->left;
	}

	return sn;
}

struct Splay *LastSplay(struct Splay **root) {
	struct Splay *sn = *root;

	if (sn == NULL)
		return NULL;

	while (sn->right != NULL) {
		sn = sn->right;
	}

	return sn;
}

struct Splay *PrevSplay(struct Splay *sn) {
	if (sn == NULL)
		return NULL;

	if (sn->left != NULL) {
		sn = sn->left;
		while (sn->right != NULL) {
			sn = sn->right;
		}
		return sn;
	} else {
		struct Splay *parent = sn->parent;
		while (parent != NULL && parent->left == sn) {
			sn = parent;
			parent = sn->parent;
		}
		return parent;
	}
}

struct Splay *NextSplay(struct Splay *sn) {
	if (sn == NULL)
		return NULL;

	if (sn->right != NULL) {
		sn = sn->right;
		while (sn->left != NULL) {
			sn = sn->left;
		}
		return sn;
	} else {
		struct Splay *parent = sn->parent;
		while (parent != NULL && parent->right == sn) {
			sn = parent;
			parent = sn->parent;
		}
		return parent;
	}
}

void RemoveSplay(struct Splay **root, struct Splay *sn) {
	if (sn == NULL)
		return;

	splay(root, sn);

	if (sn->left == NULL)
		replace(root, sn, sn->right);
	else if (sn->right == NULL)
		replace(root, sn, sn->left);
	else {
		struct Splay *s2 = sn->right;

		while (s2->left != NULL)
			s2 = s2->left;

		if (sn != s2->parent) {
			replace(root, s2, s2->right);
			s2->right = sn->right;
			s2->right->parent = s2;
		}

		replace(root, sn, s2);
		s2->left = sn->left;
		s2->left->parent = s2;
	}
}

