/*
 * support.h
 * Various support routines.
 *
 * Copyright (c) 1996, 1997
 *	Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution 
 * of this file. 
 */

#ifndef __support_h
#define	__support_h

#include "config-std.h"
#include <stdarg.h>

/* For user defined properties */
typedef struct _userProperty {
        char*                   key;
        char*                   value;
        struct _userProperty*   next;
} userProperty;

typedef struct _nativeFunction {
	char*			name;
	void*			func;
} nativeFunction;

#define	NATIVE_FUNC_INCREMENT	(256)

extern nativeFunction* native_funcs;

extern userProperty* userProperties;

struct _methods;
struct _errorInfo;

/* 64 should put us on the safe side */
#define	MAXMARGS		64

/*
 * The callMethodInfo structure describes the information necessary to 
 * invoke a native or just-in-time compiled method.
 *
 * It is the task of the sysdepCallMethod macro, usually defined in 
 * config/$arch/common.h, to generate a procedure call that conforms to 
 * the calling convention of a particular architecture or set of build tools.
 *
 * The sysdepCallMethod macro takes a single argument of type callMethodInfo *
 * that describes where the parameters are, where the return value should
 * go and what the signature of the method is.
 *
 * `jvalue' is a union defined in include/jtypes.h.  It corresponds to an
 * entry on the Java stack. 
 * The suffixes i,j,b,c,s,l,f,d access the corresponding element of
 * (Java) type int, long, boolean, char, short, ref, float, and double.
 *
 * `args' is an array containing the arguments passed to the function.
 * It corresponds to the Java stack and has `nrargs' valid entries with 
 * the following property:
 *
 * If two adjacent slots on the Java stack are combined to a 64bit value,
 * it will also use two array entries, and not one.  However, the first 
 * entry will contain the 64bit value (in j or d, depending on the type), 
 * and the second entry will be undefined.  This allows for easier access, 
 * while preserving indexing.  Thus, some array entries will have 0, some
 * 32 and some 64 bits of valid data in them.  The callsize array says
 * which one it is.  
 *
 * callsize[i] may be 0, 1, or 2, depending on the number of valid bits
 * in args[i].  Similarly, calltype[i] contains the signature type of the
 * argument in args[i] ('J', 'F', etc.)
 *
 * To simplify 32 bit assembly code, we copy the last 32 bits of a 64
 * bit arg into the next slot.  This allows you to treat args as an
 * array of 32 bit values.  This simplification also makes a C version
 * of sysdepCallMethod more viable, and such a function is defined in
 * mi.c.
 *
 * Note that "callsize[i] == 2 iff callsize[i+1] == 0" -- this 
 * property can also be exploited by a sysdepCallMethod macros. 
 *
 * `function' is a pointer to the method to be invoked.
 *
 * `retsize' and `rettype' have the same value ranges as callsize[i] and 
 * calltype[i],  except they correspond to the return value.  The 
 * sysdepCallMethod must store the return value in the proper type at *ret.
 */
typedef struct {
	void*			function;
	jvalue*			args;
	jvalue*			ret;
	int			nrargs;
	int			argsize;
	char			retsize;
	char			rettype;
	char			callsize[MAXMARGS];
	char			calltype[MAXMARGS];
} callMethodInfo;

struct Hjava_lang_String;
struct Hjava_lang_Class;
struct Hjava_lang_Object;
struct Hjava_lang_Throwable;

extern void		setProperty(void*, char*, char*);
extern char*		getEngine(void);
extern void		classname2pathname(const char*, char*);
extern jvalue		do_execute_java_method(void*, const char*, const char*, struct _methods*, int, ...);
extern jvalue		do_execute_java_method_v(void*, const char*, const char*, struct _methods*, int, va_list);
extern jvalue		do_execute_java_class_method(const char*, const char*, const char*, ...);
extern jvalue		do_execute_java_class_method_v(const char*, const char*, const char*, va_list);

extern struct Hjava_lang_Object* execute_java_constructor(const char*, struct Hjava_lang_Class*, const char*, ...);
extern struct Hjava_lang_Object* execute_java_constructor_v(const char*, struct Hjava_lang_Class*, const char*, va_list);
extern jlong		currentTime(void);
extern void		addNativeMethod(const char*, void*);

extern void	callMethodA(struct _methods*, void*, void*, jvalue*, jvalue*);
extern void	callMethodV(struct _methods*, void*, void*, va_list, jvalue*);
extern struct _methods*	lookupClassMethod(struct Hjava_lang_Class*, const char*, const char*, struct _errorInfo*);
extern struct _methods*	lookupObjectMethod(struct Hjava_lang_Object*, const char*, const char*, struct _errorInfo*);

struct _strconst;
extern void SignalError(const char *, const char *) __NORETURN__;
extern void unimp(const char*) __NORETURN__;
extern void kprintf(FILE*, const char*, ...);
extern int addClasspath(const char*);

/*
 * Macros to manipulate bit arrays.  
 */
#define BITMAP_BPI	(sizeof(int) * 8)

/* create a new bitmap for b bits */
#define BITMAP_NEW(b)	\
    (int *)gc_calloc(((b) + BITMAP_BPI - 1)/BITMAP_BPI, sizeof(int), \
		     GC_ALLOC_NOWALK)

/* set nth bit, counting from MSB to the right */
#define BITMAP_SET(m, n) \
    (m[(n) / BITMAP_BPI] |= (1 << (BITMAP_BPI - 1 - (n) % BITMAP_BPI)))

/* copy nbits from bitmap src to bitmap dst */
#define BITMAP_COPY(dst, src, nbits) \
    memcpy(dst, src, ((nbits + BITMAP_BPI - 1)/BITMAP_BPI) * sizeof(int))

/* dump a bitmap for debugging */
#define BITMAP_DUMP(m, N) { int n; \
    for (n = 0; n < N; n++) \
	if (m[(n) / BITMAP_BPI] &  \
		(1 << (BITMAP_BPI - 1 - (n) % BITMAP_BPI))) \
	    dprintf("1"); else dprintf("0"); }

#ifndef MIN
#define MIN(A,B) ((A) < (B) ? (A) : (B))
#endif

#ifndef MAX
#define MAX(A,B) ((A) > (B) ? (A) : (B))
#endif

/*
 * Structures and prototypes for getrusage based execution timing.
 */
#if defined(TIMING)

typedef struct _timespent timespent;

struct _timespent {
	char *name;
	timespent *next;
	int calls;
	struct timeval total;
	struct timeval stotal;
	struct timeval current;
	struct timeval scurrent;
};

extern void startTiming(timespent *counter, char *name);
extern void stopTiming(timespent *counter);
#else
/* We either can't or wont perform timing:  The first macro suppresses
   unused variable warnings. */
typedef char timespent;

#define startTiming(C,N) (*(C) = 0)
#define stopTiming(C)
#endif

#endif
