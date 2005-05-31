/* DynamicImplementation.java --
   Copyright (C) 2005 Free Software Foundation, Inc.

This file is part of GNU Classpath.

GNU Classpath is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Classpath is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Classpath; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
02111-1307 USA.

Linking this library statically or dynamically with other modules is
making a combined work based on this library.  Thus, the terms and
conditions of the GNU General Public License cover the whole
combination.

As a special exception, the copyright holders of this library give you
permission to link this library with independent modules to produce an
executable, regardless of the license terms of these independent
modules, and to copy and distribute the resulting executable under
terms of your choice, provided that you also meet, for each linked
independent module, the terms and conditions of the license of that
module.  An independent module is a module which is not derived from
or based on this library.  If you modify this library, you may extend
this exception to your version of the library, but you are not
obligated to do so.  If you do not wish to do so, delete this
exception statement from your version. */


package org.omg.CORBA;

import org.omg.CORBA.portable.ObjectImpl;

/**
 * This class was probably originally thinked as a base of all CORBA
 * object implementations. However the code, generated by IDL to
 * java compilers almost never use it, preferring to derive the
 * object implementation bases directly from the {@link ObjectImpl}.
 * The class has become deprecated since the 1.4 release.
 *
 * @deprecated since 1.4.
 *
 * @author Audrius Meskauskas, Lithuania (AudriusA@Bioinformatics.org)
 */
public abstract class DynamicImplementation
  extends ObjectImpl
{
  /**
   * Invoke the method of the CORBA object.
   *
   * @deprecated since 1.4.
   *
   * @param request the container for both passing and returing the
   * parameters, also contains the method name and thrown exceptions.
   */
  public abstract void invoke(ServerRequest request);

  /**
   * Returns the array of the repository ids, supported by this object.
   * In this implementation, the method must be overrridden to return
   * a sendible object-specific information. The default method returns
   * an empty array.
   *
   * @deprecated since 1.4.
   *
   * @return the empty array, always.
   */
  public String[] _ids()
  {
    return new String[ 0 ];
  }
}