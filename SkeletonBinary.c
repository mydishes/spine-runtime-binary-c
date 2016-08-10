//
// $id: SkeletonBinary.c semgilo $
//

#include "SkeletonBinary.h"
#include "spine/extension.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
typedef enum {
	SP_CURVE_LINEAR,
	SP_CURVE_STEPPED,
	SP_CURVE_BEZIER,
} spCurveType;

typedef struct {
	const char* parent;
	const char* skin;
	int slotIndex;
	spMeshAttachment* mesh;
} _spLinkedMesh;

typedef struct {
	spSkeletonBinary super;
	int ownsLoader;

	int linkedMeshCount;
	int linkedMeshCapacity;
	_spLinkedMesh* linkedMeshes;
} _spSkeletonBinary;

#define BONE_ROTATE		0
#define BONE_TRANSLATE  1
#define BONE_SCALE		2
#define BONE_SHEAR		3

#define SLOT_ATTACHMENT	0
#define SLOT_COLOR		1

#define PATH_POSITION	0
#define PATH_SPACING	1
#define PATH_MIX		2

#define CURVE_LINEAR	0
#define CURVE_STEPPED	1
#define CURVE_BEZIER	2


spSkeletonBinary* spSkeletonBinary_createWithLoader(spAttachmentLoader* attachmentLoader) {
	spSkeletonBinary* self = SUPER(NEW(_spSkeletonBinary));
	self->scale = 1;
	self->attachmentLoader = attachmentLoader;
	return self;
}

spSkeletonBinary* spSkeletonBinary_create(spAtlas* atlas) {
	spAtlasAttachmentLoader* attachmentLoader = spAtlasAttachmentLoader_create(atlas);
	spSkeletonBinary* self = spSkeletonBinary_createWithLoader(SUPER(attachmentLoader));
	SUB_CAST(_spSkeletonBinary, self)->ownsLoader = 1;
	return self;
}

void spSkeletonBinary_dispose(spSkeletonBinary* self) {
	_spSkeletonBinary* internal = SUB_CAST(_spSkeletonBinary, self);
	if (internal->ownsLoader) spAttachmentLoader_dispose(self->attachmentLoader);
	FREE(internal->linkedMeshes);
	FREE(self->error);
	FREE(self);
}
 
static void _spSkeletonBinary_addLinkedMesh(spSkeletonBinary* self, spMeshAttachment* mesh, const char* skin, int slotIndex,
	const char* parent) {
	_spLinkedMesh* linkedMesh;
	_spSkeletonBinary* internal = SUB_CAST(_spSkeletonBinary, self);

	if (internal->linkedMeshCount == internal->linkedMeshCapacity) {
		_spLinkedMesh* linkedMeshes;
		internal->linkedMeshCapacity *= 2;
		if (internal->linkedMeshCapacity < 8) internal->linkedMeshCapacity = 8;
		linkedMeshes = MALLOC(_spLinkedMesh, internal->linkedMeshCapacity);
		memcpy(linkedMeshes, internal->linkedMeshes, sizeof(_spLinkedMesh)* internal->linkedMeshCount);
		FREE(internal->linkedMeshes);
		internal->linkedMeshes = linkedMeshes;
	}

	linkedMesh = internal->linkedMeshes + internal->linkedMeshCount++;
	linkedMesh->mesh = mesh;
	linkedMesh->skin = skin;
	linkedMesh->slotIndex = slotIndex;
	linkedMesh->parent = parent;
}
#ifndef MIN
#define MIN(a, b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define MAX(a, b) (((a)>(b))?(a):(b))
#endif
#define READ() (((int)*self->reader++) & 0xFF)

#define inline __inline

static inline int readBoolean(spSkeletonBinary *self)
{
    int ch = READ();
    return ch != 0;
}

static inline char readChar(spSkeletonBinary *self)
{
    int ch = READ();
    return (char)(ch);
}

static inline int readInt(spSkeletonBinary *self)
{
    int ch1 = READ();
    int ch2 = READ();
    int ch3 = READ();
    int ch4 = READ();
    return ((ch1 << 24) | (ch2 << 16) | (ch3 << 8) | (ch4 << 0));
}

static inline int readIntOptimize(spSkeletonBinary *self,int optimizePositive)
{
	int b = READ();
	int result = (b) & 0x7F;
	if ((b & 0x80) != 0) {
		b = READ();
		result |= ((b & 0x7F) << 7);
		if ((b & 0x80) != 0) {
			b=READ();
			result |= ((b & 0x7F) << 14);
			if ((b & 0x80) != 0) {
				b=READ();
				result |= ((b & 0x7F) << 21);
				if ((b & 0x80) != 0) {
					result |= ((b & 0x7F) << 28);
				}
			}
		}
	}
	return optimizePositive ? result : ((result >> 1) ^ -(result & 1));
//	return  result;
}

static inline float readFloat(spSkeletonBinary *self)
{
    int n = readInt(self);
	return *(float*)&n;
}

static inline const char *readString(spSkeletonBinary *self)
{
    int length, i;
	char* pszContent;

	length = readIntOptimize(self,1);
	
	if ( length == 0 ) return NULL;
	pszContent = MALLOC(char, length);
    for (i = 0; i < length - 1; i ++) {
        pszContent[i] = readChar(self);
    }
    pszContent[length - 1] = '\0';
    
    if (self->cacheIndex >= 1024 * 5) {
        printf("cache is full !!!!!!!");
        return "";
    }
    self->cache[self->cacheIndex ++] = pszContent;
    return pszContent;
}

static inline float *readFloats(spSkeletonBinary *self,int n, float scale)
{
    float *arr;
    int i;
	arr = 0;
    arr = MALLOC(float, n);
    for (i = 0; i < n; i++)
    {
        arr[i] = readFloat(self) * scale;
    }

    return arr;
}



static void _readVertices(spSkeletonBinary* self,  spVertexAttachment* attachment, int verticesLength,float scale) {
	int i, b, w, nn, entrySize;

	entrySize = verticesLength << 1;
	attachment->worldVerticesLength = entrySize;
	if (!readBoolean(self))
	{
		float* vertices = MALLOC(float, entrySize);
		for (i = 0; i < entrySize; i++)
		{
			vertices[i] = readFloat(self) * scale;
		}
		attachment->bonesCount = 0;
		attachment->bones = 0;
		attachment->verticesCount = entrySize;
		attachment->vertices = vertices;
	}
	else
	{
		float * weights = MALLOC(float, entrySize * 3 * 3);
		int * bonesArray = MALLOC(int, entrySize * 3);
		int  ii, weights_idx = 0, bones_idx = 0;
		for (i = 0; i < verticesLength; i++)
		{
			int boneCount = readIntOptimize(self, 1);
			bonesArray[bones_idx++] = boneCount;
			for (ii = 0; ii < boneCount; ii++) {
				bonesArray[bones_idx++] = readIntOptimize(self, 1);
				weights[weights_idx++] = readFloat(self)*scale;
				weights[weights_idx++] = readFloat(self)*scale;
				weights[weights_idx++] = readFloat(self);
			}
			attachment->bonesCount += 1 + boneCount;
			attachment->verticesCount += 3 * boneCount;
		}
		attachment->vertices = weights;
		attachment->bones = bonesArray;
	}
}

static inline int *readShorts(spSkeletonBinary *self,int n)
{
	int *arr;
    int i;
    arr = 0;
	arr = MALLOC(int, n);
    for (i = 0; i < n; i++)
    {
		arr[i] = (READ() << 8) | READ();
      //  arr[i] = readShort(self);
    }

    return arr;
}

static inline void readColor(spSkeletonBinary *self, float *r, float *g, float *b, float *a)
{
    *r = READ() / (float)255;
    *g = READ() / (float)255;
    *b = READ() / (float)255;
    *a = READ() / (float)255;
}

static spAttachment *readAttachment(spSkeletonBinary *self, spSkin *Skin,int slotIndex, const char *attachmentName, int nonessential)
{
    int attachmentType;
    float scale = self->scale;
    const char *name = readString(self); 
    if (name == NULL) name = attachmentName;
    
    
    switch (readChar(self)) {
        case 0:
            attachmentType = SP_ATTACHMENT_REGION;
            break;
        case 1:
            attachmentType = SP_ATTACHMENT_BOUNDING_BOX;
            break;
        case 2:
            attachmentType = SP_ATTACHMENT_MESH;
			break;
		case 3:
			attachmentType = SP_ATTACHMENT_LINKED_MESH;
			break;
		case 4:
			attachmentType = SP_ATTACHMENT_PATH;
			break;
			
        default:
            printf("unknow attachment type : -----------");
            break;
    }

    switch (attachmentType)
    {
        case SP_ATTACHMENT_REGION:
        {
            spAttachment *attachment;
            spRegionAttachment *region;
            const char *path = readString(self);
            if (path == NULL) path = name;

			attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, Skin, attachmentType, name, path);
            region = SUB_CAST(spRegionAttachment, attachment);
            if (path) MALLOC_STR(region->path, path);

			region->rotation = readFloat(self);
            region->x = readFloat(self) * scale;
            region->y = readFloat(self) * scale;
            region->scaleX = readFloat(self);
            region->scaleY = readFloat(self);
            region->width = readFloat(self) * scale;
            region->height = readFloat(self) * scale;
            readColor(self, &region->r, &region->g, &region->b, &region->a);

			spRegionAttachment_updateOffset(region);
			spAttachmentLoader_configureAttachment(self->attachmentLoader, attachment);

            return SUPER_CAST(spAttachment, region);
        }
        case SP_ATTACHMENT_BOUNDING_BOX: 
		{ 
			spAttachment *attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, 
				Skin, attachmentType, attachmentName, NULL);
			spBoundingBoxAttachment *box = SUB_CAST(spBoundingBoxAttachment, attachment);
			int n = readIntOptimize(self, 1);
			_readVertices(self, SUPER(box), n, scale);
			if (nonessential) readInt(self); //int color = nonessential ? ReadInt(input) : 0; // Avoid unused local warning.
			spAttachmentLoader_configureAttachment(self->attachmentLoader, attachment);
            return SUPER_CAST(spAttachment, box);
        }
        case SP_ATTACHMENT_MESH: 
        {
            spAttachment *attachment;
            spMeshAttachment *mesh;
            const char *path = readString(self);
            if (path == NULL) path = name;

			attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, Skin, attachmentType, attachmentName, path);
            mesh = SUB_CAST(spMeshAttachment, attachment);
            if (path) MALLOC_STR(mesh->path, path);

			readColor(self, &mesh->r, &mesh->g, &mesh->b, &mesh->a);
			int n;
			int * bones;
			n = readIntOptimize(self, 1);
			mesh->regionUVs = readFloats(self, 1, n << 1);
			n = readIntOptimize(self, 1);
			mesh->triangles =(unsigned short*) readShorts(self, n);
			mesh->trianglesCount = n;
			n = readIntOptimize(self, 1);
			_readVertices(self, SUPER(mesh), n, scale);
			mesh->hullLength = readIntOptimize(self, 1) << 1;
			if (nonessential) {
				n = readIntOptimize(self, 1);
				mesh->edges = readShorts(self, n);
				mesh->edgesCount = n;
				mesh->width = readFloat(self) * scale;
				mesh->height = readFloat(self) * scale;
			}
			spMeshAttachment_updateUVs(mesh);
			spAttachmentLoader_configureAttachment(self->attachmentLoader, attachment);
			 
            return SUPER_CAST(spAttachment, mesh);
        }
		case SP_ATTACHMENT_LINKED_MESH:
        {
            spAttachment *attachment;
			spMeshAttachment *mesh;
            const char *path = readString(self);
			if (path == NULL) path = name;
			attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, Skin, attachmentType, attachmentName, path);
			mesh = SUB_CAST(spMeshAttachment, attachment);
			if (path) MALLOC_STR(mesh->path, path);
		
			readColor(self, &mesh->r, &mesh->g, &mesh->b, &mesh->a);
			const char *skin_name = readString(self);
			const char *parent_str = readString(self);
			mesh->inheritDeform = readBoolean(self);

			if (nonessential) {
				mesh->width = readFloat(self) * scale;
				mesh->height = readFloat(self) * scale;
			}
			_spSkeletonBinary_addLinkedMesh(self, mesh, skin_name, slotIndex, parent_str);
            return SUPER_CAST(spAttachment, mesh);
		}
		case SP_ATTACHMENT_PATH:
		{
			spAttachment *attachment;
			attachment = spAttachmentLoader_createAttachment(self->attachmentLoader, Skin, attachmentType, attachmentName, name);
			spPathAttachment* path = SUB_CAST(spPathAttachment, attachment);
			 
			path->closed = readBoolean(self);
			path->constantSpeed = readBoolean(self);
			int n, length, bone_count;
			int * bones;
			n = readIntOptimize(self, 1);
			_readVertices(self, SUPER(path), n, scale);
			path->lengths = readFloats(self,n/3,scale);
			if (nonessential)
			{
				readInt(self);
			}
			return SUPER_CAST(spAttachment, path);
		}
    }
    return NULL;
}

static spSkin *readSkin(spSkeletonBinary *self, const char *skinName, int nonessential)
{
    spSkin *skin;
    int i;

    int slotCount = readIntOptimize(self,1);
	if (slotCount ==0)
	{
		return NULL;
	}
    skin = spSkin_create(skinName);
    for (i = 0; i < slotCount; i++)
    {
        int ii, nn;
        int slotIndex = readIntOptimize(self,1);
        for (ii = 0, nn = readIntOptimize(self,1); ii < nn; ii++)
        {
            const char *name = readString(self);
			spSkin_addAttachment(skin, slotIndex, name, readAttachment(self, skin, slotIndex, name, nonessential));
        }
    }

    return skin;
}

static void readCurve(spSkeletonBinary *self, spCurveTimeline *timeline, int frameIndex)
{
    spCurveType type = (spCurveType)readChar(self);
    if (type == SP_CURVE_STEPPED)
    {
        spCurveTimeline_setStepped(timeline, frameIndex);
    }
    else if (type == SP_CURVE_BEZIER)
    {
        float v1 = readFloat(self);
        float v2 = readFloat(self);
        float v3 = readFloat(self);
        float v4 = readFloat(self);
        spCurveTimeline_setCurve(timeline, frameIndex, v1, v2, v3, v4);
    }
} 

static void readAnimation(spSkeletonBinary *self, spSkeletonData *skeletonData, const char *name)
{ 
    int i, ii, n, nn;
    float scale = self->scale;

	int nTempCount = 1024;
	spAnimation *animation = spAnimation_create(name, nTempCount);
    
    animation->timelinesCount = 0;

    // Slot timelines
    n = readIntOptimize(self,1);
    for (i = 0; i < n; i++)
    {
        int slotIndex = readIntOptimize(self,1);
        nn = readIntOptimize(self,1);
        for (ii = 0; ii < nn; ii++)
        {
            int frameIndex;
            int timelineType = readChar(self);
            int framesCount = readIntOptimize(self,1);
            switch (timelineType)
            {
			case SLOT_COLOR:
                {
                    spColorTimeline *timeline = spColorTimeline_create(framesCount);
                    timeline->slotIndex = slotIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        
                        float r = READ() / (float)255;
                        float g = READ() / (float)255;
                        float b = READ() / (float)255;
                        float a = READ() / (float)255;
                        spColorTimeline_setFrame(timeline, frameIndex, time, r, g, b, a);
                        if (frameIndex < framesCount - 1)
                            readCurve(self, SUPER(timeline), frameIndex);
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
					animation->duration = MAX(animation->duration, timeline->frames[(framesCount - 1)* COLOR_ENTRIES]);
                    break;
                }
			case SLOT_ATTACHMENT:
                {
                    spAttachmentTimeline *timeline = spAttachmentTimeline_create(framesCount);
                    timeline->slotIndex = slotIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        spAttachmentTimeline_setFrame(timeline, frameIndex, time, readString(self));
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                    animation->duration = MAX(animation->duration, timeline->frames[framesCount - 1]);
                    break;
                }
            }
        }
    }

    // Bone timelines.
    n = readIntOptimize(self,1);
    for (i = 0; i < n; i++)
    {
        int boneIndex = readIntOptimize(self,1);
        nn = readIntOptimize(self,1);
        for (ii = 0; ii < nn; ii++)
        {
            int frameIndex;
            int timelineType = readChar(self);
            int framesCount = readIntOptimize(self,1);
            switch (timelineType)
            {
			case BONE_ROTATE:
                {
                    spRotateTimeline *timeline = spRotateTimeline_create(framesCount);
                    timeline->boneIndex = boneIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        float angle = readFloat(self);
                        spRotateTimeline_setFrame(timeline, frameIndex, time, angle);
                        if (frameIndex < framesCount - 1)
                            readCurve(self, SUPER(timeline), frameIndex);
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
					animation->duration = MAX(animation->duration, timeline->frames[(framesCount - 1)* ROTATE_ENTRIES]);
                    break;
                }
			case BONE_TRANSLATE:
			case BONE_SCALE:
			case BONE_SHEAR:
                {
                    spTranslateTimeline *timeline;
                    float timelineScale = 1;
					if (timelineType == BONE_SCALE)
                    {
                        timeline = spScaleTimeline_create(framesCount);
                    }
					else if (timelineType == BONE_SHEAR)
					{
						timeline = spShearTimeline_create(framesCount);
					}
                    else
                    {
                        timeline = spTranslateTimeline_create(framesCount);
                        timelineScale = scale;
                    }
                    timeline->boneIndex = boneIndex;
                    for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
                    {
                        float time = readFloat(self);
                        float x = readFloat(self) * timelineScale;
                        float y = readFloat(self) * timelineScale;
                        spTranslateTimeline_setFrame(timeline, frameIndex, time, x, y);
                        if (frameIndex < framesCount - 1)
                            readCurve(self, SUPER(timeline), frameIndex);
                    }
                    animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
                    animation->duration = MAX(animation->duration, timeline->frames[(framesCount-1)* TRANSLATE_ENTRIES ]);
                    break;
                }
                default:
                {
                    printf("unknow timelineType :%d", timelineType);
                }
                 break;
            }
        }
    }
    
    //ik timelines
    n = readIntOptimize(self,1);
    for (i = 0; i < n; i++)
    {
        int frameIndex;
        int index = readIntOptimize(self,1);
        int framesCount = readIntOptimize(self,1);
        spIkConstraintTimeline *timeline = spIkConstraintTimeline_create(framesCount);
        timeline->ikConstraintIndex = index;
        for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
        {
			spIkConstraintTimeline_setFrame(timeline, frameIndex, readFloat(self), readFloat(self), READ() == 1 ? 1 : -1);
            if (frameIndex < framesCount - 1)
                readCurve(self, SUPER(timeline), frameIndex);
        }
        animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
		animation->duration = MAX(animation->duration, timeline->frames[(framesCount * 1)* IKCONSTRAINT_ENTRIES]);
    }

	// Transform constraint timelines.
	n = readIntOptimize(self, 1);
	for (i = 0; i < n; i++)
	{
		int frameIndex;
		int index = readIntOptimize(self, 1);
		int framesCount = readIntOptimize(self, 1);
		spTransformConstraintTimeline *timeline = spTransformConstraintTimeline_create(framesCount);
		timeline->transformConstraintIndex = index;
		for (frameIndex = 0; frameIndex < framesCount; frameIndex++)
		{
			spTransformConstraintTimeline_setFrame(timeline, frameIndex, readFloat(self), readFloat(self), readFloat(self), readFloat(self), readFloat(self));
			if (frameIndex < framesCount - 1)
				readCurve(self, SUPER(timeline), frameIndex);
		}
		animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
		animation->duration = MAX(animation->duration, timeline->frames[(framesCount * 1)* TRANSFORMCONSTRAINT_ENTRIES]);
	}


	// Path constraint timelines.
	n = readIntOptimize(self, 1);
	for (i = 0; i < n; i++)
	{
		int index = readIntOptimize(self, 1);
		spPathConstraintData* data = skeletonData->pathConstraints[index];
		int ii_n = readIntOptimize(self, 1);
		int ii, nn;
		for ( ii = 0, nn = ii_n; ii < nn; ii++) {
			spTimelineType timelineType = (spTimelineType)readChar(self);
			int frameCount = readIntOptimize(self, 1);
			switch (timelineType) {
			case PATH_POSITION:
			case PATH_SPACING: {
							spPathConstraintPositionTimeline* timeline;
								   float timelineScale = 1;
								   if (timelineType == PATH_SPACING) {
									   timeline = (spPathConstraintPositionTimeline*)spPathConstraintSpacingTimeline_create(frameCount);
									   if (data->spacingMode == SP_SPACING_MODE_LENGTH || data->spacingMode == SP_SPACING_MODE_FIXED) timelineScale = scale;
								   }
								   else {
									   timeline = spPathConstraintPositionTimeline_create(frameCount);
									   if (data->spacingMode == SP_SPACING_MODE_FIXED) timelineScale = scale;
								   }
								   timeline->pathConstraintIndex = index;
								   int frameIndex;
								   for ( frameIndex = 0; frameIndex < frameCount; frameIndex++) {
									   spPathConstraintPositionTimeline_setFrame(timeline, frameIndex, readFloat(self), readFloat(self)*timelineScale);
									   if (frameIndex < frameCount - 1)
										   readCurve(self, SUPER(timeline), frameIndex);
								   }
								   animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
								   animation->duration = MAX(animation->duration, timeline->frames[(frameCount * 1)* PATHCONSTRAINTPOSITION_ENTRIES]);
								   break;
			}
			case PATH_MIX: {
							  spPathConstraintMixTimeline* timeline = spPathConstraintMixTimeline_create(frameCount);
							   timeline->pathConstraintIndex = index;
							   int frameIndex;
							   for ( frameIndex = 0; frameIndex < frameCount; frameIndex++) {
								   spPathConstraintMixTimeline_setFrame(timeline, frameIndex, readFloat(self), readFloat(self), readFloat(self));
								   if (frameIndex < frameCount - 1)
									   readCurve(self, SUPER(timeline), frameIndex);
							   }
							   animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
							   animation->duration = MAX(animation->duration, timeline->frames[(frameCount * 1)* PATHCONSTRAINTMIX_ENTRIES]);
							   break;
			}
			}
		}
	}


	// Deform timelines.
	n = readIntOptimize(self, 1);
	for (i = 0; i < n; i++)
	{
		int index = readIntOptimize(self, 1);
		spSkin* skin = skeletonData->skins[index];
		int ii_n = readIntOptimize(self, 1);
		int ii, nn, frameIndex, iii, nnn,vn;
		for ( ii = 0, nn = ii_n; ii < nn; ii++) {
			int slotIndex = readIntOptimize(self, 1);
			for (iii = 0, nnn = readIntOptimize(self, 1); iii < nnn; iii++) {
				spVertexAttachment* attachment = SUB_CAST(spVertexAttachment, spSkin_getAttachment(skin, slotIndex, readString(self)));
				int weighted = attachment->bones !=NULL;
				int deformLength = weighted? attachment->verticesCount / 3 * 2 : attachment->verticesCount;
				float* tempDeform = MALLOC(float, deformLength);
				int frameCount = readIntOptimize(self, 1);

				spDeformTimeline*  timeline = spDeformTimeline_create(frameCount, deformLength);
				timeline->slotIndex = slotIndex;
				timeline->attachment = SUPER(attachment);

				for ( frameIndex = 0; frameIndex < frameCount; frameIndex++) {
					float time = readFloat(self);
					float* deform;
					int end = readIntOptimize(self, 1);
					if (end == 0)
					{
						if (weighted)
						{
							deform = tempDeform;
							memset(deform, 0, sizeof(float)* deformLength);
						}
						else
						{
							deform = attachment->vertices;
						}
					}
					else {
						int v,start = readIntOptimize(self, 1);
						deform = tempDeform;
						memset(deform, 0, sizeof(float)* start);
						end += start;
						if (scale == 1) {
							for ( v = start; v < end; v++)
								deform[v] = readFloat(self);
						}
						else {
							for ( v = start; v < end; v++)
								deform[v] = readFloat(self) * scale;
						}
						memset(deform + v, 0, sizeof(float)* (deformLength - v));
						if (!weighted) {
							for ( v = 0, vn = deformLength; v < vn; v++)
								deform[v] += attachment->vertices[v];
						}
					}
					spDeformTimeline_setFrame(timeline, frameIndex, time, deform);

					if (frameIndex < frameCount - 1)
						readCurve(self, SUPER(timeline), frameIndex);
				}
				FREE(tempDeform);

				animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
				animation->duration = MAX(animation->duration, timeline->frames[(frameCount * 1)]);
			}
		}
	}
	 
    // Draw order timeline
    n = readIntOptimize(self,1);
    if (n > 0)
    {
        spDrawOrderTimeline *timeline = spDrawOrderTimeline_create(n, skeletonData->slotsCount);
        int slotCount = skeletonData->slotsCount;
		int i,ii;
		for (i = 0; i < n; i++) {
			int* drawOrder = 0, *unchanged =0;
			float time = readFloat(self);
			int offsetCount = readIntOptimize(self, 1);
			drawOrder = MALLOC(int, slotCount);
			memset(drawOrder, -1, slotCount);
			unchanged = MALLOC(int, slotCount - offsetCount);
			int originalIndex = 0, unchangedIndex = 0;
			for (ii = 0; ii < offsetCount; ii++) {
				int slotIndex = readIntOptimize(self, 1);
				// Collect unchanged items.
				while (originalIndex != slotIndex)
					unchanged[unchangedIndex++] = originalIndex++;
				// Set changed items.
				int amount = readIntOptimize(self, 1);
				drawOrder[originalIndex + amount] = originalIndex++;
			}
			// Collect remaining unchanged items.
			while (originalIndex < slotCount)
				unchanged[unchangedIndex++] = originalIndex++;
			// Fill in unchanged items.
			for ( ii = slotCount - 1; ii >= 0; ii--)
			{
				if (drawOrder[ii] == -1)
					drawOrder[ii] = unchanged[--unchangedIndex];
			}
			FREE(unchanged);
			spDrawOrderTimeline_setFrame(timeline, i, time, drawOrder);
			FREE(drawOrder);
		}
		animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
		animation->duration = MAX(animation->duration, timeline->frames[n - 1]);
    }

    // Event timeline.
    n = readIntOptimize(self,1);
    if (n > 0)
    {
        spEventTimeline *timeline = spEventTimeline_create(n);
        int frameIndex;
        for (frameIndex = 0; frameIndex < n; frameIndex++)
		{
            spEvent *event;
            const char *stringValue;
            float time = readFloat(self);
            spEventData *eventData = skeletonData->events[readIntOptimize(self,1)];
            event = spEvent_create(time,eventData);
            event->intValue = readIntOptimize(self,0);
            event->floatValue = readFloat(self);
            stringValue = readBoolean(self) ? readString(self) : eventData->stringValue;
            if (stringValue) MALLOC_STR(event->stringValue, stringValue);
            spEventTimeline_setFrame(timeline, frameIndex,event);
        }
        animation->timelines[animation->timelinesCount++] = SUPER_CAST(spTimeline, timeline);
        animation->duration = MAX(animation->duration, timeline->frames[n - 1]);
    }
	if (animation->timelinesCount>nTempCount)
	{
		printf("timelinesCount is to small !!!!!!!");

	}
    skeletonData->animations[skeletonData->animationsCount++] = animation;
}
  
spSkeletonData* spSkeletonBinary_readSkeletonData( spSkeletonBinary* self )
{
	int size, i, nonessential;
	const char* buff;
	spSkeletonData *skeletonData;
	float scale = self->scale;
	spSkin *defaultSkin;
	_spSkeletonBinary* internal = SUB_CAST(_spSkeletonBinary, self);

	FREE(self->error);
	CONST_CAST(char*, self->error) = 0;
	internal->linkedMeshCount = 0;

	skeletonData = spSkeletonData_create();

	// Header
	if ((buff = readString(self)) != NULL) 
		MALLOC_STR(skeletonData->hash, buff);
	if ((buff = readString(self)) != NULL) 
		MALLOC_STR(skeletonData->version, buff);
	skeletonData->width = readFloat(self);
	skeletonData->height = readFloat(self);
	
	nonessential = readBoolean(self);
	if (nonessential)
	{
		buff = readString(self);
	}
	// Bones
	size = readIntOptimize(self,1);
	if (size>0)
	{
		skeletonData->bones = MALLOC(spBoneData *, size);	
		for (i = 0; i < size; i++)
		{
			const char *name = readString(self);
			//		printf("bone name : %s\n", name);
			spBoneData *parent = i == 0 ? NULL : skeletonData->bones[readIntOptimize(self, 1)];
			spBoneData *boneData = spBoneData_create(i, name, parent);
			boneData->rotation = readFloat(self);
			boneData->x = readFloat(self) * scale;
			boneData->y = readFloat(self) * scale;
			boneData->scaleX = readFloat(self);
			boneData->scaleY = readFloat(self);
			boneData->shearX = readFloat(self);
			boneData->shearY = readFloat(self);
			boneData->length = readFloat(self) * scale;
			boneData->inheritRotation = readBoolean(self);
			boneData->inheritScale = readBoolean(self);
			if (nonessential)
			{
				readInt(self); // Skip bone color.
			}
			skeletonData->bones[i] = boneData;
			++skeletonData->bonesCount;
		}
	}


	// Slots
	size = readIntOptimize(self, 1);
	if (size > 0)
	{
		skeletonData->slotsCount = size;
		skeletonData->slots = MALLOC(spSlotData *, size);
		for (i = 0; i < size; i++) {

			const char *name = readString(self);
			spBoneData *boneData = skeletonData->bones[readIntOptimize(self, 1)];
			spSlotData *slotData = spSlotData_create(i, name, boneData);
			readColor(self, &slotData->r, &slotData->g, &slotData->b, &slotData->a);
			const char *attachment_name = readString(self);
			spSlotData_setAttachmentName(slotData, attachment_name);

			slotData->blendMode = (spBlendMode)readIntOptimize(self, 1);

			skeletonData->slots[i] = slotData;
		}
	}
	//IK constraints
	size = readIntOptimize(self, 1);
	if (size>0)
	{
		skeletonData->ikConstraintsCount = size;
		skeletonData->ikConstraints = MALLOC(spIkConstraintData *, size);
		for (i = 0; i < size; i++)
		{
			int n;
			spIkConstraintData *ik = spIkConstraintData_create(readString(self));
			int boneCount = readIntOptimize(self, 1);
			ik->bonesCount = boneCount;
			ik->bones = MALLOC(spBoneData *, boneCount);
			for (n = 0; n < boneCount; n++)
			{
				ik->bones[n] = skeletonData->bones[readIntOptimize(self, 1)];
				//            printf("ik bone name : %s\n", ik->bones[n]->name);
			}
			ik->target = skeletonData->bones[readIntOptimize(self, 1)];
			ik->mix = readFloat(self);
			ik->bendDirection = READ();
			skeletonData->ikConstraints[i] = ik;
		}
	}

	// Transform constraints.
	size = readIntOptimize(self, 1);
	if (size>0)
	{
		skeletonData->transformConstraintsCount = size;
		skeletonData->transformConstraints = MALLOC(spTransformConstraintData *, size);
		for (i = 0; i < size; i++)
		{
			int n;
			spTransformConstraintData* data = spTransformConstraintData_create(readString(self));
			int boneCount = readIntOptimize(self, 1);
			data->bones = MALLOC(spBoneData *, boneCount);
			data->bonesCount = boneCount;
			for (n = 0; n < boneCount; n++)
			{
				data->bones[n] = skeletonData->bones[readIntOptimize(self, 1)];
			}
			data->target = skeletonData->bones[readIntOptimize(self, 1)];
			data->offsetRotation = readFloat(self);
			data->offsetX = readFloat(self)* scale;
			data->offsetY = readFloat(self)* scale;
			data->offsetScaleX = readFloat(self);
			data->offsetScaleY = readFloat(self);
			data->offsetShearY = readFloat(self);
			data->rotateMix = readFloat(self);
			data->translateMix = readFloat(self);
			data->scaleMix = readFloat(self);
			data->shearMix = readFloat(self);
			skeletonData->transformConstraints[i] = data;
		}

	}
	


	// Path constraints
	size = readIntOptimize(self, 1);
	if (size>0)
	{
		skeletonData->pathConstraintsCount = size;
		skeletonData->pathConstraints = MALLOC(spPathConstraintData *, size);
		for (i = 0; i < size; i++)
		{
			int n;
			spPathConstraintData* data = spPathConstraintData_create(readString(self));
			int boneCount = readIntOptimize(self, 1);
			//data->bones = MALLOC(spBoneData *, boneCount);
			CONST_CAST(spBoneData**, data->bones) = MALLOC(spBoneData*, boneCount);
			data->bonesCount = boneCount;
			for (n = 0; n < boneCount; n++)
			{
				data->bones[n] = skeletonData->bones[readIntOptimize(self, 1)];
			}
			data->target = skeletonData->slots[readIntOptimize(self, 1)];
			data->positionMode = (spPositionMode)readIntOptimize(self, 1);
			data->spacingMode = (spSpacingMode)readIntOptimize(self, 1);
			data->rotateMode = (spRotateMode)readIntOptimize(self, 1);
			data->offsetRotation = readFloat(self);
			data->position = readFloat(self);
			if (data->positionMode == SP_POSITION_MODE_FIXED) data->position *= scale;
			data->spacing = readFloat(self);
			if (data->spacingMode == SP_SPACING_MODE_LENGTH || data->spacingMode == SP_SPACING_MODE_FIXED) data->spacing *= scale;
			data->rotateMix = readFloat(self);
			data->translateMix = readFloat(self);
			skeletonData->pathConstraints[i] = data;
		}
	}
	

    // Default skin
    defaultSkin = readSkin(self, "default", nonessential);
    if (defaultSkin != NULL)
    {
        skeletonData->defaultSkin = defaultSkin;
		skeletonData->skinsCount++;
    }
    
	// user skin
	size = readIntOptimize(self,1);    
	// Skins
	if (size > 0)
	{
		skeletonData->skins = MALLOC(spSkin *, size + skeletonData->skinsCount);
        if (defaultSkin != NULL)
        {
            skeletonData->skins[0] = defaultSkin;
        }

        for (i = skeletonData->skinsCount; i < size + skeletonData->skinsCount; i++)
		{
			const char *name = readString(self);
			spSkin *skin = readSkin(self, name, nonessential);
			skeletonData->skins[skeletonData->skinsCount] = skin;
			++skeletonData->skinsCount;
		}
    }
    else
    {
        if (defaultSkin != NULL)
        {
            skeletonData->skins = MALLOC(spSkin *, 1);
            skeletonData->skins[0] = defaultSkin;
        }
    }


	/* Linked meshes. */
	for (i = 0; i < internal->linkedMeshCount; i++) {
		spAttachment* parent;
		_spLinkedMesh* linkedMesh = internal->linkedMeshes + i;
		spSkin* skin = !linkedMesh->skin ? skeletonData->defaultSkin : spSkeletonData_findSkin(skeletonData, linkedMesh->skin);
		if (!skin) {
			spSkeletonData_dispose(skeletonData); 
			printf("Skin not found : %s\n", linkedMesh->skin);
		//	_spSkeletonBinary_setError(self, 0, "Skin not found: ", linkedMesh->skin);
			return 0;
		}
		parent = spSkin_getAttachment(skin, linkedMesh->slotIndex, linkedMesh->parent);
		if (!parent) {
			spSkeletonData_dispose(skeletonData);
			printf("Parent mesh not found : %s\n", linkedMesh->parent);
		//	_spSkeletonBinary_setError(self, 0, "Parent mesh not found: ", linkedMesh->parent);
			return 0;
		}
		spMeshAttachment_setParentMesh(linkedMesh->mesh, SUB_CAST(spMeshAttachment, parent));
		spMeshAttachment_updateUVs(linkedMesh->mesh);
		spAttachmentLoader_configureAttachment(self->attachmentLoader, SUPER(SUPER(linkedMesh->mesh)));
	}


	// Events
	size = readIntOptimize(self,1);
	if (size > 0)
	{
		const char *stringValue;
		skeletonData->eventsCount = skeletonData->eventsCount;
		skeletonData->events = MALLOC(spEventData *, size);
		for (i = 0; i < size; i++)
		{
			spEventData *eventData = spEventData_create(readString(self));
			eventData->intValue = readIntOptimize(self,0);
			eventData->floatValue = readFloat(self);
			stringValue = readString(self);
			if (stringValue) MALLOC_STR(eventData->stringValue, stringValue);
			skeletonData->events[i] = eventData;
		}
	}

	// Animations
	size = readIntOptimize(self,1);
	if (size > 0)
	{
		skeletonData->animations = MALLOC(spAnimation *, size);
		for (i = 0; i < size; i++)
		{
			const char *name = readString(self);
			readAnimation(self, skeletonData, name);
		}
	}

	return skeletonData;
}

spSkeletonData *spSkeletonBinary_readSkeletonDataFile(spSkeletonBinary* self, const char * path)
{
	int length;
	spSkeletonData* skeletonData;

	self->rawdata = _spUtil_readFile(path, &length);
	self->reader = self->rawdata;
    self->cache = (char**)malloc(sizeof(char*) * 1024 * 5);
    self->cacheIndex = 0;
	if (!self->reader) {
		return 0;
	}

	skeletonData = spSkeletonBinary_readSkeletonData(self);
	
    FREE(self->cache);
	FREE(self->rawdata);
	return skeletonData;
}
