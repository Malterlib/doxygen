/******************************************************************************
 *
 * Copyright (C) 1997-2015 by Dimitri van Heesch.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License is hereby 
 * granted. No representations are made about the suitability of this software 
 * for any purpose. It is provided "as is" without express or implied warranty.
 * See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
 */

#ifndef ARGUMENTS_H
#define ARGUMENTS_H

#include <qlist.h>
#include <qcstring.h>

class StorageIntf;

/*! \brief This class contains the information about the argument of a
 *         function or template
 *
 */
struct Argument
{
  /*! Construct a new argument. */
  Argument()
    : hide(false)
  {
  }
  /*! Copy an argument (does a deep copy of all strings). */
  Argument(const Argument &a)
  {
    attrib=a.attrib;
    type=a.type;
    name=a.name;
    array=a.array;
    defval=a.defval;
    docs=a.docs;
    typeConstraint=a.typeConstraint;
    hide=a.hide;
  }
  /*! Assignment of an argument (does a deep copy of all strings). */
  Argument &operator=(const Argument &a)
  {
    if (this!=&a)
    {
      attrib=a.attrib;
      type=a.type;
      name=a.name;
      array=a.array;
      defval=a.defval;
      docs=a.docs;
      typeConstraint=a.typeConstraint;
      hide=a.hide;
    }
    return *this;
  }
  /*! return TRUE if this argument is documentation and the argument has a
   *  non empty name.
   */
  bool hasDocumentation() const
  {
    return !name.isEmpty() && !docs.isEmpty();
  }

  bool hasTemplateDocumentation() const 
  { 
    return !docs.isEmpty(); 
  }
  
  bool isHidden() const
  {
    return hide;
  }
  
  QCString attrib;   /*!< Argument's attribute (IDL only) */
  QCString type;     /*!< Argument's type */
  QCString canType;  /*!< Cached value of canonical type (after type resolution). Empty initially. */
  QCString name;     /*!< Argument's name (may be empty) */
  QCString array;    /*!< Argument's array specifier (may be empty) */
  QCString defval;   /*!< Argument's default value (may be empty) */
  QCString docs;     /*!< Argument's documentation (may be empty) */
  QCString typeConstraint;  /*!< Used for Java generics: \<T extends C\> */
  bool hide;
};

enum RefQualifierType
{
  RefQualifierNone,
  RefQualifierLValue,
  RefQualifierRValue
};

/*! \brief This class represents an function or template argument list. 
 *
 *  This class also stores some information about member that is typically
 *  put after the argument list, such as whether the member is const, 
 *  volatile or pure virtual.
 */
class ArgumentList : public QList<Argument> 
{
  public:
    /*! Creates an empty argument list */
    ArgumentList() : QList<Argument>(), 
                     constSpecifier(FALSE),
                     volatileSpecifier(FALSE),
                     pureSpecifier(FALSE),
                     isDeleted(FALSE),
                     refQualifier(RefQualifierNone)
                     { setAutoDelete(TRUE); }
    /*! Destroys the argument list */
   ~ArgumentList() {}
    /*! Makes a deep copy of this object */
    ArgumentList *deepCopy() const;
    /*! Does any argument of this list have documentation? */
    bool hasDocumentation() const;
    /*! Does any argument of this list have template documentation? */
    bool hasTemplateDocumentation() const;
    /*! Are all parameters hidden*/
    bool allHidden() const;
    /*! Does the member modify the state of the class? default: FALSE. */
    bool constSpecifier;
    /*! Is the member volatile? default: FALSE. */
    bool volatileSpecifier;
    /*! Is this a pure virtual member? default: FALSE */
    bool pureSpecifier;
    /*! C++11 style Trailing return type? */
    QCString trailingReturnType;
    /*! method with =delete */
    bool isDeleted;
    /*! C++11 ref qualifier */
    RefQualifierType refQualifier;

    static ArgumentList *unmarshal(StorageIntf *s);
    static void marshal(StorageIntf *s,ArgumentList *argList);
};

typedef QListIterator<Argument> ArgumentListIterator;

#endif
