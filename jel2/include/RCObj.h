/*
 * RCObj header.  Implements reference counted objects.
 * http://www.swig.org/Doc1.3/Python.html#Python_nn20
 */

#ifndef __RCOBJ_H__

class RCObj
{
  int __ref_count;

  // implement the ref counting mechanism
  int _add_ref ();
  int _del_ref ();
  int _ref_count ();

public:
  RCObj () : __ref_count (1) {}
  virtual ~RCObj () = 0;

  int ref ();
  int unref ();
};

#define __RCOBJ_H__
#endif /* !def __RCOBJ_H__ */
