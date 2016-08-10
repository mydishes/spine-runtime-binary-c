#include "SpineObj.h"
#include <spine/extension.h>
using namespace spine;
USING_NS_CC;
struct SSpineData
{
	spAtlas* _atlas;
	spAttachmentLoader* _attachmentLoader;
	spSkeletonData* _skeletonData;
};
static  std::map<size_t, SSpineData> g_SpineCacheMap;

CSpineObj*  CSpineObj::createWithFile(const char* skeletonDataFile, const char* atlasFile, float scale )
{
	CSpineObj * ret = new (std::nothrow) CSpineObj(skeletonDataFile, atlasFile, scale);
	 if (ret)
	 {
		 ret->autorelease();
	 }
	 else
	 {
		 CC_SAFE_DELETE(ret);
	 }
	 return ret;
}
spSkeletonData*  CSpineObj::cacheData(const char* skeletonDataFile, const char* atlasFile, float scale)
{
	std::string name_str = skeletonDataFile;
	name_str += atlasFile;
	name_str += scale;
	std::hash<std::string> hash_fn;
	size_t hash_code = hash_fn(name_str);
	if (g_SpineCacheMap.find(hash_code) != g_SpineCacheMap.end())
	{
		return g_SpineCacheMap[hash_code]._skeletonData;
	}
	SSpineData data;
	data._atlas = spAtlas_createFromFile(atlasFile, 0);
	CCASSERT(data._atlas, "Error reading atlas file.");
	data._attachmentLoader = SUPER(Cocos2dAttachmentLoader_create(data._atlas));


	std::string  name = skeletonDataFile;
	std::string str_ext = name.substr(name.length() - 5, 5);
	if (str_ext.compare(".skel") == 0)
	{
		spSkeletonBinary* binary = spSkeletonBinary_createWithLoader(data._attachmentLoader);
		binary->scale = scale;
		data._skeletonData = spSkeletonBinary_readSkeletonDataFile(binary, name.c_str());
	}
	else
	{
		spSkeletonJson* json = spSkeletonJson_createWithLoader(data._attachmentLoader);
		json->scale = scale;
		data._skeletonData = spSkeletonJson_readSkeletonDataFile(json, skeletonDataFile);

	}
	g_SpineCacheMap[hash_code] = data;
	return data._skeletonData;
}

void CSpineObj::resetData()
{
	auto it = g_SpineCacheMap.begin();
	while (it != g_SpineCacheMap.end())
	{
		SSpineData data = it->second;
		if (data._skeletonData)
		{
			spSkeletonData_dispose(data._skeletonData);
		}
		if (data._atlas)
		{
			spAtlas_dispose(data._atlas);
		}
		if (data._attachmentLoader)
		{
			spAttachmentLoader_dispose(data._attachmentLoader);
		}
		++it;
	}
	g_SpineCacheMap.clear();
}


CSpineObj::CSpineObj(const char* skeletonDataFile, const char* atlasFile, float scale) 
:spine::SkeletonAnimation(cacheData(skeletonDataFile, atlasFile, scale), false)
{
}
