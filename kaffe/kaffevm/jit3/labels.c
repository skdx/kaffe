/* labels.c
 * Manage the code labels and links.
 *
 * Copyright (c) 1996, 1997
 *	Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution 
 * of this file. 
 */

#include "config.h"
#include "config-std.h"
#include "config-mem.h"
#include "gtypes.h"
#include "classMethod.h"
#include "labels.h"
#include "code-analyse.h"
#include "itypes.h"
#include "seq.h"
#include "constpool.h"
#include "gc.h"
#include "md.h"
#include "support.h"
#include "machine.h"

static label* firstLabel;
static label* lastLabel;
static label* currLabel;

/* Custom edition */
#define	kprintf	kaffe_dprintf
#define	gc_calloc_fixed(A,B)	gc_malloc((A)*(B), GC_ALLOC_JITTEMP)
#include "debug.h"

void
resetLabels(void)
{
	currLabel = firstLabel;
}


/*
 * Set epilogue label destination.
 */
void
setEpilogueLabel(uintp to)
{
	label* l;

	for (l = firstLabel; l != currLabel; l = l->next) {
		if ((l->type & Ltomask) == Lepilogue) {
			l->type = Linternal | (l->type & ~Ltomask);
			l->to = to;
		}
	}
}


/*
 * Link labels.
 * This function uses the saved label information to link the new code
 * fragment into the program.
 */
void
linkLabels(uintp codebase)
{
	long dest;
	int* place;
	label* l;

	for (l = firstLabel; l != currLabel; l = l->next) {

		/* Ignore this label if it hasn't been used */
		if (l->type == Lnull) {
			continue;
		}

		/* Find destination of label */
		switch (l->type & Ltomask) {
		case Lexternal:
			dest = l->to;	/* External code reference */
			break;
		case Lconstant:		/* Constant reference */
			dest = ((constpool*)l->to)->at;
			break;
		case Linternal:		/* Internal code reference */
		/* Lepilogue is changed to Linternal in setEpilogueLabel() */
			dest = l->to + codebase;
			break;
		case Lcode:		/* Reference to a bytecode */
			assert(INSNPC(l->to) != (uintp)-1);
			dest = INSNPC(l->to) + codebase;
			break;
		case Lgeneral:		/* Dest not used */
			dest = 0;
			break;
		default:
			goto unhandled;
		}

		/* Find the source of the reference */
		switch (l->type & Lfrommask) {
		case Labsolute:		/* Absolute address */
			break;
		case Lrelative:		/* Relative address */
			dest -= l->from + codebase;
			break;
		case Lfuncrelative:	/* Relative to function */
			dest -= codebase;
			break;
		default:
			goto unhandled;
		}

		/* Find place to store result */
		switch (l->type & Latmask) {
		case Lconstantpool:
			place = (int*)((constpool*)l->at)->at;
			break;
		default:
			place = (int*)(l->at + codebase);
			break;
		}

		switch (l->type & Ltypemask) {
		case Lquad:
			*(uint64*)place = dest;
			break;
		case Llong:
			*(uint32*)place = dest;
			break;

		/* Machine specific labels go in this magic macro */
			EXTRA_LABELS(place, dest, l);

		default:
		unhandled:
#if defined(DEBUG)
			kprintf("Label type 0x%x not supported (%p).\n", l->type, l);
#endif
			ABORT();
		}
#if 0
		/*
		 * If we were saving relocation information we must save all
		 * labels which are 'Labsolute', that is they hold an absolute
		 * address for something.  Note that this doesn't catch
		 * everything, specifically it doesn't catch string objects
		 * or references to classes.
		 */
		if ((l->type & Labsolute) != 0) {
			l->snext = savedLabel;
			savedLabel = l;
		}
#endif
	}
}

/*
 * Allocate a new label.
 */
label*
newLabel(void)
{
	int i;
	label* ret;

	ret = currLabel;
	if (ret == 0) {
		/* Allocate chunk of label elements */
		ret = gc_calloc_fixed(ALLOCLABELNR, sizeof(label));

		/* Attach to current chain */
		if (lastLabel == 0) {
			firstLabel = ret;
		}
		else {
			lastLabel->next = ret;
		}
		lastLabel = &ret[ALLOCLABELNR-1];

		/* Link elements into list */
		for (i = 0; i < ALLOCLABELNR-1; i++) {
			ret[i].next = &ret[i+1];
		}
		ret[ALLOCLABELNR-1].next = 0;
	}
	currLabel = ret->next;
	return (ret);
}
