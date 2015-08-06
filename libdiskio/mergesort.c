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

#include "diskio_internal.h"

#define OneInMinList(list) ((list)->mlh_Head == (list)->mlh_TailPred)

typedef int (*CompareNodesFunc)(struct MinNode *, struct MinNode *);

static int CountNodes(struct MinNode *head) {
	int count = 0;
	while (head != NULL) {
		count++;
		head = head->mln_Succ;
	}
	return count;
}

static struct MinNode *MergeLists(struct MinNode *head, int size,
	struct MinNode **headRef, struct MinNode **tailRef,
	CompareNodesFunc cmpfunc)
{
	struct MinNode *head2;
	struct MinNode *tail;
	struct MinNode *node;
	struct MinNode *node2;
	struct MinNode *pred;
	int count;

	*tailRef = NULL;
	*headRef = NULL;

	pred = NULL;
	node = head;
	count = 0;
	while (node != NULL && count < size) {
		pred = node;
		count++;
		node = node->mln_Succ;
	}

	if (node == NULL) {
		*tailRef = pred;
		return head;
	}

	pred->mln_Succ = NULL;
	head2 = node;
	count = 0;
	while (node != NULL && count < size) {
		pred = node;
		count++;
		node = node->mln_Succ;
	}

	pred->mln_Succ = NULL;
	*headRef = node;

	if (head == NULL && head2 == NULL) {
		return NULL;
	} else if (head == NULL || head2 == NULL) {
		if (head2 != NULL)
			head = head2;
		tail = head;
		while (tail->mln_Succ != NULL) tail = tail->mln_Succ;
		*tailRef = tail;
		return head;
	}

	node = head;
	node2 = head2;
	head = NULL;
	while (node != NULL && node2 != NULL) {
		if (cmpfunc(node, node2) <= 0) {
			if (head == NULL)
				head = tail = node;
			else
				tail = tail->mln_Succ = node;
			node = node->mln_Succ;
		} else {
			if (head == NULL)
				head = tail = node2;
			else
				tail = tail->mln_Succ = node2;
			node2 = node2->mln_Succ;
		}
	}

	if (node2 != NULL)
		node = node2;
	tail->mln_Succ = node;
	tail = node;
	while (tail->mln_Succ != NULL) tail = tail->mln_Succ;
	*tailRef = tail;
	return head;
}

static void MergeSort(struct MinNode **headRef, CompareNodesFunc cmpfunc) {
	struct MinNode *head = *headRef;
	struct MinNode *newhead = head;
	struct MinNode *tail;
	struct MinNode *newtail;
	int total;
	int curlen;

	if (head == NULL || head->mln_Succ == NULL)
		return;

	total = CountNodes(head);
	curlen = 1;

	while (curlen < total) {
		newhead = MergeLists(newhead, curlen, &head, &tail, cmpfunc);
		while (head != NULL) {
			tail->mln_Succ = MergeLists(head, curlen, &head, &newtail, cmpfunc);
			tail = newtail;
		}
		curlen <<= 1;
	}

	*headRef = newhead;
}

static int CompareCacheNodes(struct MinNode *node1, struct MinNode *node2) {
	struct BlockCacheNode *cn1 = (struct BlockCacheNode *)node1;
	struct BlockCacheNode *cn2 = (struct BlockCacheNode *)node2;

	if (cn1->sector > cn2->sector)
		return 1;
	else if (cn1->sector < cn2->sector)
		return -1;
	else
		return 0;
};

void SortBlockCacheNodes(struct MinList *list) {
	struct MinNode *head, *succ;

	if (IsMinListEmpty(list) || OneInMinList(list)) return;

	list->mlh_TailPred->mln_Succ = NULL;
	head = list->mlh_Head;

	MergeSort(&head, CompareCacheNodes);

	NEWLIST(list);
	while (head != NULL) {
		succ = head->mln_Succ;
		AddTail((struct List *)list, (struct Node *)head);
		head = succ;
	}
}

