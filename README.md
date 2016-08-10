# spine-runtime-binary-c
spine runtime binary for c 
# environment
* cocos2dx 3.10
* spine 3.4.01

# use stage 
* copy 'SkeletonBinary.h' &&　'SkeletonBinary.c' to spine dir
* import to spine.h
`#include <spine/SkeletonBinary.h>`

# error known
* Draw order timeline & SP_ATTACHMENT_MESH  would crash
  
# addition  class  (cash  for spine)
* use  'CSpineObj' class instead of ‘spine::SkeletonAnimation’
