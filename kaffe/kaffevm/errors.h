/*
 * errors.h
 * Error return codes.
 *
 * Copyright (c) 1996, 1997
 *	Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution 
 * of this file. 
 */

#ifndef __errors_h
#define __errors_h

/*
 * This struct serves to keep information about an exception
 * that will be thrown later if some operation failed.
 */
typedef struct _errorInfo {
	char *classname;		/* name of exception class */
	void *mess;			/* accompanying data, usually msg */
} errorInfo;

#define SET_LANG_EXCEPTION(ep, NAME) { \
  (ep)->classname = "java.lang." #NAME; \
  (ep)->mess = ""; }

#define SET_LANG_EXCEPTION_MESSAGE(ep, NAME, MESS) { \
  (ep)->classname = "java.lang." #NAME; \
  (ep)->mess = (void*)MESS; }

#define SET_IO_EXCEPTION_MESSAGE(ep, NAME, MESS) { \
  (ep)->classname = "java.io." #NAME; \
  (ep)->mess = (void*)MESS; }

#define NEW_LANG_EXCEPTION(NAME) \
  (struct Hjava_lang_Throwable*)execute_java_constructor("java.lang." #NAME, \
	0, "()V")

#define NEW_LANG_EXCEPTION_MESSAGE(NAME, MESS) \
  (struct Hjava_lang_Throwable*)execute_java_constructor("java.lang." #NAME, \
	0, "(Ljava/lang/String;)V", stringC2Java(MESS))

#define NEW_IO_EXCEPTION(NAME) \
  (struct Hjava_lang_Throwable*)execute_java_constructor("java.io." #NAME, \
	0, "()V")

#define NEW_IO_EXCEPTION_MESSAGE(NAME, MESS) \
  (struct Hjava_lang_Throwable*)execute_java_constructor("java.io." #NAME, \
	0, "(Ljava/lang/String;)V", stringC2Java(MESS))

#define NoSuchMethodError(M) NEW_LANG_EXCEPTION_MESSAGE(NoSuchMethodError, M)
#define ClassFormatError NEW_LANG_EXCEPTION(ClassFormatError)
#define LinkageError NEW_LANG_EXCEPTION(LinkageError)
#define ClassNotFoundException(M) NEW_LANG_EXCEPTION_MESSAGE(ClassNotFoundException, M)
#define NoSuchFieldError(M) NEW_LANG_EXCEPTION_MESSAGE(NoSuchFieldError, M)
#define OutOfMemoryError NEW_LANG_EXCEPTION(OutOfMemoryError)
#define UnsatisfiedLinkError NEW_LANG_EXCEPTION(UnsatisfiedLinkError)
#define VirtualMachineError NEW_LANG_EXCEPTION(VirtualMachineError)
#define ClassCircularityError NEW_LANG_EXCEPTION(ClassCircularityError)
#define IncompatibleClassChangeError NEW_LANG_EXCEPTION(IncompatibleClassChangeError)
#define IllegalAccessError NEW_LANG_EXCEPTION(IllegalAccessError)
#define NegativeArraySizeException NEW_LANG_EXCEPTION(NegativeArraySizeException)
#define ClassCastException(M) NEW_LANG_EXCEPTION_MESSAGE(ClassCastException, M)
#define IllegalMonitorStateException NEW_LANG_EXCEPTION(IllegalMonitorStateException)
#define NullPointerException NEW_LANG_EXCEPTION(NullPointerException)
#define ArrayIndexOutOfBoundsException NEW_LANG_EXCEPTION(ArrayIndexOutOfBoundsException)
#define ArrayStoreException NEW_LANG_EXCEPTION(ArrayStoreException)
#define ArithmeticException NEW_LANG_EXCEPTION(ArithmeticException)
#define AbstractMethodError NEW_LANG_EXCEPTION(AbstractMethodError)
#define VerifyError NEW_LANG_EXCEPTION(VerifyError)
#define ThreadDeath NEW_LANG_EXCEPTION(ThreadDeath)
#define StackOverflowError NEW_LANG_EXCEPTION(StackOverflowError)
#define IllegalThreadStateException NEW_LANG_EXCEPTION(IllegalThreadStateException)
#define	InstantiationException(M) NEW_LANG_EXCEPTION_MESSAGE(InstantiationException, M)
#define	IOException(M) NEW_IO_EXCEPTION_MESSAGE(IOException, M)

#endif
