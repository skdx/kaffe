/*
 * Java core library component.
 *
 * Copyright (c) 1997, 1998, 2001
 *      Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file.
 *
 * Checked Since: JDK 1.3
 */

package java.lang.reflect;

public class Modifier {

  public static final int PUBLIC = 0x0001;
  public static final int PRIVATE = 0x0002;
  public static final int PROTECTED = 0x0004;
  public static final int STATIC = 0x0008;
  public static final int FINAL = 0x0010;
  public static final int SYNCHRONIZED = 0x0020;

  /**
   * Super - treat invokespecial as polymorphic so that super.foo() works
   * according to the JLS. This is a reuse of the synchronized constant
   * to patch a hole in JDK 1.0. *shudder*.
   */
  static final int SUPER = 0x0020;

  public static final int VOLATILE = 0x0040;
  public static final int TRANSIENT = 0x0080;
  public static final int NATIVE = 0x0100;
  public static final int INTERFACE = 0x0200;
  public static final int ABSTRACT = 0x0400;
  public static final int STRICT = 0x0800;

  public Modifier()
  {
	// ???
  }

  public static boolean isPublic(int mod)
  {
    return ((mod & PUBLIC) == 0 ? false : true);
  }

  public static boolean isPrivate(int mod)
  {
    return ((mod & PRIVATE) == 0 ? false : true);
  }

  public static boolean isProtected(int mod)
  {
    return ((mod & PROTECTED) == 0 ? false : true);
  }

  public static boolean isStatic(int mod)
  {
    return ((mod & STATIC) == 0 ? false : true);
  }

  public static boolean isFinal(int mod)
  {
    return ((mod & FINAL) == 0 ? false : true);
  }

  public static boolean isSynchronized(int mod)
  {
    return ((mod & SYNCHRONIZED) == 0 ? false : true);
  }

  public static boolean isVolatile(int mod)
  {
    return ((mod & VOLATILE) == 0 ? false : true);
  }

  public static boolean isTransient(int mod)
  {
    return ((mod & TRANSIENT) == 0 ? false : true);
  }

  public static boolean isNative(int mod)
  {
    return ((mod & NATIVE) == 0 ? false : true);
  }

  public static boolean isInterface(int mod)
  {
    return ((mod & INTERFACE) == 0 ? false : true);
  }

  public static boolean isAbstract(int mod)
  {
    return ((mod & ABSTRACT) == 0 ? false : true);
  }

  public static boolean isStrict(int mod)
  {
    return ((mod & STRICT) == 0 ? false : true);
  }

  public static String toString(int mod)
  {
    StringBuffer str = new StringBuffer();

    if ((mod & PUBLIC) != 0) {
      append(str, "public");
    }
    if ((mod & PROTECTED) != 0) {
      append(str, "protected");
    }
    if ((mod & PRIVATE) != 0) {
      append(str, "private");
    }

    if ((mod & ABSTRACT) != 0) {
      append(str, "abstract");
    }
    if ((mod & STATIC) != 0) {
      append(str, "static");
    }
    if ((mod & FINAL) != 0) {
      append(str, "final");
    }
    if ((mod & TRANSIENT) != 0) {
      append(str, "transient");
    }
    if ((mod & VOLATILE) != 0) {
      append(str, "volatile");
    }
    if ((mod & SYNCHRONIZED) != 0) {
      append(str, "synchronized");
    }
    if ((mod & NATIVE) != 0) {
      append(str, "native");
    }

    if ((mod & STRICT) != 0) {
      append(str, "strictfp");
    }
    if ((mod & INTERFACE) != 0) {
      append(str, "interface");
    }
    return (new String(str));
  }

  private static void append(StringBuffer buf, String str)
  {
    if (buf.length() != 0) {
      buf.append(" ");
    }
    buf.append(str);
  }

}
