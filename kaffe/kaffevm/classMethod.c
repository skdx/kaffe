/*
 * classMethod.c
 * Dictionary of classes, methods and fields.
 *
 * Copyright (c) 1996, 1997
 *	Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution 
 * of this file. 
 */

#include "config.h"
#include "debug.h"
#include "config-std.h"
#include "config-mem.h"
#include "gtypes.h"
#include "slots.h"
#include "access.h"
#include "object.h"
#include "errors.h"
#include "code.h"
#include "file.h"
#include "readClass.h"
#include "baseClasses.h"
#include "stringSupport.h"
#include "thread.h"
#include "jthread.h"
#include "itypes.h"
#include "bytecode.h"
#include "exception.h"
#include "classMethod.h"
#include "md.h"
#include "external.h"
#include "lookup.h"
#include "support.h"
#include "gc.h"
#include "locks.h"
#include "md.h"
#include "jni.h"
#include "gcj/gcj.h"

#define	CLASSHASHSZ	256	/* Must be a power of two */
static iLock classHashLock;
static classEntry* classEntryPool[CLASSHASHSZ];

#if 0
#define	METHOD_TRUE_NCODE(METH)			(METH)->c.ncode.ncode_start
#define	METHOD_PRE_COMPILED(METH)		((int16)(METH)->localsz < 0)
#define	SET_METHOD_PRE_COMPILED(METH, VAL)	((METH)->localsz = -(VAL))
#endif
#define METHOD_NEEDS_TRAMPOLINE(meth) \
	(!METHOD_TRANSLATED(meth) || (((meth)->accflags & ACC_STATIC) && (meth)->class->state < CSTATE_DOING_INIT))

/* interfaces supported by arrays */
static Hjava_lang_Class* arr_interfaces[2];

extern JNIEnv Kaffe_JNIEnv;
extern gcFuncs gcClassObject;

extern bool verify2(Hjava_lang_Class*, errorInfo*);
extern bool verify3(Hjava_lang_Class*, errorInfo*);

static void internalSetupClass(Hjava_lang_Class*, Utf8Const*, int, int, Hjava_lang_ClassLoader*);

static void buildDispatchTable(Hjava_lang_Class*);
static bool checkForAbstractMethods(Hjava_lang_Class* class, errorInfo *einfo);
static void buildInterfaceDispatchTable(Hjava_lang_Class*);
static void allocStaticFields(Hjava_lang_Class*);
static void resolveObjectFields(Hjava_lang_Class*);
static void resolveStaticFields(Hjava_lang_Class*);
static bool resolveConstants(Hjava_lang_Class*, errorInfo *einfo);

#if !defined(ALIGNMENT_OF_SIZE)
#define	ALIGNMENT_OF_SIZE(S)	(S)
#endif

/*
 * Process all the stage of a classes initialisation.  We can provide
 * a state to aim for (so we don't have to do this all at once).  This
 * is called by various parts of the machine in order to load, link
 * and initialise the class.  Putting it all together here makes it a damn
 * sight easier to understand what's happening.
 *
 * Returns true if processing was successful, false otherwise.
 */
bool
processClass(Hjava_lang_Class* class, int tostate, errorInfo *einfo)
{
	int i;
	int j;
	int k;
	Method* meth;
	Hjava_lang_Class* nclass;
	Hjava_lang_Class** newifaces;
	iLock *classLock;
	bool success = true;	/* optimistic */
#ifdef DEBUG
	static int depth;
#endif

	/* If this class is initialised to the required point, quit now */
	if (class->state >= tostate) {
		return (true);
	}

#define	SET_CLASS_STATE(S)	class->state = (S)
#define	DO_CLASS_STATE(S)	if ((S) > class->state && (S) <= tostate)

	/* For the moment we only allow one thread to initialise any classes
	 * at once.  This is because we've got circular class dependencies
	 * we've got to work out.
	 */

	classLock = lockMutex(class);

DBG(RESERROR,
	/* show calls to processClass when debugging resolution errors */
	depth++;
	for (i = 0; i < depth; dprintf("  ", i++));
	dprintf("%p entering process class %s %d->%d\n", 
		jthread_current(), class->name->data,
		class->state, tostate);
    )

retry:
	/* If the initialization of that class failed once before, don't
	 * bother and report that no definition for this class exists.
	 * We must do that after the retry label so that threads waiting
	 * on other threads performing a particular initialization step
	 * can learn that things went wrong.
	 */
	if (class->state == CSTATE_FAILED) {
		SET_LANG_EXCEPTION_MESSAGE(einfo, 
			NoClassDefFoundError, class->name->data)
		success = false;
		goto done;
	}

	DO_CLASS_STATE(CSTATE_PREPARED) {

		if (class->state == CSTATE_DOING_PREPARE) {
			if (THREAD_NATIVE() == class->processingThread) {
				/* Check for circular dependent classes */
				SET_LANG_EXCEPTION(einfo, ClassCircularityError)
				success = false;
				goto done;
			} else {
				while (class->state == CSTATE_DOING_PREPARE) {
					waitStaticCond(classLock, 0);
					goto retry;
				}
			}
		}

		/* Allocate any static space required by class and initialise
		 * the space with any constant values.  This isn't necessary
		 * for pre-loaded classes.
		 */
		if (class->state != CSTATE_PRELOADED) {
			allocStaticFields(class);
		}

		SET_CLASS_STATE(CSTATE_DOING_PREPARE);
		class->processingThread = THREAD_NATIVE();

		/* Load and link the super class */
		if (class->superclass) {
			/* propagate failures in super class loading and 
			 * processing.  Since getClass might involve an
			 * upcall to a classloader, we must release the
			 * classLock here.
			 */
			unlockKnownMutex(classLock);
			class->superclass = getClass((uintp)class->superclass, 
							class, einfo);
			classLock = lockMutex(class);
			if (class->superclass == 0) {
				success = false;
				goto done;
			}
			/* that's pretty much obsolete. */
			assert(class->superclass->state >= CSTATE_LINKED);
						
			/* Copy initial field size and gc layout. 
			 * Later, as this class's fields are resolved, they
			 * are added to the superclass's layout.
			 */
			CLASS_FSIZE(class) = CLASS_FSIZE(class->superclass);
			class->gc_layout = class->superclass->gc_layout;
		}
		if (class->superclass)
			assert(class->superclass->state >= CSTATE_LINKED);

		/* Load all the implemented interfaces. */
		j = class->interface_len;
		nclass = class->superclass;
		if (nclass != 0 && nclass != ObjectClass) {
			/* If class is an interface, its superclass must
			 * be java.lang.Object or the class file is broken.
			 */
			if (CLASS_IS_INTERFACE(class)) {
				SET_LANG_EXCEPTION(einfo, VerifyError)
				success = false;
				goto done;
			}
			j += class->superclass->total_interface_len;
		}
		for (i = 0; i < class->interface_len; i++) {
			uintp iface = (uintp)class->interfaces[i];
			unlockKnownMutex(classLock);
			class->interfaces[i] = getClass(iface, class, einfo);
			classLock = lockMutex(class);
			if (class->interfaces[i] == 0) {
				success = false;
				goto done;
			}
			j += class->interfaces[i]->total_interface_len;
		}
		class->total_interface_len = j;

		/* We build a list of *all* interfaces this class can use */
		if (class->interface_len != class->total_interface_len) {
			newifaces = (Hjava_lang_Class**)gc_malloc(sizeof(Hjava_lang_Class**) * j, GC_ALLOC_INTERFACE);
			for (i = 0; i < class->interface_len; i++) {
				newifaces[i] = class->interfaces[i];
			}
			nclass = class->superclass;
			if (nclass != 0 && nclass != ObjectClass) {
				for (j = 0; j < nclass->total_interface_len; j++, i++) {
					newifaces[i] = nclass->interfaces[j];
				}
			}
			for (k = 0; k < class->interface_len; k++) {
				nclass = class->interfaces[k];
				for (j = 0; j < nclass->total_interface_len; j++, i++) {
					newifaces[i] = nclass->interfaces[j];
				}
			}
			/* free old list of interfaces */
			if (class->interfaces != 0) {
				KFREE(class->interfaces);
			}
			class->interfaces = newifaces;
		}

		/* This shouldn't be necessary - but it is at the moment!! */
		class->head.dtable = ClassClass->dtable;

		resolveObjectFields(class);
		resolveStaticFields(class);

#if defined(HAVE_GCJ_SUPPORT)
		if (CLASS_GCJ(class)) {
			gcjProcessClass(class);
		}
#endif

		/* Build dispatch table.  We must handle interfaces a little
		 * differently since they only have a <clinit> method.
		 */
		if (!CLASS_IS_INTERFACE(class)) {
			buildDispatchTable(class);
			success = checkForAbstractMethods(class, einfo);
			if (success == false) {
				goto done;
			}
		}
		else {
			buildInterfaceDispatchTable(class);
		}

		SET_CLASS_STATE(CSTATE_PREPARED);
	}

	assert(class == ObjectClass || class->superclass != 0);

	DO_CLASS_STATE(CSTATE_LINKED) {

		/* Second stage verification - check the class format is okay */
		success =  verify2(class, einfo);
		if (success == false) {
			goto done;
		}

		/* Third stage verification - check the bytecode is okay */
		success = verify3(class, einfo);
		if (success == false) {
			goto done;
		}

		SET_CLASS_STATE(CSTATE_LINKED);
	}

	/* NB: the reason that CONSTINIT is a separate state is that
	 * CONSTINIT depends on StringClass, which isn't available during 
	 * initialization when we bring the base classes to the LINKED state.
	 */
	DO_CLASS_STATE(CSTATE_CONSTINIT) {
		/* Initialise the constants */
		success = resolveConstants(class, einfo);
		if (success == false) {
			goto done;
		}

		/* And note that it's done */
		SET_CLASS_STATE(CSTATE_CONSTINIT);
	}

	DO_CLASS_STATE(CSTATE_USABLE) {

		/* If somebody's already processing the super class,
		 * check whether it's us.  If so, return.
		 * Else, wait for the other thread to complete and
		 * start over to reevaluate the situation.
		 */
		if (class->state == CSTATE_DOING_SUPER) {
			if (THREAD_NATIVE() == class->processingThread) {
				goto done;
			} else {
				while (class->state == CSTATE_DOING_SUPER) {
					waitStaticCond(classLock, 0);
					goto retry;
				}
			}
		}

		SET_CLASS_STATE(CSTATE_DOING_SUPER);

		/* Now determine the method used to finalize this object.
		 * If the finalizer is empty, we set class->finalizer to null.
		 * Find finalizer first without calling findMethod.
		 */
		meth = 0;
		for (nclass = class; nclass != 0; nclass = nclass->superclass) {
			meth = findMethodLocal(nclass, final_name, void_signature);
			if (meth != NULL) {
				break;
			}
		}

		/* every class must have one since java.lang.Object has one */
		if (meth == NULL) {
			SET_LANG_EXCEPTION(einfo, InternalError);
			success = false;
			goto done;
		}

		/* is it empty? */
		if (meth->c.bcode.codelen == 1 && meth->c.bcode.code[0] == RETURN) {
			class->finalizer = 0;
		} else {
			class->finalizer = meth;
		}

		if (class->superclass != NULL) {
			class->processingThread = THREAD_NATIVE();

			/* We must not hold the class lock here because we 
			 * might call out into the superclass's initializer 
			 * here!
			 */
			unlockKnownMutex(classLock);
			success = processClass(class->superclass, 
					     CSTATE_COMPLETE, einfo);
			classLock = lockMutex(class);
			if (success == false) {
				goto done;
			}
		}

		SET_CLASS_STATE(CSTATE_USABLE);
	}

	DO_CLASS_STATE(CSTATE_COMPLETE) {
		JNIEnv *env = &Kaffe_JNIEnv;
		jthrowable exc = 0;
		JavaVM *vms[1];
		jsize jniworking;

		/* If we need a successfully initialized class here, but its
		 * initializer failed, return false as well
		 */
		if (class->state == CSTATE_INIT_FAILED) {
			SET_LANG_EXCEPTION_MESSAGE(einfo, 
				NoClassDefFoundError, class->name->data)
			success = false;
			goto done;
		}

DBG(STATICINIT, dprintf("Initialising %s static %d\n", class->name->data,
			CLASS_FSIZE(class)); 	)
		meth = findMethodLocal(class, init_name, void_signature);
		if (meth == NULL) {
			SET_CLASS_STATE(CSTATE_COMPLETE);
			goto done;
		} 

		if (class->state == CSTATE_DOING_INIT) {
			if (THREAD_NATIVE() == class->processingThread) {
				goto done;
			} else {
				while (class->state == CSTATE_DOING_INIT) {
					waitStaticCond(classLock, 0);
					goto retry;
				}
			}
		}

		SET_CLASS_STATE(CSTATE_DOING_INIT);
		class->processingThread = THREAD_NATIVE();

		/* give classLock up for the duration of this call */
		unlockKnownMutex(classLock);

		/* we use JNI to catch possible exceptions, except
		 * during initialization, when JNI doesn't work yet.
		 * Should an exception occur at that time, we're
		 * lost anyway.
		 */
		JNI_GetCreatedJavaVMs(vms, 1, &jniworking);
		if (jniworking) {
DBG(STATICINIT, 		
			dprintf("using JNI\n");	
)
			(*env)->CallStaticVoidMethodA(env, class, meth, 0);
			exc = (*env)->ExceptionOccurred(env);
			(*env)->ExceptionClear(env);
		} else {
DBG(STATICINIT, 
			dprintf("using callMethodA\n");	
    )
			callMethodA(meth, METHOD_INDIRECTMETHOD(meth), 0, 0, 0);
		}

		classLock = lockMutex(class);

		if (exc != 0) {
			/* this is special-cased in throwError */
			SET_LANG_EXCEPTION_MESSAGE(einfo,
				ExceptionInInitializerError, exc)
			/*
			 * we return false here because COMPLETE fails
			 */
			success = false;
			SET_CLASS_STATE(CSTATE_INIT_FAILED);
		} else {
			SET_CLASS_STATE(CSTATE_COMPLETE);
		}

		/* Since we'll never run this again we might as well
		 * loose it now.
		 */
		METHOD_NATIVECODE(meth) = 0;
		KFREE(meth->c.ncode.ncode_start);
		meth->c.ncode.ncode_start = 0;
		meth->c.ncode.ncode_end = 0;
	}

done:
	/* If anything ever goes wrong with this class, we declare it dead
	 * and will respond with NoClassDefFoundErrors to any future attempts
	 * to access that class.
	 * NB: this does not include when a static initializer failed.
	 */
	if (success == false && class->state != CSTATE_INIT_FAILED) {
		SET_CLASS_STATE(CSTATE_FAILED);
	}

	/* wake up any waiting threads */
	broadcastStaticCond(classLock);
	unlockKnownMutex(classLock);

DBG(RESERROR,
	for (i = 0; i < depth; dprintf("  ", i++));
	depth--;
	dprintf("%p leaving process class %s -> %s\n", 
		jthread_current(), class->name->data,
		success ? "success" : "failure");
    )
	return (success);
}

Hjava_lang_Class*
setupClass(Hjava_lang_Class* cl, constIndex c, constIndex s, u2 flags, Hjava_lang_ClassLoader* loader)
{
	constants* pool;

	pool = CLASS_CONSTANTS(cl);

	/* Find the name of the class */
	if (pool->tags[c] != CONSTANT_Class) {
DBG(RESERROR,	dprintf("setupClass: not a class.\n");			)
		return (0);
	}

	internalSetupClass(cl, WORD2UTF(pool->data[c]), flags, s, loader);

	return (cl);
}

/*
 * Take a prepared class and register it with the class pool.
 */
void
registerClass(classEntry* entry)
{
	lockMutex(entry);

	/* not used at this time */

	unlockMutex(entry);
}

static
void
internalSetupClass(Hjava_lang_Class* cl, Utf8Const* name, int flags, int su, Hjava_lang_ClassLoader* loader)
{
	cl->name = name;
	utf8ConstAddRef(name);
	CLASS_METHODS(cl) = NULL;
	CLASS_NMETHODS(cl) = 0;
	assert(cl->superclass == 0);
	cl->superclass = (Hjava_lang_Class*)(uintp)su;
	cl->msize = 0;
	CLASS_FIELDS(cl) = 0;
	CLASS_FSIZE(cl) = 0;
	cl->accflags = flags;
	cl->dtable = 0;
        cl->interfaces = 0;
	cl->interface_len = 0;
	assert(cl->state < CSTATE_LOADED);
	cl->state = CSTATE_LOADED;
	cl->loader = loader;
}

/*
 * add source file name to be printed in exception backtraces
 */
void
addSourceFile(Hjava_lang_Class* c, int idx)
{
	constants* pool;
	const char* sourcefile;
	const char* basename;

	pool = CLASS_CONSTANTS (c);
	sourcefile = WORD2UTF (pool->data[idx])->data;
	basename = strrchr(sourcefile, '/');
	if (basename == 0) {
		basename = sourcefile;
	} else {
		basename++;
	}
	c->sourcefile = KMALLOC(strlen(basename) + 1);
	strcpy(c->sourcefile, basename);	
}

Method*
addMethod(Hjava_lang_Class* c, method_info* m)
{
	constIndex nc;
	constIndex sc;
	Method* mt;
	constants* pool;
	Utf8Const* name;
	Utf8Const* signature;
#ifdef DEBUG
	int ni;
#endif

	pool = CLASS_CONSTANTS (c);

	nc = m->name_index;
	if (pool->tags[nc] != CONSTANT_Utf8) {
DBG(RESERROR,	dprintf("addMethod: no method name.\n");		)
		return (0);
	}
	sc = m->signature_index;
	if (pool->tags[sc] != CONSTANT_Utf8) {
DBG(RESERROR,	dprintf("addMethod: no signature name.\n");	)
		return (0);
	}
	name = WORD2UTF (pool->data[nc]);
	signature = WORD2UTF (pool->data[sc]);
  
#ifdef DEBUG
	/* Search down class for method name - don't allow duplicates */
	for (ni = CLASS_NMETHODS(c), mt = CLASS_METHODS(c); --ni >= 0; ) {
		assert(! utf8ConstEqual (name, mt->name)
		       || ! utf8ConstEqual (signature, mt->signature));
	}
#endif

DBG(CLASSFILE,
	dprintf("Adding method %s:%s%s (%x)\n", c->name->data, WORD2UTF(pool->data[nc])->data, WORD2UTF(pool->data[sc])->data, m->access_flags);	
    )

	mt = &c->methods[c->nmethods++];
	mt->name = name;
	utf8ConstAddRef(name);
	mt->signature = signature;
	utf8ConstAddRef(signature);
	mt->class = c;
	mt->accflags = m->access_flags;
	mt->c.bcode.code = 0;
	mt->stacksz = 0;
	mt->localsz = 0;
	mt->exception_table = 0;
	mt->idx = -1;

	/* Mark constructors as such */
	if (utf8ConstEqual (name, constructor_name)) {
		mt->accflags |= ACC_CONSTRUCTOR;
	}

	return (mt);
}

Field*
addField(Hjava_lang_Class* c, field_info* f)
{
	constIndex nc;
	constIndex sc;
	Field* ft;
	constants* pool;
	int index;
	const char* sig;

	pool = CLASS_CONSTANTS (c);

	nc = f->name_index;
	if (pool->tags[nc] != CONSTANT_Utf8) {
DBG(RESERROR,	dprintf("addField: no field name.\n");			)
		return (0);
	}

	--CLASS_FSIZE(c);
	if (f->access_flags & ACC_STATIC) {
		index = CLASS_NSFIELDS(c)++;
	}
	else {
		index = CLASS_FSIZE(c) + CLASS_NSFIELDS(c);
	}
	ft = &CLASS_FIELDS(c)[index];

DBG(CLASSFILE,	
	dprintf("Adding field %s:%s\n", 
		c->name->data, WORD2UTF(pool->data[nc])->data);
    )

	sc = f->signature_index;
	if (pool->tags[sc] != CONSTANT_Utf8) {
DBG(RESERROR,	dprintf("addField: no signature name.\n");		)
		return (0);
	}
	ft->name = WORD2UTF(pool->data[nc]);
	utf8ConstAddRef(ft->name);
	ft->accflags = f->access_flags;

	sig = CLASS_CONST_UTF8(c, sc)->data;
	if (sig[0] == 'L' || sig[0] == '[') {
		/* This `type' field is used to hold a utf8Const describing
		 * the type of the field.  This utf8Const will be replaced
		 * with a pointer to an actual class type in resolveFieldType
		 * Between now and then, we add a reference to it.
		 */
		Utf8Const *ftype = CLASS_CONST_UTF8(c, sc);
		FIELD_TYPE(ft) = (Hjava_lang_Class*)ftype;
		utf8ConstAddRef(ftype);
		FIELD_SIZE(ft) = PTR_TYPE_SIZE;
		ft->accflags |= FIELD_UNRESOLVED_FLAG;
	}
	else {
		/* NB: since this class is primitive, getClassFromSignature
		 * will not fail.  Hence it's okay to pass errorInfo as NULL
		 */
		FIELD_TYPE(ft) = getClassFromSignature(sig, 0, NULL);
		FIELD_SIZE(ft) = TYPE_PRIM_SIZE(FIELD_TYPE(ft));
	}

	return (ft);
}

void
setFieldValue(Field* ft, u2 idx)
{
	/* Set value index */
	FIELD_CONSTIDX(ft) = idx;
	ft->accflags |= FIELD_CONSTANT_VALUE;
}

void
finishFields(Hjava_lang_Class *cl)
{
	Field tmp;
	Field* fld;
	int n;

	/* Reverse the instance fields, so they are in "proper" order. */
	fld = CLASS_IFIELDS(cl);
	n = CLASS_NIFIELDS(cl);
	while (n > 1) {
		tmp = fld[0];
		fld[0] = fld[n-1];
		fld[n-1] = tmp;
		fld++;
		n -= 2;
	}
}

void
addMethodCode(Method* m, Code* c)
{
	m->c.bcode.code = c->code;
	m->c.bcode.codelen = c->code_length;
	m->stacksz = c->max_stack;
	m->localsz = c->max_locals;
	m->exception_table = c->exception_table;
}

void
addInterfaces(Hjava_lang_Class* c, int inr, Hjava_lang_Class** inf)
{
	assert(inr > 0);

        c->interfaces = inf;
	c->interface_len = inr;
}

/*
 * Lookup a named class, loading it if necessary.
 * The name is as used in class files, e.g. "java/lang/String".
 */
Hjava_lang_Class*
loadClass(Utf8Const* name, Hjava_lang_ClassLoader* loader, errorInfo *einfo)
{
	iLock* celock;
	classEntry* centry;
	Hjava_lang_Class* clazz = NULL;

        centry = lookupClassEntry(name, loader);
	/*
	 * An invariant for classEntries is that if centry->class != 0, then
	 * the corresponding class object has been read completely and it is
	 * safe to invoke processClass on it.  processClass will resolve any
	 * races between threads.
	 */
	if (centry->class != NULL) {
		clazz = centry->class;
		goto found;
	}

	/* 
	 * Failed to find class, so must now load it.
	 * We send at most one thread to load a class. 
	 */
	celock = lockMutex(centry);

	/* Check again in case someone else did it */
	if (centry->class == NULL) {

		if (loader != NULL) {
			Hjava_lang_String* str;
			JNIEnv *env = &Kaffe_JNIEnv;
			jmethodID meth;
			jthrowable excobj, excpending;

DBG(VMCLASSLOADER,		
			dprintf("classLoader: loading %s\n", name->data); 
    )
			str = utf8Const2JavaReplace(name, '/', '.');
			/* If an exception is already pending, for instance
			 * because we're resolving one that has occurred,
			 * save it and clear it for the upcall.
			 */
			excpending = (*env)->ExceptionOccurred(env);
			(*env)->ExceptionClear(env);

			/*
			 * We use JNI here so that all exceptions are caught
			 * and we'll always return.
			 */
			meth = (*env)->GetMethodID(env,
				    (*env)->GetObjectClass(env, loader),
				    "loadClass",
				    "(Ljava/lang/String;Z)Ljava/lang/Class;");
			assert(meth != 0);

			clazz = (Hjava_lang_Class*)
				(*env)->CallObjectMethod(env, loader, meth, 
							str, true);

			/*
			 * Check whether an exception occurred.
			 * If one was pending, the new exception will
			 * override this one.
			 */
			excobj = (*env)->ExceptionOccurred(env);
			(*env)->ExceptionClear(env);
			if (excobj != 0) {
DBG(VMCLASSLOADER,		
				dprintf("exception!\n");			
    )
				SET_LANG_EXCEPTION_MESSAGE(einfo, 
					RethrowException, excobj)
				clazz = NULL;
			} else
			if (clazz == NULL) {
DBG(VMCLASSLOADER,		
				dprintf("loadClass returned clazz == NULL!\n");
    )
				SET_LANG_EXCEPTION_MESSAGE(einfo, 
					NoClassDefFoundError, name->data)
			} else
			if (strcmp(clazz->name->data, name->data)) {
				/* 1.2 says: 
				 * Bad class name (expect: THIS, get: THAT) 
				 * XXX make error more informative.
				 */
DBG(VMCLASSLOADER,		
				dprintf("loadClass returned wrong name!\n");
    )
				SET_LANG_EXCEPTION_MESSAGE(einfo, 
					NoClassDefFoundError, name->data)
				clazz = NULL;
			}
DBG(VMCLASSLOADER,		
			dprintf("classLoader: done %p\n", clazz);			
    )
			/* rethrow pending exception */
			if (excpending != NULL) {
				(*env)->Throw(env, excpending);
			}
			/*
			 * NB: if the classloader we invoked defined that class
			 * by the time we're here, we must ignore whatever it
			 * returns.  It can return null or lie or whatever.
			 *
			 * If, however, the classloader we initiated returns
			 * and has not defined the class --- for instance,
			 * because it has used delegation --- then we must
			 * record this classloader's answer in the class entry
			 * pool to guarantee temporal consistency.
			 */
			if (centry->class == 0) {
				/* NB: centry->class->centry != centry */
				centry->class = clazz;
			}
		}
#if defined(HAVE_GCJ_SUPPORT)
		else if (gcjFindClass(centry) == true) {
			clazz = centry->class;
		}
#endif
		else {
			/* no classloader, use findClass */
			clazz = findClass(centry, einfo);

			/* we do not ever unload system classes without a
			 * classloader, so anchor this one
			 */
			if (clazz != NULL) {
				gc_add_ref(clazz);
			} else {
DBG(RESERROR,
				dprintf("findClass failed: %s:`%s'\n", 
					einfo->classname, (char*)einfo->mess);
    )
			}
			centry->class = clazz;
		}
	}
	else {
		/* get the result from some other thread */
		clazz = centry->class;
	}

	/* Release lock now class has been entered */
	unlockKnownMutex(celock);

	if (clazz == NULL) {
		return (NULL);
	}

	/*
	 * A post condition of getClass is that the class is at least in
	 * state LINKED.  However, we must not call processClass (and attempt
	 * to get the global lock there) while having the lock on centry.
	 * Otherwise, we would deadlock with a thread calling getClass out
	 * of processClass.
	 */
found:;
	if (clazz->state < CSTATE_LINKED) {
		if (processClass(clazz, CSTATE_LINKED, einfo) == false)  {
			return (NULL);
		}
	}

	return (clazz);
}

Hjava_lang_Class*
loadArray(Utf8Const* name, Hjava_lang_ClassLoader* loader, errorInfo *einfo)
{
	Hjava_lang_Class *clazz;

	clazz = getClassFromSignature(&name->data[1], loader, einfo);
	if (clazz != 0) {
		return (lookupArray(clazz));
	}
	return (0);
}

/*
 * Load a class to whose Class object we refer globally.
 * This is used only for essential classes, so let's bail if this fails.
 */
void
loadStaticClass(Hjava_lang_Class** class, const char* name)
{
	Hjava_lang_Class *clazz;
	errorInfo info;
	Utf8Const *utf8;
	classEntry* centry;
	iLock* celock;

	utf8 = utf8ConstNew(name, -1);
	centry = lookupClassEntry(utf8, 0);
	utf8ConstRelease(utf8);
	celock = lockMutex(centry);
	if (centry->class == 0) {
		clazz = findClass(centry, &info);
		if (clazz == 0) {
			goto bad;
		}
		/* we won't ever want to lose these classes */
		gc_add_ref(clazz);
		(*class) = centry->class = clazz;
	}
	unlockKnownMutex(celock);

	if (processClass(centry->class, CSTATE_LINKED, &info) == true) {
		return;
	}

bad:
	fprintf(stderr, "Couldn't find or load essential class `%s' %s %s\n", 
			name, info.classname, (char*)info.mess);
	ABORT();
}

/*
 * Look a class up by name.
 */
Hjava_lang_Class*
lookupClass(const char* name, errorInfo *einfo)
{
	Hjava_lang_Class* class;
	Utf8Const *utf8;

	utf8 = utf8ConstNew(name, -1);
	class = loadClass(utf8, NULL, einfo);
	utf8ConstRelease(utf8);
	if (class != 0) {
		if (processClass(class, CSTATE_COMPLETE, einfo) == true) {
			return (class);
		}
	}
	return (0);
}

/*
 * Return FIELD_TYPE(FLD), but if !FIELD_RESOLVED, resolve the field first.
 */
Hjava_lang_Class*
resolveFieldType(Field *fld, Hjava_lang_Class* this, errorInfo *einfo)
{
	Hjava_lang_Class* clas;
	const char* name;
	iLock* lock;

	/* Avoid locking if we can */
	if (FIELD_RESOLVED(fld)) {
		return (FIELD_TYPE(fld));
	}

	/* We lock the class while we retrieve the field name since someone
	 * else may update it while we're doing this.  Once we've got the
	 * name we don't really care.
	 */
	lock = lockMutex(this->centry);
	if (FIELD_RESOLVED(fld)) {
		unlockKnownMutex(lock);
		return (FIELD_TYPE(fld));
	}
	name = ((Utf8Const*)fld->type)->data;
	unlockKnownMutex(lock);

	clas = getClassFromSignature(name, this->loader, einfo);

	utf8ConstRelease((Utf8Const*)fld->type);
	FIELD_TYPE(fld) = clas;
	fld->accflags &= ~FIELD_UNRESOLVED_FLAG;

	return (clas);
}

static
void
resolveObjectFields(Hjava_lang_Class* class)
{
	int fsize;
	int align;
	Field* fld;
	int nbits, n;
	int offset;
	int maxalign;
	int oldoffset;
	int *map;

	/* Find start of new fields in this object.  If start is zero, we must
	 * allow for the object headers.
	 */
	offset = CLASS_FSIZE(class);
	oldoffset = offset;	/* remember initial offset */
	if (offset == 0) {
		offset = sizeof(Hjava_lang_Object);
	}

	/* Find the largest alignment in this class */
	maxalign = 1;
	fld = CLASS_IFIELDS(class);
	n = CLASS_NIFIELDS(class);
	for (; --n >= 0; fld++) {
		fsize = FIELD_SIZE(fld);
		/* Work out alignment for this size entity */
		fsize = ALIGNMENT_OF_SIZE(fsize);
		/* If field is bigger than biggest alignment, change
		 * biggest alignment
		 */
		if (fsize > maxalign) {
			maxalign = fsize;
		}
	}
#if 0
	/* Align start of this class's data */
	if (oldoffset == 0) {
		offset = ((offset + maxalign - 1) / maxalign) * maxalign;
	}
#endif

	/* Now work out where to put each field */
	fld = CLASS_IFIELDS(class);
	n = CLASS_NIFIELDS(class);
	for (; --n >= 0; fld++) {
		fsize = FIELD_SIZE(fld);
		/* Align field */
		align = ALIGNMENT_OF_SIZE(fsize);
		offset = ((offset + align - 1) / align) * align;
		FIELD_OFFSET(fld) = offset;
		offset += fsize;
	}

	CLASS_FSIZE(class) = offset;

	/* recall old offset */
	offset = oldoffset;

	/* Now that we know how big that object is going to be, create
	 * a bitmap to help the gc scan the object.  The first part is
	 * inherited from the superclass.
	 */
	map = BITMAP_NEW(CLASS_FSIZE(class)/ALIGNMENTOF_VOIDP);
	if (offset > 0) {
		nbits = offset/ALIGNMENTOF_VOIDP;
		BITMAP_COPY(map, class->gc_layout, nbits);
	} else {
		/* The walkObject routine marks the class object explicitly.
		 * We assume that the header does not contain anything ELSE
		 * that must be marked.  
		 */
		offset = sizeof(Hjava_lang_Object);
		nbits = offset/ALIGNMENTOF_VOIDP;
	}
	class->gc_layout = map;

#if 0
	/* Find and align start of object */
	if (oldoffset == 0) {
		offset = ((offset + maxalign - 1) / maxalign) * maxalign;
	}
#endif
	nbits = offset/ALIGNMENTOF_VOIDP;

DBG(GCPRECISE, 
	dprintf("GCLayout for %s:\n", class->name->data);	
    )

	/* Now work out the gc layout */
	fld = CLASS_IFIELDS(class);
	n = CLASS_NIFIELDS(class);
	for (; --n >= 0; fld++) {
		fsize = FIELD_SIZE(fld);
		/* Align field */
		align = ALIGNMENT_OF_SIZE(fsize);
		offset += (align - (offset % align)) % align;
		nbits = offset/ALIGNMENTOF_VOIDP;

		/* paranoia */
		assert(FIELD_OFFSET(fld) == offset);

		/* Set bit if this field is a reference type, except if 
		 * it's a kaffe.util.Ptr (PTRCLASS).  */
		if (!FIELD_RESOLVED(fld)) {
			Utf8Const *sig = (Utf8Const*)FIELD_TYPE(fld);
			if ((sig->data[0] == 'L' || sig->data[0] == '[') &&
			    strcmp(sig->data, PTRCLASSSIG)) {
				BITMAP_SET(map, nbits);
			}
		} else {
			if (FIELD_ISREF(fld) && 
			    strcmp(FIELD_TYPE(fld)->name->data, PTRCLASS)) {
				BITMAP_SET(map, nbits);
			}
		}
DBG(GCPRECISE,
		dprintf(" offset=%3d nbits=%2d ", offset, nbits);
		BITMAP_DUMP(map, nbits+1)
		dprintf(" fsize=%3d (%s)\n", fsize, fld->name->data);
    )
		offset += fsize;
	}
}

/*
 * Allocate the space for the static class data.
 */
static
void
allocStaticFields(Hjava_lang_Class* class)
{
	int fsize;
	int align;
	uint8* mem;
	int offset;
	int n;
	Field* fld;

	/* No static fields */
	if (CLASS_NSFIELDS(class) == 0) {
		return;
	}

	/* Calculate size and position of static data */
	offset = 0;
	n = CLASS_NSFIELDS(class);
	fld = CLASS_SFIELDS(class);
	for (; --n >= 0; fld++) {
		fsize = FIELD_SIZE(fld);
		/* Align field offset */
		align = ALIGNMENT_OF_SIZE(fsize);
		offset = ((offset + align - 1) / align) * align;
		FIELD_SIZE(fld) = offset;
		offset += fsize;
	}

	/* Allocate memory required */
	mem = gc_malloc(offset, GC_ALLOC_STATICDATA);
	CLASS_STATICDATA(class) = mem;

	/* Rewalk the fields, pointing them at the relevant memory and/or
	 * setting any constant values.
	 */
	fld = CLASS_SFIELDS(class);
	n = CLASS_NSFIELDS(class);
	for (; --n >= 0; fld++) {
		offset = FIELD_SIZE(fld);
		FIELD_SIZE(fld) = FIELD_CONSTIDX(fld);	/* Keep idx in size */
		FIELD_ADDRESS(fld) = mem + offset;
	}
}

static
void
resolveStaticFields(Hjava_lang_Class* class)
{
	uint8* mem;
	constants* pool;
	Utf8Const* utf8;
	Field* fld;
	int idx;
	int n;

	/* No locking here, assume class is already locked. */
	pool = CLASS_CONSTANTS(class);
	fld = CLASS_SFIELDS(class);
	n = CLASS_NSFIELDS(class);
	for (; --n >= 0; fld++) {
		if ((fld->accflags & FIELD_CONSTANT_VALUE) != 0) {

			mem = FIELD_ADDRESS(fld);
			idx = FIELD_SIZE(fld);

			switch (CONST_TAG(idx, pool)) {
			case CONSTANT_Integer:
				if (FIELD_TYPE(fld) == _Jv_booleanClass ||
				    FIELD_TYPE(fld) == _Jv_byteClass) {
					*(jbyte*)mem = CLASS_CONST_INT(class, idx);
					FIELD_SIZE(fld) = TYPE_PRIM_SIZE(_Jv_byteClass);
				}
				else if (FIELD_TYPE(fld) == _Jv_charClass ||
					 FIELD_TYPE(fld) == _Jv_shortClass) {
					*(jshort*)mem = CLASS_CONST_INT(class, idx);
					FIELD_SIZE(fld) = TYPE_PRIM_SIZE(_Jv_shortClass);
				}
				else {
					*(jint*)mem = CLASS_CONST_INT(class, idx);
					FIELD_SIZE(fld) = TYPE_PRIM_SIZE(_Jv_intClass);
				}
				break;

			case CONSTANT_Float:
				*(jint*)mem = CLASS_CONST_INT(class, idx);
				FIELD_SIZE(fld) = TYPE_PRIM_SIZE(_Jv_floatClass);
				break;

			case CONSTANT_Long:
			case CONSTANT_Double:
				*(jlong*)mem = CLASS_CONST_LONG(class, idx);
				FIELD_SIZE(fld) = TYPE_PRIM_SIZE(_Jv_longClass);
				break;

			case CONSTANT_String:
				utf8 = WORD2UTF(pool->data[idx]);
				pool->data[idx] = (ConstSlot)utf8Const2Java(utf8);
				pool->tags[idx] = CONSTANT_ResolvedString;
				utf8ConstRelease(utf8);
				/* ... fall through ... */
			case CONSTANT_ResolvedString:
				*(jref*)mem = (jref)CLASS_CONST_DATA(class, idx);
				FIELD_SIZE(fld) = PTR_TYPE_SIZE;
				break;
			}
		}
	}
}

static
void
buildDispatchTable(Hjava_lang_Class* class)
{
	Method* meth;
	void** mtab;
	int i;
	int j;
#if defined(TRANSLATOR)
	int ntramps = 0;
	methodTrampoline* tramp;
#endif

	if (class->superclass != NULL) {
		class->msize = class->superclass->msize;
	}
	else {
		class->msize = 0;
	}

	meth = CLASS_METHODS(class);
	i = CLASS_NMETHODS(class);
	for (; --i >= 0; meth++) {
		Hjava_lang_Class* super = class->superclass;
#if defined(TRANSLATOR)
		if (METHOD_NEEDS_TRAMPOLINE(meth)) {
			ntramps++;
		}
#endif
		if ((meth->accflags & ACC_STATIC)
		    || utf8ConstEqual(meth->name, constructor_name)) {
			meth->idx = -1;
			continue;
		}
		/* Search superclasses for equivalent method name.
		 * If found extract its index nr.
		 */
		for (; super != NULL;  super = super->superclass) {
			int j = CLASS_NMETHODS(super);
			Method* mt = CLASS_METHODS(super);
			for (; --j >= 0;  ++mt) {
				if (utf8ConstEqual (mt->name, meth->name)
				    && utf8ConstEqual (mt->signature,
							meth->signature)) {
					meth->idx = mt->idx;
					goto foundmatch;
				}
			}
		}
		/* No match found so allocate a new index number */
		meth->idx = class->msize++;
		foundmatch:;
	}

#if defined(TRANSLATOR)
	/* Allocate the dispatch table and this class' trampolines all in
	   one block of memory.  This is primarily done for GC reasons in
	   that I didn't want to add another slot on class just for holding
	   the trampolines, but it also works out for space reasons.  */
	class->dtable = (dispatchTable*)gc_malloc(sizeof(dispatchTable) + class->msize * sizeof(void*) + ntramps * sizeof(methodTrampoline), GC_ALLOC_DISPATCHTABLE);
	tramp = (methodTrampoline*) &class->dtable->method[class->msize];
#else
	class->dtable = (dispatchTable*)gc_malloc(sizeof(dispatchTable) + (class->msize * sizeof(void*)), GC_ALLOC_DISPATCHTABLE);
#endif

	class->dtable->class = class;
	mtab = class->dtable->method;

	/* Install inherited methods into dispatch table. */
	if (class->superclass != NULL) {
		Method** super_mtab = (Method**)class->superclass->dtable->method;
		for (i = 0; i < class->superclass->msize; i++) {
			mtab[i] = super_mtab[i];
		}
	}

	meth = CLASS_METHODS(class);
	i = CLASS_NMETHODS(class);
#if defined(TRANSLATOR)
	for (; --i >= 0; meth++) {
		if (METHOD_NEEDS_TRAMPOLINE(meth)) {
#if 0
			if (METHOD_TRANSLATED(meth)) {
				SET_METHOD_PRE_COMPILED(meth, 1);
				METHOD_TRUE_NCODE(meth) = METHOD_NATIVECODE(meth);
			}
#endif
			FILL_IN_TRAMPOLINE(tramp, meth);
			METHOD_NATIVECODE(meth) = (nativecode*)tramp;
			tramp++;
		}
		if (meth->idx >= 0) {
			mtab[meth->idx] = METHOD_NATIVECODE(meth);
		}
	}
	FLUSH_DCACHE(class->dtable, tramp);
#else
	for (;  --i >= 0; meth++) {
		if (meth->idx >= 0) {
			mtab[meth->idx] = meth;
		}
	}
#endif
	/* Construct two tables:
	 * This first table maps interfaces to indices in itable.
	 * The itable maps the indices for each interface method to the
	 * index in the dispatch table that corresponds to this interface
	 * method.  If a class does not implement an interface it declared,
	 * or if it attempts to implement it with improper methods, the second 
	 * table will have a -1 index.  This will cause a NoSuchMethodError
	 * to be thrown later, in soft_lookupmethod.
	 */
	/* don't bother if we don't implement any interfaces */
	if (class->total_interface_len == 0) {
		return;
	}

	class->if2itable = KMALLOC(class->total_interface_len * sizeof(short));

	/* first count how many indices we need */
	j = 0;
	for (i = 0; i < class->total_interface_len; i++) {
		class->if2itable[i] = j;
		j += class->interfaces[i]->msize;
	}
	if (j == 0) {	/* this means only pseudo interfaces without methods
			 * are implemented, such as Serializable or Cloneable
			 */
		return;
	}
	class->itable2dtable = KMALLOC(j * sizeof(short));
	j = 0;
	for (i = 0; i < class->total_interface_len; i++) {
		int nm = CLASS_NMETHODS(class->interfaces[i]);
		Method *meth = CLASS_METHODS(class->interfaces[i]);
		for (; nm--; meth++) {
			Hjava_lang_Class* ncl;
			Method *mt = 0;
			int idx = -1;

			/* ignore static methods in interface --- can an
			 * interface have any beside <clinit>?
			 */
			if (meth->accflags & ACC_STATIC) {
				continue;
			}

			/* Search the real method that implements this
			 * interface method by name.
			 */
			for (ncl = class; ncl != NULL;  ncl = ncl->superclass) {
				int k = CLASS_NMETHODS(ncl);
				mt = CLASS_METHODS(ncl);
				for (; --k >= 0;  ++mt) {
					if (utf8ConstEqual (mt->name, 
							     meth->name)
					    && utf8ConstEqual (mt->signature,
							meth->signature)) 
					{
						idx = mt->idx;
						goto found;
					}
				}
			}

		    found:;
			/* idx may be -1 here if 
			 * a) class attempts to implement an interface 
			 *    method with a static method or constructor.
			 * b) class does not implement the interface at all.
			 */
			/* store method table index of real method */
			class->itable2dtable[j++] = idx;
		}
	}
}

/* Check for undefined abstract methods if class is not abstract.
 * See "Java Language Specification" (1996) section 12.3.2. 
 *
 * Returns true if the class is abstract or if no abstract methods were 
 * found, false otherwise.
 */
static
bool
checkForAbstractMethods(Hjava_lang_Class* class, errorInfo *einfo)
{
	int i;
	void **mtab = class->dtable->method;

	if ((class->accflags & ACC_ABSTRACT) == 0) {
		for (i = class->msize - 1; i >= 0; i--) {
			if (mtab[i] == NULL) {
				SET_LANG_EXCEPTION(einfo, AbstractMethodError)
				return (false);
			}
		}
	}
	return (true);
}

static
void
buildInterfaceDispatchTable(Hjava_lang_Class* class)
{
	Method* meth;
	int i;

	meth = CLASS_METHODS(class);
	class->msize = 0;

	/* enumerate indices and store them in meth->idx */
	for (i = 0; i < CLASS_NMETHODS(class); i++, meth++) {
		if (meth->accflags & ACC_STATIC) {
			meth->idx = -1;
#if defined(TRANSLATOR)
			/* Handle <clinit> */
			if (utf8ConstEqual(meth->name, init_name) && 
				METHOD_NEEDS_TRAMPOLINE(meth)) 
			{
				methodTrampoline* tramp = (methodTrampoline*)gc_malloc(sizeof(methodTrampoline), GC_ALLOC_DISPATCHTABLE);
				FILL_IN_TRAMPOLINE(tramp, meth);
				METHOD_NATIVECODE(meth) = (nativecode*)tramp;
				FLUSH_DCACHE(tramp, tramp+1);
			}
#endif
		}
		else {
			meth->idx = class->msize++;
		}
	}
	return;

}

/*
 * convert a CONSTANT_String entry in the constant poool
 * from utf8 to java.lang.String
 */
Hjava_lang_String*
resolveString(constants* pool, int idx)
{
	Utf8Const* utf8;
	Hjava_lang_String* str = 0;
	iLock* lock;

	lock = lockMutex(pool);
	switch (pool->tags[idx]) {
	case CONSTANT_String:
		utf8 = WORD2UTF(pool->data[idx]);
		pool->data[idx] = (ConstSlot)(str = utf8Const2Java(utf8));
		pool->tags[idx] = CONSTANT_ResolvedString;
		utf8ConstRelease(utf8);
		break;

	case CONSTANT_ResolvedString:	/* somebody else resolved it */
		str = (Hjava_lang_String*)pool->data[idx];
		break;

	default:
		assert(!!!"Neither String nor ResolvedString?");
	}
	unlockKnownMutex(lock);
	return (str);
}

#undef EAGER_LOADING
/*
 * Initialise the constants.
 * First we make sure all the constant strings are converted to java strings.
 *
 * This code removed:
 * There seems to be no need to be so eager in loading
 * referenced classes or even resolving strings.
 */
static
bool
resolveConstants(Hjava_lang_Class* class, errorInfo *einfo)
{
	bool success = true;
#ifdef EAGER_LOADING
	iLock* lock;
	int idx;
	constants* pool;
	Utf8Const* utf8;

	lock = lockMutex(class->centry);

	/* Scan constant pool and convert any constant strings into true
	 * java strings.
	 */
	pool = CLASS_CONSTANTS (class);
	for (idx = 0; idx < pool->size; idx++) {
		switch (pool->tags[idx]) {
		case CONSTANT_String:
			utf8 = WORD2UTF(pool->data[idx]);
			pool->data[idx] = (ConstSlot)utf8Const2Java(utf8);
			pool->tags[idx] = CONSTANT_ResolvedString;
			utf8ConstRelease(utf8);
			break;

		case CONSTANT_Class:
			if (getClass(idx, class, einfo) == 0) {
				success = false;
				goto done;
			}
			break;
		}
	}

done:
	unlockKnownMutex(lock);
#endif	/* EAGER_LOADING */
	return (success);
}

/*
 * Lookup an entry for a given (name, loader) pair.
 * Return null if none is found.
 */
classEntry*
lookupClassEntryInternal(Utf8Const* name, Hjava_lang_ClassLoader* loader)
{
	classEntry* entry;

	entry = classEntryPool[utf8ConstHashValue(name) & (CLASSHASHSZ-1)];
	for (; entry != 0; entry = entry->next) {
		if (utf8ConstEqual(name, entry->name) && loader == entry->loader) {
			return (entry);
		}
	}
	return (0);
}

/*
 * Lookup an entry for a given (name, loader) pair.  
 * Create one if none is found.
 */
classEntry*
lookupClassEntry(Utf8Const* name, Hjava_lang_ClassLoader* loader)
{
	classEntry* entry;
	classEntry** entryp;

        if (!staticLockIsInitialized(&classHashLock)) {
		initStaticLock(&classHashLock);
        }

	entry = lookupClassEntryInternal(name, loader);
	if (entry != 0)
		return (entry);

	/* Failed to find class entry - create a new one */
	entry = KMALLOC(sizeof(classEntry));
	entry->name = name;
	entry->loader = loader;
	entry->class = 0;
	entry->next = 0;

	/* Lock the class table and insert entry into it (if not already
	   there) */
        lockStaticMutex(&classHashLock);

	entryp = &classEntryPool[utf8ConstHashValue(name) & (CLASSHASHSZ-1)];
	for (; *entryp != 0; entryp = &(*entryp)->next) {
		if (utf8ConstEqual(name, (*entryp)->name) && loader == (*entryp)->loader) {
			/* Someone else added it - discard ours and return
			   the new one. */
			unlockStaticMutex(&classHashLock);
			KFREE(entry);
			return (*entryp);
		}
	}

	/* Add ours to end of hash */
	*entryp = entry;

	/* 
	 * This reference to the utf8 name will be released if and when this 
	 * class entry is freed in finalizeClassLoader.
	 */
	utf8ConstAddRef(entry->name);

        unlockStaticMutex(&classHashLock);

	return (entry);
}

/*
 * Lookup a named field.
 */
Field*
lookupClassField(Hjava_lang_Class* clp, Utf8Const* name, bool isStatic, errorInfo *einfo)
{
	Field* fptr;
	int n;

	/* Search down class for field name */
	if (isStatic) {
		fptr = CLASS_SFIELDS(clp);
		n = CLASS_NSFIELDS(clp);
	}
	else {
		fptr = CLASS_IFIELDS(clp);
		n = CLASS_NIFIELDS(clp);
	}
	while (--n >= 0) {
		if (utf8ConstEqual (name, fptr->name)) {
			/* Resolve field if necessary */
			if (resolveFieldType(fptr, clp, einfo) == 0) {
				return (NULL);
			}
			return (fptr);
		}
		fptr++;
	}
DBG(RESERROR,
	dprintf("lookupClassField failed %s:%s\n", 
		clp->name->data, name->data);
    )
	SET_LANG_EXCEPTION_MESSAGE(einfo, NoSuchFieldError, name->data)
	return (0);
}

/*
 * Determine the number of arguments and return values from the
 * method signature.
 */
void
countInsAndOuts(const char* str, short* ins, short* outs, char* outtype)
{
	*ins = sizeofSig(&str, false);
	*outtype = str[0];
	*outs = sizeofSig(&str, false);
}

/*
 * Calculate size of data item based on signature.
 */
int
sizeofSig(const char** strp, bool want_wide_refs)
{
	int count;
	int c;

	count = 0;
	for (;;) {
		c = sizeofSigItem(strp, want_wide_refs);
		if (c == -1) {
			return (count);
		}
		count += c;
	}
}

/*
 * Calculate size (in words) of a signature item.
 */
int
sizeofSigItem(const char** strp, bool want_wide_refs)
{
	int count;
	const char* str;

	for (str = *strp; ; str++) {
		switch (*str) {
		case '(':
			continue;
		case 0:
		case ')':
			count = -1;
			break;
		case 'V':
			count = 0;
			break;
		case 'I':
		case 'Z':
		case 'S':
		case 'B':
		case 'C':
		case 'F':
			count = 1;
			break;
		case 'D':
		case 'J':
			count = 2;
			break;
		case '[':
			count = want_wide_refs ? sizeof(void*) / sizeof(int32) : 1;
			arrayofarray:
			str++;
			if (*str == 'L') {
				while (*str != ';') {
					str++;
				}
			}
			else if (*str == '[') {
				goto arrayofarray;
			}
			break;
		case 'L':
			count = want_wide_refs ? sizeof(void*) / sizeof(int32) : 1;
			/* Skip to end of reference */
			while (*str != ';') {
				str++;
			}
			break;
		default:
			count = 0;	/* avoid compiler warning */
			ABORT();
		}

		*strp = str + 1;
		return (count);
	}
}

/*
 * Find (or create) an array class with component type C.
 */
Hjava_lang_Class*
lookupArray(Hjava_lang_Class* c)
{
	Utf8Const *arr_name;
	char sig[CLASSMAXSIG];  /* FIXME! unchecked fixed buffer! */
	classEntry* centry;
	Hjava_lang_Class* arr_class;
	int arr_flags;
	iLock* lock;

	/* If we couldn't resolve the element type, there's no way we can
	 * construct the array type.
	 */
	if (c == 0) {
		return (0);
	}

	/* Build signature for array type */
	if (CLASS_IS_PRIMITIVE (c)) {
		arr_class = CLASS_ARRAY_CACHE(c);
		if (arr_class) {
			return (arr_class);
		}
		sprintf (sig, "[%c", CLASS_PRIM_SIG(c));
	}
	else {
		const char* cname = CLASS_CNAME (c);
		sprintf (sig, cname[0] == '[' ? "[%s" : "[L%s;", cname);
	}
	arr_name = utf8ConstNew(sig, -1);	/* release before returning */
	centry = lookupClassEntry(arr_name, c->loader);

	if (centry->class != 0) {
		goto found;
	}

	/* Lock class entry */
	lock = lockMutex(centry);

	/* Incase someone else did it */
	if (centry->class != 0) {
		unlockKnownMutex(lock);
		goto found;
	}

	arr_class = newClass();
	/* anchor arrays created without classloader */
	if (c->loader == 0) {
		gc_add_ref(arr_class);
	}
	centry->class = arr_class;
	/*
	 * This is what Sun's JDK returns for A[].class.getModifiers();
	 */
	arr_flags = ACC_ABSTRACT | ACC_FINAL | ACC_PUBLIC;
	internalSetupClass(arr_class, arr_name, arr_flags, 0, c->loader);
	arr_class->superclass = ObjectClass;
	buildDispatchTable(arr_class);
	CLASS_ELEMENT_TYPE(arr_class) = c;

	/* Add the interfaces that arrays implement.  Note that addInterface 
	 * will hold on to arr_interfaces, so it must be a static variable.
	 */
	if (arr_interfaces[0] == 0) {
		arr_interfaces[0] = SerialClass;
		arr_interfaces[1] = CloneClass;
	}
	addInterfaces(arr_class, 2, arr_interfaces);

	arr_class->total_interface_len = arr_class->interface_len;
	arr_class->head.dtable = ClassClass->dtable;
	arr_class->state = CSTATE_COMPLETE;
	arr_class->centry = centry;

	unlockKnownMutex(lock);

	found:;
	if (CLASS_IS_PRIMITIVE(c)) {
		CLASS_ARRAY_CACHE(c) = centry->class;
	}

	utf8ConstRelease(arr_name);
	return (centry->class);
}

#if defined(TRANSLATOR)
/*
 * Find method containing pc.
 */
Method*
findMethodFromPC(uintp pc)
{
	classEntry* entry;
	Method* ptr;
	int ipool;
	int imeth;

	for (ipool = CLASSHASHSZ;  --ipool >= 0; ) {
		for (entry = classEntryPool[ipool];  entry != NULL; entry = entry->next) {
			if (entry->class != 0) {
				imeth = CLASS_NMETHODS(entry->class);
				ptr = CLASS_METHODS(entry->class);
				for (; --imeth >= 0;  ptr++) {
					if (pc >= (uintp)METHOD_NATIVECODE(ptr) && pc < (uintp)ptr->c.ncode.ncode_end) {
						return (ptr);
					}
				}
			}
		}
	}
	return (NULL);
}
#endif

/*
 * Remove all entries from the class entry pool that belong to a given
 * class.  Return the number of entries removed.
 * NB: this will go in a sep file dealing with class entries some day.
 */
int
removeClassEntries(Hjava_lang_ClassLoader* loader)
{
	classEntry** entryp;
	classEntry* entry;
	int ipool;
	int totalent = 0;

        lockStaticMutex(&classHashLock);
	for (ipool = CLASSHASHSZ;  --ipool >= 0; ) {
		entryp = &classEntryPool[ipool];
		for (;  *entryp != NULL; entryp = &(*entryp)->next) {
			totalent++;
			entry = *entryp;
			if (entry->loader == loader) {
				if (Kaffe_JavaVMArgs[0].enableVerboseGC > 0 &&
				    entry->class != 0) 
				{
					fprintf(stderr, 
						"<GC: unloading class `%s'>\n",
						entry->name->data);
				}
DBG(CLASSGC,
				dprintf("removing %s l=%p/c=%p\n", 
				    entry->name->data, loader, entry->class);
    )
				/* release reference to name */
				utf8ConstRelease(entry->name);
				(*entryp) = entry->next;
				KFREE(entry);
			}
			/* if this was the last item, break */
			if (*entryp == 0)
				break;
		}
	}
        unlockStaticMutex(&classHashLock);
	return (totalent);
}

/*
 * Finalize a classloader and remove its entries in the class entry pool.
 */
void
finalizeClassLoader(Hjava_lang_ClassLoader* loader)
{
        int totalent;
 
DBG(CLASSGC,
        dprintf("Finalizing classloader @%p\n", loader);
    )
        totalent = removeClassEntries(loader);
   
DBG(CLASSGC,
        dprintf("removed entries from class entry pool: %d\n", totalent);
    )
}
