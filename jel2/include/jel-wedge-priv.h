/*
 * jel-wedge-priv header.  Includes jel_wedge and jel_unwedge API, and defines ImagePath.
 */

#ifndef __JEL_WEDGE_PRIV_H__

#include <vector>

#include "jel-wedge.h"


class ImagePath
{
  static std::vector<ImagePath *> _activeImagePaths;
  static std::vector<ImagePath *> _usedImagePaths;

  static void _cleanUp (std::vector<ImagePath *> imgPaths);

  std::string *_imagePath;
  FILE        *_fp;

 public:

  // Class methods

  static void          save (ImagePath *imgPath);
  static ImagePath    *getRandom ();
  static unsigned int  size ();
  static void          cleanUp ();

  // Instance methods

  ImagePath (const std::string &str);
  ~ImagePath ();

  FILE        *open  ();
  ImagePath   *close ();
  std::string *path ();
};

typedef ImagePath *ImagePtr;


#define __JEL_WEDGE_PRIV_H__
#endif
