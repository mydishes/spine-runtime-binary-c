///******************************************************************
// FILENAME:	SpineObj.h
// AUTHOR  :	Steaphan
// VERSION :	1.0.0
// DESCRIBE:	spine缓存等扩展应用
// GitHub  :    https://github.com/mydishes/spine-runtime-binary-c
//******************************************************************
#ifndef _SpineObj_H__
#define _SpineObj_H__ 
#include "cocos2d.h" 
#include <spine/spine-cocos2dx.h>
USING_NS_CC;

class CSpineObj : public spine::SkeletonAnimation
{
public:
	static CSpineObj*  createWithFile(const char* skeletonDataFile, const char* atlasFile,  float scale = 1 );

	static spSkeletonData*  cacheData(const char* skeletonDataFile, const char* atlasFile, float scale);

	static void resetData();


protected:

	CSpineObj(const char* skeletonDataFile, const char* atlasFile,  float scale);

};

#endif // _SpineObj_H__
