#include "arguments.h"
#include "marshal.h"
#include <assert.h>

/*! the argument list is documented if one of its
 *  arguments is documented 
 */
bool ArgumentList::hasDocumentation() const
{
  ArgumentListIterator ali(*this);
  Argument *a;
  for (ali.toFirst();(a=ali.current());++ali)
  {
    if (a->hasDocumentation())
      return true;
  }
  return false;
}

bool ArgumentList::hasTemplateDocumentation() const
{
  ArgumentListIterator ali(*this);
  Argument *a;
  for (ali.toFirst();(a=ali.current());++ali)
  {
    if (a->hasTemplateDocumentation())
      return true;
  }
  return false;
}

bool ArgumentList::allHidden() const
{
  bool hasDocs=FALSE;
  ArgumentListIterator ali(*this);
  Argument *a;
  for (ali.toFirst();(a=ali.current());++ali)
  {
    if (!a->isHidden())
      return false;
  }
  return true;
}

ArgumentList *ArgumentList::deepCopy() const
{
  ArgumentList *argList = new ArgumentList;
  argList->setAutoDelete(TRUE);

  QListIterator<Argument> ali(*this);
  Argument *a;
  for (;(a=ali.current());++ali)
  {
    argList->append(new Argument(*a));
  }
  argList->constSpecifier     = constSpecifier;
  argList->volatileSpecifier  = volatileSpecifier;
  argList->pureSpecifier      = pureSpecifier;
  argList->trailingReturnType = trailingReturnType;
  argList->isDeleted          = isDeleted;
  argList->refQualifier       = refQualifier;

  return argList;
}

ArgumentList *ArgumentList::unmarshal(StorageIntf *s)
{
  uint i;
  uint count = unmarshalUInt(s);
  if (count==NULL_LIST) return 0; // null list
  ArgumentList *result = new ArgumentList;
  assert(count<1000000);
  //printf("unmarshalArgumentList: %d\n",count);
  for (i=0;i<count;i++)
  {
    Argument *a = new Argument;
    a->attrib  = unmarshalQCString(s);
    a->type    = unmarshalQCString(s);
    a->canType = unmarshalQCString(s);
    a->name    = unmarshalQCString(s);
    a->array   = unmarshalQCString(s);
    a->defval  = unmarshalQCString(s);
    a->docs    = unmarshalQCString(s);
    a->typeConstraint = unmarshalQCString(s);
    a->hide    = unmarshalBool(s);
    result->append(a);
  }
  result->constSpecifier     = unmarshalBool(s);
  result->volatileSpecifier  = unmarshalBool(s);
  result->pureSpecifier      = unmarshalBool(s);
  result->trailingReturnType = unmarshalQCString(s);
  result->isDeleted          = unmarshalBool(s);
  result->refQualifier       = (RefQualifierType)unmarshalInt(s);
  return result;
}

void ArgumentList::marshal(StorageIntf *s,ArgumentList *argList)
{
  if (argList==0)
  {
    marshalUInt(s,NULL_LIST); // null pointer representation
  }
  else
  {
    marshalUInt(s,argList->count());
    if (argList->count()>0)
    {
      ArgumentListIterator ali(*argList);
      Argument *a;
      for (ali.toFirst();(a=ali.current());++ali)
      {
        marshalQCString(s,a->attrib);
        marshalQCString(s,a->type);
        marshalQCString(s,a->canType);
        marshalQCString(s,a->name);
        marshalQCString(s,a->array);
        marshalQCString(s,a->defval);
        marshalQCString(s,a->docs);
        marshalQCString(s,a->typeConstraint);
        marshalBool(s,a->hide);
      }
    }
    marshalBool(s,argList->constSpecifier);
    marshalBool(s,argList->volatileSpecifier);
    marshalBool(s,argList->pureSpecifier);
    marshalQCString(s,argList->trailingReturnType);
    marshalBool(s,argList->isDeleted);
    marshalInt(s,(int)argList->refQualifier);
  }
}

