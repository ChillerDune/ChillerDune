// LordSk
#include "editor2.h"

#include <zlib.h> // crc32
#include <stdio.h> // sscanf
#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/shared/config.h>

//#include <intrin.h>

// TODO:
// - Easily know if we're clicking on UI or elsewhere
// ---- what event gets handled where should be VERY clear

// - Binds
// - Smooth zoom
// - Fix selecting while moving the camera

// - Localize everything

// - Stability is very important (crashes should be easy to catch)
// - Input should be handled well (careful of input-locks https://github.com/teeworlds/teeworlds/issues/828)
// ! Do not use DoButton_SpriteClean (dunno why)

// FIXME: deleting every layer/group then ctrl z causes a crash

static char s_aEdMsg[256];
#define ed_log(...)\
	str_format(s_aEdMsg, sizeof(s_aEdMsg), ##__VA_ARGS__);\
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editor", s_aEdMsg);

#ifdef CONF_DEBUG
	#define ed_dbg(...)\
		str_format(s_aEdMsg, sizeof(s_aEdMsg), ##__VA_ARGS__);\
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "editor", s_aEdMsg);
#else
	#define ed_dbg(...)
	#undef dbg_assert
	#define dbg_assert(test,msg)
#endif

// TODO: move this elsewhere
#define ARR_COUNT(arr) (sizeof(arr)/sizeof(arr[0]))

// Fisico
const vec4 StyleColorLayer1(46/255.f, 46/255.f, 46/255.f, 1);
const vec4 StyleColorSubHeader(55/255.f, 55/255.f, 55/255.f, 1);
const vec4 StyleColorLayer2 = StyleColorSubHeader;
const vec4 StyleColorBg(31/255.f, 31/255.f, 31/255.f, 1);
const vec4 StyleColorButton(95/255.f, 95/255.f, 95/255.f, 1);
const vec4 StyleColorButtonBorder(79/255.f, 79/255.f, 79/255.f, 1);
const vec4 StyleColorButtonPressed(84/255.f, 122/255.f, 181/255.f, 1);
const vec4 StyleColorButtonHover = StyleColorButtonPressed; 
const vec4 StyleColorInputSelected(0,0.2,1,1);
const vec4 StyleColorTileSelection(0.0, 0.31, 1, 0.4);
const vec4 StyleColorTileHover(1, 1, 1, 0.25);
const vec4 StyleColorTileHoverBorder(0.0, 0.31, 1, 1);

// LordSk
// const vec4 StyleColorBg(0.03, 0, 0.085, 1);
// const vec4 StyleColorButton(0.062, 0, 0.19, 1);
// const vec4 StyleColorButtonBorder(0.18, 0.00, 0.56, 1);
// const vec4 StyleColorButtonHover(0.28, 0.10, 0.64, 1);
// const vec4 StyleColorButtonPressed(0.13, 0, 0.40, 1);
// const vec4 StyleColorInputSelected(0,0.2,1,1);
// const vec4 StyleColorTileSelection(0.0, 0.31, 1, 0.4);
// const vec4 StyleColorTileHover(1, 1, 1, 0.25);
// const vec4 StyleColorTileHoverBorder(0.0, 0.31, 1, 1);

inline float fract(float f)
{
	return f - (int)f;
}

inline int floor(float f)
{
	return f < 0 ? (int)f-1 : (int)f;
}

inline bool IsInsideRect(vec2 Pos, CUIRect Rect)
{
	return (Pos.x >= Rect.x && Pos.x < (Rect.x+Rect.w) &&
			Pos.y >= Rect.y && Pos.y < (Rect.y+Rect.h));
}

inline bool DoRectIntersect(CUIRect Rect1, CUIRect Rect2)
{
	if(Rect1.x < Rect2.x + Rect2.w &&
	   Rect1.x + Rect1.w > Rect2.x &&
	   Rect1.y < Rect2.y + Rect2.h &&
	   Rect1.h + Rect2.y > Rect2.y)
	{
		return true;
	}
	return false;
}

// hash
inline u32 fnv1a32(const void* data, u32 dataSize)
{
	u32 hash = 2166136261;
	for(u32 i = 0; i < dataSize; ++i)
		hash = (hash ^ ((const char*)data)[i]) * 16777619;
	return hash;
}

// increase size and zero out / construct new items
template<typename T>
inline void ArraySetSizeAndZero(array<T>* pArray, int NewSize)
{
	const int OldElementCount = pArray->size();
	pArray->set_size(NewSize);
	const int Diff = pArray->size() - OldElementCount;
	for(int i = 0; i < Diff; i++)
		(*pArray)[i+OldElementCount] = T();
}

void CEditorMap2::Init(IStorage* pStorage, IGraphics* pGraphics, IConsole* pConsole)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	m_pConsole = pConsole;

	m_aEnvPoints.hint_size(1024);
	m_aLayers.hint_size(32);
	m_aGroups.hint_size(16);
	m_aEnvelopes.hint_size(32);

	// speedtest
#if 0
	CChainAllocator<CTile> TestDispenser;
	TestDispenser.Init(1000000, 10);

	const int TestLoopCount = 1000000;
	uint64_t StartCycles = __rdtsc();
	uint64_t MallocCycles, ChainCycles;

	int DummyVar = 0;
	for(int i = 0; i < TestLoopCount; i++)
	{
		CTile* pBuff = (CTile*)malloc(sizeof(CTile) * ((i%1000)+1));
		mem_zero(pBuff, sizeof(CTile) * ((i%1000)+1));
		DummyVar += pBuff[0].m_Index;
		free(pBuff);
	}

	MallocCycles = __rdtsc() - StartCycles;
	StartCycles = __rdtsc();

	for(int i = 0; i < TestLoopCount; i++)
	{
		CMemBlock<CTile> Block = TestDispenser.Alloc((i%1000)+1);
		DummyVar += Block.Get()[0].m_Index;
		TestDispenser.Dealloc(&Block);
	}

	ChainCycles = __rdtsc() - StartCycles;

	ed_log("DummyVar=%d MallocAvgCycles=%llu ChainAvgCycles=%llu", DummyVar, MallocCycles/TestLoopCount,
		   ChainCycles/TestLoopCount);

	const int TestLoopCount2 = 1000;
	array<CTile> BaseTileArray;
	CArray<CTile> OurTileArray;
	OurTileArray.Init(&TestDispenser, 2);
	StartCycles = __rdtsc();

	for(int i = 0; i < TestLoopCount2; i++)
	{
		CTile t;
		t.m_Index = i;
		BaseTileArray.add(t);
		DummyVar += BaseTileArray.size();
	}

	MallocCycles = __rdtsc() - StartCycles;
	StartCycles = __rdtsc();

	for(int i = 0; i < TestLoopCount2; i++)
	{
		CTile t;
		t.m_Index = i;
		OurTileArray.Add(t);
		DummyVar += OurTileArray.Count();
	}

	ChainCycles = __rdtsc() - StartCycles;

	ed_log("ARRAYS DummyVar=%d MallocAvgCycles=%llu ChainAvgCycles=%llu", DummyVar,
		   MallocCycles/TestLoopCount2,
		   ChainCycles/TestLoopCount2);

#endif

	// test
#if 0
	CChainAllocator<CTile, 100, 8> TestDispenser;
	TestDispenser.Init();
	CMemBlock<CTile> Block = TestDispenser.Alloc(57);
	TestDispenser.Dealloc(Block);

	Block = TestDispenser.Alloc(57);
	CMemBlock<CTile> Block2 = TestDispenser.Alloc(18);
	TestDispenser.Dealloc(Block);
	Block = TestDispenser.Alloc(57);
	TestDispenser.Dealloc(Block2);
#endif
}

bool CEditorMap2::Save(const char* pFileName)
{
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "saving to '%s'...", pFileName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editor", aBuf);
	CDataFileWriter File;
	if(!File.Open(m_pStorage, pFileName))
	{
		str_format(aBuf, sizeof(aBuf), "failed to open file '%s'...", pFileName);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "editor", aBuf);
		return 0;
	}

	// save version
	{
		CMapItemVersion Item;
		Item.m_Version = CMapItemVersion::CURRENT_VERSION;
		File.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Item), &Item);
	}

	// save map info
	{
		CMapItemInfo Item;
		Item.m_Version = CMapItemInfo::CURRENT_VERSION;

		// if(m_MapInfo.m_aAuthor[0])
			// Item.m_Author = df.AddData(str_length(m_MapInfo.m_aAuthor)+1, m_MapInfo.m_aAuthor);
		// else
			Item.m_Author = -1;
		// if(m_MapInfo.m_aVersion[0])
			// Item.m_MapVersion = df.AddData(str_length(m_MapInfo.m_aVersion)+1, m_MapInfo.m_aVersion);
		// else
			Item.m_MapVersion = -1;
		// if(m_MapInfo.m_aCredits[0])
			// Item.m_Credits = df.AddData(str_length(m_MapInfo.m_aCredits)+1, m_MapInfo.m_aCredits);
		// else
			Item.m_Credits = -1;
		// if(m_MapInfo.m_aLicense[0])
			// Item.m_License = df.AddData(str_length(m_MapInfo.m_aLicense)+1, m_MapInfo.m_aLicense);
		// else
			Item.m_License = -1;

		File.AddItem(MAPITEMTYPE_INFO, 0, sizeof(Item), &Item);
	}

	// save images
	for(int i = 0; i < m_Assets.m_ImageCount; i++)
	{
		// old code: analyse the image for when saving (should be done when we load the image)
		// pImg->AnalyseTileFlags();

		CImageName ImgName = m_Assets.m_aImageNames[i];
		CImageInfo ImgInfo = m_Assets.m_aTextureInfos[i];
		const bool IsExternal = m_Assets.m_aImageEmbeddedCrc[i] == 0; // external

		CMapItemImage Item;
		Item.m_Version = CMapItemImage::CURRENT_VERSION;

		Item.m_Width = ImgInfo.m_Width;
		Item.m_Height = ImgInfo.m_Height;
		Item.m_External = IsExternal;
		Item.m_ImageName = File.AddData(str_length(ImgName.m_Buff)+1, ImgName.m_Buff);
		if(IsExternal)
			Item.m_ImageData = -1;
		else
		{
			int PixelSize = ImgInfo.m_Format == CImageInfo::FORMAT_RGB ? 3 : 4;
			Item.m_ImageData = File.AddData(Item.m_Width*Item.m_Height*PixelSize, ImgInfo.m_pData);
		}
		Item.m_Format = ImgInfo.m_Format;
		File.AddItem(MAPITEMTYPE_IMAGE, i, sizeof(Item), &Item);
	}

	// save layers
	for(int li = 0, gi = 0; gi < m_aGroups.size(); gi++)
	{
		const CGroup& Group = m_aGroups[gi];
		ed_dbg("Group#%d NumLayers=%d Offset=(%d, %d)", gi, Group.m_LayerCount, Group.m_OffsetX, Group.m_OffsetY);
		// old feature
		// if(!Group->m_SaveToMap)
		// 	continue;

		CMapItemGroup GItem;
		GItem.m_Version = CMapItemGroup::CURRENT_VERSION;

		GItem.m_ParallaxX = Group.m_ParallaxX;
		GItem.m_ParallaxY = Group.m_ParallaxY;
		GItem.m_OffsetX = Group.m_OffsetX;
		GItem.m_OffsetY = Group.m_OffsetY;
		GItem.m_UseClipping = Group.m_UseClipping;
		GItem.m_ClipX = Group.m_ClipX;
		GItem.m_ClipY = Group.m_ClipY;
		// GItem.m_ClipW = Group.m_ClipW;
		// GItem.m_ClipH = Group.m_ClipH;
		GItem.m_ClipW = 0;
		GItem.m_ClipH = 0;
		GItem.m_StartLayer = li;
		GItem.m_NumLayers = 0;

		// save group name
		StrToInts(GItem.m_aName, sizeof(GItem.m_aName)/sizeof(int), Group.m_aName);


		for(; li < GItem.m_StartLayer + Group.m_LayerCount; li++)
		{

			// old feature
			// if(!Group->m_lLayers[l]->m_SaveToMap)
			// 	continue;

			if(m_aLayers[li].IsTileLayer())
			{
				// Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "editor", "saving tiles layer");
				CLayer& Layer = m_aLayers[li];
				// Layer.PrepareForSave();

				ed_dbg("  Group#%d Layer=%d (w=%d, h=%d)", gi, li, Layer.m_Width, Layer.m_Height);

				CMapItemLayerTilemap Item;
				Item.m_Version = CMapItemLayerTilemap::CURRENT_VERSION;

				// Item.m_Layer.m_Flags = Layer.m_Flags;
				Item.m_Layer.m_Flags = 0; // LAYERFLAG_DETAIL or 0
				Item.m_Layer.m_Type = Layer.m_Type;

				Item.m_Color.r = Layer.m_Color.r*255;
				Item.m_Color.g = Layer.m_Color.g*255;
				Item.m_Color.b = Layer.m_Color.b*255;
				Item.m_Color.a = Layer.m_Color.a*255;
				Item.m_ColorEnv = Layer.m_ColorEnvelopeID;
				Item.m_ColorEnvOffset = Layer.m_ColorEnvOffset;

				Item.m_Width = Layer.m_Width;
				Item.m_Height = Layer.m_Height;

				if(m_GameGroupID == gi && m_GameLayerID == li)
				{
					Item.m_Flags = TILESLAYERFLAG_GAME;
					ed_dbg("Game layer reached");
				}
				else
					Item.m_Flags = 0;
				Item.m_Image = Layer.m_ImageID;
				// Item.m_Data = File.AddData(Layer.m_SaveTilesSize, Layer.m_pSaveTiles);
				Item.m_Data = File.AddData(Layer.m_aTiles.size()*sizeof(CTile), Layer.m_aTiles.base_ptr());

				// save layer name
				StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), Layer.m_aName);

				File.AddItem(MAPITEMTYPE_LAYER, li, sizeof(Item), &Item);

				GItem.m_NumLayers++;
			}
			else if(m_aLayers[li].IsQuadLayer())
			{
				// Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "editor", "saving quads layer");
				CLayer& LayerQuad = m_aLayers[li];

				ed_dbg("  Group#%d Quad=%d (w=%d, h=%d)", gi, li, LayerQuad.m_Width, LayerQuad.m_Height);

				if(LayerQuad.m_aQuads.size())
				{
					CMapItemLayerQuads Item;
					Item.m_Version = CMapItemLayerQuads::CURRENT_VERSION;
					Item.m_Layer.m_Flags = 0; // LAYERFLAG_DETAIL or 0
					Item.m_Layer.m_Type = LayerQuad.m_Type;
					Item.m_Image = LayerQuad.m_ImageID;

					// add the data
					Item.m_NumQuads = LayerQuad.m_aQuads.size();
					Item.m_Data = File.AddDataSwapped(LayerQuad.m_aQuads.size()*sizeof(CQuad), LayerQuad.m_aQuads.base_ptr());

					// save layer name
					StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), LayerQuad.m_aName);

					File.AddItem(MAPITEMTYPE_LAYER, li, sizeof(Item), &Item);

					GItem.m_NumLayers++;
				}
			}
		}

		File.AddItem(MAPITEMTYPE_GROUP, gi, sizeof(GItem), &GItem);
	}

	// check for bezier curve envelopes, otherwise use older, smaller envelope points
	int Version = CMapItemEnvelope_v2::CURRENT_VERSION;
	int Size = sizeof(CEnvPoint_v1);	
	// for(int e = 0; e < m_aEnvelopes.Count(); e++)
	{
		for(int p = 0; p < m_aEnvPoints.size(); p++)
		{
			if(m_aEnvPoints[p].m_Curvetype == CURVETYPE_BEZIER)
			{
				Version = CMapItemEnvelope::CURRENT_VERSION;
				Size = sizeof(CEnvPoint);
				break;
			}
		}
	}

	// save envelopes
	int PointCount = 0;
	for(int e = 0; e < m_aEnvelopes.size(); e++)
	{
		CMapItemEnvelope Item;
		Item.m_Version = Version;
		Item.m_Channels = m_aEnvelopes[e].m_Channels;
		Item.m_StartPoint = PointCount;
		Item.m_NumPoints = m_aEnvelopes[e].m_NumPoints;
		Item.m_Synchronized = m_aEnvelopes[e].m_Synchronized;
		mem_copy(Item.m_aName, m_aEnvelopes[e].m_aName, sizeof(m_aEnvelopes[e].m_aName)*sizeof(int));

		File.AddItem(MAPITEMTYPE_ENVELOPE, e, sizeof(Item), &Item);
		PointCount += Item.m_NumPoints;
	}

	// save points
	int TotalSize = Size * PointCount;
	unsigned char *pPoints = (unsigned char *)mem_alloc(TotalSize, 1);
	int Offset = 0;
	// for(int e = 0; e < m_aEnvelopes.Count(); e++)
	{
		for(int p = 0; p < m_aEnvPoints.size(); p++)
		{
			mem_copy(pPoints + Offset, &(m_aEnvPoints[p]), Size);
			Offset += Size;
		}
	}

	File.AddItem(MAPITEMTYPE_ENVPOINTS, 0, TotalSize, pPoints);
	mem_free(pPoints);

	// finish the data file
	File.Finish();
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "editor", "saving done");

	return true;
}

bool CEditorMap2::Load(const char* pFileName)
{
	CDataFileReader File;
	if(!File.Open(m_pStorage, pFileName, IStorage::TYPE_ALL))
		return false;
	// check version
	CMapItemVersion *pItem = (CMapItemVersion *)File.FindItem(MAPITEMTYPE_VERSION, 0);
	if(!pItem || pItem->m_Version != CMapItemVersion::CURRENT_VERSION)
		return false;

	const int FilenameLen = str_length(pFileName);
	mem_copy(m_aPath, pFileName, FilenameLen);
	m_aPath[FilenameLen] = 0;

	Clear();
	AssetsClearEmbeddedFiles();

	int GroupsStart, GroupsNum;
	int LayersStart, LayersNum;
	File.GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	File.GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	ed_dbg("GroupsStart=%d GroupsNum=%d LayersStart=%d LayersNum=%d",
			GroupsStart, GroupsNum, LayersStart, LayersNum);

	for(int gi = 0; gi < GroupsNum; gi++)
	{
		CMapItemGroup* pGroup = (CMapItemGroup*)File.GetItem(GroupsStart+gi, 0, 0);
		ed_dbg("Group#%d NumLayers=%d Offset=(%d, %d)", gi, pGroup->m_NumLayers,
				pGroup->m_OffsetX, pGroup->m_OffsetY);
		const int GroupLayerCount = pGroup->m_NumLayers;
		const int GroupLayerStart = pGroup->m_StartLayer;
		CEditorMap2::CGroup Group;
		IntsToStr(pGroup->m_aName, ARR_COUNT(pGroup->m_aName), Group.m_aName);
		Group.m_ParallaxX = pGroup->m_ParallaxX;
		Group.m_ParallaxY = pGroup->m_ParallaxY;
		Group.m_OffsetX = pGroup->m_OffsetX;
		Group.m_OffsetY = pGroup->m_OffsetY;
		Group.m_ClipX = pGroup->m_ClipX;
		Group.m_ClipY = pGroup->m_ClipY;
		Group.m_ClipWidth = pGroup->m_ClipW;
		Group.m_ClipHeight = pGroup->m_ClipH;
		Group.m_UseClipping = pGroup->m_UseClipping;
		Group.m_LayerCount = 0;

		for(int li = 0; li < GroupLayerCount; li++)
		{
			CMapItemLayer* pLayer = (CMapItemLayer*)File.GetItem(LayersStart+GroupLayerStart+li, 0, 0);

			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const CMapItemLayerTilemap& Tilemap = *(CMapItemLayerTilemap*)pLayer;
				ed_dbg("Group#%d Layer=%d (w=%d, h=%d)", gi, li,
						Tilemap.m_Width, Tilemap.m_Height);

				m_MapMaxWidth = max(m_MapMaxWidth, Tilemap.m_Width);
				m_MapMaxHeight = max(m_MapMaxHeight, Tilemap.m_Height);

				CLayer LayerTile;
				IntsToStr(Tilemap.m_aName, ARR_COUNT(Tilemap.m_aName), LayerTile.m_aName);
				LayerTile.m_Type = LAYERTYPE_TILES;
				LayerTile.m_ImageID = Tilemap.m_Image;
				LayerTile.m_Width = Tilemap.m_Width;
				LayerTile.m_Height = Tilemap.m_Height;
				LayerTile.m_ColorEnvelopeID = Tilemap.m_ColorEnv;
				LayerTile.m_ColorEnvOffset = Tilemap.m_ColorEnvOffset;
				LayerTile.m_Color = vec4(Tilemap.m_Color.r/255.f, Tilemap.m_Color.g/255.f,
										 Tilemap.m_Color.b/255.f, Tilemap.m_Color.a/255.f);
				LayerTile.m_aTiles.clear();

				CTile *pTiles = (CTile *)File.GetData(Tilemap.m_Data);
				int TileCount = Tilemap.m_Width*Tilemap.m_Height;
				for(int ti = 0; ti < TileCount; ti++)
				{
					CTile Tile = pTiles[ti];
					const int Skips = Tile.m_Skip;
					Tile.m_Skip = 0;

					for(int s = 0; s < Skips; s++)
						LayerTile.m_aTiles.add(Tile);
					LayerTile.m_aTiles.add(Tile);

					TileCount -= Skips;
				}

				const int LayerID = m_aLayers.size();
				m_aLayers.add(LayerTile);

				if(Group.m_LayerCount < MAX_GROUP_LAYERS)
					Group.m_apLayerIDs[Group.m_LayerCount++] = LayerID;

				if(Tilemap.m_Flags&TILESLAYERFLAG_GAME)
				{
					m_GameLayerID = LayerID;
					m_GameGroupID = m_aGroups.size();
				}
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				const CMapItemLayerQuads& ItemQuadLayer = *(CMapItemLayerQuads*)pLayer;

				CLayer LayerQuad;
				IntsToStr(ItemQuadLayer.m_aName, ARR_COUNT(ItemQuadLayer.m_aName), LayerQuad.m_aName);
				LayerQuad.m_Type = LAYERTYPE_QUADS;
				LayerQuad.m_ImageID = ItemQuadLayer.m_Image;

				CQuad *pQuads = (CQuad *)File.GetData(ItemQuadLayer.m_Data);
				LayerQuad.m_aQuads.clear();
				LayerQuad.m_aQuads.add(pQuads, ItemQuadLayer.m_NumQuads);

				const int LayerID = m_aLayers.size();
				m_aLayers.add(LayerQuad);

				if(Group.m_LayerCount < MAX_GROUP_LAYERS)
					Group.m_apLayerIDs[Group.m_LayerCount++] = LayerID;
			}
		}

		m_aGroups.add(Group);
	}

	dbg_assert(m_GameLayerID >= 0, "Game layer not found");
	dbg_assert(m_GameGroupID >= 0, "Game group not found");

	// load envelopes
	int EnvelopeStart, EnvelopeCount;
	int EnvPointStart, EnvPointCount;
	File.GetType(MAPITEMTYPE_ENVELOPE, &EnvelopeStart, &EnvelopeCount);
	File.GetType(MAPITEMTYPE_ENVPOINTS, &EnvPointStart, &EnvPointCount);
	// FIXME: EnvPointCount is always 1?

	ed_dbg("EnvelopeStart=%d EnvelopeCount=%d EnvPointStart=%d EnvPointCount=%d",
			EnvelopeStart, EnvelopeCount, EnvPointStart, EnvPointCount);

	CEnvPoint* pEnvPoints = (CEnvPoint*)File.GetItem(EnvPointStart, 0, 0);

	// envelopes
	for(int ei = 0; ei < EnvelopeCount; ei++)
	{
		CMapItemEnvelope *pItem = (CMapItemEnvelope *)File.GetItem(EnvelopeStart+ei, 0, 0);
		const CMapItemEnvelope Env = *pItem;
		m_aEnvelopes.add(Env);

		if(Env.m_Version >= 3)
		{
			m_aEnvPoints.add(&pEnvPoints[Env.m_StartPoint], Env.m_NumPoints);
		}
		else
		{
			// backwards compatibility
			for(int i = 0; i < Env.m_NumPoints; i++)
			{
				// convert CEnvPoint_v1 -> CEnvPoint
				CEnvPoint_v1 *pEnvPoint_v1 = &((CEnvPoint_v1 *)pEnvPoints)[i + Env.m_StartPoint];
				CEnvPoint Point;
				mem_zero(&Point, sizeof(Point));
				mem_copy(&Point, pEnvPoint_v1, sizeof(CEnvPoint_v1));
				m_aEnvPoints.add(Point);
			}
		}
	}

	// load textures
	int ImagesStart, ImagesCount;
	File.GetType(MAPITEMTYPE_IMAGE, &ImagesStart, &ImagesCount);
	CImageName aImageName[MAX_IMAGES];
	CImageInfo aImageInfo[MAX_IMAGES];
	u32 aImageEmbeddedCrc[MAX_IMAGES];

	for(int i = 0; i < ImagesCount && i < MAX_IMAGES; i++)
	{
		mem_zero(&aImageName[i], sizeof(aImageName[i]));
		CMapItemImage *pImg = (CMapItemImage *)File.GetItem(ImagesStart+i, 0, 0);
		const char *pImgName = (char *)File.GetData(pImg->m_ImageName);
		mem_copy(&aImageName[i], pImgName, min((u64)str_length(pImgName), sizeof(aImageName[i].m_Buff)-1));
		aImageInfo[i].m_pData = 0x0;
		aImageInfo[i].m_Width = pImg->m_Width;
		aImageInfo[i].m_Height = pImg->m_Height;

		if(pImg->m_External)
		{
			aImageEmbeddedCrc[i] = 0; // external
			aImageInfo[i].m_Format = CImageInfo::FORMAT_AUTO;
		}
		else
		{
			unsigned long DataSize = 0;
			void *pData = File.GetData(pImg->m_ImageData, &DataSize);

			// save embedded data
			aImageEmbeddedCrc[i] = AssetsAddEmbeddedData(pData, DataSize);
			aImageInfo[i].m_Format = pImg->m_Version == 1 ? CImageInfo::FORMAT_RGBA : pImg->m_Format;
		}
	}

	AssetsClearAndSetImages(aImageName, aImageInfo, aImageEmbeddedCrc, ImagesCount);
	AssetsLoadMissingAutomapFiles();

	return true;
}

void CEditorMap2::LoadDefault()
{
	Clear();

	CGroup BgGroup;
	BgGroup.m_OffsetX = 0;
	BgGroup.m_OffsetY = 0;
	BgGroup.m_ParallaxX = 0;
	BgGroup.m_ParallaxY = 0;

	CLayer& BgQuadLayer = NewQuadLayer();

	CQuad SkyQuad;
	SkyQuad.m_ColorEnv = -1;
	SkyQuad.m_PosEnv = -1;
	const int Width = i2fx(800);
	const int Height = i2fx(600);
	SkyQuad.m_aPoints[0].x = SkyQuad.m_aPoints[2].x = -Width;
	SkyQuad.m_aPoints[1].x = SkyQuad.m_aPoints[3].x = Width;
	SkyQuad.m_aPoints[0].y = SkyQuad.m_aPoints[1].y = -Height;
	SkyQuad.m_aPoints[2].y = SkyQuad.m_aPoints[3].y = Height;
	SkyQuad.m_aPoints[4].x = SkyQuad.m_aPoints[4].y = 0;
	SkyQuad.m_aColors[0].r = SkyQuad.m_aColors[1].r = 94;
	SkyQuad.m_aColors[0].g = SkyQuad.m_aColors[1].g = 132;
	SkyQuad.m_aColors[0].b = SkyQuad.m_aColors[1].b = 174;
	SkyQuad.m_aColors[2].r = SkyQuad.m_aColors[3].r = 204;
	SkyQuad.m_aColors[2].g = SkyQuad.m_aColors[3].g = 232;
	SkyQuad.m_aColors[2].b = SkyQuad.m_aColors[3].b = 255;
	SkyQuad.m_aColors[0].a = SkyQuad.m_aColors[1].a = 255;
	SkyQuad.m_aColors[2].a = SkyQuad.m_aColors[3].a = 255;
	BgQuadLayer.m_aQuads.add(SkyQuad);

	BgGroup.m_apLayerIDs[BgGroup.m_LayerCount++] = m_aLayers.size()-1;
	m_aGroups.add(BgGroup);

	CGroup GameGroup;
	GameGroup.m_OffsetX = 0;
	GameGroup.m_OffsetY = 0;
	GameGroup.m_ParallaxX = 100;
	GameGroup.m_ParallaxY = 100;

	/*CLayer& Gamelayer =*/ NewTileLayer(50, 50);

	GameGroup.m_apLayerIDs[GameGroup.m_LayerCount++] = m_aLayers.size()-1;
	m_aGroups.add(GameGroup);

	m_GameGroupID = m_aGroups.size()-1;
	m_GameLayerID = GameGroup.m_apLayerIDs[0];
}

void CEditorMap2::Clear()
{
	m_GameLayerID = -1;
	m_GameGroupID = -1;
	m_MapMaxWidth = 0;
	m_MapMaxHeight = 0;

	// release tiles and quads
	const int LayerCount = m_aLayers.size();
	for(int li = 0; li < LayerCount; li++)
	{
		if(m_aLayers[li].IsTileLayer())
			m_aLayers[li].m_aTiles.clear();
		else
			m_aLayers[li].m_aQuads.clear();
	}

	m_aLayers.clear();
	m_aGroups.clear();
	m_aEnvelopes.clear();
	m_aEnvPoints.clear();
}

void CEditorMap2::AssetsClearAndSetImages(CEditorMap2::CImageName* aName, CImageInfo* aInfo,
	u32* aImageEmbeddedCrc, int ImageCount)
{
	dbg_assert(ImageCount <= MAX_IMAGES, "ImageCount > MAX_IMAGES");

	IGraphics::CTextureHandle aKeepTexHandle[MAX_IMAGES];
	for(int ni = 0; ni < ImageCount; ni++)
		aKeepTexHandle[ni].Invalidate();

	for(int i = 0; i < m_Assets.m_ImageCount; i++)
	{
		bool Found = false;
		for(int ni = 0; ni < ImageCount; ni++)
		{
			if(m_Assets.m_aImageEmbeddedCrc[i] == aImageEmbeddedCrc[ni] &&
			   mem_comp(m_Assets.m_aImageNames[i].m_Buff, aName[ni].m_Buff, sizeof(aName[ni].m_Buff)) == 0)
			{
				aKeepTexHandle[ni] = m_Assets.m_aTextureHandle[i];
				Found = true;
				break;
			}
		}

		if(!Found)
			m_pGraphics->UnloadTexture(&m_Assets.m_aTextureHandle[i]);
	}

	m_Assets.m_ImageCount = ImageCount;

	memmove(m_Assets.m_aImageNames, aName, sizeof(aName[0]) * ImageCount);
	memmove(m_Assets.m_aTextureInfos, aInfo, sizeof(aInfo[0]) * ImageCount);
	memmove(m_Assets.m_aImageEmbeddedCrc, aImageEmbeddedCrc, sizeof(aImageEmbeddedCrc[0]) * ImageCount);

	for(int i = 0; i < ImageCount && i < MAX_IMAGES; i++)
	{
		const int TextureFlags = IGraphics::TEXLOAD_MULTI_DIMENSION;

		if(aKeepTexHandle[i].IsValid())
		{
			ed_dbg("%s kept", aName[i].m_Buff);
			m_Assets.m_aTextureHandle[i] = aKeepTexHandle[i];
			continue;
		}

		if(m_Assets.m_aImageEmbeddedCrc[i] == 0) // external
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "mapres/%s.png", aName[i].m_Buff);
			m_Assets.m_aTextureHandle[i] = m_pGraphics->LoadTexture(aBuf, IStorage::TYPE_ALL,
				CImageInfo::FORMAT_AUTO, TextureFlags);
			m_Assets.m_aTextureInfos[i].m_Format = CImageInfo::FORMAT_AUTO;
			ed_dbg("%s loaded", aBuf);
		}
		else
		{
			const u32 Crc = aImageEmbeddedCrc[i];
			const int EmbeddedFileCount = m_Assets.m_EmbeddedFileCount;
			CEmbeddedFile* pFile = 0x0;
			for(int f = 0; f < EmbeddedFileCount; f++)
			{
				if(m_Assets.m_aEmbeddedFile[f].m_Crc == Crc)
				{
					pFile = &m_Assets.m_aEmbeddedFile[f];
					break;
				}
			}

			// embedded file should be found
			dbg_assert(pFile != 0x0, "Embedded file not found");

			m_Assets.m_aTextureHandle[i] = m_pGraphics->LoadTextureRaw(aInfo[i].m_Width, aInfo[i].m_Height,
				aInfo[i].m_Format, pFile->m_pData, CImageInfo::FORMAT_RGBA, TextureFlags);
		}

		m_Assets.m_aImageNameHash[i] = fnv1a32(&m_Assets.m_aImageNames[i],
			sizeof(m_Assets.m_aImageNames[i]));

		dbg_assert(m_Assets.m_aTextureHandle[i].IsValid(), "Invalid texture");
	}
}

u32 CEditorMap2::AssetsAddEmbeddedData(void* pData, u64 DataSize)
{
	// 10 Mb limit
	dbg_assert(DataSize < (u64)(1024*1024*10), "DataSize is invalid");
	void* pDataCopy = mem_alloc(DataSize, 0);
	memmove(pDataCopy, pData, DataSize);

	CEmbeddedFile EmbeddedFile;
	u32 Crc = crc32(0L, 0x0, 0);
	Crc = crc32(Crc, (u8*)pData, DataSize);
	EmbeddedFile.m_Crc = Crc;
	EmbeddedFile.m_Type = 0;
	EmbeddedFile.m_pData = pDataCopy;
	m_Assets.m_aEmbeddedFile[m_Assets.m_EmbeddedFileCount++] = EmbeddedFile;
	return Crc;
}

void CEditorMap2::AssetsClearEmbeddedFiles()
{
	for(int i = 0; i < m_Assets.m_EmbeddedFileCount; i++)
	{
		if(m_Assets.m_aEmbeddedFile[i].m_pData)
		{
			free(m_Assets.m_aEmbeddedFile[i].m_pData);
			m_Assets.m_aEmbeddedFile[i].m_pData = 0x0;
		}
	}
	m_Assets.m_EmbeddedFileCount = 0;
}

bool CEditorMap2::AssetsAddAndLoadImage(const char* pFilename)
{
	// TODO: return error ID
	if(m_Assets.m_ImageCount > MAX_IMAGES-1)
		return false;

	const int StrLen = str_length(pFilename);
	const bool EndsWithPng = StrLen > 4 && str_comp_nocase_num(pFilename+StrLen-4, ".png", 4) == 0;
	if(!EndsWithPng)
	{
		// TODO: can we load anything other than png files?
		ed_dbg("ERROR: '%s' image file not supported", pFilename);
		return false;
	}


	CImageName ImgName;
	mem_zero(&ImgName, sizeof(ImgName));
	const int NameLen = min(StrLen-4, (int)sizeof(ImgName.m_Buff)-1);
	memmove(ImgName.m_Buff, pFilename, NameLen);
	ImgName.m_Buff[NameLen] = 0;
	const u32 NameHash = fnv1a32(&ImgName, sizeof(ImgName));

	// find image by name
	const int ImageCount = m_Assets.m_ImageCount;
	for(int i = 0; i < ImageCount; i++)
	{
		if(m_Assets.m_aImageNameHash[i] == NameHash)
		{
			ed_log("'%s' image file already loaded", pFilename);
			return false;
		}
	}

	const int ImgID = m_Assets.m_ImageCount++;
	m_Assets.m_aImageNames[ImgID] = ImgName;
	m_Assets.m_aImageNameHash[ImgID] = NameHash;
	m_Assets.m_aImageEmbeddedCrc[ImgID] = 0;

	char aFilePath[256];
	str_format(aFilePath, sizeof(aFilePath), "mapres/%s", pFilename);

	CImageInfo ImgInfo;
	IGraphics::CTextureHandle TexHnd;
	if(Graphics()->LoadPNG(&ImgInfo, aFilePath, IStorage::TYPE_ALL))
	{
		const int TextureFlags = IGraphics::TEXLOAD_MULTI_DIMENSION;
		ImgInfo.m_Format = CImageInfo::FORMAT_AUTO;
		TexHnd = Graphics()->LoadTextureRaw(ImgInfo.m_Width, ImgInfo.m_Height, ImgInfo.m_Format,
			ImgInfo.m_pData, ImgInfo.m_Format, TextureFlags);
		mem_free(ImgInfo.m_pData); // TODO: keep this?
		ImgInfo.m_pData = NULL;

		if(!TexHnd.IsValid())
		{
			ed_dbg("LoadTextureRaw ERROR: could not load '%s' image file", aFilePath);
			m_Assets.m_ImageCount--;
			return false;
		}
	}
	else
	{
		ed_dbg("LoadPNG ERROR: could not load '%s' image file", aFilePath);
		m_Assets.m_ImageCount--;
		return false;
	}

	m_Assets.m_aTextureHandle[ImgID] = TexHnd;
	m_Assets.m_aTextureInfos[ImgID] = ImgInfo;

	ed_dbg("Image '%s' loaded", aFilePath);

	AssetsLoadAutomapFileForImage(ImgID);

	return true;
}

void CEditorMap2::AssetsDeleteImage(int ImgID)
{
	Graphics()->UnloadTexture(&m_Assets.m_aTextureHandle[ImgID]);
	const int SwappedID = m_Assets.m_ImageCount-1;
	tl_swap(m_Assets.m_aImageNames[ImgID], m_Assets.m_aImageNames[SwappedID]);
	tl_swap(m_Assets.m_aImageEmbeddedCrc[ImgID], m_Assets.m_aImageEmbeddedCrc[SwappedID]);
	tl_swap(m_Assets.m_aTextureHandle[ImgID], m_Assets.m_aTextureHandle[SwappedID]);
	tl_swap(m_Assets.m_aTextureInfos[ImgID], m_Assets.m_aTextureInfos[SwappedID]);
	m_Assets.m_ImageCount--;

	const int LayerCount = m_aLayers.size();
	for(int li = 0; li < LayerCount; li++)
	{
		if(m_aLayers[li].m_ImageID == ImgID)
			m_aLayers[li].m_ImageID = -1;
		if(m_aLayers[li].m_ImageID == SwappedID)
			m_aLayers[li].m_ImageID = ImgID;
	}
}

void CEditorMap2::AssetsLoadAutomapFileForImage(int ImgID)
{
	u32 ImgNameHash = m_Assets.m_aImageNameHash[ImgID];
	dbg_assert(ImgNameHash != 0x0, "Image hash is invalid");

	bool FoundTileMapper = false;
	const u32* aAutomapTileHashID = m_Assets.m_aAutomapTileHashID.base_ptr();
	const int aAutomapTileHashIDCount = m_Assets.m_aAutomapTileHashID.size();
	for(int i = 0; i < aAutomapTileHashIDCount; i++)
	{
		if(aAutomapTileHashID[i] == ImgNameHash)
		{
			FoundTileMapper = true;
			break;
		}
	}

	if(FoundTileMapper)
		return;

	char aAutomapFilePath[256];
	str_format(aAutomapFilePath, sizeof(aAutomapFilePath), "editor/automap/%s.json", m_Assets.m_aImageNames[ImgID].m_Buff);
	IOHANDLE File = Storage()->OpenFile(aAutomapFilePath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(File)
	{
		int FileSize = (int)io_length(File);
		char *pFileData = (char *)mem_alloc(FileSize, 1);
		io_read(File, pFileData, FileSize);
		io_close(File);

		// parse json data
		json_settings JsonSettings;
		mem_zero(&JsonSettings, sizeof(JsonSettings));
		char aError[256];
		json_value *pJsonData = json_parse_ex(&JsonSettings, pFileData, FileSize, aError);
		mem_free(pFileData);

		if(pJsonData == 0)
		{
			ed_log("Error parsing json: '%s' (%s)", aError, aAutomapFilePath);
			return;
		}

		// generate configurations
		const json_value &rTileset = (*pJsonData)[(const char *)AutoMap::GetTypeName(AutoMap::TYPE_TILESET)];
		if(rTileset.type == json_array)
		{
			// tile automapper
			m_Assets.m_aAutomapTileHashID.add(ImgNameHash);
			int TileMapperArrayID = m_Assets.m_aAutomapTile.add(CTilesetMapper2());
			CTilesetMapper2& TileMapper = m_Assets.m_aAutomapTile[TileMapperArrayID];
			TileMapper.LoadJsonRuleSets(rTileset);
			ed_dbg("Automap file '%s' loaded (Tileset mapper)", aAutomapFilePath);
		}
		else
		{
			const json_value &rDoodads = (*pJsonData)[(const char *)AutoMap::GetTypeName(AutoMap::TYPE_DOODADS)];
			if(rDoodads.type == json_array)
			{
				// doodad automapper
				// TODO: implement this
				//ed_dbg("Automap file '%s' loaded (Doodads mapper)", aAutomapFilePath);
			}
		}

		// clean up
		json_value_free(pJsonData);
	}
}

void CEditorMap2::AssetsLoadMissingAutomapFiles()
{
	// TODO: this could be much better
	const int ImgCount = m_Assets.m_ImageCount;
	for(int i = 0; i < ImgCount; i++)
	{
		AssetsLoadAutomapFileForImage(i);
	}
}

CTilesetMapper2* CEditorMap2::AssetsFindTilesetMapper(int ImgID)
{
	if(ImgID < 0)
		return 0;
	dbg_assert(ImgID < m_Assets.m_ImageCount, "ImgID out of bounds");

	CTilesetMapper2* pMapper = 0;

	const u32 ImgNameHash = m_Assets.m_aImageNameHash[ImgID];
	const u32* aAutomapTileHashID = m_Assets.m_aAutomapTileHashID.base_ptr();
	const int aAutomapTileHashIDCount = m_Assets.m_aAutomapTileHashID.size();
	for(int i = 0; i < aAutomapTileHashIDCount; i++)
	{
		if(aAutomapTileHashID[i] == ImgNameHash)
		{
			pMapper = &m_Assets.m_aAutomapTile[i];
			break;
		}
	}

	return pMapper;
}

CEditorMap2::CSnapshot* CEditorMap2::SaveSnapshot()
{
	// calculate snapshot size
	u64 SnapSize = sizeof(CSnapshot)-1;

	const int GroupCount = m_aGroups.size();
	SnapSize += sizeof(CGroup) * GroupCount;

	const int LayerCount = m_aLayers.size();
	SnapSize += sizeof(CMapItemLayer*) * LayerCount;
	for(int li = 0; li < LayerCount; li++)
	{
		const CLayer& Layer = m_aLayers[li];
		if(Layer.IsTileLayer())
		{
			SnapSize += sizeof(CMapItemLayerTilemap);
			SnapSize += sizeof(CTile) * Layer.m_aTiles.size();
		}
		else
		{
			SnapSize += sizeof(CMapItemLayerQuads);
			SnapSize += sizeof(CQuad) * Layer.m_aQuads.size();
		}
	}

	const int EnvelopeCount = m_aEnvelopes.size();
	SnapSize += sizeof(CMapItemEnvelope) * EnvelopeCount;

	for(int ei = 0; ei < EnvelopeCount; ei++)
	{
		SnapSize += sizeof(CEnvPoint) * m_aEnvelopes[ei].m_NumPoints;
	}

	const int ImageCount = m_Assets.m_ImageCount;
	SnapSize += sizeof(CSnapshot::m_aImageNames[0]) * ImageCount; // m_aImageNames
	SnapSize += sizeof(CSnapshot::m_aImageInfos[0]) * ImageCount; // m_aImageInfos
	SnapSize += sizeof(CSnapshot::m_aImageEmbeddedCrc[0]) * ImageCount; // m_aImageEmbeddedCrc

	ed_dbg("Map snapshot size = %luKo", SnapSize/1024);

	CSnapshot& Snap = *(CSnapshot*)mem_alloc(SnapSize, 0);
	Snap.m_GroupCount = GroupCount;
	Snap.m_LayerCount = LayerCount;
	Snap.m_EnvelopeCount = EnvelopeCount;
	Snap.m_ImageCount = ImageCount;
	Snap.m_GameGroupID = m_GameGroupID;
	Snap.m_GameLayerID = m_GameLayerID;
	Snap.m_aImageNames = (CImageName*)(Snap.m_Data);
	Snap.m_aImageEmbeddedCrc = (u32*)(Snap.m_aImageNames + ImageCount);
	Snap.m_aImageInfos = (CImageInfo*)(Snap.m_aImageEmbeddedCrc + ImageCount);
	Snap.m_aGroups = (CGroup*)(Snap.m_aImageInfos + ImageCount);
	Snap.m_apLayers = (CMapItemLayer**)(Snap.m_aGroups + GroupCount);

	for(int i = 0; i < ImageCount; i++)
	{
		mem_copy(&Snap.m_aImageNames[i], &m_Assets.m_aImageNames[i], sizeof(m_Assets.m_aImageNames[i]));
	}

	mem_copy(Snap.m_aImageEmbeddedCrc, m_Assets.m_aImageEmbeddedCrc,
		sizeof(Snap.m_aImageEmbeddedCrc[0]) * ImageCount);

	for(int i = 0; i < ImageCount; i++)
	{
		mem_copy(&Snap.m_aImageInfos[i], &m_Assets.m_aTextureInfos[i], sizeof(m_Assets.m_aTextureInfos[i]));
	}

	for(int gi = 0; gi < GroupCount; gi++)
	{
		mem_copy(&Snap.m_aGroups[gi], &m_aGroups[gi], sizeof(m_aGroups[gi]));
	}

	CMapItemLayer* pCurrentLayerData = (CMapItemLayer*)(Snap.m_apLayers + Snap.m_LayerCount);
	int TileStartID = 0;
	int QuadStartID = 0;

	for(int li = 0; li < LayerCount; li++)
	{
		const CLayer& Layer = m_aLayers[li];
		Snap.m_apLayers[li] = pCurrentLayerData;

		int aNameInt[3];
		StrToInts(aNameInt, ARR_COUNT(aNameInt), Layer.m_aName);

		if(Layer.IsTileLayer())
		{
			CMapItemLayerTilemap& Tilemap = *(CMapItemLayerTilemap*)Snap.m_apLayers[li];
			Tilemap.m_Layer.m_Type = LAYERTYPE_TILES;
			if(li == m_GameLayerID)
				Tilemap.m_Layer.m_Type = LAYERTYPE_GAME;

			memmove(Tilemap.m_aName, aNameInt, sizeof(Tilemap.m_aName));
			Tilemap.m_Color.r = (int)(Layer.m_Color.r*255);
			Tilemap.m_Color.g = (int)(Layer.m_Color.g*255);
			Tilemap.m_Color.b = (int)(Layer.m_Color.b*255);
			Tilemap.m_Color.a = (int)(Layer.m_Color.a*255);
			Tilemap.m_ColorEnv = Layer.m_ColorEnvelopeID;
			Tilemap.m_Data = TileStartID;
			TileStartID += Layer.m_Width*Layer.m_Height;
			Tilemap.m_Width = Layer.m_Width;
			Tilemap.m_Height = Layer.m_Height;
			Tilemap.m_Image = Layer.m_ImageID;

			pCurrentLayerData = (CMapItemLayer*)(&Tilemap + 1);
		}
		else
		{
			CMapItemLayerQuads& LayerQuads = *(CMapItemLayerQuads*)Snap.m_apLayers[li];
			memmove(LayerQuads.m_aName, aNameInt, sizeof(LayerQuads.m_aName));
			LayerQuads.m_Layer.m_Type = LAYERTYPE_QUADS;
			LayerQuads.m_Data = QuadStartID;
			LayerQuads.m_NumQuads = Layer.m_aQuads.size();
			QuadStartID += LayerQuads.m_NumQuads;
			LayerQuads.m_Image = Layer.m_ImageID;

			pCurrentLayerData = (CMapItemLayer*)(&LayerQuads + 1);
		}
	}

	Snap.m_aEnvelopes = (CMapItemEnvelope*)pCurrentLayerData;
	for(int ei = 0; ei < EnvelopeCount; ei++)
	{
		mem_copy(&Snap.m_aEnvelopes[ei], &m_aEnvelopes[ei], sizeof(m_aEnvelopes[ei]));
	}

	Snap.m_aTiles = (CTile*)(Snap.m_aEnvelopes + EnvelopeCount);
	CTile* pCurrentTile = Snap.m_aTiles;

	for(int li = 0; li < LayerCount; li++)
	{
		const CLayer& Layer = m_aLayers[li];
		if(Layer.IsTileLayer())
		{
			mem_copy(pCurrentTile, Layer.m_aTiles.base_ptr(), sizeof(CTile)*Layer.m_aTiles.size());
			pCurrentTile += Layer.m_aTiles.size();
		}
	}

	Snap.m_aQuads = (CQuad*)pCurrentTile;
	CQuad* pCurrentQuad = Snap.m_aQuads;

	for(int li = 0; li < LayerCount; li++)
	{
		const CLayer& Layer = m_aLayers[li];
		if(Layer.IsQuadLayer())
		{
			mem_copy(pCurrentQuad, Layer.m_aQuads.base_ptr(), sizeof(CQuad)*Layer.m_aQuads.size());
			pCurrentQuad += Layer.m_aQuads.size();
		}
	}

	Snap.m_aEnvPoints = (CEnvPoint*)pCurrentQuad;
	CEnvPoint* pCurrentEnvPoint = Snap.m_aEnvPoints;
	mem_copy(pCurrentEnvPoint, m_aEnvPoints.base_ptr(), sizeof(CEnvPoint) * m_aEnvPoints.size());

#ifdef CONF_DEBUG
	CompareSnapshot(&Snap);
#endif
	return &Snap;
}

void CEditorMap2::RestoreSnapshot(const CEditorMap2::CSnapshot* pSnapshot)
{
	const CEditorMap2::CSnapshot& Snap = *pSnapshot;

	Clear();

	m_GameGroupID = Snap.m_GameGroupID;
	m_GameLayerID = Snap.m_GameLayerID;

	AssetsClearAndSetImages(Snap.m_aImageNames, Snap.m_aImageInfos, Snap.m_aImageEmbeddedCrc,
		Snap.m_ImageCount);

	m_aGroups.add(Snap.m_aGroups, Snap.m_GroupCount);
	m_aLayers.add_empty(Snap.m_LayerCount);

	const CTile* pSnapTiles = Snap.m_aTiles;
	const CQuad* pSnapQuads = Snap.m_aQuads;

	for(int li = 0; li < Snap.m_LayerCount; li++)
	{
		const CMapItemLayer& SnapLayer = *Snap.m_apLayers[li];
		CLayer& Layer = m_aLayers[li];

		if(SnapLayer.m_Type == LAYERTYPE_GAME || SnapLayer.m_Type == LAYERTYPE_TILES)
		{
			const CMapItemLayerTilemap& SnapTilemap = *(CMapItemLayerTilemap*)Snap.m_apLayers[li];
			IntsToStr(SnapTilemap.m_aName, ARR_COUNT(SnapTilemap.m_aName), Layer.m_aName);
			Layer.m_Type = LAYERTYPE_TILES;
			Layer.m_ImageID = SnapTilemap.m_Image;
			Layer.m_Color.r = SnapTilemap.m_Color.r/255.f;
			Layer.m_Color.g = SnapTilemap.m_Color.g/255.f;
			Layer.m_Color.b = SnapTilemap.m_Color.b/255.f;
			Layer.m_Color.a = SnapTilemap.m_Color.a/255.f;
			Layer.m_ColorEnvelopeID = SnapTilemap.m_ColorEnv;
			Layer.m_Width = SnapTilemap.m_Width;
			Layer.m_Height = SnapTilemap.m_Height;
			Layer.m_aTiles.clear();
			Layer.m_aTiles.add(pSnapTiles, Layer.m_Width*Layer.m_Height);
			pSnapTiles += Layer.m_Width*Layer.m_Height;
		}
		else
		{
			const CMapItemLayerQuads& SnapQuadLayer = *(CMapItemLayerQuads*)Snap.m_apLayers[li];
			IntsToStr(SnapQuadLayer.m_aName, ARR_COUNT(SnapQuadLayer.m_aName), Layer.m_aName);
			Layer.m_Type = LAYERTYPE_QUADS;
			Layer.m_ImageID = SnapQuadLayer.m_Image;
			Layer.m_aQuads.clear();
			Layer.m_aQuads.add(pSnapQuads, SnapQuadLayer.m_NumQuads);
			pSnapQuads += SnapQuadLayer.m_NumQuads;
		}
	}

	m_aEnvelopes.add(Snap.m_aEnvelopes, Snap.m_EnvelopeCount);

	const CEnvPoint* pSnapEnvPoint = Snap.m_aEnvPoints;
	for(int ei = 0; ei < Snap.m_EnvelopeCount; ei++)
	{
		m_aEnvPoints.add(pSnapEnvPoint, m_aEnvelopes[ei].m_NumPoints);
		pSnapEnvPoint += m_aEnvelopes[ei].m_NumPoints;
	}

	ed_dbg("Map snapshot restored");

#ifdef CONF_DEBUG
	CompareSnapshot(&Snap);
#endif
}

#ifdef CONF_DEBUG
void CEditorMap2::CompareSnapshot(const CEditorMap2::CSnapshot* pSnapshot)
{
	const CEditorMap2::CSnapshot& Snap = *pSnapshot;
	dbg_assert(Snap.m_GroupCount == m_aGroups.size(), "");
	dbg_assert(Snap.m_LayerCount == m_aLayers.size(), "");
	dbg_assert(Snap.m_EnvelopeCount == m_aEnvelopes.size(), "");
	dbg_assert(Snap.m_ImageCount == m_Assets.m_ImageCount, "");
	dbg_assert(Snap.m_GameGroupID == m_GameGroupID, "");
	dbg_assert(Snap.m_GameLayerID == m_GameLayerID, "");

	for(int i = 0; i < Snap.m_ImageCount; i++)
	{
		dbg_assert(mem_comp(&Snap.m_aImageEmbeddedCrc[i], &m_Assets.m_aImageEmbeddedCrc[i],
			sizeof(Snap.m_aImageEmbeddedCrc[i])) == 0, "");
		dbg_assert(mem_comp(&Snap.m_aImageNames[i], &m_Assets.m_aImageNames[i],
			sizeof(Snap.m_aImageNames[i])) == 0, "");
		dbg_assert(mem_comp(&Snap.m_aImageInfos[i], &m_Assets.m_aTextureInfos[i],
			sizeof(Snap.m_aImageInfos[i])) == 0, "");
	}

	for(int gi = 0; gi < Snap.m_GroupCount; gi++)
	{
		const CGroup& SnapGroup = Snap.m_aGroups[gi];
		const CGroup& Group = m_aGroups[gi];
		dbg_assert(mem_comp(&SnapGroup, &Group, sizeof(CGroup)) == 0, "Groups don't match");
	}

	const CTile* pSnapTiles = Snap.m_aTiles;
	const CQuad* pSnapQuads = Snap.m_aQuads;

	for(int li = 0; li < Snap.m_LayerCount; li++)
	{
		const CMapItemLayer& SnapLayer = *Snap.m_apLayers[li];
		const CLayer& Layer = m_aLayers[li];

		int aNameInt[3];
		StrToInts(aNameInt, ARR_COUNT(aNameInt), Layer.m_aName);

		if(SnapLayer.m_Type == LAYERTYPE_GAME)
			dbg_assert(Layer.m_Type == LAYERTYPE_TILES && li == m_GameLayerID, "");
		else
			dbg_assert(SnapLayer.m_Type == LAYERTYPE_GAME || SnapLayer.m_Type == Layer.m_Type, "");

		if(Layer.IsTileLayer())
		{
			const CMapItemLayerTilemap& SnapTilemap = *(CMapItemLayerTilemap*)Snap.m_apLayers[li];
			dbg_assert(sizeof(SnapTilemap.m_aName) == sizeof(aNameInt), "");
			dbg_assert(mem_comp(SnapTilemap.m_aName, aNameInt, sizeof(SnapTilemap.m_aName)) == 0,
					   "Names don't match");
			dbg_assert(SnapTilemap.m_Image == Layer.m_ImageID, "");
			dbg_assert(SnapTilemap.m_ColorEnv == Layer.m_ColorEnvelopeID, "");
			dbg_assert(SnapTilemap.m_Color.r == (int)(Layer.m_Color.r*255), "");
			dbg_assert(SnapTilemap.m_Color.g == (int)(Layer.m_Color.g*255), "");
			dbg_assert(SnapTilemap.m_Color.b == (int)(Layer.m_Color.b*255), "");
			dbg_assert(SnapTilemap.m_Color.a == (int)(Layer.m_Color.a*255), "");
			dbg_assert(SnapTilemap.m_Width == Layer.m_Width, "");
			dbg_assert(SnapTilemap.m_Height == Layer.m_Height, "");
			dbg_assert(mem_comp(pSnapTiles, Layer.m_aTiles.base_ptr(),
				Layer.m_Width*Layer.m_Height*sizeof(CTile)) == 0, "Layer tiles don't match");
			pSnapTiles += Layer.m_Width*Layer.m_Height;
		}
		else if(Layer.IsQuadLayer())
		{
			const CMapItemLayerQuads& SnapQuadLayer = *(CMapItemLayerQuads*)Snap.m_apLayers[li];
			dbg_assert(sizeof(SnapQuadLayer.m_aName) == sizeof(aNameInt), "");
			dbg_assert(mem_comp(SnapQuadLayer.m_aName, aNameInt, sizeof(SnapQuadLayer.m_aName)) == 0,
					   "Names don't match");
			dbg_assert(SnapQuadLayer.m_Image == Layer.m_ImageID, "");
			dbg_assert(SnapQuadLayer.m_NumQuads == Layer.m_aQuads.size(), "");
			dbg_assert(mem_comp(pSnapQuads, Layer.m_aQuads.base_ptr(),
				SnapQuadLayer.m_NumQuads*sizeof(CQuad)) == 0, "Quads don't match");
			pSnapQuads += SnapQuadLayer.m_NumQuads;
		}
	}

	const CEnvPoint* pSnapEnvPoint = Snap.m_aEnvPoints;
	for(int ei = 0; ei < Snap.m_EnvelopeCount; ei++)
	{
		dbg_assert(mem_comp(&Snap.m_aEnvelopes[ei], &m_aEnvelopes[ei], sizeof(CMapItemEnvelope)) == 0, "");
		dbg_assert(mem_comp(pSnapEnvPoint, &m_aEnvPoints[m_aEnvelopes[ei].m_StartPoint],
			sizeof(CEnvPoint)*Snap.m_aEnvelopes[ei].m_NumPoints) == 0, "");
		pSnapEnvPoint += Snap.m_aEnvelopes[ei].m_NumPoints;
	}

	ed_dbg("[✔] Map snapshot matches current map data");
}
#endif

CEditorMap2::CLayer& CEditorMap2::NewTileLayer(int Width, int Height)
{
	CLayer TileLayer;
	mem_zero(TileLayer.m_aName, sizeof(TileLayer.m_aName));
	TileLayer.m_Type = LAYERTYPE_TILES;
	TileLayer.m_ImageID = -1;
	TileLayer.m_Color = vec4(1, 1, 1, 1);
	TileLayer.m_Width = Width;
	TileLayer.m_Height = Height;
	TileLayer.m_ColorEnvelopeID = -1;
	TileLayer.m_aTiles.clear();
	TileLayer.m_aTiles.add_empty(TileLayer.m_Width * TileLayer.m_Height);
	return m_aLayers.add(TileLayer);
}

CEditorMap2::CLayer&CEditorMap2::NewQuadLayer()
{
	CLayer QuadLayer;
	mem_zero(QuadLayer.m_aName, sizeof(QuadLayer.m_aName));
	QuadLayer.m_Type = LAYERTYPE_QUADS;
	QuadLayer.m_ImageID = -1;
	QuadLayer.m_Color = vec4(1, 1, 1, 1);
	QuadLayer.m_aQuads.clear();
	return m_aLayers.add(QuadLayer);
}

/*CEditorAssets::CEditorAssets()
{
	Clear();
}

void CEditorAssets::Init(IGraphics* pGraphics)
{
	m_pGraphics = pGraphics;
}

void CEditorAssets::Clear()
{
	m_ImageCount = 0;
	mem_zero(m_aImagePathHash, sizeof(m_aImagePathHash));
	mem_zero(m_aImageDataHash, sizeof(m_aImageDataHash));

	for(int i = 0; i < MAX_IMAGES; i++)
		m_aImageTextureHandle[i].Invalidate();
}

int CEditorAssets::LoadQuadImage(const char* aPath)
{
	const int PathLen = str_length(aPath);
	const u32 Hash = fnv1a32(aPath, PathLen);



	IGraphics::CTextureHandle TexHnd = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL,
		CImageInfo::FORMAT_AUTO, IGraphics::TEXLOAD_MULTI_DIMENSION);
	return -1;
}*/

IEditor *CreateEditor2() { return new CEditor2; }

CEditor2::CEditor2()
{

}

CEditor2::~CEditor2()
{

}

void CEditor2::Init()
{
	m_pInput = Kernel()->RequestInterface<IInput>();
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pConsole = CreateConsole(CFGFLAG_EDITOR);
	m_pGraphics = Kernel()->RequestInterface<IGraphics>();
	m_pTextRender = Kernel()->RequestInterface<ITextRender>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_RenderTools.m_pGraphics = m_pGraphics;
	m_RenderTools.m_pUI = &m_UI;
	m_UI.SetGraphics(m_pGraphics, m_pTextRender);

	m_MousePos = vec2(Graphics()->ScreenWidth() * 0.5f, Graphics()->ScreenHeight() * 0.5f);
	m_UiMousePos = vec2(0, 0);
	m_UiMouseDelta = vec2(0, 0);
	m_MapUiPosOffset = vec2(0,0);

	m_CheckerTexture = Graphics()->LoadTexture("editor/checker.png",
		IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	m_CursorTexture = Graphics()->LoadTexture("editor/cursor.png",
		IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	m_EntitiesTexture = Graphics()->LoadTexture("editor/entities.png",
		IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, IGraphics::TEXLOAD_MULTI_DIMENSION);
	m_GameTexture = Graphics()->LoadTexture("game.png",
		IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);

	m_pConsole->Register("load", "r", CFGFLAG_EDITOR, ConLoad, this, "Load map");
	m_pConsole->Register("save", "r", CFGFLAG_EDITOR, ConSave, this, "Save map");
	m_pConsole->Register("+show_palette", "", CFGFLAG_EDITOR, ConShowPalette, this, "Show palette");
	m_pConsole->Register("game_view", "i", CFGFLAG_EDITOR, ConGameView, this, "Toggle game view");
	m_pConsole->Register("show_grid", "i", CFGFLAG_EDITOR, ConShowGrid, this, "Toggle grid");
	m_pConsole->Register("undo", "", CFGFLAG_EDITOR, ConUndo, this, "Undo");
	m_pConsole->Register("redo", "", CFGFLAG_EDITOR, ConRedo, this, "Redo");
	m_pConsole->Register("delete_image", "i", CFGFLAG_EDITOR, ConDeleteImage, this, "Delete image");
	m_InputConsole.Init(m_pConsole, m_pGraphics, &m_UI, m_pTextRender, m_pInput);

	// grenade pickup
	{
		const float SpriteW = g_pData->m_aSprites[SPRITE_PICKUP_GRENADE].m_W;
		const float SpriteH = g_pData->m_aSprites[SPRITE_PICKUP_GRENADE].m_H;
		const float ScaleFactor = sqrt(SpriteW*SpriteW+SpriteH*SpriteH);
		const int VisualSize = g_pData->m_Weapons.m_aId[WEAPON_GRENADE].m_VisualSize;
		m_RenderGrenadePickupSize = vec2(VisualSize * (SpriteW/ScaleFactor),
										 VisualSize * (SpriteH/ScaleFactor));
	}
	// shotgun pickup
	{
		const float SpriteW = g_pData->m_aSprites[SPRITE_PICKUP_SHOTGUN].m_W;
		const float SpriteH = g_pData->m_aSprites[SPRITE_PICKUP_SHOTGUN].m_H;
		const float ScaleFactor = sqrt(SpriteW*SpriteW+SpriteH*SpriteH);
		const int VisualSize = g_pData->m_Weapons.m_aId[WEAPON_SHOTGUN].m_VisualSize;
		m_RenderShotgunPickupSize = vec2(VisualSize * (SpriteW/ScaleFactor),
										 VisualSize * (SpriteH/ScaleFactor));
	}
	// laser pickup
	{
		const float SpriteW = g_pData->m_aSprites[SPRITE_PICKUP_LASER].m_W;
		const float SpriteH = g_pData->m_aSprites[SPRITE_PICKUP_LASER].m_H;
		const float ScaleFactor = sqrt(SpriteW*SpriteW+SpriteH*SpriteH);
		const int VisualSize = g_pData->m_Weapons.m_aId[WEAPON_LASER].m_VisualSize;
		m_RenderLaserPickupSize = vec2(VisualSize * (SpriteW/ScaleFactor),
									   VisualSize * (SpriteH/ScaleFactor));
	}

	m_Map.Init(m_pStorage, m_pGraphics, m_pConsole);
	m_Brush.m_aTiles.clear();

	HistoryClear();
	m_pHistoryEntryCurrent = 0x0;

	/*
	m_Map.LoadDefault();
	OnMapLoaded();
	*/
	if(!LoadMap("maps/ctf7.map")) {
		dbg_break();
	}
}

void CEditor2::UpdateAndRender()
{
	Update();
	Render();
}

bool CEditor2::HasUnsavedData() const
{
	return false;
}

void CEditor2::OnInput(IInput::CEvent Event)
{
	if(m_InputConsole.IsOpen())
	{
		m_InputConsole.OnInput(Event);
		return;
	}
}

void CEditor2::Update()
{
	m_LocalTime = Client()->LocalTime();

	for(int i = 0; i < Input()->NumEvents(); i++)
	{
		IInput::CEvent e = Input()->GetEvent(i);
		// FIXME: this doesn't work with limitfps or asyncrender 1 (when a frame gets skipped)
		// since we update only when we render
		// in practice this isn't a big issue since the input stack gets cleared at the end of Render()
		/*if(!Input()->IsEventValid(&e))
			continue;*/
		OnInput(e);
	}

	UI()->StartCheck();
	const CUIRect UiScreenRect = *UI()->Screen();

	// mouse input
	float rx = 0, ry = 0;
	Input()->MouseRelative(&rx, &ry);
	UI()->ConvertMouseMove(&rx, &ry);

	m_MousePos.x = clamp(m_MousePos.x + rx, 0.0f, (float)Graphics()->ScreenWidth());
	m_MousePos.y = clamp(m_MousePos.y + ry, 0.0f, (float)Graphics()->ScreenHeight());
	float NewUiMousePosX = (m_MousePos.x / (float)Graphics()->ScreenWidth()) * UiScreenRect.w;
	float NewUiMousePosY = (m_MousePos.y / (float)Graphics()->ScreenHeight()) * UiScreenRect.h;
	m_UiMouseDelta.x = NewUiMousePosX-m_UiMousePos.x;
	m_UiMouseDelta.y = NewUiMousePosY-m_UiMousePos.y;
	m_UiMousePos.x = NewUiMousePosX;
	m_UiMousePos.y = NewUiMousePosY;

	enum
	{
		MOUSE_LEFT=1,
		MOUSE_RIGHT=2,
		MOUSE_MIDDLE=4,
	};

	int MouseButtons = 0;
	if(Input()->KeyIsPressed(KEY_MOUSE_1)) MouseButtons |= MOUSE_LEFT;
	if(Input()->KeyIsPressed(KEY_MOUSE_2)) MouseButtons |= MOUSE_RIGHT;
	if(Input()->KeyIsPressed(KEY_MOUSE_3)) MouseButtons |= MOUSE_MIDDLE;
	UI()->Update(m_UiMousePos.x, m_UiMousePos.y, 0, 0, MouseButtons);

	// was the mouse on a ui element
	m_WasMouseOnUiElement = UI()->HotItem() != 0;
	UI()->SetHotItem(0);

	if(Input()->KeyPress(KEY_F1))
	{
		m_InputConsole.ToggleOpen();
	}

	if(!m_InputConsole.IsOpen())
	{
		if(m_UiCurrentPopupID == POPUP_NONE)
		{
			// move view
			if(MouseButtons&MOUSE_RIGHT)
			{
				m_MapUiPosOffset -= m_UiMouseDelta;
			}

			// zoom with mouse wheel
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
				ChangeZoom(m_Zoom / 1.1f);
			else if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				ChangeZoom(m_Zoom * 1.1f);

			if(Input()->KeyPress(KEY_HOME))
			{
				ResetCamera();
			}

			// undo / redo
			const bool IsCtrlPressed = Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL);
			if(IsCtrlPressed && Input()->KeyPress(KEY_Z))
				HistoryUndo();
			else if(IsCtrlPressed && Input()->KeyPress(KEY_Y))
				HistoryRedo();

			// TODO: remove
			if(IsCtrlPressed && Input()->KeyPress(KEY_A))
				ChangePage((m_Page+1) % PAGE_COUNT_);

			if(IsToolSelect() && Input()->KeyPress(KEY_ESCAPE))
				m_TileSelection.Deselect();
			if(IsToolBrush() && Input()->KeyPress(KEY_ESCAPE))
				BrushClear();
		}

		if(IsToolBrush() && Input()->KeyIsPressed(KEY_SPACE) && m_UiCurrentPopupID != POPUP_BRUSH_PALETTE)
			m_UiCurrentPopupID = POPUP_BRUSH_PALETTE;
		else if((!IsToolBrush() || !Input()->KeyIsPressed(KEY_SPACE)) && m_UiCurrentPopupID == POPUP_BRUSH_PALETTE)
			m_UiCurrentPopupID = POPUP_NONE;
	}
}

void CEditor2::Render()
{
	const CUIRect UiScreenRect = *UI()->Screen();
	m_UiScreenRect = UiScreenRect;
	m_GfxScreenWidth = Graphics()->ScreenWidth();
	m_GfxScreenHeight = Graphics()->ScreenHeight();

	Graphics()->Clear(0.3f, 0.3f, 0.3f);

	if(m_Page == PAGE_MAP_EDITOR)
		RenderMap();
	else if(m_Page == PAGE_ASSET_MANAGER)
		RenderAssetManager();

	// console
	m_InputConsole.Render();

	// render mouse cursor
	{
		Graphics()->MapScreen(UiScreenRect.x, UiScreenRect.y, UiScreenRect.w, UiScreenRect.h);
		Graphics()->TextureSet(m_CursorTexture);
		Graphics()->WrapClamp();
		Graphics()->QuadsBegin();

		const vec3 aToolColors[] = {
			vec3(1, 0.2, 1),
			vec3(1, 0.5, 0.2),
			vec3(0.2, 0.7, 1),
		};
		const vec3& ToolColor = aToolColors[m_Tool];
		Graphics()->SetColor(ToolColor.r, ToolColor.g, ToolColor.b, 1);
		/*if(UI()->HotItem())
			Graphics()->SetColor(1,0.5,1,1);*/
		IGraphics::CQuadItem QuadItem(m_UiMousePos.x, m_UiMousePos.y, 16.0f, 16.0f);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	}

	UI()->FinishCheck();
	Input()->Clear();
}

void CEditor2::RenderLayerGameEntities(const CEditorMap2::CLayer& GameLayer)
{
	const CTile* pTiles = GameLayer.m_aTiles.base_ptr();
	const int LayerWidth = GameLayer.m_Width;
	const int LayerHeight = GameLayer.m_Height;

	Graphics()->TextureSet(m_GameTexture);
	Graphics()->QuadsBegin();

	// TODO: cache sprite base positions?
	struct CEntitySprite
	{
		int m_SpriteID;
		vec2 m_Pos;
		vec2 m_Size;
	};

	CEntitySprite aEntitySprites[2048];
	int EntitySpriteCount = 0;

	const float HealthArmorSize = 64*0.7071067811865475244;
	const vec2 NinjaSize(128*(256/263.87876003953027518857),
						 128*(64/263.87876003953027518857));

	const float TileSize = 32.f;
	const float Time = Client()->LocalTime();

	for(int ty = 0; ty < LayerHeight; ty++)
	{
		for(int tx = 0; tx < LayerWidth; tx++)
		{
			const int tid = ty*LayerWidth+tx;
			const u8 Index = pTiles[tid].m_Index - ENTITY_OFFSET;
			if(!Index)
				continue;

			vec2 BasePos(tx*TileSize, ty*TileSize);
			const float Offset = tx + ty;
			vec2 PickupPos = BasePos;
			PickupPos.x += cosf(Time*2.0f+Offset)*2.5f;
			PickupPos.y += sinf(Time*2.0f+Offset)*2.5f;

			if(Index == ENTITY_HEALTH_1)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_PICKUP_HEALTH,
					PickupPos - vec2((HealthArmorSize-TileSize)*0.5f,(HealthArmorSize-TileSize)*0.5f),
					vec2(HealthArmorSize, HealthArmorSize)
				};
			}
			else if(Index == ENTITY_ARMOR_1)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_PICKUP_ARMOR,
					PickupPos - vec2((HealthArmorSize-TileSize)*0.5f,(HealthArmorSize-TileSize)*0.5f),
					vec2(HealthArmorSize, HealthArmorSize)
				};
			}
			else if(Index == ENTITY_WEAPON_GRENADE)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_PICKUP_GRENADE,
					PickupPos - vec2((m_RenderGrenadePickupSize.x-TileSize)*0.5f,
						(m_RenderGrenadePickupSize.y-TileSize)*0.5f),
					m_RenderGrenadePickupSize
				};
			}
			else if(Index == ENTITY_WEAPON_SHOTGUN)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_PICKUP_SHOTGUN,
					PickupPos - vec2((m_RenderShotgunPickupSize.x-TileSize)*0.5f,
						(m_RenderShotgunPickupSize.y-TileSize)*0.5f),
					m_RenderShotgunPickupSize
				};
			}
			else if(Index == ENTITY_WEAPON_LASER)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_PICKUP_LASER,
					PickupPos - vec2((m_RenderLaserPickupSize.x-TileSize)*0.5f,
						(m_RenderLaserPickupSize.y-TileSize)*0.5f),
					m_RenderLaserPickupSize
				};
			}
			else if(Index == ENTITY_POWERUP_NINJA)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_PICKUP_NINJA,
					PickupPos - vec2((NinjaSize.x-TileSize)*0.5f, (NinjaSize.y-TileSize)*0.5f),
					NinjaSize
				};
			}
			else if(Index == ENTITY_FLAGSTAND_RED)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_FLAG_RED,
					BasePos - vec2(0, 54),
					vec2(42, 84)
				};
			}
			else if(Index == ENTITY_FLAGSTAND_BLUE)
			{
				aEntitySprites[EntitySpriteCount++] = {
					SPRITE_FLAG_BLUE,
					BasePos - vec2(0, 54),
					vec2(42, 84)
				};
			}
		}
	}

	for(int i = 0; i < EntitySpriteCount; i++)
	{
		const CEntitySprite& e = aEntitySprites[i];
		RenderTools()->SelectSprite(e.m_SpriteID);
		IGraphics::CQuadItem Quad(e.m_Pos.x, e.m_Pos.y, e.m_Size.x, e.m_Size.y);
		Graphics()->QuadsDrawTL(&Quad, 1);
	}

	Graphics()->QuadsEnd();
}

inline vec2 CEditor2::CalcGroupScreenOffset(float WorldWidth, float WorldHeight, float PosX, float PosY,
										   float ParallaxX, float ParallaxY)
{
	// we add UiScreenRect.w*0.5 and UiScreenRect.h*0.5 because in the game the view
	// is based on the center of the screen
	const CUIRect UiScreenRect = m_UiScreenRect;
	const float MapOffX = (((m_MapUiPosOffset.x+UiScreenRect.w*0.5) * ParallaxX) - UiScreenRect.w*0.5)/
						  UiScreenRect.w * WorldWidth + PosX;
	const float MapOffY = (((m_MapUiPosOffset.y+UiScreenRect.h*0.5) * ParallaxY) - UiScreenRect.h*0.5)/
						  UiScreenRect.h * WorldHeight + PosY;
	return vec2(MapOffX, MapOffY);
}

vec2 CEditor2::CalcGroupWorldPosFromUiPos(int GroupID, float WorldWidth, float WorldHeight, vec2 UiPos)
{
	const CEditorMap2::CGroup& G = m_Map.m_aGroups[GroupID];
	const float OffX = G.m_OffsetX;
	const float OffY = G.m_OffsetY;
	const float ParaX = G.m_ParallaxX/100.f;
	const float ParaY = G.m_ParallaxY/100.f;
	// we add UiScreenRect.w*0.5 and UiScreenRect.h*0.5 because in the game the view
	// is based on the center of the screen
	const CUIRect UiScreenRect = m_UiScreenRect;
	const float MapOffX = (((m_MapUiPosOffset.x + UiScreenRect.w*0.5) * ParaX) -
		UiScreenRect.w*0.5 + UiPos.x)/ UiScreenRect.w * WorldWidth + OffX;
	const float MapOffY = (((m_MapUiPosOffset.y + UiScreenRect.h*0.5) * ParaY) -
		UiScreenRect.h*0.5 + UiPos.y)/ UiScreenRect.h * WorldHeight + OffY;
	return vec2(MapOffX, MapOffY);
}

CUIRect CEditor2::CalcUiRectFromGroupWorldRect(int GroupID, float WorldWidth, float WorldHeight,
	CUIRect WorldRect)
{
	const CEditorMap2::CGroup& G = m_Map.m_aGroups[GroupID];
	const float OffX = G.m_OffsetX;
	const float OffY = G.m_OffsetY;
	const float ParaX = G.m_ParallaxX/100.f;
	const float ParaY = G.m_ParallaxY/100.f;

	const CUIRect UiScreenRect = m_UiScreenRect;
	const vec2 MapOff = CalcGroupScreenOffset(WorldWidth, WorldHeight, OffX, OffY, ParaX, ParaY);

	const float UiX = ((-MapOff.x + WorldRect.x) / WorldWidth) * UiScreenRect.w;
	const float UiY = ((-MapOff.y + WorldRect.y) / WorldHeight) * UiScreenRect.h;
	const float UiW = (WorldRect.w / WorldWidth) * UiScreenRect.w;
	const float UiH = (WorldRect.h / WorldHeight) * UiScreenRect.h;
	const CUIRect r = {UiX, UiY, UiW, UiH};
	return r;
}

void CEditor2::StaticEnvelopeEval(float TimeOffset, int EnvID, float* pChannels, void* pUser)
{
	CEditor2 *pThis = (CEditor2 *)pUser;
	if(EnvID >= 0)
		pThis->EnvelopeEval(TimeOffset, EnvID, pChannels);
}

void CEditor2::EnvelopeEval(float TimeOffset, int EnvID, float* pChannels)
{
	pChannels[0] = 0;
	pChannels[1] = 0;
	pChannels[2] = 0;
	pChannels[3] = 0;

	dbg_assert(EnvID < m_Map.m_aEnvelopes.size(), "EnvID out of bounds");
	if(EnvID >= m_Map.m_aEnvelopes.size())
		return;

	const CMapItemEnvelope& Env = m_Map.m_aEnvelopes[EnvID];
	const CEnvPoint* pPoints = &m_Map.m_aEnvPoints[0];

	float Time = Client()->LocalTime();
	RenderTools()->RenderEvalEnvelope(pPoints + Env.m_StartPoint, Env.m_NumPoints, 4,
									  Time+TimeOffset, pChannels);
}

void CEditor2::RenderMap()
{
	// get world view points based on neutral paramters
	float aWorldViewRectPoints[4];
	RenderTools()->MapScreenToWorld(0, 0, 1, 1, 0, 0, Graphics()->ScreenAspect(), 1, aWorldViewRectPoints);

	const float WorldViewWidth = aWorldViewRectPoints[2]-aWorldViewRectPoints[0];
	const float WorldViewHeight = aWorldViewRectPoints[3]-aWorldViewRectPoints[1];
	const float ZoomWorldViewWidth = WorldViewWidth * m_Zoom;
	const float ZoomWorldViewHeight = WorldViewHeight * m_Zoom;
	m_ZoomWorldViewWidth = ZoomWorldViewWidth;
	m_ZoomWorldViewHeight = ZoomWorldViewHeight;

	const float FakeToScreenX = m_GfxScreenWidth/ZoomWorldViewWidth;
	const float FakeToScreenY = m_GfxScreenHeight/ZoomWorldViewHeight;
	const float TileSize = 32;

	float SelectedParallaxX = 1;
	float SelectedParallaxY = 1;
	float SelectedPositionX = 0;
	float SelectedPositionY = 0;
	const int SelectedLayerID = m_UiSelectedLayerID != -1 ? m_UiSelectedLayerID : m_Map.m_GameLayerID;
	const int SelectedGroupID = m_UiSelectedLayerID != -1 ? m_UiSelectedGroupID : m_Map.m_GameGroupID;
	dbg_assert(SelectedLayerID >= 0, "No layer selected");
	dbg_assert(SelectedGroupID >= 0, "Parent group of selected layer not found");
	const CEditorMap2::CLayer& SelectedLayer = m_Map.m_aLayers[SelectedLayerID];
	const CEditorMap2::CGroup& SelectedGroup = m_Map.m_aGroups[SelectedGroupID];
	SelectedParallaxX = SelectedGroup.m_ParallaxX / 100.f;
	SelectedParallaxY = SelectedGroup.m_ParallaxY / 100.f;
	SelectedPositionX = SelectedGroup.m_OffsetX;
	SelectedPositionY = SelectedGroup.m_OffsetY;

	const vec2 SelectedScreenOff = CalcGroupScreenOffset(ZoomWorldViewWidth, ZoomWorldViewHeight,
		SelectedPositionX, SelectedPositionY, SelectedParallaxX, SelectedParallaxY);

	// background
	{
		Graphics()->MapScreen(0, 0, ZoomWorldViewWidth, ZoomWorldViewHeight);
		Graphics()->TextureSet(m_CheckerTexture);
		Graphics()->BlendNormal();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.5f, 0.5f, 0.5f, 1.0f);

		// align background with grid
		float StartX = fract(SelectedScreenOff.x/(TileSize*2));
		float StartY = fract(SelectedScreenOff.y/(TileSize*2));
		Graphics()->QuadsSetSubset(StartX, StartY,
								   ZoomWorldViewWidth/(TileSize*2)+StartX,
								   ZoomWorldViewHeight/(TileSize*2)+StartY);

		IGraphics::CQuadItem QuadItem(0, 0, ZoomWorldViewWidth, ZoomWorldViewHeight);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}

	// render map
	const int GroupCount = m_Map.m_aGroups.size();
	const int BaseTilemapFlags = m_ConfigShowExtendedTilemaps ? TILERENDERFLAG_EXTEND:0;

	for(int gi = 0; gi < GroupCount; gi++)
	{
		if(m_UiGroupHidden[gi])
			continue;

		CEditorMap2::CGroup& Group = m_Map.m_aGroups[gi];

		// group clip
		const vec2 ClipOff = CalcGroupScreenOffset(ZoomWorldViewWidth, ZoomWorldViewHeight,
			0, 0, 1, 1);
		const CUIRect ClipScreenRect = { ClipOff.x, ClipOff.y, ZoomWorldViewWidth, ZoomWorldViewHeight };
		Graphics()->MapScreen(ClipScreenRect.x, ClipScreenRect.y, ClipScreenRect.x+ClipScreenRect.w,
			ClipScreenRect.y+ClipScreenRect.h);

		const bool Clipped = Group.m_ClipWidth > 0 && Group.m_ClipHeight > 0;
		if(Clipped)
		{
			float x0 = (Group.m_ClipX - ClipScreenRect.x) / ClipScreenRect.w;
			float y0 = (Group.m_ClipY - ClipScreenRect.y) / ClipScreenRect.h;
			float x1 = ((Group.m_ClipX+Group.m_ClipWidth) - ClipScreenRect.x) / ClipScreenRect.w;
			float y1 = ((Group.m_ClipY+Group.m_ClipHeight) - ClipScreenRect.y) / ClipScreenRect.h;

			if(x1 < 0.0f || x0 > 1.0f || y1 < 0.0f || y0 > 1.0f)
				continue;

			const float GfxScreenW = m_GfxScreenWidth;
			const float GfxScreenH = m_GfxScreenHeight;
			Graphics()->ClipEnable((int)(x0*GfxScreenW), (int)(y0*GfxScreenH),
				(int)((x1-x0)*GfxScreenW), (int)((y1-y0)*GfxScreenH));
		}

		const float ParallaxX = Group.m_ParallaxX / 100.f;
		const float ParallaxY = Group.m_ParallaxY / 100.f;
		const float OffsetX = Group.m_OffsetX;
		const float OffsetY = Group.m_OffsetY;
		const vec2 MapOff = CalcGroupScreenOffset(ZoomWorldViewWidth, ZoomWorldViewHeight,
												  OffsetX, OffsetY, ParallaxX, ParallaxY);
		CUIRect ScreenRect = { MapOff.x, MapOff.y, ZoomWorldViewWidth, ZoomWorldViewHeight };

		Graphics()->MapScreen(ScreenRect.x, ScreenRect.y, ScreenRect.x+ScreenRect.w,
							  ScreenRect.y+ScreenRect.h);


		const int LayerCount = Group.m_LayerCount;

		for(int li = 0; li < LayerCount; li++)
		{
			const int LyID = Group.m_apLayerIDs[li];
			if(m_UiLayerHidden[LyID])
				continue;

			const CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LyID];

			if(Layer.m_Type == LAYERTYPE_TILES)
			{
				const float LyWidth = Layer.m_Width;
				const float LyHeight = Layer.m_Height;
				vec4 LyColor = Layer.m_Color;
				const CTile *pTiles = Layer.m_aTiles.base_ptr();

				if(LyID == m_Map.m_GameLayerID)
				{
					if(m_ConfigShowGameEntities)
						RenderLayerGameEntities(Layer);
					continue;
				}

				if(m_UiGroupHovered[gi] || m_UiLayerHovered[LyID])
					LyColor = vec4(1, 0, 1, 1);

				/*if(SelectedLayerID >= 0 && SelectedLayerID != LyID)
					LyColor.a = 0.5f;*/

				if(Layer.m_ImageID == -1)
					Graphics()->TextureClear();
				else
					Graphics()->TextureSet(m_Map.m_Assets.m_aTextureHandle[Layer.m_ImageID]);

				Graphics()->BlendNone();
				RenderTools()->RenderTilemap(pTiles, LyWidth, LyHeight, TileSize, LyColor,
											 BaseTilemapFlags|LAYERRENDERFLAG_OPAQUE,
											 StaticEnvelopeEval, this, Layer.m_ColorEnvelopeID,
											 Layer.m_ColorEnvOffset);

				Graphics()->BlendNormal();
				RenderTools()->RenderTilemap(pTiles, LyWidth, LyHeight, TileSize, LyColor,
											 BaseTilemapFlags|LAYERRENDERFLAG_TRANSPARENT,
											 StaticEnvelopeEval, this, Layer.m_ColorEnvelopeID,
											 Layer.m_ColorEnvOffset);
			}
			else if(Layer.m_Type == LAYERTYPE_QUADS)
			{
				if(Layer.m_ImageID == -1)
					Graphics()->TextureClear();
				else
					Graphics()->TextureSet(m_Map.m_Assets.m_aTextureHandle[Layer.m_ImageID]);

				Graphics()->BlendNormal();
				if(m_UiGroupHovered[gi] || m_UiLayerHovered[LyID])
					Graphics()->BlendAdditive();

				RenderTools()->RenderQuads(Layer.m_aQuads.base_ptr(), Layer.m_aQuads.size(),
						LAYERRENDERFLAG_TRANSPARENT, StaticEnvelopeEval, this);
			}
		}

		if(Clipped)
			Graphics()->ClipDisable();
	}

	// game layer
	const int LyID = m_Map.m_GameLayerID;
	if(!m_ConfigShowGameEntities && !m_UiLayerHidden[LyID] && !m_UiGroupHidden[m_Map.m_GameGroupID])
	{
		const vec2 MapOff = CalcGroupScreenOffset(ZoomWorldViewWidth, ZoomWorldViewHeight, 0, 0, 1, 1);
		CUIRect ScreenRect = { MapOff.x, MapOff.y, ZoomWorldViewWidth, ZoomWorldViewHeight };

		Graphics()->MapScreen(ScreenRect.x, ScreenRect.y, ScreenRect.x+ScreenRect.w,
							  ScreenRect.y+ScreenRect.h);

		const CEditorMap2::CLayer& LayerTile = m_Map.m_aLayers[LyID];
		const float LyWidth = LayerTile.m_Width;
		const float LyHeight = LayerTile.m_Height;
		vec4 LyColor = LayerTile.m_Color;
		const CTile *pTiles = LayerTile.m_aTiles.base_ptr();

		Graphics()->TextureSet(m_EntitiesTexture);
		Graphics()->BlendNone();
		RenderTools()->RenderTilemap(pTiles, LyWidth, LyHeight, TileSize, LyColor,
									 /*TILERENDERFLAG_EXTEND|*/LAYERRENDERFLAG_OPAQUE,
									 0, 0, -1, 0);

		Graphics()->BlendNormal();
		RenderTools()->RenderTilemap(pTiles, LyWidth, LyHeight, TileSize, LyColor,
									 /*TILERENDERFLAG_EXTEND|*/LAYERRENDERFLAG_TRANSPARENT,
									 0, 0, -1, 0);
	}


	Graphics()->BlendNormal();

	// origin and border
	CUIRect ScreenRect = { SelectedScreenOff.x, SelectedScreenOff.y,
						   ZoomWorldViewWidth, ZoomWorldViewHeight };
	Graphics()->MapScreen(ScreenRect.x, ScreenRect.y, ScreenRect.x+ScreenRect.w,
						  ScreenRect.y+ScreenRect.h);

	IGraphics::CQuadItem OriginLineX(0, 0, TileSize, 2/FakeToScreenY);
	IGraphics::CQuadItem RecOriginLineYtY(0, 0, 2/FakeToScreenX, TileSize);
	float LayerWidth = SelectedLayer.m_Width * TileSize;
	float LayerHeight = SelectedLayer.m_Height * TileSize;

	const float bw = 1.0f / FakeToScreenX;
	const float bh = 1.0f / FakeToScreenY;
	IGraphics::CQuadItem aBorders[4] = {
		IGraphics::CQuadItem(0, 0, LayerWidth, bh),
		IGraphics::CQuadItem(0, LayerHeight, LayerWidth, bh),
		IGraphics::CQuadItem(0, 0, bw, LayerHeight),
		IGraphics::CQuadItem(LayerWidth, 0, bw, LayerHeight)
	};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	if(SelectedLayer.IsTileLayer())
	{
		// grid
		if(m_ConfigShowGrid)
		{
			const float GridAlpha =  0.25f;
			Graphics()->SetColor(1.0f * GridAlpha, 1.0f * GridAlpha, 1.0f * GridAlpha, GridAlpha);
			float StartX = SelectedScreenOff.x - fract(SelectedScreenOff.x/TileSize) * TileSize;
			float StartY = SelectedScreenOff.y - fract(SelectedScreenOff.y/TileSize) * TileSize;
			float EndX = SelectedScreenOff.x+ZoomWorldViewWidth;
			float EndY = SelectedScreenOff.y+ZoomWorldViewHeight;
			for(float x = StartX; x < EndX; x+= TileSize)
			{
				const bool MajorLine = (int)(x/TileSize)%10 == 0 && m_ConfigShowGridMajor;
				if(MajorLine)
				{
					Graphics()->SetColor(0.5f * GridAlpha, 0.5f * GridAlpha, 1.0f * GridAlpha,
						GridAlpha);
				}

				IGraphics::CQuadItem Line(x, SelectedScreenOff.y, bw, ZoomWorldViewHeight);
				Graphics()->QuadsDrawTL(&Line, 1);

				if(MajorLine)
				{
					Graphics()->SetColor(1.0f * GridAlpha, 1.0f * GridAlpha, 1.0f * GridAlpha, GridAlpha);
				}
			}
			for(float y = StartY; y < EndY; y+= TileSize)
			{
				const bool MajorLine = (int)(y/TileSize)%10 == 0 && m_ConfigShowGridMajor;
				if(MajorLine)
				{
					Graphics()->SetColor(1.0f * GridAlpha*2.0f, 1.0f * GridAlpha*2.0f, 1.0f * GridAlpha*2.0f,
						GridAlpha*2.0f);
				}

				IGraphics::CQuadItem Line(SelectedScreenOff.x, y, ZoomWorldViewWidth, bh);
				Graphics()->QuadsDrawTL(&Line, 1);

				if(MajorLine)
				{
					Graphics()->SetColor(1.0f * GridAlpha, 1.0f * GridAlpha, 1.0f * GridAlpha, GridAlpha);
				}
			}
		}

		// borders
		Graphics()->SetColor(1, 1, 1, 1);
		Graphics()->QuadsDrawTL(aBorders, 4);
	}

	Graphics()->SetColor(0, 0, 1, 1);
	Graphics()->QuadsDrawTL(&OriginLineX, 1);
	Graphics()->SetColor(0, 1, 0, 1);
	Graphics()->QuadsDrawTL(&RecOriginLineYtY, 1);

	Graphics()->QuadsEnd();

	// hud
	RenderMapOverlay();

	// user interface
	RenderMapEditorUI();
}

void CEditor2::RenderMapOverlay()
{
	// NOTE: we're in selected group world space here
	if(m_UiCurrentPopupID != POPUP_NONE)
		return;

	const float TileSize = 32;

	const vec2 MouseWorldPos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID, m_ZoomWorldViewWidth,
		m_ZoomWorldViewHeight, m_UiMousePos);

	const int MouseTx = floor(MouseWorldPos.x/TileSize);
	const int MouseTy = floor(MouseWorldPos.y/TileSize);
	const vec2 GridMousePos(MouseTx*TileSize, MouseTy*TileSize);

	static CUIMouseDrag s_MapViewDrag;
	bool FinishedDragging = UiDoMouseDragging(0, m_UiMainViewRect, &s_MapViewDrag);

	const bool CanClick = !m_WasMouseOnUiElement;

	// TODO: kinda weird?
	if(!CanClick)
		s_MapViewDrag = {};

	const int SelectedLayerID = m_UiSelectedLayerID != -1 ? m_UiSelectedLayerID : m_Map.m_GameLayerID;
	const CEditorMap2::CGroup& SelectedGroup = m_Map.m_aGroups[m_UiSelectedGroupID];
	const CEditorMap2::CLayer& SelectedTileLayer = m_Map.m_aLayers[SelectedLayerID];

	if(SelectedTileLayer.IsTileLayer())
	{
		// cell overlay, select rectangle
		if(IsToolBrush() || IsToolSelect())
		{
			if(s_MapViewDrag.m_IsDragging)
			{
				const vec2 StartMouseWorldPos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID,
					m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, s_MapViewDrag.m_StartDragPos);
				const vec2 EndWorldPos = MouseWorldPos;

				const int StartTX = floor(StartMouseWorldPos.x/TileSize);
				const int StartTY = floor(StartMouseWorldPos.y/TileSize);
				const int EndTX = floor(EndWorldPos.x/TileSize);
				const int EndTY = floor(EndWorldPos.y/TileSize);

				const int DragStartTX = min(StartTX, EndTX);
				const int DragStartTY = min(StartTY, EndTY);
				const int DragEndTX = max(StartTX, EndTX);
				const int DragEndTY = max(StartTY, EndTY);

				const CUIRect HoverRect = {DragStartTX*TileSize, DragStartTY*TileSize,
					(DragEndTX+1-DragStartTX)*TileSize, (DragEndTY+1-DragStartTY)*TileSize};
				vec4 HoverColor = StyleColorTileHover;
				HoverColor.a += sinf(m_LocalTime * 2.0) * 0.1;
				DrawRectBorderMiddle(HoverRect, HoverColor, 2, StyleColorTileHoverBorder);
			}
			else if(m_Brush.IsEmpty())
			{
				const CUIRect HoverRect = {GridMousePos.x, GridMousePos.y, TileSize, TileSize};
				vec4 HoverColor = StyleColorTileHover;
				HoverColor.a += sinf(m_LocalTime * 2.0) * 0.1;
				DrawRect(HoverRect, HoverColor);
			}
		}

		if(IsToolSelect())
		{
			if(s_MapViewDrag.m_IsDragging)
				m_TileSelection.Deselect();

			if(FinishedDragging)
			{
				const vec2 StartMouseWorldPos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID,
					m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, s_MapViewDrag.m_StartDragPos);
				const vec2 EndMouseWorldPos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID,
					m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, s_MapViewDrag.m_EndDragPos);

				// const int StartTX = floor(StartMouseWorldPos.x/TileSize);
				// const int StartTY = floor(StartMouseWorldPos.y/TileSize);
				// const int EndTX = floor(EndMouseWorldPos.x/TileSize);
				// const int EndTY = floor(EndMouseWorldPos.y/TileSize);

				// const int SelStartX = clamp(min(StartTX, EndTX), 0, SelectedTileLayer.m_Width-1);
				// const int SelStartY = clamp(min(StartTY, EndTY), 0, SelectedTileLayer.m_Height-1);
				// const int SelEndX = clamp(max(StartTX, EndTX), 0, SelectedTileLayer.m_Width-1) + 1;
				// const int SelEndY = clamp(max(StartTY, EndTY), 0, SelectedTileLayer.m_Height-1) + 1;

				m_TileSelection.Select(
					floor(StartMouseWorldPos.x/TileSize),
					floor(StartMouseWorldPos.y/TileSize),
					floor(EndMouseWorldPos.x/TileSize),
					floor(EndMouseWorldPos.y/TileSize)
				);

				m_TileSelection.FitLayer(SelectedTileLayer);
			}

			if(m_TileSelection.IsSelected())
			{
				// fit selection to possibly newly selected layer
				m_TileSelection.FitLayer(SelectedTileLayer);

				CUIRect SelectRect = {
					m_TileSelection.m_StartTX * TileSize,
					m_TileSelection.m_StartTY * TileSize,
					(m_TileSelection.m_EndTX+1-m_TileSelection.m_StartTX)*TileSize,
					(m_TileSelection.m_EndTY+1-m_TileSelection.m_StartTY)*TileSize
				};
				vec4 HoverColor = StyleColorTileSelection;
				HoverColor.a += sinf(m_LocalTime * 2.0) * 0.05;
				DrawRectBorderMiddle(SelectRect, HoverColor, 2, vec4(1,1,1,1));
			}
		}

		if(IsToolBrush())
		{
			if(m_Brush.IsEmpty())
			{
				// get tiles from map when we're done selecting
				if(FinishedDragging)
				{
					const vec2 StartMouseWorldPos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID,
						m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, s_MapViewDrag.m_StartDragPos);
					const vec2 EndMouseWorldPos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID,
						m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, s_MapViewDrag.m_EndDragPos);

					const int StartTX = floor(StartMouseWorldPos.x/TileSize);
					const int StartTY = floor(StartMouseWorldPos.y/TileSize);
					const int EndTX = floor(EndMouseWorldPos.x/TileSize);
					const int EndTY = floor(EndMouseWorldPos.y/TileSize);

					TileLayerRegionToBrush(SelectedLayerID, StartTX, StartTY, EndTX, EndTY);
				}
			}
			else
			{
				// draw brush
				RenderBrush(GridMousePos);

				if(FinishedDragging)
				{
					const vec2 StartMouseWorldPos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID,
						m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, s_MapViewDrag.m_StartDragPos);
					const int StartTX = floor(StartMouseWorldPos.x/TileSize);
					const int StartTY = floor(StartMouseWorldPos.y/TileSize);

					const int RectStartX = min(MouseTx, StartTX);
					const int RectStartY = min(MouseTy, StartTY);
					const int RectEndX = max(MouseTx, StartTX);
					const int RectEndY = max(MouseTy, StartTY);

					// automap
					if(m_BrushAutomapRuleID >= 0)
					{
						// click without dragging, paint whole brush in place
						if(StartTX == MouseTx && StartTY == MouseTy)
							EditBrushPaintLayerAutomap(MouseTx, MouseTy, SelectedLayerID, m_BrushAutomapRuleID);
						else // drag, fill the rectangle by repeating the brush
						{
							EditBrushPaintLayerFillRectAutomap(RectStartX, RectStartY, RectEndX-RectStartX+1, RectEndY-RectStartY+1, SelectedLayerID, m_BrushAutomapRuleID);
						}
					}
					// no automap
					else
					{
						// click without dragging, paint whole brush in place
						if(StartTX == MouseTx && StartTY == MouseTy)
							EditBrushPaintLayer(MouseTx, MouseTy, SelectedLayerID);
						else // drag, fill the rectangle by repeating the brush
							EditBrushPaintLayerFillRectRepeat(RectStartX, RectStartY, RectEndX-RectStartX+1, RectEndY-RectStartY+1, SelectedLayerID);
					}
				}
			}
		}
	}

	if(IsToolDimension())
	{
		// draw clip rect
		const vec2 ClipOff = CalcGroupScreenOffset(m_ZoomWorldViewWidth, m_ZoomWorldViewHeight,
			0, 0, 1, 1);
		const CUIRect ClipScreenRect = { ClipOff.x, ClipOff.y, m_ZoomWorldViewWidth, m_ZoomWorldViewHeight };
		Graphics()->MapScreen(ClipScreenRect.x, ClipScreenRect.y, ClipScreenRect.x+ClipScreenRect.w,
			ClipScreenRect.y+ClipScreenRect.h);

		const CUIRect ClipRect = {
			(float)SelectedGroup.m_ClipX,
			(float)SelectedGroup.m_ClipY,
			(float)SelectedGroup.m_ClipWidth,
			(float)SelectedGroup.m_ClipHeight
		};

		DrawRectBorder(ClipRect, vec4(0,0,0,0), 1, vec4(1,0,0,1));
	}
}


CEditor2::CButtonStyle::CButtonStyle() : ColNormal(StyleColorButton), ColHover(StyleColorButtonHover), ColPress(StyleColorButtonPressed), ColBorder(StyleColorButtonBorder), FontSize(10), Align(-1) { }
CEditor2::CButtonStyle& CEditor2::CButtonStyle::Normal(vec4 Col) 	{ ColNormal = Col; return *this; }
CEditor2::CButtonStyle& CEditor2::CButtonStyle::Hover(vec4 Col) 	{ ColHover = Col; return *this; }
CEditor2::CButtonStyle& CEditor2::CButtonStyle::Press(vec4 Col) 	{ ColPress = Col; return *this; }
CEditor2::CButtonStyle& CEditor2::CButtonStyle::Border(vec4 Col) 	{ ColBorder = Col; return *this; }

void CEditor2::RenderTopPanel(CUIRect TopPanel)
{
	CUIRect SecondaryPanel;
	TopPanel.HSplitMid(&TopPanel, &SecondaryPanel);
	DrawRect(TopPanel, StyleColorBg);
	// DrawRect(SecondaryPanel, vec4(0.16, 0.1, 0.185, 1));
	DrawRect(SecondaryPanel, vec4(66/255.f, 66/255.f, 66/255.f, 1));

	CUIRect ButtonRect;
	TopPanel.VSplitLeft(40.0f, &ButtonRect, &TopPanel);

	CUIRect FileMenuRect = {ButtonRect.x, ButtonRect.y+ButtonRect.h, 120, 20*7};
	static CUIButton s_File;
	// if(UiButton(ButtonRect, "File", &s_File, 10, 0))
	if(UiButtonEx(ButtonRect, "File", &s_File, CButtonStyle().Normal(vec4(0,0,0,0)).Border(vec4(0,0,0,0)).Center()))
	{
		m_UiCurrentPopupID = POPUP_MENU_FILE;
		m_UiCurrentPopupRect = FileMenuRect;
	}

	TopPanel.VSplitLeft(40.0f, &ButtonRect, &TopPanel);
	static CUIButton s_Help;
	if(UiButtonEx(ButtonRect, "Help", &s_Help, CButtonStyle().Normal(vec4(0,0,0,0)).Border(vec4(0,0,0,0)).Center()))
	{

	}

	// page switcher
	TopPanel.VSplitRight(5.0f, &TopPanel, 0);
	TopPanel.VSplitRight(120.0f, &TopPanel, &ButtonRect);
	static CUIButton s_PageSwitchButton;
	if(m_Page == PAGE_MAP_EDITOR)
	{
		if(UiButton(ButtonRect, "Go to Asset Manager", &s_PageSwitchButton))
			ChangePage(PAGE_ASSET_MANAGER);
	}
	else if(m_Page == PAGE_ASSET_MANAGER)
	{
		if(UiButton(ButtonRect, "Go to Map Editor", &s_PageSwitchButton))
			ChangePage(PAGE_MAP_EDITOR);
	}

	// tools
	if(m_Page != PAGE_MAP_EDITOR)
		return;

	static CUIButton s_ButTools[TOOL_COUNT_];
	const char* aButName[] = {
		"Selection",
		"Dimension",
		"Brush"
	};
	TopPanel.VSplitLeft(50.0f, 0, &TopPanel);

	for(int t = 0; t < TOOL_COUNT_; t++)
	{
		TopPanel.VSplitLeft(60.0f, &ButtonRect, &TopPanel);
		if(UiButtonEx(ButtonRect, aButName[t], &s_ButTools[t], m_Tool == t ? StyleColorButtonPressed : StyleColorButton, m_Tool == t ? StyleColorButtonPressed : StyleColorButtonHover, StyleColorButtonPressed, StyleColorButtonBorder, 10.0f, 0))
			ChangeTool(t);
	}

	TopPanel.VSplitLeft(30.0f, 0, &TopPanel);
	TopPanel.VSplitLeft(7*30.0f, &ButtonRect, &TopPanel);
	UiButtonEx(ButtonRect, aButName[m_Tool], &s_ButTools[m_Tool], StyleColorButtonPressed, StyleColorButtonPressed, StyleColorButtonPressed, StyleColorButtonBorder, 10.0f, 0);
	{
		CUIRect ToolsRect = ButtonRect;
		ToolsRect.y += 20.0f;
		ToolsRect.VSplitLeft(30.0f, &ButtonRect, &ToolsRect);
		static CUIButton s_Fill;
		UiButton(ButtonRect, "Fill", &s_Fill, 10, 0);
		ToolsRect.VSplitLeft(30.0f, &ButtonRect, &ToolsRect);
		static CUIButton s_Cut;
		UiButton(ButtonRect, "Cut", &s_Cut, 10, 0);
		ToolsRect.VSplitLeft(30.0f, &ButtonRect, &ToolsRect);
		static CUIButton s_Move;
		UiButton(ButtonRect, "Move", &s_Move, 10, 0);
		ToolsRect.VSplitLeft(30.0f, &ButtonRect, &ToolsRect);
		static CUIButton s_XX;
		UiButton(ButtonRect, "", &s_XX, 10, 0);
		ButtonRect.x += (ButtonRect.w-ButtonRect.h)/2.f;
		ButtonRect.w = ButtonRect.h;
		DoIcon(IMAGE_EDITORICONS, SPRITE_EDITOR_XFLIP, &ButtonRect);
		ToolsRect.VSplitLeft(30.0f, &ButtonRect, &ToolsRect);
		static CUIButton s_YY;
		UiButton(ButtonRect, "", &s_YY, 10, 0);
		ButtonRect.x += (ButtonRect.w-ButtonRect.h)/2.f;
		ButtonRect.w = ButtonRect.h;
		DoIcon(IMAGE_EDITORICONS, SPRITE_EDITOR_YFLIP, &ButtonRect);
		ToolsRect.VSplitLeft(30.0f, &ButtonRect, &ToolsRect);
		static CUIButton s_BIG1;
		UiButton(ButtonRect, "", &s_BIG1, 10, 0);
		ButtonRect.x += (ButtonRect.w-ButtonRect.h)/2.f;
		ButtonRect.w = ButtonRect.h;
		DoIcon(IMAGE_EDITORICONS, SPRITE_EDITOR_BIG1, &ButtonRect);
		ToolsRect.VSplitLeft(30.0f, &ButtonRect, &ToolsRect);
		static CUIButton s_BIG2;
		UiButton(ButtonRect, "", &s_BIG2, 10, 0);
		ButtonRect.x += (ButtonRect.w-ButtonRect.h)/2.f;
		ButtonRect.w = ButtonRect.h;
		DoIcon(IMAGE_EDITORICONS, SPRITE_EDITOR_BIG2, &ButtonRect);
	}
}

void CEditor2::RenderMapEditorUI()
{
	const CUIRect UiScreenRect = m_UiScreenRect;
	Graphics()->MapScreen(UiScreenRect.x, UiScreenRect.y, UiScreenRect.w, UiScreenRect.h);

	CUIRect TopPanel, RightPanel, DetailPanel, ToolColumnRect;
	UiScreenRect.VSplitRight(150.0f, &m_UiMainViewRect, &RightPanel);
	m_UiMainViewRect.HSplitTop(40.0f, &TopPanel, &m_UiMainViewRect);

	RenderTopPanel(TopPanel);

	DrawRect(RightPanel, StyleColorBg);

	CUIRect NavRect, ButtonRect;
	RightPanel.Margin(3.0f, &NavRect);
	NavRect.HSplitTop(20, &ButtonRect, &NavRect);
	NavRect.HSplitTop(2, 0, &NavRect);

	// tabs
	enum
	{
		TAB_GROUPS=0,
		TAB_HISTORY,
	};
	static int s_CurrentTab = TAB_GROUPS;
	CUIRect ButtonRectRight;
	ButtonRect.VSplitMid(&ButtonRect, &ButtonRectRight);

	static CUIButton s_ButGroups;
	if(UiButtonEx(ButtonRect, "Layers", &s_ButGroups, CButtonStyle().Normal(s_CurrentTab == TAB_GROUPS ? StyleColorLayer1 : StyleColorLayer2).Center()))
	{
		s_CurrentTab = TAB_GROUPS;
	}

	static CUIButton s_ButHistory;
	if(UiButtonEx(ButtonRectRight, "History", &s_ButHistory, CButtonStyle().Normal(s_CurrentTab == TAB_HISTORY ? StyleColorLayer1 : StyleColorLayer2).Center()))
	{
		s_CurrentTab = TAB_HISTORY;
	}

	if(s_CurrentTab == TAB_GROUPS)
	{
		RenderMapEditorUiLayerGroups(NavRect);
	}
	else if(s_CurrentTab == TAB_HISTORY)
	{
		static CScrollRegion s_ScrollRegion;
		vec2 ScrollOff(0, 0);
		UiBeginScrollRegion(&s_ScrollRegion, &NavRect, &ScrollOff);
		NavRect.y += ScrollOff.y;

		CHistoryEntry* pFirstEntry = m_pHistoryEntryCurrent;
		while(pFirstEntry->m_pPrev)
			pFirstEntry = pFirstEntry->m_pPrev;

		static CUIButton s_ButEntry[50];
		const float ButtonHeight = 20.0f;
		const float Spacing = 2.0f;

		CHistoryEntry* pCurrentEntry = pFirstEntry;
		int i = 0;
		while(pCurrentEntry)
		{
			NavRect.HSplitTop(ButtonHeight*2, &ButtonRect, &NavRect);
			NavRect.HSplitTop(Spacing, 0, &NavRect);
			UiScrollRegionAddRect(&s_ScrollRegion, ButtonRect);

			// somewhat hacky
			CUIButton& ButState = s_ButEntry[i % (sizeof(s_ButEntry)/sizeof(s_ButEntry[0]))];
			UiDoButtonBehavior(pCurrentEntry, ButtonRect, &ButState);

			// clickety click, restore to this entry
			if(ButState.m_Clicked && pCurrentEntry != m_pHistoryEntryCurrent)
			{
				HistoryRestoreToEntry(pCurrentEntry);
			}

			vec4 ColorButton = StyleColorButton;
			if(ButState.m_Pressed)
				ColorButton = StyleColorButtonPressed;
			else if(ButState.m_Hovered)
				ColorButton = StyleColorButtonHover;

			vec4 ColorBorder = StyleColorButtonBorder;
			if(pCurrentEntry == m_pHistoryEntryCurrent)
				ColorBorder = vec4(1, 0, 0, 1);
			DrawRectBorder(ButtonRect, ColorButton, 1, ColorBorder);

			CUIRect ButTopRect, ButBotRect;
			ButtonRect.HSplitMid(&ButTopRect, &ButBotRect);

			DrawText(ButTopRect, pCurrentEntry->m_aActionStr, 8.0f, vec4(1, 1, 1, 1));
			DrawText(ButBotRect, pCurrentEntry->m_aDescStr, 8.0f, vec4(0, 0.5, 1, 1));

			pCurrentEntry = pCurrentEntry->m_pNext;
			i++;
		}

		// some padding at the end
		NavRect.HSplitTop(10, &ButtonRect, &NavRect);
		UiScrollRegionAddRect(&s_ScrollRegion, ButtonRect);

		UiEndScrollRegion(&s_ScrollRegion);
	}

	// detail panel
	if(m_UiDetailPanelIsOpen)
	{
		m_UiMainViewRect.VSplitRight(150, &m_UiMainViewRect, &DetailPanel);
		DrawRect(DetailPanel, StyleColorBg);
		RenderMapEditorUiDetailPanel(DetailPanel);
	}
	else
	{
		const float ButtonSize = 20.0f;
		const float Margin = 5.0f;
		m_UiMainViewRect.VSplitRight(ButtonSize + Margin*2, 0, &DetailPanel);
		DetailPanel.Margin(Margin, &ButtonRect);
		ButtonRect.h = ButtonSize;

		static CUIButton s_OpenDetailPanelButton;
		if(UiButton(ButtonRect, "<", &s_OpenDetailPanelButton, 7)) {
			m_UiDetailPanelIsOpen = true;
		}
	}

	UI()->ClipEnable(&m_UiMainViewRect); // clip main view rect

	// tools
	const float ButtonSize = 20.0f;
	const float Margin = 5.0f;
	m_UiMainViewRect.VSplitLeft(ButtonSize + Margin * 2, &ToolColumnRect, 0);
	ToolColumnRect.HSplitTop(150.0f, 0, &ToolColumnRect);
	ToolColumnRect.VMargin(Margin, &ToolColumnRect);

	// static CUIButton s_ButTools[TOOL_COUNT_];
	// const char* aButName[] = {
	// 	"Se",
	// 	"Di",
	// 	"TB"
	// };

	// for(int t = 0; t < TOOL_COUNT_; t++)
	// {
	// 	ToolColumnRect.HSplitTop(Margin, 0, &ToolColumnRect);
	// 	ToolColumnRect.HSplitTop(ButtonSize, &ButtonRect, &ToolColumnRect);

	// 	if(UiButtonSelect(ButtonRect, aButName[t], &s_ButTools[t], m_Tool == t))
	// 		ChangeTool(t);
	// }

	// selection context buttons
	if(m_UiSelectedLayerID != -1 && IsToolSelect() && m_TileSelection.IsSelected())
	{
		const CEditorMap2::CLayer& SelectedTileLayer = m_Map.m_aLayers[m_UiSelectedLayerID];

		if(SelectedTileLayer.IsTileLayer())
		{
			const float TileSize = 32;

			CUIRect SelectRect = {
				m_TileSelection.m_StartTX * TileSize,
				m_TileSelection.m_StartTY * TileSize,
				(m_TileSelection.m_EndTX+1-m_TileSelection.m_StartTX)*TileSize,
				(m_TileSelection.m_EndTY+1-m_TileSelection.m_StartTY)*TileSize
			};

			CUIRect UiRect = CalcUiRectFromGroupWorldRect(m_UiSelectedGroupID, m_ZoomWorldViewWidth,
				m_ZoomWorldViewHeight, SelectRect);

			const float Margin = 4.0f;
			const float ButtonHeight = 25;
			UiRect.y -= ButtonHeight + Margin;

			CUIRect LineRect, ButtonRect;
			UiRect.HSplitTop(ButtonHeight, &LineRect, 0);
			LineRect.VSplitLeft(30, &ButtonRect, &LineRect);
			LineRect.VSplitLeft(Margin, 0, &LineRect);

			static CUIButton s_ButFlipX, s_ButFlipY;

			if(UiButton(ButtonRect, "X/X", &s_ButFlipX))
			{
				EditTileSelectionFlipX(m_UiSelectedLayerID);
			}

			LineRect.VSplitLeft(30, &ButtonRect, &LineRect);
			LineRect.VSplitLeft(Margin, 0, &LineRect);

			if(UiButton(ButtonRect, "Y/Y", &s_ButFlipY))
			{
				EditTileSelectionFlipY(m_UiSelectedLayerID);
			}

			// Auto map
			CTilesetMapper2* pMapper = m_Map.AssetsFindTilesetMapper(SelectedTileLayer.m_ImageID);

			if(pMapper)
			{
				const int RulesetCount = pMapper->RuleSetNum();
				static CUIButton s_ButtonAutoMap[16];
				 // TODO: find a better solution to this
				dbg_assert(RulesetCount <= 16, "RulesetCount is too big");

				for(int r = 0; r < RulesetCount; r++)
				{
					LineRect.VSplitLeft(50, &ButtonRect, &LineRect);
					LineRect.VSplitLeft(Margin, 0, &LineRect);

					if(UiButton(ButtonRect, pMapper->GetRuleSetName(r), &s_ButtonAutoMap[r]))
					{
						EditTileLayerAutoMapSection(m_UiSelectedLayerID, r, m_TileSelection.m_StartTX, m_TileSelection.m_StartTY, m_TileSelection.m_EndTX+1-m_TileSelection.m_StartTX, m_TileSelection.m_EndTY+1-m_TileSelection.m_StartTY);
					}
				}
			}
		}
	}

	// Clip and tilelayer resize handles
	if(IsToolDimension())
	{
		CEditorMap2::CGroup& SelectedGroup = m_Map.m_aGroups[m_UiSelectedGroupID];
		if(SelectedGroup.m_UseClipping)
		{
			CUIRect ClipRect = {
				(float)SelectedGroup.m_ClipX,
				(float)SelectedGroup.m_ClipY,
				(float)SelectedGroup.m_ClipWidth,
				(float)SelectedGroup.m_ClipHeight
			};

			CUIRect ClipUiRect = CalcUiRectFromGroupWorldRect(m_Map.m_GameGroupID, m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, ClipRect);

			const float HandleSize = 10.0f;
			const vec4 ColNormal(0.85, 0.0, 0.0, 1);
			const vec4 ColActive(1.0, 0.0, 0.0, 1);

			// handles
			CUIRect HandleTop = {
				ClipUiRect.x - HandleSize * 0.5f + ClipUiRect.w * 0.5f,
				ClipUiRect.y - HandleSize * 0.5f,
				HandleSize, HandleSize
			};

			CUIRect HandleBottom = {
				ClipUiRect.x - HandleSize * 0.5f + ClipUiRect.w * 0.5f,
				ClipUiRect.y - HandleSize * 0.5f + ClipUiRect.h,
				HandleSize, HandleSize
			};

			CUIRect HandleLeft = {
				ClipUiRect.x - HandleSize * 0.5f,
				ClipUiRect.y - HandleSize * 0.5f + ClipUiRect.h * 0.5f,
				HandleSize, HandleSize
			};

			CUIRect HandleRight = {
				ClipUiRect.x - HandleSize * 0.5f + ClipUiRect.w,
				ClipUiRect.y - HandleSize * 0.5f + ClipUiRect.h * 0.5f,
				HandleSize, HandleSize
			};

			const vec2 WorldMousePos = CalcGroupWorldPosFromUiPos(m_Map.m_GameGroupID, m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, m_UiMousePos);

			static CUIGrabHandle s_GrabHandleTop, s_GrabHandleLeft, s_GrabHandleBot, s_GrabHandleRight;
			bool WasGrabbingTop = s_GrabHandleTop.m_IsGrabbed;
			bool WasGrabbingLeft = s_GrabHandleLeft.m_IsGrabbed;
			bool WasGrabbingBot = s_GrabHandleBot.m_IsGrabbed;
			bool WasGrabbingRight = s_GrabHandleRight.m_IsGrabbed;
			static int BeforeGrabbingClipX,BeforeGrabbingClipY, BeforeGrabbingClipWidth, BeforeGrabbingClipHeight;
			if(!WasGrabbingTop && !WasGrabbingBot)
			{
				BeforeGrabbingClipY = SelectedGroup.m_ClipY;
				BeforeGrabbingClipHeight = SelectedGroup.m_ClipHeight;
			}
			if(!WasGrabbingLeft && !WasGrabbingRight)
			{
				BeforeGrabbingClipX = SelectedGroup.m_ClipX;
				BeforeGrabbingClipWidth = SelectedGroup.m_ClipWidth;
			}

			vec2 ToolTipPos;

			if(UiGrabHandle(HandleTop, &s_GrabHandleTop, ColNormal, ColActive))
			{
				EditHistCondGroupChangeClipY(m_UiSelectedGroupID, WorldMousePos.y, false);
				ToolTipPos = vec2(HandleTop.x, HandleTop.y);
			}

			if(UiGrabHandle(HandleLeft, &s_GrabHandleLeft, ColNormal, ColActive))
			{
				EditHistCondGroupChangeClipX(m_UiSelectedGroupID, WorldMousePos.x, false);
				ToolTipPos = vec2(HandleLeft.x, HandleLeft.y);
			}

			if(UiGrabHandle(HandleBottom, &s_GrabHandleBot, ColNormal, ColActive))
			{
				EditHistCondGroupChangeClipBottom(m_UiSelectedGroupID, WorldMousePos.y, false);
				ToolTipPos = vec2(HandleBottom.x, HandleBottom.y);
			}

			if(UiGrabHandle(HandleRight, &s_GrabHandleRight, ColNormal, ColActive))
			{
				EditHistCondGroupChangeClipRight(m_UiSelectedGroupID, WorldMousePos.x, false);
				ToolTipPos = vec2(HandleRight.x, HandleRight.y);
			}

			// finished grabbing
			if(!s_GrabHandleLeft.m_IsGrabbed && WasGrabbingLeft)
			{
				SelectedGroup.m_ClipX = BeforeGrabbingClipX;
				SelectedGroup.m_ClipWidth = BeforeGrabbingClipWidth;
				EditHistCondGroupChangeClipX(m_UiSelectedGroupID, WorldMousePos.x, true);
			}

			if(!s_GrabHandleTop.m_IsGrabbed && WasGrabbingTop)
			{
				SelectedGroup.m_ClipY = BeforeGrabbingClipY;
				SelectedGroup.m_ClipHeight = BeforeGrabbingClipHeight;
				EditHistCondGroupChangeClipY(m_UiSelectedGroupID, WorldMousePos.y, true);
			}

			if(!s_GrabHandleRight.m_IsGrabbed && WasGrabbingRight)
			{
				SelectedGroup.m_ClipWidth = BeforeGrabbingClipWidth;
				EditHistCondGroupChangeClipRight(m_UiSelectedGroupID, WorldMousePos.x, true);
			}

			if(!s_GrabHandleBot.m_IsGrabbed && WasGrabbingBot)
			{
				SelectedGroup.m_ClipHeight = BeforeGrabbingClipHeight;
				EditHistCondGroupChangeClipBottom(m_UiSelectedGroupID, WorldMousePos.y, true);
			}

			// Size tooltip info
			if(s_GrabHandleLeft.m_IsGrabbed || s_GrabHandleTop.m_IsGrabbed ||
			   s_GrabHandleRight.m_IsGrabbed || s_GrabHandleBot.m_IsGrabbed)
			{
				CUIRect ToolTipRect = {
					ToolTipPos.x + 20.0f,
					ToolTipPos.y,
					30, 48
				};
				DrawRectBorder(ToolTipRect, vec4(0.6, 0.0, 0.0, 1.0), 1, vec4(1.0, 1.0, 1.0, 1));

				ToolTipRect.x += 2;
				CUIRect TopPart;
				char aWidthBuff[16];
				char aHeightBuff[16];
				char aPosXBuff[16];
				char aPosYBuff[16];
				str_format(aPosXBuff, sizeof(aPosXBuff), "%d", SelectedGroup.m_ClipX);
				str_format(aPosYBuff, sizeof(aPosYBuff), "%d", SelectedGroup.m_ClipY);
				str_format(aWidthBuff, sizeof(aWidthBuff), "%d", SelectedGroup.m_ClipWidth);
				str_format(aHeightBuff, sizeof(aHeightBuff), "%d", SelectedGroup.m_ClipHeight);

				ToolTipRect.HSplitTop(12, &TopPart, &ToolTipRect);
				DrawText(TopPart, aPosXBuff, 8, vec4(1, 1, 1, 1.0));

				ToolTipRect.HSplitTop(12, &TopPart, &ToolTipRect);
				DrawText(TopPart, aPosYBuff, 8, vec4(1, 1, 1, 1.0));

				ToolTipRect.HSplitTop(12, &TopPart, &ToolTipRect);
				DrawText(TopPart, aWidthBuff, 8, vec4(1, 1, 1, 1.0));

				ToolTipRect.HSplitTop(12, &TopPart, &ToolTipRect);
				DrawText(TopPart, aHeightBuff, 8, vec4(1, 1, 1, 1.0));
			}
		}

		// tile layer resize
		CEditorMap2::CLayer& SelectedTileLayer = m_Map.m_aLayers[m_UiSelectedLayerID];

		if(SelectedTileLayer.IsTileLayer())
		{
			const float TileSize = 32;
			CUIRect LayerRect = {
				0,
				0,
				SelectedTileLayer.m_Width * TileSize,
				SelectedTileLayer.m_Height * TileSize
			};

			const float HandleSize = 10.0f;
			const vec4 ColNormal(0.85, 0.85, 0.85, 1);
			const vec4 ColActive(1.0, 1.0, 1.0, 1);

			static CUIGrabHandle s_GrabHandleBot, s_GrabHandleRight, s_GrabHandleBotRight;
			bool WasGrabbingBot = s_GrabHandleBot.m_IsGrabbed;
			bool WasGrabbingRight = s_GrabHandleRight.m_IsGrabbed;
			bool WasGrabbingBotRight = s_GrabHandleBotRight.m_IsGrabbed;

			static CUIRect PreviewRect;

			bool IsAnyHandleGrabbed = true;
			if(!s_GrabHandleBot.m_IsGrabbed && !s_GrabHandleRight.m_IsGrabbed && !s_GrabHandleBotRight.m_IsGrabbed)
			{
				PreviewRect = LayerRect;
				IsAnyHandleGrabbed = false;
			}

			CUIRect LayerUiRect = CalcUiRectFromGroupWorldRect(m_UiSelectedGroupID, m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, PreviewRect);

			// handles
			CUIRect HandleBottom = {
				LayerUiRect.x - HandleSize * 0.5f + LayerUiRect.w * 0.5f,
				LayerUiRect.y - HandleSize * 0.5f + LayerUiRect.h,
				HandleSize, HandleSize
			};

			CUIRect HandleRight = {
				LayerUiRect.x - HandleSize * 0.5f + LayerUiRect.w,
				LayerUiRect.y - HandleSize * 0.5f + LayerUiRect.h * 0.5f,
				HandleSize, HandleSize
			};

			CUIRect HandleBottomRight = {
				LayerUiRect.x - HandleSize * 0.5f + LayerUiRect.w,
				LayerUiRect.y - HandleSize * 0.5f + LayerUiRect.h,
				HandleSize, HandleSize
			};

			if(IsAnyHandleGrabbed)
				DrawRectBorder(LayerUiRect, vec4(1,1,1,0.2), 1, vec4(1, 1, 1, 1));

			const vec2 WorldMousePos = CalcGroupWorldPosFromUiPos(m_UiSelectedGroupID, m_ZoomWorldViewWidth, m_ZoomWorldViewHeight, m_UiMousePos);

			vec2 ToolTipPos;
			if(UiGrabHandle(HandleBottom, &s_GrabHandleBot, ColNormal, ColActive))
			{
				PreviewRect.h = (int)round(WorldMousePos.y / TileSize) * TileSize;
				ToolTipPos = vec2(HandleBottom.x, HandleBottom.y + HandleSize);
			}

			if(UiGrabHandle(HandleRight, &s_GrabHandleRight, ColNormal, ColActive))
			{
				PreviewRect.w = (int)round(WorldMousePos.x / TileSize) * TileSize;
				ToolTipPos = vec2(HandleRight.x, HandleRight.y);
			}

			if(UiGrabHandle(HandleBottomRight, &s_GrabHandleBotRight, ColNormal, ColActive))
			{
				PreviewRect.w = (int)round(WorldMousePos.x / TileSize) * TileSize;
				PreviewRect.h = (int)round(WorldMousePos.y / TileSize) * TileSize;
				ToolTipPos = vec2(HandleBottomRight.x, HandleBottomRight.y);
			}

			// clamp
			if(PreviewRect.w < TileSize)
				PreviewRect.w = TileSize;
			if(PreviewRect.h < TileSize)
				PreviewRect.h = TileSize;

			// Width/height tooltip info
			if(IsAnyHandleGrabbed)
			{
				CUIRect ToolTipRect = {
					ToolTipPos.x + 20.0f,
					ToolTipPos.y,
					30, 25
				};
				DrawRectBorder(ToolTipRect, vec4(0.6, 0.6, 0.6, 1.0), 1, vec4(1.0, 0.5, 0, 1));

				ToolTipRect.x += 2;
				CUIRect TopPart, BotPart;
				ToolTipRect.HSplitMid(&TopPart, &BotPart);

				char aWidthBuff[16];
				char aHeightBuff[16];
				str_format(aWidthBuff, sizeof(aWidthBuff), "%d", (int)(PreviewRect.w / TileSize));
				str_format(aHeightBuff, sizeof(aHeightBuff), "%d", (int)(PreviewRect.h / TileSize));

				DrawText(TopPart, aWidthBuff, 8, vec4(1, 1, 1, 1.0));
				DrawText(BotPart, aHeightBuff, 8, vec4(1, 1, 1, 1.0));
			}

			// IF we let go of any handle, resize tile layer
			if((WasGrabbingBot && !s_GrabHandleBot.m_IsGrabbed) ||
			   (WasGrabbingRight && !s_GrabHandleRight.m_IsGrabbed) ||
			   (WasGrabbingBotRight && !s_GrabHandleBotRight.m_IsGrabbed))
			{
				EditTileLayerResize(m_UiSelectedLayerID, (int)(PreviewRect.w / TileSize), (int)(PreviewRect.h / TileSize));
			}
		}
	}

	// popups
	if(m_UiCurrentPopupID == POPUP_BRUSH_PALETTE)
		RenderPopupBrushPalette();

	if(m_UiCurrentPopupID == POPUP_MENU_FILE)
		RenderPopupMenuFile();

	UI()->ClipDisable(); // main view rect clip
}

void CEditor2::RenderMapEditorUiLayerGroups(CUIRect NavRect)
{
	const float FontSize = 8.0f;
	const float ButtonHeight = 20.0f;
	const float Spacing = 2.0f;
	const float ShowButtonWidth = 15.0f;

	CUIRect ActionLineRect, ButtonRect;
	NavRect.HSplitBottom((ButtonHeight + Spacing) * 3.0f, &NavRect, &ActionLineRect);

	const int GroupCount = m_Map.m_aGroups.size();
	const int TotalLayerCount = m_Map.m_aLayers.size();

	static array<CUIButton> s_UiGroupButState;
	static array<CUIButton> s_UiGroupShowButState;
	static array<CUIButton> s_UiLayerButState;
	static array<CUIButton> s_UiLayerShowButState;

	ArraySetSizeAndZero(&s_UiGroupButState, GroupCount);
	ArraySetSizeAndZero(&s_UiGroupButState, GroupCount);
	ArraySetSizeAndZero(&s_UiGroupShowButState, GroupCount);
	ArraySetSizeAndZero(&s_UiLayerButState, TotalLayerCount);
	ArraySetSizeAndZero(&s_UiLayerShowButState, TotalLayerCount);

	ArraySetSizeAndZero(&m_UiGroupOpen, GroupCount);
	ArraySetSizeAndZero(&m_UiGroupHidden, GroupCount);
	ArraySetSizeAndZero(&m_UiGroupHovered, GroupCount);
	ArraySetSizeAndZero(&m_UiLayerHovered, TotalLayerCount);
	ArraySetSizeAndZero(&m_UiLayerHidden, TotalLayerCount);

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOff(0, 0);
	UiBeginScrollRegion(&s_ScrollRegion, &NavRect, &ScrollOff);
	NavRect.y += ScrollOff.y;

	// drag to reorder items
	static CUIMouseDrag s_DragMove;
	static const void* pDragMoveID = NULL;
	if(!s_DragMove.m_IsDragging)
		pDragMoveID = NULL;

	bool OldIsMouseDragging = s_DragMove.m_IsDragging;
	bool FinishedMouseDragging = UiDoMouseDragging(0, NavRect, &s_DragMove);
	bool StartedMouseDragging = s_DragMove.m_IsDragging && OldIsMouseDragging == false;

	bool DisplayDragMoveOverlay = s_DragMove.m_IsDragging && pDragMoveID;
	CUIRect DragMoveOverlayRect;
	int DragMoveDir = 0;
	// -----------------------

	for(int gi = 0; gi < GroupCount; gi++)
	{
		const CEditorMap2::CGroup& Group = m_Map.m_aGroups[gi];

		if(gi != 0)
			NavRect.HSplitTop(Spacing, 0, &NavRect);
		NavRect.HSplitTop(ButtonHeight, &ButtonRect, &NavRect);
		UiScrollRegionAddRect(&s_ScrollRegion, ButtonRect);

		// check whole line for hover
		CUIButton WholeLineState;
		UiDoButtonBehavior(0, ButtonRect, &WholeLineState);
		m_UiGroupHovered[gi] = WholeLineState.m_Hovered;

		// drag started on this item
		if(StartedMouseDragging && WholeLineState.m_Hovered)
		{
			pDragMoveID = &Group;
		}
		if(pDragMoveID == &Group)
		{
			DragMoveOverlayRect = ButtonRect;
			DragMoveDir = (int)sign(m_UiMousePos.y - ButtonRect.y);
		}

		CUIRect ExpandBut, ShowButton;

		// show button
		ButtonRect.VSplitRight(ShowButtonWidth, &ButtonRect, &ShowButton);
		CUIButton& ShowButState = s_UiGroupShowButState[gi];
		UiDoButtonBehavior(&ShowButState, ShowButton, &ShowButState);

		if(ShowButState.m_Clicked)
			m_UiGroupHidden[gi] ^= 1;

		const bool IsShown = !m_UiGroupHidden[gi];

		vec4 ShowButColor = StyleColorButton;
		if(ShowButState.m_Hovered)
			ShowButColor = StyleColorButtonHover;
		if(ShowButState.m_Pressed)
			ShowButColor = StyleColorButtonPressed;

		DrawRectBorder(ShowButton, ShowButColor, 1, StyleColorButtonBorder);
		DrawText(ShowButton, IsShown ? "o" : "x", FontSize);

		// group button
		CUIButton& ButState = s_UiGroupButState[gi];
		UiDoButtonBehavior(&ButState, ButtonRect, &ButState);

		if(ButState.m_Clicked)
		{
			if(m_UiSelectedGroupID == gi)
				m_UiGroupOpen[gi] ^= 1;

			m_UiSelectedGroupID = gi;
			if(m_Map.m_aGroups[gi].m_LayerCount > 0)
				m_UiSelectedLayerID = m_Map.m_aGroups[gi].m_apLayerIDs[0];
			else
				m_UiSelectedLayerID = -1;
		}

		const bool IsSelected = m_UiSelectedGroupID == gi;
		const bool IsOpen = m_UiGroupOpen[gi];

		vec4 ButColor = StyleColorButton;
		if(ButState.m_Hovered)
			ButColor = StyleColorButtonHover;
		if(ButState.m_Pressed)
			ButColor = StyleColorButtonPressed;

		if(IsSelected)
			DrawRectBorder(ButtonRect, ButColor, 1, StyleColorButtonBorder);
		else
			DrawRect(ButtonRect, ButColor);


		ButtonRect.VSplitLeft(ButtonRect.h, &ExpandBut, &ButtonRect);
		DrawText(ExpandBut, IsOpen ? "-" : "+", FontSize);

		char aGroupName[64];
		if(m_Map.m_GameGroupID == gi)
			str_format(aGroupName, sizeof(aGroupName), "Game group");
		else
			str_format(aGroupName, sizeof(aGroupName), "Group #%d", gi);
		DrawText(ButtonRect, aGroupName, FontSize);

		if(m_UiGroupOpen[gi])
		{
			const int LayerCount = m_Map.m_aGroups[gi].m_LayerCount;

			for(int li = 0; li < LayerCount; li++)
			{
				const int LyID = m_Map.m_aGroups[gi].m_apLayerIDs[li];
				const CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LyID];
				NavRect.HSplitTop(Spacing, 0, &NavRect);
				NavRect.HSplitTop(ButtonHeight, &ButtonRect, &NavRect);
				UiScrollRegionAddRect(&s_ScrollRegion, ButtonRect);
				ButtonRect.VSplitLeft(10.0f, 0, &ButtonRect);

				dbg_assert(LyID >= 0 && LyID < m_Map.m_aLayers.size(), "LayerID out of bounds");

				// check whole line for hover
				CUIButton WholeLineState;
				UiDoButtonBehavior(0, ButtonRect, &WholeLineState);
				m_UiLayerHovered[LyID] = WholeLineState.m_Hovered;

				// drag started on this item
				if(StartedMouseDragging && WholeLineState.m_Hovered)
				{
					pDragMoveID = &Layer;
				}
				if(pDragMoveID == &Layer)
				{
					DragMoveOverlayRect = ButtonRect;
					DragMoveDir = (int)sign(m_UiMousePos.y - ButtonRect.y);
				}

				// show button
				ButtonRect.VSplitRight(ShowButtonWidth, &ButtonRect, &ShowButton);
				CUIButton& ShowButState = s_UiLayerShowButState[LyID];
				UiDoButtonBehavior(&ShowButState, ShowButton, &ShowButState);

				if(ShowButState.m_Clicked)
					m_UiLayerHidden[LyID] ^= 1;

				const bool IsShown = !m_UiLayerHidden[LyID];

				vec4 ShowButColor = StyleColorButton;
				if(ShowButState.m_Hovered)
					ShowButColor = StyleColorButtonHover;
				if(ShowButState.m_Pressed)
					ShowButColor = StyleColorButtonPressed;

				DrawRectBorder(ShowButton, ShowButColor, 1, StyleColorButtonBorder);
				DrawText(ShowButton, IsShown ? "o" : "x", FontSize);

				// layer button
				CUIButton& ButState = s_UiLayerButState[LyID];
				UiDoButtonBehavior(&ButState, ButtonRect, &ButState);

				vec4 ButColor = StyleColorButton;
				if(ButState.m_Hovered)
					ButColor = StyleColorButtonHover;
				if(ButState.m_Pressed)
					ButColor = StyleColorButtonPressed;

				if(ButState.m_Clicked)
				{
					m_UiSelectedLayerID = LyID;
					m_UiSelectedGroupID = gi;
				}

				const bool IsSelected = m_UiSelectedLayerID == LyID;

				if(IsSelected)
					DrawRectBorder(ButtonRect, ButColor, 1, vec4(1, 0, 0, 1));
				else
					DrawRect(ButtonRect, ButColor);

				char aLayerName[64];
				const int ImageID = m_Map.m_aLayers[LyID].m_ImageID;
				if(m_Map.m_GameLayerID == LyID)
					str_format(aLayerName, sizeof(aLayerName), "Game Layer");
				else
					str_format(aLayerName, sizeof(aLayerName), "%s (%s)",
							   GetLayerName(LyID),
							   ImageID >= 0 ? m_Map.m_Assets.m_aImageNames[ImageID].m_Buff : "none");
				DrawText(ButtonRect, aLayerName, FontSize);
			}
		}
	}

	// add some extra padding
	NavRect.HSplitTop(10, &ButtonRect, &NavRect);
	UiScrollRegionAddRect(&s_ScrollRegion, ButtonRect);

	UiEndScrollRegion(&s_ScrollRegion);

	// Add buttons
	CUIRect ButtonRect2;
	ActionLineRect.HSplitTop(ButtonHeight, &ButtonRect, &ActionLineRect);
	ActionLineRect.HSplitTop(Spacing, 0, &ActionLineRect);
	ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);

	static CUIButton s_ButAddTileLayer, s_ButAddQuadLayer, s_ButAddGroup;
	if(UiButton(ButtonRect2, Localize("New group"), &s_ButAddGroup))
	{
		EditCreateAndAddGroup();
	}

	ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);

	if(UiButton(ButtonRect, Localize("T+"), &s_ButAddTileLayer))
	{
		m_UiSelectedLayerID = EditCreateAndAddTileLayerUnder(m_UiSelectedLayerID, m_UiSelectedGroupID);
	}

	if(UiButton(ButtonRect2, Localize("Q+"), &s_ButAddQuadLayer))
	{
		m_UiSelectedLayerID = EditCreateAndAddQuadLayerUnder(m_UiSelectedLayerID, m_UiSelectedGroupID);
	}

	// delete buttons
	const bool IsGameLayer = m_UiSelectedLayerID == m_Map.m_GameLayerID;
	const bool IsGameGroup = m_UiSelectedGroupID == m_Map.m_GameGroupID;

	ActionLineRect.HSplitTop(ButtonHeight, &ButtonRect, &ActionLineRect);
	ActionLineRect.HSplitTop(Spacing, 0, &ActionLineRect);

	static CUIButton s_LayerDeleteButton;
	if(UiButtonEx(ButtonRect, Localize("Delete layer"), &s_LayerDeleteButton,
		vec4(0.4, 0.04, 0.04, 1), vec4(0.96, 0.16, 0.16, 1), vec4(0.31, 0, 0, 1),
		vec4(0.63, 0.035, 0.035, 1), 10) && !IsGameLayer)
	{
		int SelectedLayerID = m_UiSelectedLayerID;
		int SelectedGroupID = m_UiSelectedGroupID;
		SelectLayerBelowCurrentOne();

		EditDeleteLayer(SelectedLayerID, SelectedGroupID);

		// this can happen since we select the layer below before deleting
		if(m_UiSelectedLayerID >= m_Map.m_aLayers.size())
		{
			m_UiSelectedLayerID = m_Map.m_GameLayerID;
			m_UiSelectedGroupID = m_Map.m_GameGroupID;
		}
	}

	ActionLineRect.HSplitTop(ButtonHeight, &ButtonRect, &ActionLineRect);

	// delete button
	static CUIButton s_GroupDeleteButton;
	if(UiButtonEx(ButtonRect, Localize("Delete group"), &s_GroupDeleteButton, vec4(0.4, 0.04, 0.04, 1),
		vec4(0.96, 0.16, 0.16, 1), vec4(0.31, 0, 0, 1), vec4(0.63, 0.035, 0.035, 1), 10) && !IsGameGroup)
	{
		EditDeleteGroup(m_UiSelectedGroupID);
		// TODO: select group below
		m_UiSelectedGroupID = m_Map.m_GameGroupID;
		m_UiSelectedLayerID = m_Map.m_GameLayerID;
	}

	// drag overlay (arrows for now)
	if(DisplayDragMoveOverlay && !UI()->MouseInside(&DragMoveOverlayRect))
	{
		const vec4 Color = StyleColorInputSelected;
		const float MarginX = 10.0f;
		const float x = DragMoveOverlayRect.x + MarginX;
		const float y = DragMoveOverlayRect.y + (DragMoveDir == 1 ? DragMoveOverlayRect.h : 0);
		const float w = DragMoveOverlayRect.w - (MarginX * 2);

		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		IGraphics::CFreeformItem Triangle(
			x, y,
			x + w, y,
			x + w * 0.5f, y - 25 * -DragMoveDir,
			x + w, y
		);

		Graphics()->SetColor(Color.r*Color.a, Color.g*Color.a, Color.b*Color.a, Color.a);

		Graphics()->QuadsDrawFreeform(&Triangle, 1);
		Graphics()->QuadsEnd();
	}

	// finished dragging, move
	// TODO: merge the 2 ifs
	if(FinishedMouseDragging)
	{
		if(pDragMoveID && !IsInsideRect(s_DragMove.m_EndDragPos, DragMoveOverlayRect))
		{
			int GroupID = (CEditorMap2::CGroup*)pDragMoveID - m_Map.m_aGroups.base_ptr();
			if(GroupID < 0 || GroupID > m_Map.m_aGroups.size()-1)
				GroupID = -1;
			int LayerID = (CEditorMap2::CLayer*)pDragMoveID - m_Map.m_aLayers.base_ptr();
			if(LayerID < 0 || LayerID > m_Map.m_aLayers.size()-1)
				LayerID = -1;

			if(GroupID != -1)
			{
				int NewGroupID = EditGroupOrderMove(GroupID,  DragMoveDir < 0 ? -1 : 1);
				m_UiGroupOpen[NewGroupID] = true;

				if(GroupID == m_UiSelectedGroupID && NewGroupID != GroupID)
				{
					m_UiSelectedGroupID = NewGroupID;
					m_UiSelectedLayerID = -1;
				}
			}
			else if(LayerID != -1)
			{
				int NewGroupID = EditLayerOrderMove(LayerID, DragMoveDir < 0 ? -1 : 1);
				m_UiGroupOpen[NewGroupID] = true;
			}
		}
	}
}

void CEditor2::RenderMapEditorUiDetailPanel(CUIRect DetailRect)
{
	DetailRect.Margin(3.0f, &DetailRect);

	// GROUP/LAYER DETAILS
	static CScrollRegion s_DetailSR;
	vec2 DetailScrollOff(0, 0);
	UiBeginScrollRegion(&s_DetailSR, &DetailRect, &DetailScrollOff);
	DetailRect.y += DetailScrollOff.y;

	// group
	dbg_assert(m_UiSelectedGroupID >= 0, "No selected group");
	CEditorMap2::CGroup& SelectedGroup = m_Map.m_aGroups[m_UiSelectedGroupID];
	const bool IsGameGroup = m_UiSelectedGroupID == m_Map.m_GameGroupID;
	char aBuff[128];

	CUIRect ButtonRect, ButtonRect2;
	const float FontSize = 8.0f;
	const float ButtonHeight = 20.0f;
	const float Spacing = 2.0f;

	// close button
	DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
	DetailRect.HSplitTop(Spacing, 0, &DetailRect);
	static CUIButton s_CloseDetailPanelButton;
	if(UiButton(ButtonRect, ">", &s_CloseDetailPanelButton, 8)) {
		m_UiDetailPanelIsOpen = false;
	}

	// label
	DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
	DetailRect.HSplitTop(Spacing, 0, &DetailRect);
	DrawRect(ButtonRect, StyleColorButtonPressed);
	DrawText(ButtonRect, Localize("Group"), FontSize);

	if(!IsGameGroup)
	{
		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);

		static CUITextInput s_TIGroupName;

		static char aBeforeSelectionName[sizeof(SelectedGroup.m_aName)];
		char aNewName[sizeof(SelectedGroup.m_aName)];
		str_copy(aNewName, SelectedGroup.m_aName, sizeof(aNewName));

		// save name before text input selection
		if(!s_TIGroupName.m_Selected)
			str_copy(aBeforeSelectionName, SelectedGroup.m_aName, sizeof(aBeforeSelectionName));

		// "preview" new name instantly
		if(UiTextInput(ButtonRect, aNewName, sizeof(aNewName), &s_TIGroupName))
			EditHistCondGroupChangeName(m_UiSelectedGroupID, aNewName, false);

		// if not selected, restore old name and change to new name with history entry
		if(!s_TIGroupName.m_Selected)
		{
			str_copy(SelectedGroup.m_aName, aBeforeSelectionName, sizeof(SelectedGroup.m_aName));
			EditHistCondGroupChangeName(m_UiSelectedGroupID, aNewName, true);
		}
	}

	// parallax
	DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
	DetailRect.HSplitTop(Spacing, 0, &DetailRect);
	ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);

	DrawRect(ButtonRect, vec4(0,0,0,1));
	DrawText(ButtonRect, Localize("Parallax"), FontSize);

	ButtonRect2.VSplitMid(&ButtonRect, &ButtonRect2);
	static CUIIntegerInput s_IntInpParallaxX, s_IntInpParallaxY;

	// before selection
	static int BsGroupParallaX, BsGroupParallaY;
	if(!s_IntInpParallaxX.m_TextInput.m_Selected && !s_IntInpParallaxY.m_TextInput.m_Selected)
	{
		BsGroupParallaX = SelectedGroup.m_ParallaxX;
		BsGroupParallaY = SelectedGroup.m_ParallaxY;
	}

	int NewGroupParallaxX = SelectedGroup.m_ParallaxX;
	int NewGroupParallaxY = SelectedGroup.m_ParallaxY;
	bool ParallaxChanged = false;
	ParallaxChanged |= UiIntegerInput(ButtonRect, &NewGroupParallaxX, &s_IntInpParallaxX);
	ParallaxChanged |= UiIntegerInput(ButtonRect2, &NewGroupParallaxY, &s_IntInpParallaxY);
	if(ParallaxChanged)
		EditHistCondGroupChangeParallax(m_UiSelectedGroupID, NewGroupParallaxX, NewGroupParallaxY, false);

	// restore "before preview" parallax, the nchange to new one
	if(!s_IntInpParallaxX.m_TextInput.m_Selected && !s_IntInpParallaxY.m_TextInput.m_Selected)
	{
		SelectedGroup.m_ParallaxX = BsGroupParallaX;
		SelectedGroup.m_ParallaxY = BsGroupParallaY;
		EditHistCondGroupChangeParallax(m_UiSelectedGroupID, NewGroupParallaxX, NewGroupParallaxY, true);
	}

	// offset
	DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
	DetailRect.HSplitTop(Spacing, 0, &DetailRect);
	ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);

	DrawRect(ButtonRect, vec4(0,0,0,1));
	DrawText(ButtonRect, Localize("Offset"), FontSize);

	ButtonRect2.VSplitMid(&ButtonRect, &ButtonRect2);
	static CUIIntegerInput s_IntInpOffsetX, s_IntInpOffsetY;

	// before selection
	static int BsGroupOffsetX, BsGroupOffsetY;
	if(!s_IntInpOffsetX.m_TextInput.m_Selected && !s_IntInpOffsetY.m_TextInput.m_Selected)
	{
		BsGroupOffsetX = SelectedGroup.m_OffsetX;
		BsGroupOffsetY = SelectedGroup.m_OffsetY;
	}

	int NewGroupOffsetX = SelectedGroup.m_OffsetX;
	int NewGroupOffsetY = SelectedGroup.m_OffsetY;
	bool OffsetChanged = false;
	OffsetChanged |= UiIntegerInput(ButtonRect, &NewGroupOffsetX, &s_IntInpOffsetX);
	OffsetChanged |= UiIntegerInput(ButtonRect2, &NewGroupOffsetY, &s_IntInpOffsetY);
	if(OffsetChanged)
		EditHistCondGroupChangeOffset(m_UiSelectedGroupID, NewGroupOffsetX, NewGroupOffsetY, false);

	// restore "before preview" offset, the nchange to new one
	if(!s_IntInpOffsetX.m_TextInput.m_Selected && !s_IntInpOffsetY.m_TextInput.m_Selected)
	{
		SelectedGroup.m_OffsetX = BsGroupOffsetX;
		SelectedGroup.m_OffsetY = BsGroupOffsetY;
		EditHistCondGroupChangeOffset(m_UiSelectedGroupID, NewGroupOffsetX, NewGroupOffsetY, true);
	}

	// clip
	DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
	DetailRect.HSplitTop(Spacing, 0, &DetailRect);
	ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);
	DrawRect(ButtonRect, vec4(0,0,0,1));
	DrawRect(ButtonRect2, StyleColorBg);
	DrawText(ButtonRect, Localize("Clip start"), FontSize);
	ButtonRect2.VSplitMid(&ButtonRect, &ButtonRect2);

	str_format(aBuff, sizeof(aBuff), "%d", SelectedGroup.m_ClipX);
	DrawText(ButtonRect, aBuff, FontSize);
	str_format(aBuff, sizeof(aBuff), "%d", SelectedGroup.m_ClipY);
	DrawText(ButtonRect2, aBuff, FontSize);

	DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
	DetailRect.HSplitTop(Spacing, 0, &DetailRect);
	ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);
	DrawRect(ButtonRect, vec4(0,0,0,1));
	DrawRect(ButtonRect2, StyleColorBg);
	DrawText(ButtonRect, Localize("Clip size"), FontSize);
	ButtonRect2.VSplitMid(&ButtonRect, &ButtonRect2);

	str_format(aBuff, sizeof(aBuff), "%d", SelectedGroup.m_ClipWidth);
	DrawText(ButtonRect, aBuff, FontSize);
	str_format(aBuff, sizeof(aBuff), "%d", SelectedGroup.m_ClipHeight);
	DrawText(ButtonRect2, aBuff, FontSize);

	DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
	DetailRect.HSplitTop(Spacing, 0, &DetailRect);
	ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);
	DrawRect(ButtonRect, vec4(0,0,0,1));
	DrawText(ButtonRect, Localize("Use clipping"), FontSize);
	static CUICheckboxYesNo s_CbClipping;
	bool NewUseClipping = SelectedGroup.m_UseClipping;
	if(UiCheckboxYesNo(ButtonRect2, &NewUseClipping, &s_CbClipping))
		EditGroupUseClipping(m_UiSelectedGroupID, NewUseClipping);

	UiScrollRegionAddRect(&s_DetailSR, ButtonRect);

	// layer
	if(m_UiSelectedLayerID >= 0)
	{
		CEditorMap2::CLayer& SelectedLayer = m_Map.m_aLayers[m_UiSelectedLayerID];
		const bool IsGameLayer = m_UiSelectedLayerID == m_Map.m_GameLayerID;

		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);

		// label
		DrawRect(ButtonRect, StyleColorButtonPressed);
		DrawText(ButtonRect, Localize("Layer"), FontSize);

		if(!IsGameLayer)
		{
			DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
			DetailRect.HSplitTop(Spacing, 0, &DetailRect);

			static CUITextInput s_TILayerName;

			static char aBeforeSelectionName[sizeof(SelectedLayer.m_aName)];
			char aNewName[sizeof(SelectedLayer.m_aName)];
			str_copy(aNewName, SelectedLayer.m_aName, sizeof(aNewName));

			// save name before text input selection
			if(!s_TILayerName.m_Selected)
				str_copy(aBeforeSelectionName, SelectedLayer.m_aName, sizeof(aBeforeSelectionName));

			// "preview" new name instantly
			if(UiTextInput(ButtonRect, aNewName, sizeof(aNewName), &s_TILayerName))
				EditHistCondLayerChangeName(m_UiSelectedLayerID, aNewName, false);

			// if not selected, restore old name and change to new name with history entry
			if(!s_TILayerName.m_Selected)
			{
				str_copy(SelectedLayer.m_aName, aBeforeSelectionName, sizeof(SelectedLayer.m_aName));
				EditHistCondLayerChangeName(m_UiSelectedLayerID, aNewName, true);
			}
		}

		// tile layer
		if(SelectedLayer.IsTileLayer())
		{
			// size
			DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
			DetailRect.HSplitTop(Spacing, 0, &DetailRect);
			ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);
			DrawRect(ButtonRect, vec4(0,0,0,1));
			DrawRect(ButtonRect2, StyleColorBg);

			str_format(aBuff, sizeof(aBuff), "%d", SelectedLayer.m_Width);
			DrawText(ButtonRect, Localize("Width"), FontSize);
			DrawText(ButtonRect2, aBuff, FontSize);

			DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
			DetailRect.HSplitTop(Spacing, 0, &DetailRect);
			ButtonRect.VSplitMid(&ButtonRect, &ButtonRect2);
			DrawRect(ButtonRect, vec4(0,0,0,1));
			DrawRect(ButtonRect2, StyleColorBg);

			str_format(aBuff, sizeof(aBuff), "%d", SelectedLayer.m_Height);
			DrawText(ButtonRect, Localize("Height"), FontSize);
			DrawText(ButtonRect2, aBuff, FontSize);

			UiScrollRegionAddRect(&s_DetailSR, ButtonRect);

			// game layer
			if(IsGameLayer)
			{

			}
			else
			{
				// image
				DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
				DetailRect.HSplitTop(Spacing * 2, 0, &DetailRect);
				static CUIButton s_ImageButton;
				const char* pText = SelectedLayer.m_ImageID >= 0 ?
					m_Map.m_Assets.m_aImageNames[SelectedLayer.m_ImageID].m_Buff : Localize("none");
				if(UiButton(ButtonRect, pText, &s_ImageButton, FontSize))
				{
					int ImageID = SelectedLayer.m_ImageID + 1;
					if(ImageID >= m_Map.m_Assets.m_ImageCount)
						ImageID = -1;
					EditLayerChangeImage(m_UiSelectedLayerID, ImageID);
				}

				// color
				CUIRect ColorRect;
				DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
				DetailRect.HSplitTop(Spacing, 0, &DetailRect);
				ButtonRect.VSplitMid(&ButtonRect, &ColorRect);

				DrawRect(ButtonRect, vec4(0,0,0,1));
				DrawText(ButtonRect, "Color", FontSize);
				DrawRect(ColorRect, SelectedLayer.m_Color);

				static CUIButton s_SliderColorR, s_SliderColorG, s_SliderColorB, s_SliderColorA;

				DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
				ButtonRect.VMargin(2, &ButtonRect);
				DetailRect.HSplitTop(Spacing, 0, &DetailRect);

				vec4 NewColor = SelectedLayer.m_Color;
				bool SliderModified = false;
				static bool AnySliderSelected = false;
				static vec4 ColorBeforePreview = SelectedLayer.m_Color;

				if(!AnySliderSelected)
					ColorBeforePreview = SelectedLayer.m_Color;
				AnySliderSelected = false;

				vec4 SliderColor(0.7, 0.1, 0.1, 1);
				SliderModified |= UiSliderFloat(ButtonRect, &NewColor.r, 0.0f, 1.0f, &s_SliderColorR, &SliderColor);
				AnySliderSelected |= UI()->CheckActiveItem(&s_SliderColorR);

				DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
				ButtonRect.VMargin(2, &ButtonRect);
				DetailRect.HSplitTop(Spacing, 0, &DetailRect);

				SliderColor = vec4(0.1, 0.7, 0.1, 1);
				SliderModified |= UiSliderFloat(ButtonRect, &NewColor.g, 0.0f, 1.0f, &s_SliderColorG, &SliderColor);
				AnySliderSelected |= UI()->CheckActiveItem(&s_SliderColorG);

				DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
				ButtonRect.VMargin(2, &ButtonRect);
				DetailRect.HSplitTop(Spacing, 0, &DetailRect);

				SliderColor = vec4(0.1, 0.1, 0.7, 1);
				SliderModified |= UiSliderFloat(ButtonRect, &NewColor.b, 0.0f, 1.0f, &s_SliderColorB, &SliderColor);
				AnySliderSelected |= UI()->CheckActiveItem(&s_SliderColorB);

				DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
				ButtonRect.VMargin(2, &ButtonRect);
				DetailRect.HSplitTop(Spacing, 0, &DetailRect);

				SliderColor = vec4(0.5, 0.5, 0.5, 1);
				SliderModified |= UiSliderFloat(ButtonRect, &NewColor.a, 0.0f, 1.0f, &s_SliderColorA, &SliderColor);
				AnySliderSelected |= UI()->CheckActiveItem(&s_SliderColorA);

				// "preview" change instantly
				if(SliderModified)
					EditHistCondLayerChangeColor(m_UiSelectedLayerID, NewColor, false);

				// restore old color before preview, then change it
				if(!AnySliderSelected)
				{
					SelectedLayer.m_Color = ColorBeforePreview;
					EditHistCondLayerChangeColor(m_UiSelectedLayerID, NewColor, true);
				}

				DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
				DetailRect.HSplitTop(Spacing, 0, &DetailRect);

				// Auto map
				CTilesetMapper2* pMapper = m_Map.AssetsFindTilesetMapper(SelectedLayer.m_ImageID);

				if(pMapper)
				{
					// label
					DrawRect(ButtonRect, StyleColorButtonPressed);
					DrawText(ButtonRect, Localize("Auto-map"), FontSize);

					const int RulesetCount = pMapper->RuleSetNum();
					static CUIButton s_ButtonAutoMap[16];
					 // TODO: find a better solution to this
					dbg_assert(RulesetCount <= 16, "RulesetCount is too big");

					for(int r = 0; r < RulesetCount; r++)
					{
						DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
						DetailRect.HSplitTop(Spacing, 0, &DetailRect);

						if(UiButton(ButtonRect, pMapper->GetRuleSetName(r), &s_ButtonAutoMap[r], FontSize))
						{
							EditTileLayerAutoMapWhole(m_UiSelectedLayerID, r);
						}
					}
				}

				UiScrollRegionAddRect(&s_DetailSR, ButtonRect);
			}
		}
		else if(SelectedLayer.IsQuadLayer())
		{
			// image
			DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
			DetailRect.HSplitTop(Spacing, 0, &DetailRect);
			static CUIButton s_ImageButton;
			const char* pText = SelectedLayer.m_ImageID >= 0 ?
				m_Map.m_Assets.m_aImageNames[SelectedLayer.m_ImageID].m_Buff : Localize("none");
			if(UiButton(ButtonRect, pText, &s_ImageButton, FontSize))
			{
				int ImageID = SelectedLayer.m_ImageID + 1;
				if(ImageID >= m_Map.m_Assets.m_ImageCount)
					ImageID = -1;
				EditLayerChangeImage(m_UiSelectedLayerID, ImageID);
			}

			// quad count
			DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
			DetailRect.HSplitTop(Spacing, 0, &DetailRect);
			DrawRect(ButtonRect, vec4(0,0,0,1));
			str_format(aBuff, sizeof(aBuff), "Quads = %d", SelectedLayer.m_aQuads.size());
			DrawText(ButtonRect, aBuff, FontSize);

			UiScrollRegionAddRect(&s_DetailSR, ButtonRect);
		}
	}

	UiEndScrollRegion(&s_DetailSR);
}

void CEditor2::RenderPopupMenuFile()
{
	CUIRect Rect = m_UiCurrentPopupRect;
	if(m_UiCurrentPopupID == POPUP_MENU_FILE)
	{
		bool Inside = UI()->MouseInside(&Rect);
		bool Close = false;
		static int s_FileMenuID = 0;
		UI()->SetHotItem(&s_FileMenuID);

		if(UI()->CheckActiveItem(&s_FileMenuID))
		{
			if(!UI()->MouseButton(0))
			{
				if(!Inside)
					Close = true;
				UI()->SetActiveItem(0);
			}
		}
		else if(UI()->HotItem() == &s_FileMenuID)
		{
			if(UI()->MouseButton(0))
				UI()->SetActiveItem(&s_FileMenuID);
		}

		if(Input()->KeyPress(KEY_ESCAPE))
			Close = true;

		// render the actual menu
		static CUIButton s_NewMapButton;
		static CUIButton s_SaveButton;
		static CUIButton s_SaveAsButton;
		static CUIButton s_OpenButton;
		static CUIButton s_OpenCurrentButton;
		static CUIButton s_AppendButton;
		static CUIButton s_ExitButton;

		CUIRect Slot;
		// Rect.HSplitTop(2.0f, &Slot, &Rect);
		Rect.HSplitTop(20.0f, &Slot, &Rect);
		if(UiButton(Slot, "New", &s_NewMapButton))
		{
			if(HasUnsavedData())
			{

			}
			else
			{
				Reset();
				// pEditor->m_aFileName[0] = 0;
			}
			Close = true;
		}

		Rect.HSplitTop(20.0f, &Slot, &Rect);
		if(UiButton(Slot, "Load", &s_OpenButton))
		{
			if(HasUnsavedData())
			{

			}
			else
			{
				// pEditor->InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_MAP, "Load map", "Load", "maps", "", pEditor->CallbackOpenMap, pEditor);
			}
			Close = true;
		}

		Rect.HSplitTop(20.0f, &Slot, &Rect);
		if(UiButton(Slot, "Append", &s_AppendButton))
		{
			Close = true;
		}

		Rect.HSplitTop(20.0f, &Slot, &Rect);
		if(UiButton(Slot, "Save", &s_SaveButton))
		{
			Close = true;
		}

		Rect.HSplitTop(20.0f, &Slot, &Rect);
		if(UiButton(Slot, "Save As", &s_SaveAsButton))
		{
			Close = true;
		}

		Rect.HSplitTop(20.0f, &Slot, &Rect);
		if(UiButton(Slot, "Exit", &s_ExitButton))
		{
			Close = true;
			g_Config.m_ClEditor = 0;
		}

		if(Close)
		{
			m_UiCurrentPopupID = POPUP_NONE;
			UI()->SetActiveItem(0);
		}
	}
}

void CEditor2::RenderPopupBrushPalette()
{
	dbg_assert(m_UiSelectedLayerID >= 0 && m_UiSelectedLayerID < m_Map.m_aLayers.size(),
			   "m_UiSelectedLayerID is out of bounds");

	const CUIRect UiScreenRect = m_UiScreenRect;
	Graphics()->MapScreen(UiScreenRect.x, UiScreenRect.y, UiScreenRect.w, UiScreenRect.h);

	DrawRect(UiScreenRect, vec4(0, 0, 0, 0.5)); // darken the background a bit

	CUIRect MainRect = {0, 0, m_UiMainViewRect.h, m_UiMainViewRect.h};
	MainRect.x += (m_UiMainViewRect.w - MainRect.w) * 0.5;
	MainRect.Margin(50.0f, &MainRect);
	m_UiPopupBrushPaletteRect = MainRect;
	//DrawRect(MainRect, StyleColorBg);

	CUIRect TopRow;
	MainRect.HSplitTop(40, &TopRow, &MainRect);

	const CEditorMap2::CLayer& SelectedTileLayer = m_Map.m_aLayers[m_UiSelectedLayerID];
	dbg_assert(SelectedTileLayer.IsTileLayer(), "Selected layer is not a tile layer");

	IGraphics::CTextureHandle PaletteTexture;
	if(m_UiSelectedLayerID == m_Map.m_GameLayerID)
		PaletteTexture = m_EntitiesTexture;
	else
		PaletteTexture = m_Map.m_Assets.m_aTextureHandle[SelectedTileLayer.m_ImageID];

	CUIRect ImageRect = MainRect;
	ImageRect.w = min(ImageRect.w, ImageRect.h);
	ImageRect.h = ImageRect.w;
	m_UiPopupBrushPaletteImageRect = ImageRect;

	// checker background
	Graphics()->BlendNone();
	Graphics()->TextureSet(m_CheckerTexture);

	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	Graphics()->QuadsSetSubset(0, 0, 64.f, 64.f);
	IGraphics::CQuadItem BgQuad(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
	Graphics()->QuadsDrawTL(&BgQuad, 1);
	Graphics()->QuadsEnd();

	// palette image
	Graphics()->BlendNormal();
	Graphics()->TextureSet(PaletteTexture);

	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	IGraphics::CQuadItem Quad(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();

	const float TileSize = ImageRect.w / 16.f;

	CUIBrushPaletteState& Bps = m_UiBrushPaletteState;
	u8* aTileSelected = Bps.m_aTileSelected;

	// right click clears brush
	if(UI()->MouseButtonClicked(1))
	{
		BrushClear();
	}

	// do mouse dragging
	static CUIMouseDrag s_DragState;
	bool FinishedDragging = UiDoMouseDragging(&s_DragState, m_UiPopupBrushPaletteImageRect, &s_DragState);
	// TODO: perhaps allow dragging from outside the popup for convenience

	// finished dragging
	if(FinishedDragging)
	{
		u8* aTileSelected = Bps.m_aTileSelected;
		const float TileSize = m_UiPopupBrushPaletteImageRect.w / 16.f;
		const vec2 RelMouseStartPos = s_DragState.m_StartDragPos -
			vec2(m_UiPopupBrushPaletteImageRect.x, m_UiPopupBrushPaletteImageRect.y);
		const vec2 RelMouseEndPos = s_DragState.m_EndDragPos -
			vec2(m_UiPopupBrushPaletteImageRect.x, m_UiPopupBrushPaletteImageRect.y);
		const int DragStartTileX = clamp(RelMouseStartPos.x / TileSize, 0.f, 15.f);
		const int DragStartTileY = clamp(RelMouseStartPos.y / TileSize, 0.f, 15.f);
		const int DragEndTileX = clamp(RelMouseEndPos.x / TileSize, 0.f, 15.f);
		const int DragEndTileY = clamp(RelMouseEndPos.y / TileSize, 0.f, 15.f);

		const int DragTLX = min(DragStartTileX, DragEndTileX);
		const int DragTLY = min(DragStartTileY, DragEndTileY);
		const int DragBRX = max(DragStartTileX, DragEndTileX);
		const int DragBRY = max(DragStartTileY, DragEndTileY);

		for(int ty = DragTLY; ty <= DragBRY; ty++)
		{
			for(int tx = DragTLX; tx <= DragBRX; tx++)
			{
				aTileSelected[ty * 16 + tx] = 1;
			}
		}

		int StartX = 999;
		int StartY = 999;
		int EndX = -1;
		int EndY = -1;
		for(int ti = 0; ti < 256; ti++)
		{
			if(aTileSelected[ti])
			{
				int tx = (ti & 0xF);
				int ty = (ti / 16);
				StartX = min(StartX, tx);
				StartY = min(StartY, ty);
				EndX = max(EndX, tx);
				EndY = max(EndY, ty);
			}
		}

		EndX++;
		EndY++;

		array2<CTile> aBrushTiles;
		const int Width = EndX - StartX;
		const int Height = EndY - StartY;
		aBrushTiles.add_empty(Width * Height);

		const int LastTid = (EndY-1)*16+EndX;
		for(int ti = (StartY*16+StartX); ti < LastTid; ti++)
		{
			if(aTileSelected[ti])
			{
				int tx = (ti & 0xF) - StartX;
				int ty = (ti / 16) - StartY;
				aBrushTiles[ty * Width + tx].m_Index = ti;
			}
		}

		SetNewBrush(aBrushTiles.base_ptr(), Width, Height);
	}

	// selected overlay
	for(int ti = 0; ti < 256; ti++)
	{
		if(aTileSelected[ti])
		{
			const int tx = ti & 0xF;
			const int ty = ti / 16;
			CUIRect TileRect = {
				ImageRect.x + tx*TileSize,
				ImageRect.y + ty*TileSize,
				TileSize, TileSize
			};
			DrawRect(TileRect, StyleColorTileSelection);
		}
	}

	// hover tile
	if(!s_DragState.m_IsDragging && UI()->MouseInside(&ImageRect))
	{
		const vec2 RelMousePos = m_UiMousePos - vec2(ImageRect.x, ImageRect.y);
		const int HoveredTileX = RelMousePos.x / TileSize;
		const int HoveredTileY = RelMousePos.y / TileSize;

		CUIRect HoverTileRect = {
			ImageRect.x + HoveredTileX*TileSize,
			ImageRect.y + HoveredTileY*TileSize,
			TileSize, TileSize
		};
		DrawRectBorderOutside(HoverTileRect, StyleColorTileHover, 2, StyleColorTileHoverBorder);
	}

	// drag rectangle
	if(s_DragState.m_IsDragging)
	{
		const vec2 RelMouseStartPos = s_DragState.m_StartDragPos - vec2(ImageRect.x, ImageRect.y);
		const vec2 RelMouseEndPos = m_UiMousePos - vec2(ImageRect.x, ImageRect.y);
		const int DragStartTileX = clamp(RelMouseStartPos.x / TileSize, 0.f, 15.f);
		const int DragStartTileY = clamp(RelMouseStartPos.y / TileSize, 0.f, 15.f);
		const int DragEndTileX = clamp(RelMouseEndPos.x / TileSize, 0.f, 15.f);
		const int DragEndTileY = clamp(RelMouseEndPos.y / TileSize, 0.f, 15.f);

		const int DragTLX = min(DragStartTileX, DragEndTileX);
		const int DragTLY = min(DragStartTileY, DragEndTileY);
		const int DragBRX = max(DragStartTileX, DragEndTileX);
		const int DragBRY = max(DragStartTileY, DragEndTileY);

		CUIRect DragTileRect = {
			ImageRect.x + DragTLX*TileSize,
			ImageRect.y + DragTLY*TileSize,
			(DragBRX-DragTLX+1)*TileSize,
			(DragBRY-DragTLY+1)*TileSize
		};
		DrawRectBorder(DragTileRect, StyleColorTileHover, 2, StyleColorTileHoverBorder);
	}

	CUIRect ButtonRect;
	TopRow.Margin(3.0f, &TopRow);
	TopRow.VSplitLeft(100, &ButtonRect, &TopRow);

	static CUIButton s_ButClear;
	if(UiButton(ButtonRect, Localize("Clear"), &s_ButClear))
	{
		BrushClear();
	}

	TopRow.VSplitLeft(2, 0, &TopRow);
	TopRow.VSplitLeft(100, &ButtonRect, &TopRow);

	static CUIButton s_ButEraser;
	if(UiButton(ButtonRect, Localize("Eraser"), &s_ButEraser))
	{
		BrushClear();
		CTile TileZero;
		mem_zero(&TileZero, sizeof(TileZero));
		SetNewBrush(&TileZero, 1, 1);
	}

	TopRow.VSplitLeft(2, 0, &TopRow);
	TopRow.VSplitLeft(40, &ButtonRect, &TopRow);
	static CUIButton s_ButFlipX;
	if(UiButton(ButtonRect, Localize("X/X"), &s_ButFlipX))
	{
		BrushFlipX();
	}

	TopRow.VSplitLeft(2, 0, &TopRow);
	TopRow.VSplitLeft(40, &ButtonRect, &TopRow);
	static CUIButton s_ButFlipY;
	if(UiButton(ButtonRect, Localize("Y/Y"), &s_ButFlipY))
	{
		BrushFlipY();
	}

	TopRow.VSplitLeft(2, 0, &TopRow);
	TopRow.VSplitLeft(50, &ButtonRect, &TopRow);
	static CUIButton s_ButRotClockwise;
	if(UiButton(ButtonRect, "90° ⟳", &s_ButRotClockwise))
	{
		BrushRotate90Clockwise();
	}

	TopRow.VSplitLeft(2, 0, &TopRow);
	TopRow.VSplitLeft(50, &ButtonRect, &TopRow);
	static CUIButton s_ButRotCounterClockwise;
	if(UiButton(ButtonRect, "90° ⟲", &s_ButRotCounterClockwise))
	{
		BrushRotate90CounterClockwise();
	}

	// Auto map
	CUIRect RightCol = ImageRect;
	RightCol.x = ImageRect.x + ImageRect.w + 2;
	RightCol.w = 80;

	// reset selected rule when changing image
	if(SelectedTileLayer.m_ImageID != m_UiBrushPaletteState.m_ImageID)
		m_BrushAutomapRuleID = -1;
	m_UiBrushPaletteState.m_ImageID = SelectedTileLayer.m_ImageID;

	CTilesetMapper2* pMapper = m_Map.AssetsFindTilesetMapper(SelectedTileLayer.m_ImageID);
	const float ButtonHeight = 20;
	const float Spacing = 2;

	if(pMapper)
	{
		const int RulesetCount = pMapper->RuleSetNum();
		static CUIButton s_ButtonAutoMap[16];
		 // TODO: find a better solution to this
		dbg_assert(RulesetCount <= 16, "RulesetCount is too big");

		for(int r = 0; r < RulesetCount; r++)
		{
			RightCol.HSplitTop(ButtonHeight, &ButtonRect, &RightCol);
			RightCol.HSplitTop(Spacing, 0, &RightCol);

			bool Selected = m_BrushAutomapRuleID == r;
			if(UiButtonSelect(ButtonRect, pMapper->GetRuleSetName(r), &s_ButtonAutoMap[r], Selected, 10))
			{
				m_BrushAutomapRuleID = r;
			}
		}
	}

	RenderBrush(m_UiMousePos);
}

void CEditor2::RenderBrush(vec2 Pos)
{
	if(m_Brush.IsEmpty())
		return;

	float ScreenX0, ScreenX1, ScreenY0, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	Graphics()->MapScreen(ScreenX0 - Pos.x, ScreenY0 - Pos.y,
		ScreenX1 - Pos.x, ScreenY1 - Pos.y);

	const CEditorMap2::CLayer& SelectedTileLayer = m_Map.m_aLayers[m_UiSelectedLayerID];
	dbg_assert(SelectedTileLayer.IsTileLayer(), "Selected layer is not a tile layer");

	const float TileSize = 32;
	const CUIRect BrushRect = {0, 0, m_Brush.m_Width * TileSize, m_Brush.m_Height * TileSize};
	DrawRect(BrushRect, vec4(1, 1, 1, 0.1));

	IGraphics::CTextureHandle LayerTexture;
	if(m_UiSelectedLayerID == m_Map.m_GameLayerID)
		LayerTexture = m_EntitiesTexture;
	else
		LayerTexture = m_Map.m_Assets.m_aTextureHandle[SelectedTileLayer.m_ImageID];

	const vec4 White(1, 1, 1, 1);

	Graphics()->TextureSet(LayerTexture);
	Graphics()->BlendNone();
	RenderTools()->RenderTilemap(m_Brush.m_aTiles.base_ptr(), m_Brush.m_Width, m_Brush.m_Height, TileSize, White, LAYERRENDERFLAG_OPAQUE, 0, 0, -1, 0);
	Graphics()->BlendNormal();
	RenderTools()->RenderTilemap(m_Brush.m_aTiles.base_ptr(), m_Brush.m_Width, m_Brush.m_Height, TileSize, White, LAYERRENDERFLAG_TRANSPARENT, 0, 0, -1, 0);

	DrawRectBorder(BrushRect, vec4(0, 0, 0, 0), 1, vec4(1, 1, 1, 1));

	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

struct CImageNameItem
{
	int m_Index;
	CEditorMap2::CImageName m_Name;
};

static int CompareImageNameItems(const void* pA, const void* pB)
{
	const CImageNameItem& a = *(CImageNameItem*)pA;
	const CImageNameItem& b = *(CImageNameItem*)pB;
	return str_comp_nocase(a.m_Name.m_Buff, b.m_Name.m_Buff);
}

void CEditor2::RenderAssetManager()
{
	CEditorMap2::CAssets& Assets = m_Map.m_Assets;

	const CUIRect UiScreenRect = m_UiScreenRect;
	Graphics()->MapScreen(UiScreenRect.x, UiScreenRect.y, UiScreenRect.w, UiScreenRect.h);

	CUIRect TopPanel, RightPanel, MainViewRect;
	UiScreenRect.VSplitRight(150.0f, &MainViewRect, &RightPanel);
	MainViewRect.HSplitTop(40.0f, &TopPanel, &MainViewRect);

	DrawRect(RightPanel, StyleColorBg);

	CUIRect NavRect, ButtonRect, DetailRect;
	RightPanel.Margin(3.0f, &NavRect);

	NavRect.HSplitBottom(200, &NavRect, &DetailRect);
	UI()->ClipEnable(&NavRect);

	static CUIButton s_UiImageButState[CEditorMap2::MAX_IMAGES] = {};
	const float FontSize = 8.0f;
	const float ButtonHeight = 20.0f;
	const float Spacing = 2.0f;

	static CImageNameItem s_aImageItems[CEditorMap2::MAX_IMAGES];
	const int ImageCount = Assets.m_ImageCount;

	// sort images by name
	static float s_LastSortTime = 0;
	if(m_LocalTime - s_LastSortTime > 0.1f) // this should be very fast but still, limit it
	{
		s_LastSortTime = m_LocalTime;
		for(int i = 0; i < ImageCount; i++)
		{
			s_aImageItems[i].m_Index = i;
			s_aImageItems[i].m_Name = Assets.m_aImageNames[i];
		}

		qsort(s_aImageItems, ImageCount, sizeof(s_aImageItems[0]), CompareImageNameItems);
	}

	for(int i = 0; i < ImageCount; i++)
	{
		if(i != 0)
			NavRect.HSplitTop(Spacing, 0, &NavRect);
		NavRect.HSplitTop(ButtonHeight, &ButtonRect, &NavRect);

		const bool Selected = (m_UiSelectedImageID == s_aImageItems[i].m_Index);
		const vec4 ColBorder = Selected ? vec4(1, 0, 0, 1) : StyleColorButtonBorder;
		if(UiButtonEx(ButtonRect, s_aImageItems[i].m_Name.m_Buff, &s_UiImageButState[i],
			StyleColorButton,StyleColorButtonHover, StyleColorButtonPressed, ColBorder, FontSize))
		{
			m_UiSelectedImageID = s_aImageItems[i].m_Index;
		}
	}

	UI()->ClipDisable(); // NavRect

	if(m_UiSelectedImageID == -1 && Assets.m_ImageCount > 0)
		m_UiSelectedImageID = 0;

	if(m_UiSelectedImageID != -1)
	{
		// display image
		CUIRect ImageRect = MainViewRect;
		ImageRect.w = Assets.m_aTextureInfos[m_UiSelectedImageID].m_Width/m_GfxScreenWidth *
			UiScreenRect.w * (1.0/m_Zoom);
		ImageRect.h = Assets.m_aTextureInfos[m_UiSelectedImageID].m_Height/m_GfxScreenHeight *
			UiScreenRect.h * (1.0/m_Zoom);

		UI()->ClipEnable(&MainViewRect);

		Graphics()->TextureSet(m_CheckerTexture);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, ImageRect.w/16.f, ImageRect.h/16.f);
		IGraphics::CQuadItem BgQuad(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
		Graphics()->QuadsDrawTL(&BgQuad, 1);
		Graphics()->QuadsEnd();

		Graphics()->WrapClamp();

		Graphics()->TextureSet(Assets.m_aTextureHandle[m_UiSelectedImageID]);
		Graphics()->QuadsBegin();
		IGraphics::CQuadItem Quad(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();

		Graphics()->WrapNormal();
		UI()->ClipDisable();

		// details

		// label
		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);
		DrawRect(ButtonRect, StyleColorButtonPressed);
		DrawText(ButtonRect, Localize("Image"), FontSize);

		// name
		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);
		DrawRect(ButtonRect, vec4(0,0,0,1));
		DrawText(ButtonRect, Assets.m_aImageNames[m_UiSelectedImageID].m_Buff, FontSize);

		// size
		char aBuff[128];
		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);
		DrawRect(ButtonRect, vec4(0,0,0,1));
		str_format(aBuff, sizeof(aBuff), "size = (%d, %d)",
			Assets.m_aTextureInfos[m_UiSelectedImageID].m_Width,
			Assets.m_aTextureInfos[m_UiSelectedImageID].m_Height);
		DrawText(ButtonRect, aBuff, FontSize);

		// size
		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);
		DrawRect(ButtonRect, vec4(0,0,0,1));
		str_format(aBuff, sizeof(aBuff), "Embedded Crc = 0x%X",
			Assets.m_aImageEmbeddedCrc[m_UiSelectedImageID]);
		DrawText(ButtonRect, aBuff, FontSize);

		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);

		static CUIButton s_ButDelete;
		if(UiButton(ButtonRect, Localize("Delete"), &s_ButDelete))
		{
			EditDeleteImage(m_UiSelectedImageID);
		}

		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);

		static CUITextInput s_TextInputAdd;
		static char aAddPath[256] = "grass_main.png";
		UiTextInput(ButtonRect, aAddPath, sizeof(aAddPath), &s_TextInputAdd);

		DetailRect.HSplitTop(ButtonHeight, &ButtonRect, &DetailRect);
		DetailRect.HSplitTop(Spacing, 0, &DetailRect);

		static CUIButton s_ButAdd;
		if(UiButton(ButtonRect, Localize("Add image"), &s_ButAdd))
		{
			EditAddImage(aAddPath);
		}
	}

	if(m_UiSelectedImageID >= Assets.m_ImageCount)
		m_UiSelectedImageID = -1;

	RenderTopPanel(TopPanel);
}

inline void CEditor2::DrawRect(const CUIRect& Rect, const vec4& Color)
{
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	Graphics()->SetColor(Color.r*Color.a, Color.g*Color.a, Color.b*Color.a, Color.a);
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();
}

void CEditor2::DrawRectBorder(const CUIRect& Rect, const vec4& Color, float Border, const vec4 BorderColor)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float FakeToScreenX = m_GfxScreenWidth/(ScreenX1-ScreenX0);
	const float FakeToScreenY = m_GfxScreenHeight/(ScreenY1-ScreenY0);
	const float BorderW = Border/FakeToScreenX;
	const float BorderH = Border/FakeToScreenY;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	// border pass
	if(Color.a == 1)
	{
		IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
		Graphics()->SetColor(BorderColor.r*BorderColor.a, BorderColor.g*BorderColor.a,
							 BorderColor.b*BorderColor.a, BorderColor.a);
		Graphics()->QuadsDrawTL(&Quad, 1);
	}
	else
	{
		// border pass
		IGraphics::CQuadItem Quads[4] = {
			IGraphics::CQuadItem(Rect.x, Rect.y, BorderW, Rect.h),
			IGraphics::CQuadItem(Rect.x+Rect.w-BorderW, Rect.y, BorderW, Rect.h),
			IGraphics::CQuadItem(Rect.x+BorderW, Rect.y, Rect.w-BorderW, BorderH),
			IGraphics::CQuadItem(Rect.x+BorderW, Rect.y+Rect.h-BorderH, Rect.w-BorderW, BorderH)
		};
		Graphics()->SetColor(BorderColor.r*BorderColor.a, BorderColor.g*BorderColor.a,
							 BorderColor.b*BorderColor.a, BorderColor.a);
		Graphics()->QuadsDrawTL(Quads, 4);
	}

	// front pass
	if(Color.a > 0.001)
	{
		IGraphics::CQuadItem QuadCenter(Rect.x + BorderW, Rect.y + BorderH,
										Rect.w - BorderW*2, Rect.h - BorderH*2);
		Graphics()->SetColor(Color.r*Color.a, Color.g*Color.a,
							 Color.b*Color.a, Color.a);
		Graphics()->QuadsDrawTL(&QuadCenter, 1);
	}

	Graphics()->QuadsEnd();
}

void CEditor2::DrawRectBorderOutside(const CUIRect& Rect, const vec4& Color, float Border, const vec4 BorderColor)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float FakeToScreenX = m_GfxScreenWidth/(ScreenX1-ScreenX0);
	const float FakeToScreenY = m_GfxScreenHeight/(ScreenY1-ScreenY0);
	const float BorderW = Border/FakeToScreenX;
	const float BorderH = Border/FakeToScreenY;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	// border pass
	if(Color.a == 1)
	{
		IGraphics::CQuadItem Quad(Rect.x-BorderW, Rect.y-BorderH, Rect.w+BorderW*2, Rect.h+BorderH*2);
		Graphics()->SetColor(BorderColor.r*BorderColor.a, BorderColor.g*BorderColor.a,
							 BorderColor.b*BorderColor.a, BorderColor.a);
		Graphics()->QuadsDrawTL(&Quad, 1);
	}
	else
	{
		// border pass
		IGraphics::CQuadItem Quads[4] = {
			IGraphics::CQuadItem(Rect.x-BorderW, Rect.y-BorderH, BorderW, Rect.h+BorderH*2),
			IGraphics::CQuadItem(Rect.x+Rect.w, Rect.y-BorderH, BorderW, Rect.h+BorderH*2),
			IGraphics::CQuadItem(Rect.x, Rect.y-BorderH, Rect.w, BorderH),
			IGraphics::CQuadItem(Rect.x, Rect.y+Rect.h, Rect.w, BorderH)
		};
		Graphics()->SetColor(BorderColor.r*BorderColor.a, BorderColor.g*BorderColor.a,
							 BorderColor.b*BorderColor.a, BorderColor.a);
		Graphics()->QuadsDrawTL(Quads, 4);
	}

	// front pass
	if(Color.a > 0.001)
	{
		IGraphics::CQuadItem QuadCenter(Rect.x, Rect.y, Rect.w, Rect.h);
		Graphics()->SetColor(Color.r*Color.a, Color.g*Color.a,
							 Color.b*Color.a, Color.a);
		Graphics()->QuadsDrawTL(&QuadCenter, 1);
	}

	Graphics()->QuadsEnd();
}

void CEditor2::DrawRectBorderMiddle(const CUIRect& Rect, const vec4& Color, float Border, const vec4 BorderColor)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float FakeToScreenX = m_GfxScreenWidth/(ScreenX1-ScreenX0);
	const float FakeToScreenY = m_GfxScreenHeight/(ScreenY1-ScreenY0);
	const float BorderW = Border/FakeToScreenX;
	const float BorderH = Border/FakeToScreenY;
	const float BorderWhalf = BorderW/2.f;
	const float BorderHhalf = BorderH/2.f;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	// border pass
	if(Color.a == 1)
	{
		IGraphics::CQuadItem Quad(Rect.x-BorderWhalf, Rect.y-BorderHhalf, Rect.w+BorderW, Rect.h+BorderH);
		Graphics()->SetColor(BorderColor.r*BorderColor.a, BorderColor.g*BorderColor.a,
							 BorderColor.b*BorderColor.a, BorderColor.a);
		Graphics()->QuadsDrawTL(&Quad, 1);
	}
	else
	{
		// border pass
		IGraphics::CQuadItem Quads[4] = {
			IGraphics::CQuadItem(Rect.x-BorderWhalf, Rect.y-BorderHhalf, BorderW, Rect.h+BorderH),
			IGraphics::CQuadItem(Rect.x+Rect.w-BorderHhalf, Rect.y-BorderHhalf, BorderW, Rect.h+BorderH),
			IGraphics::CQuadItem(Rect.x, Rect.y-BorderHhalf, Rect.w, BorderH),
			IGraphics::CQuadItem(Rect.x, Rect.y+Rect.h-BorderHhalf, Rect.w, BorderH)
		};
		Graphics()->SetColor(BorderColor.r*BorderColor.a, BorderColor.g*BorderColor.a,
							 BorderColor.b*BorderColor.a, BorderColor.a);
		Graphics()->QuadsDrawTL(Quads, 4);
	}

	// front pass
	if(Color.a > 0.001)
	{
		IGraphics::CQuadItem QuadCenter(Rect.x+BorderWhalf, Rect.y+BorderHhalf,
			Rect.w-BorderW, Rect.h-BorderH);
		Graphics()->SetColor(Color.r*Color.a, Color.g*Color.a,
							 Color.b*Color.a, Color.a);
		Graphics()->QuadsDrawTL(&QuadCenter, 1);
	}

	Graphics()->QuadsEnd();
}

inline void CEditor2::DrawText(const CUIRect& Rect, const char* pText, float FontSize, vec4 Color, int Align)
{
	const float OffY = (Rect.h - FontSize - 3.0f) * 0.5f;
	CTextCursor Cursor;
	if(Align == -1)
	{
		TextRender()->SetCursor(&Cursor, Rect.x + OffY, Rect.y + OffY, FontSize, TEXTFLAG_RENDER);
		TextRender()->TextShadowed(&Cursor, pText, -1, vec2(0,0), vec4(0,0,0,0), Color);
	}
	else if(Align == 0)
	{
		float tw = TextRender()->TextWidth(0, FontSize, pText, -1, -1.0f);
		TextRender()->SetCursor(&Cursor, Rect.x + /*OffY*/ + Rect.w/2-tw/2, Rect.y + OffY, FontSize, TEXTFLAG_RENDER);
		TextRender()->TextShadowed(&Cursor, pText, -1, vec2(0,0), vec4(0,0,0,0), Color);	
	}
}

void CEditor2::UiDoButtonBehavior(const void* pID, const CUIRect& Rect, CUIButton* pButState)
{
	pButState->m_Clicked = false;

	if(UI()->CheckActiveItem(pID))
	{
		if(!UI()->MouseButton(0))
		{
			pButState->m_Clicked = true;
			UI()->SetActiveItem(0);
		}
	}

	pButState->m_Hovered = false;
	pButState->m_Pressed = false;

	if(UI()->MouseInside(&Rect) && UI()->MouseInsideClip())
	{
		pButState->m_Hovered = true;
		if(pID)
			UI()->SetHotItem(pID);

		if(UI()->MouseButton(0))
		{
			pButState->m_Pressed = true;
			if(pID && UI()->MouseButtonClicked(0))
				UI()->SetActiveItem(pID);
		}
	}
	else if(pID && UI()->HotItem() == pID)
	{
		UI()->SetHotItem(0);
	}

}

bool CEditor2::UiDoMouseDragging(const void* pID, const CUIRect& Rect, CUIMouseDrag* pDragState)
{
	bool Return = false;
	CUIButton ButState;
	UiDoButtonBehavior(pID, Rect, &ButState);

	if((!pID || UI()->CheckActiveItem(pID)) && UI()->MouseButton(0))
	{
		if(!pDragState->m_IsDragging && UI()->MouseButtonClicked(0) && ButState.m_Pressed)
		{
			pDragState->m_StartDragPos = m_UiMousePos;
			pDragState->m_IsDragging = true;
		}
	}
	else
	{
		if(pDragState->m_IsDragging)
		{
			pDragState->m_EndDragPos = m_UiMousePos;
			Return = true; // finished dragging
		}
		pDragState->m_IsDragging = false;
	}

	return Return;
}

bool CEditor2::UiButton(const CUIRect& Rect, const char* pText, CUIButton* pButState, float FontSize, int Align)
{
	return UiButtonEx(Rect, pText, pButState, StyleColorButton, StyleColorButtonHover,
		StyleColorButtonPressed, StyleColorButtonBorder, FontSize, Align);
}

bool CEditor2::UiButtonEx(const CUIRect& Rect, const char* pText, CUIButton* pButState, vec4 ColNormal,
	vec4 ColHover, vec4 ColPress, vec4 ColBorder, float FontSize, int Align)
{
	UiDoButtonBehavior(pButState, Rect, pButState);

	vec4 ShowButColor = ColNormal;
	if(pButState->m_Hovered)
		ShowButColor = ColHover;
	if(pButState->m_Pressed)
		ShowButColor = ColPress;

	DrawRectBorder(Rect, ShowButColor, 1, ColBorder);
	DrawText(Rect, pText, FontSize, vec4(1,1,1,1), Align);
	return pButState->m_Clicked;
}

bool CEditor2::UiTextInput(const CUIRect& Rect, char* pText, int TextMaxLength, CUITextInput* pInputState)
{
	UiDoButtonBehavior(pInputState, Rect, &pInputState->m_Button);
	if(pInputState->m_Button.m_Clicked)
	{
		UI()->SetActiveItem(pInputState);
	}
	else if(UI()->CheckActiveItem(pInputState) && !pInputState->m_Button.m_Hovered &&
		UI()->MouseButtonClicked(0))
	{
		UI()->SetActiveItem(0);
	}

	const bool PrevSelected = pInputState->m_Selected;
	pInputState->m_Selected = UI()->CheckActiveItem(pInputState);

	// just got selected
	if(!PrevSelected && pInputState->m_Selected)
	{
		pInputState->m_CursorPos = str_length(pText);
	}

	static float s_StartBlinkTime = 0;
	const int OldCursorPos = pInputState->m_CursorPos;

	bool Changed = false;
	if(pInputState->m_Selected)
	{
		m_UiTextInputConsumeKeyboardEvents = true;
		for(int i = 0; i < Input()->NumEvents(); i++)
		{
			int Len = str_length(pText);
			int NumChars = Len;
			Changed |= CLineInput::Manipulate(Input()->GetEvent(i), pText, TextMaxLength, TextMaxLength,
				&Len, &pInputState->m_CursorPos, &NumChars, Input());
		}
	}

	const float FontSize = 8.0f;

	vec4 BorderColor;
	if(pInputState->m_Selected)
		BorderColor = StyleColorInputSelected;
	else if(pInputState->m_Button.m_Hovered)
		BorderColor = StyleColorButtonHover;
	else
		BorderColor = StyleColorButtonBorder;

	DrawRectBorder(Rect, vec4(0, 0, 0, 1), 1, BorderColor);
	DrawText(Rect, pText, FontSize);

	// cursor
	if(OldCursorPos != pInputState->m_CursorPos)
		s_StartBlinkTime = m_LocalTime;

	if(pInputState->m_Selected && fmod(m_LocalTime - s_StartBlinkTime, 1.0f) < 0.5f)
	{
		const float OffY = (Rect.h - FontSize - 3.0f) * 0.5f; // see DrawText
		float w = TextRender()->TextWidth(0, FontSize, pText, pInputState->m_CursorPos, -1);
		CUIRect CursorRect = Rect;
		CursorRect.x += w + OffY;
		CursorRect.y += 2;
		CursorRect.w = m_UiScreenRect.w / m_GfxScreenWidth; // 1px
		CursorRect.h -= 4;
		DrawRect(CursorRect, vec4(1,1,1,1));
	}

	return Changed;
}

bool CEditor2::UiIntegerInput(const CUIRect& Rect, int* pInteger, CUIIntegerInput* pInputState)
{
	const int OldInteger = *pInteger;
	const u8 OldSelected = pInputState->m_TextInput.m_Selected;

	// string format, when value differ, empty or on select/deselect
	if((pInputState->m_Value != *pInteger && !OldSelected) ||
		(pInputState->m_aIntBuff[0] == 0 && !OldSelected) ||
		(OldSelected != pInputState->m_TextInput.m_Selected))
	{
		pInputState->m_Value = *pInteger;
		str_format(pInputState->m_aIntBuff, sizeof(pInputState->m_aIntBuff), "%d", *pInteger);
		pInputState->m_TextInput.m_CursorPos = str_length(pInputState->m_aIntBuff);
	}

	UiTextInput(Rect, pInputState->m_aIntBuff, sizeof(pInputState->m_aIntBuff), &pInputState->m_TextInput);

	// string parse
	if(sscanf(pInputState->m_aIntBuff, "%d", pInteger) < 1)
		*pInteger = 0;

	char aBuff[sizeof(pInputState->m_aIntBuff)];
	str_format(aBuff, sizeof(aBuff), "%d", *pInteger);
	const bool IsEmpty = pInputState->m_aIntBuff[0] == 0;
	const bool IsMinusOnly = pInputState->m_aIntBuff[0] == '-' && pInputState->m_aIntBuff[1] == 0;
	if(!IsEmpty && !IsMinusOnly && str_comp(aBuff, pInputState->m_aIntBuff) != 0)
	{
		pInputState->m_Value = *pInteger;
		str_copy(pInputState->m_aIntBuff, aBuff, sizeof(pInputState->m_aIntBuff));
		pInputState->m_TextInput.m_CursorPos = str_length(pInputState->m_aIntBuff);
	}

	return OldInteger != *pInteger;
}

bool CEditor2::UiSliderInt(const CUIRect& Rect, int* pInt, int Min, int Max, CUIButton* pInputState)
{
	dbg_assert(false, "implement, see UiSliderFloat");

	const int OldInt = *pInt;
	UiDoButtonBehavior(pInputState, Rect, pInputState);
	DrawRect(Rect, StyleColorBg);

	*pInt = clamp(*pInt, Min, Max);
	float Progress = (float)*pInt/(Max-Min);
	CUIRect ProgressRect = Rect;
	ProgressRect.w = Rect.w * Progress;
	DrawRect(ProgressRect, StyleColorButtonHover);

	char aIntBuff[32];
	str_format(aIntBuff, sizeof(aIntBuff), "%d", *pInt);
	DrawText(Rect, aIntBuff, 8.0f, vec4(1,1,1,1));

	return OldInt != *pInt;
}

bool CEditor2::UiSliderFloat(const CUIRect& Rect, float* pVal, float Min, float Max, CUIButton* pInputState,
	const vec4* pColor)
{
	const float OldInt = *pVal;
	UiDoButtonBehavior(pInputState, Rect, pInputState);

	// start grabbing
	if(!UI()->CheckActiveItem(pInputState) && pInputState->m_Pressed && UI()->MouseButtonClicked(0))
	{
		UI()->SetActiveItem(pInputState);
	}

	// stop grabbing
	if(UI()->CheckActiveItem(pInputState) && !UI()->MouseButton(0))
	{
		UI()->SetActiveItem(0);
	}

	if(UI()->CheckActiveItem(pInputState))
	{
		const float Precision = 200.0f; // TODO: precision should perhaps depend on exact rect pixel width
		float ClickedX = m_UiMousePos.x - Rect.x;
		float Cx01 = clamp(ClickedX, 0.0f, Rect.w) / Rect.w;
		*pVal  = (int)(roundf(Cx01 * Precision)) * (Max-Min) / Precision + Min;
	}

	DrawRect(Rect, StyleColorBg);

	*pVal = clamp(*pVal, Min, Max);
	float Progress = *pVal/(Max-Min);
	CUIRect ProgressRect = Rect;
	ProgressRect.w = Rect.w * Progress;

	vec4 Color = StyleColorButtonHover;
	if(pColor)
		Color = *pColor;

	if(pInputState->m_Hovered)
		Color += vec4(0.1, 0.1, 0.1, 0.0);

	DrawRect(ProgressRect, Color);

	char aIntBuff[32];
	str_format(aIntBuff, sizeof(aIntBuff), "%.3f", *pVal);
	DrawText(Rect, aIntBuff, 8.0f, vec4(1,1,1,1));

	return OldInt != *pVal;
}

bool CEditor2::UiCheckboxYesNo(const CUIRect& Rect, bool* pVal, CUICheckboxYesNo* pCbyn)
{
	const bool OldVal = *pVal;
	CUIRect YesRect, NoRect;
	Rect.VSplitMid(&NoRect, &YesRect);
	UiDoButtonBehavior(&pCbyn->m_NoBut, NoRect, &pCbyn->m_NoBut);
	UiDoButtonBehavior(&pCbyn->m_YesBut, YesRect, &pCbyn->m_YesBut);

	if(pCbyn->m_NoBut.m_Clicked)
		*pVal = false;
	if(pCbyn->m_YesBut.m_Clicked)
		*pVal = true;

	vec4 ColorYes = StyleColorButton;
	vec4 ColorNo = StyleColorButton;
	if(!*pVal)
	{
		ColorNo = StyleColorInputSelected;
		if(pCbyn->m_YesBut.m_Pressed)
			ColorYes = StyleColorButtonPressed;
		else if(pCbyn->m_YesBut.m_Hovered)
			ColorYes = StyleColorButtonPressed;
	}
	else
	{
		ColorYes = StyleColorInputSelected;
		if(pCbyn->m_NoBut.m_Pressed)
			ColorNo = StyleColorButtonPressed;
		else if(pCbyn->m_NoBut.m_Hovered)
			ColorNo = StyleColorButtonPressed;
	}

	DrawRect(NoRect, ColorNo);
	DrawRect(YesRect, ColorYes);
	DrawText(NoRect, Localize("No"), 8);
	DrawText(YesRect, Localize("Yes"), 8);

	return OldVal != *pVal;
}

bool CEditor2::UiButtonSelect(const CUIRect& Rect, const char* pText, CUIButton* pButState, bool Selected,
	float FontSize)
{
	return UiButtonEx(Rect, pText, pButState, StyleColorButton, StyleColorButtonHover, StyleColorButtonPressed,
					  Selected ? vec4(1, 0, 0, 1):StyleColorButtonBorder, FontSize);
}

bool CEditor2::UiGrabHandle(const CUIRect& Rect, CUIGrabHandle* pGrabHandle, const vec4& ColorNormal, const vec4& ColorActive)
{
	UiDoMouseDragging(pGrabHandle, Rect, pGrabHandle);
	const bool Active = IsInsideRect(m_UiMousePos, Rect) || pGrabHandle->m_IsDragging;
	DrawRect(Rect, Active ? ColorActive : ColorNormal);
	pGrabHandle->m_IsGrabbed = UI()->CheckActiveItem(pGrabHandle);
	return pGrabHandle->m_IsDragging;
}


void CEditor2::UiBeginScrollRegion(CScrollRegion* pSr, CUIRect* pClipRect, vec2* pOutOffset, const CScrollRegionParams* pParams)
{
	if(pParams)
		pSr->m_Params = *pParams;

	pSr->m_WasClipped = UI()->IsClipped();
	pSr->m_OldClipRect = *UI()->ClipArea();

	// only show scrollbar if content overflows
	const bool ShowScrollbar = pSr->m_ContentH > pClipRect->h;

	CUIRect ScrollBarBg;
	CUIRect* pModifyRect = ShowScrollbar ? pClipRect : 0;
	pClipRect->VSplitRight(pSr->m_Params.m_ScrollbarWidth, pModifyRect, &ScrollBarBg);
	ScrollBarBg.Margin(pSr->m_Params.m_ScrollbarMargin, &pSr->m_RailRect);

	if(ShowScrollbar)
	{
		if(pSr->m_Params.m_ScrollbarBgColor.a > 0)
			RenderTools()->DrawRoundRect(&ScrollBarBg, pSr->m_Params.m_ScrollbarBgColor, 4.0f);
		if(pSr->m_Params.m_RailBgColor.a > 0)
			RenderTools()->DrawRoundRect(&pSr->m_RailRect, pSr->m_Params.m_RailBgColor, 0);
	}
	else
		pSr->m_ContentScrollOff.y = 0;

	if(pSr->m_Params.m_ClipBgColor.a > 0)
		RenderTools()->DrawRoundRect(pClipRect, pSr->m_Params.m_ClipBgColor, 4.0f);

	UI()->ClipEnable(pClipRect);

	pSr->m_ClipRect = *pClipRect;
	pSr->m_ContentH = 0;
	*pOutOffset = pSr->m_ContentScrollOff;
}

void CEditor2::UiEndScrollRegion(CScrollRegion* pSr)
{
	UI()->ClipDisable();
	if(pSr->m_WasClipped)
		UI()->ClipEnable(&pSr->m_OldClipRect);

	dbg_assert(pSr->m_ContentH > 0, "Add some rects with ScrollRegionAddRect()");

	// only show scrollbar if content overflows
	if(pSr->m_ContentH <= pSr->m_ClipRect.h)
		return;

	// scroll wheel
	CUIRect RegionRect = pSr->m_ClipRect;
	RegionRect.w += pSr->m_Params.m_ScrollbarWidth;
	if(UI()->MouseInside(&RegionRect))
	{
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
			pSr->m_ScrollY -= pSr->m_Params.m_ScrollSpeed;
		else if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
			pSr->m_ScrollY += pSr->m_Params.m_ScrollSpeed;
	}

	const float SliderHeight = max(pSr->m_Params.m_SliderMinHeight,
		pSr->m_ClipRect.h/pSr->m_ContentH * pSr->m_RailRect.h);

	CUIRect Slider = pSr->m_RailRect;
	Slider.h = SliderHeight;
	const float MaxScroll = pSr->m_RailRect.h - SliderHeight;

	if(pSr->m_RequestScrollY >= 0)
	{
		pSr->m_ScrollY = pSr->m_RequestScrollY/(pSr->m_ContentH - pSr->m_ClipRect.h) * MaxScroll;
		pSr->m_RequestScrollY = -1;
	}

	pSr->m_ScrollY = clamp(pSr->m_ScrollY, 0.0f, MaxScroll);
	Slider.y += pSr->m_ScrollY;

	bool Hovered = false;
	bool Grabbed = false;
	const void* pID = &pSr->m_ScrollY;
	int Inside = UI()->MouseInside(&Slider);

	if(Inside)
	{
		UI()->SetHotItem(pID);

		if(!UI()->CheckActiveItem(pID) && UI()->MouseButtonClicked(0))
		{
			UI()->SetActiveItem(pID);
			pSr->m_MouseGrabStart.y = UI()->MouseY();
		}

		Hovered = true;
	}

	if(UI()->CheckActiveItem(pID) && !UI()->MouseButton(0))
		UI()->SetActiveItem(0);

	// move slider
	if(UI()->CheckActiveItem(pID) && UI()->MouseButton(0))
	{
		float my = UI()->MouseY();
		pSr->m_ScrollY += my - pSr->m_MouseGrabStart.y;
		pSr->m_MouseGrabStart.y = my;

		Grabbed = true;
	}

	pSr->m_ScrollY = clamp(pSr->m_ScrollY, 0.0f, MaxScroll);
	pSr->m_ContentScrollOff.y = -pSr->m_ScrollY/MaxScroll * (pSr->m_ContentH - pSr->m_ClipRect.h);

	vec4 SliderColor = pSr->m_Params.m_SliderColor;
	if(Grabbed)
		SliderColor = pSr->m_Params.m_SliderColorGrabbed;
	else if(Hovered)
		SliderColor = pSr->m_Params.m_SliderColorHover;

	RenderTools()->DrawRoundRect(&Slider, SliderColor, 0);
}

void CEditor2::UiScrollRegionAddRect(CScrollRegion* pSr, CUIRect Rect)
{
	vec2 ContentPos = vec2(pSr->m_ClipRect.x, pSr->m_ClipRect.y);
	ContentPos.x += pSr->m_ContentScrollOff.x;
	ContentPos.y += pSr->m_ContentScrollOff.y;
	pSr->m_LastAddedRect = Rect;
	pSr->m_ContentH = max(Rect.y + Rect.h - ContentPos.y, pSr->m_ContentH);
}

void CEditor2::UiScrollRegionScrollHere(CScrollRegion* pSr, int Option)
{
	const float MinHeight = min(pSr->m_ClipRect.h, pSr->m_LastAddedRect.h);
	const float TopScroll = pSr->m_LastAddedRect.y -
		(pSr->m_ClipRect.y + pSr->m_ContentScrollOff.y);

	switch(Option)
	{
		case CScrollRegion::SCROLLHERE_TOP:
			pSr->m_RequestScrollY = TopScroll;
			break;

		case CScrollRegion::SCROLLHERE_BOTTOM:
			pSr->m_RequestScrollY = TopScroll - (pSr->m_ClipRect.h - MinHeight);
			break;

		case CScrollRegion::SCROLLHERE_KEEP_IN_VIEW:
		default: {
			const float dy = pSr->m_LastAddedRect.y - pSr->m_ClipRect.y;

			if(dy < 0)
				pSr->m_RequestScrollY = TopScroll;
			else if(dy > (pSr->m_ClipRect.h-MinHeight))
				pSr->m_RequestScrollY = TopScroll - (pSr->m_ClipRect.h - MinHeight);
		} break;
	}
}

bool CEditor2::UiScrollRegionIsRectClipped(CScrollRegion* pSr, const CUIRect& Rect)
{
	return (pSr->m_ClipRect.x > (Rect.x + Rect.w)
		|| (pSr->m_ClipRect.x + pSr->m_ClipRect.w) < Rect.x
		|| pSr->m_ClipRect.y > (Rect.y + Rect.h)
		|| (pSr->m_ClipRect.y + pSr->m_ClipRect.h) < Rect.y);
}


void CEditor2::Reset()
{

}

void CEditor2::ResetCamera()
{
	m_Zoom = 1;
	m_MapUiPosOffset = vec2(0,0);
}

void CEditor2::ChangeZoom(float Zoom)
{
	// zoom centered on mouse
	const float WorldWidth = m_ZoomWorldViewWidth/m_Zoom;
	const float WorldHeight = m_ZoomWorldViewHeight/m_Zoom;
	const float MuiX = (m_UiMousePos.x+m_MapUiPosOffset.x)/m_UiScreenRect.w;
	const float MuiY = (m_UiMousePos.y+m_MapUiPosOffset.y)/m_UiScreenRect.h;
	const float RelMouseX = MuiX * m_ZoomWorldViewWidth;
	const float RelMouseY = MuiY * m_ZoomWorldViewHeight;
	const float NewZoomWorldViewWidth = WorldWidth * Zoom;
	const float NewZoomWorldViewHeight = WorldHeight * Zoom;
	const float NewRelMouseX = MuiX * NewZoomWorldViewWidth;
	const float NewRelMouseY = MuiY * NewZoomWorldViewHeight;
	m_MapUiPosOffset.x -= (NewRelMouseX-RelMouseX)/NewZoomWorldViewWidth*m_UiScreenRect.w;
	m_MapUiPosOffset.y -= (NewRelMouseY-RelMouseY)/NewZoomWorldViewHeight*m_UiScreenRect.h;
	m_Zoom = Zoom;
}

void CEditor2::ChangePage(int Page)
{
	UI()->SetHotItem(0);
	UI()->SetActiveItem(0);
	m_Page = Page;
}

void CEditor2::ChangeTool(int Tool)
{
	if(m_Tool == Tool) return;

	if(m_Tool == TOOL_TILE_BRUSH)
		BrushClear();

	m_Tool = Tool;
}

void CEditor2::SelectLayerBelowCurrentOne()
{
	dbg_assert(m_UiSelectedLayerID >= 0 && m_UiSelectedLayerID < m_Map.m_aLayers.size(),
		"m_UiSelectedLayerID is invalid");
	dbg_assert(m_UiSelectedGroupID >= 0 && m_UiSelectedGroupID < m_Map.m_aGroups.size(),
		"m_UiSelectedGroupID is invalid");

	const CEditorMap2::CGroup& Group = m_Map.m_aGroups[m_UiSelectedGroupID];
	int LayerPos = -1;
	for(int i = 0; i < (Group.m_LayerCount-1); i++)
	{
		if(Group.m_apLayerIDs[i] == m_UiSelectedLayerID)
		{
			LayerPos = i;
			break;
		}
	}

	if(LayerPos != -1)
	{
		m_UiSelectedLayerID = Group.m_apLayerIDs[LayerPos+1];
	}
	else
	{
		m_UiSelectedLayerID = m_Map.m_GameLayerID;
		m_UiSelectedGroupID = m_Map.m_GameGroupID;
	}

	dbg_assert(m_UiSelectedLayerID >= 0 && m_UiSelectedLayerID < m_Map.m_aLayers.size(),
		"m_UiSelectedLayerID is invalid");
	dbg_assert(m_UiSelectedGroupID >= 0 && m_UiSelectedGroupID < m_Map.m_aGroups.size(),
		"m_UiSelectedGroupID is invalid");
}

void CEditor2::SetNewBrush(CTile* aTiles, int Width, int Height)
{
	dbg_assert(Width > 0 && Height > 0, "Brush: wrong dimensions");
	m_Brush.m_Width = Width;
	m_Brush.m_Height = Height;
	m_Brush.m_aTiles.clear();
	m_Brush.m_aTiles.add(aTiles, Width*Height);
}

void CEditor2::BrushClear()
{
	m_Brush.m_Width = 0;
	m_Brush.m_Height = 0;
	m_Brush.m_aTiles.clear();
	mem_zero(m_UiBrushPaletteState.m_aTileSelected, sizeof(m_UiBrushPaletteState.m_aTileSelected));
}

void CEditor2::BrushFlipX()
{
	if(m_Brush.m_Width <= 0)
		return;

	const int BrushWidth = m_Brush.m_Width;
	const int BrushHeight = m_Brush.m_Height;
	array2<CTile>& aTiles = m_Brush.m_aTiles;
	array2<CTile> aTilesCopy;
	aTilesCopy.add(aTiles.base_ptr(), aTiles.size());

	for(int ty = 0; ty < BrushHeight; ty++)
	{
		for(int tx = 0; tx < BrushWidth; tx++)
		{
			const int tid = ty * BrushWidth + tx;
			aTiles[tid] = aTilesCopy[ty * BrushWidth + (BrushWidth-tx-1)];
			aTiles[tid].m_Flags ^= aTiles[tid].m_Flags&TILEFLAG_ROTATE ? TILEFLAG_HFLIP:TILEFLAG_VFLIP;
		}
	}
}

void CEditor2::BrushFlipY()
{
	if(m_Brush.m_Width <= 0)
		return;

	const int BrushWidth = m_Brush.m_Width;
	const int BrushHeight = m_Brush.m_Height;
	array2<CTile>& aTiles = m_Brush.m_aTiles;
	array2<CTile> aTilesCopy;
	aTilesCopy.add(aTiles.base_ptr(), aTiles.size());

	for(int ty = 0; ty < BrushHeight; ty++)
	{
		for(int tx = 0; tx < BrushWidth; tx++)
		{
			const int tid = ty * BrushWidth + tx;
			aTiles[tid] = aTilesCopy[(BrushHeight-ty-1) * BrushWidth + tx];
			aTiles[tid].m_Flags ^= aTiles[tid].m_Flags&TILEFLAG_ROTATE ? TILEFLAG_VFLIP:TILEFLAG_HFLIP;
		}
	}
}

void CEditor2::BrushRotate90Clockwise()
{
	if(m_Brush.m_Width <= 0)
		return;

	const int BrushWidth = m_Brush.m_Width;
	const int BrushHeight = m_Brush.m_Height;
	array2<CTile>& aTiles = m_Brush.m_aTiles;
	array2<CTile> aTilesCopy;
	aTilesCopy.add(aTiles.base_ptr(), aTiles.size());

	for(int ty = 0; ty < BrushHeight; ty++)
	{
		for(int tx = 0; tx < BrushWidth; tx++)
		{
			const int tid = tx * BrushHeight + (BrushHeight-1-ty);
			aTiles[tid] = aTilesCopy[ty * BrushWidth + tx];
			if(aTiles[tid].m_Flags&TILEFLAG_ROTATE)
				aTiles[tid].m_Flags ^= (TILEFLAG_HFLIP|TILEFLAG_VFLIP);
			aTiles[tid].m_Flags ^= TILEFLAG_ROTATE;
		}
	}

	m_Brush.m_Width = BrushHeight;
	m_Brush.m_Height = BrushWidth;
}

void CEditor2::BrushRotate90CounterClockwise()
{
	if(m_Brush.m_Width <= 0)
		return;

	const int BrushWidth = m_Brush.m_Width;
	const int BrushHeight = m_Brush.m_Height;
	array2<CTile>& aTiles = m_Brush.m_aTiles;
	array2<CTile> aTilesCopy;
	aTilesCopy.add(aTiles.base_ptr(), aTiles.size());

	for(int ty = 0; ty < BrushHeight; ty++)
	{
		for(int tx = 0; tx < BrushWidth; tx++)
		{
			const int tid = (BrushWidth-1-tx) * BrushHeight + ty;
			aTiles[tid] = aTilesCopy[ty * BrushWidth + tx];
			if(!(aTiles[tid].m_Flags&TILEFLAG_ROTATE))
				aTiles[tid].m_Flags ^= (TILEFLAG_HFLIP|TILEFLAG_VFLIP);
			aTiles[tid].m_Flags ^= TILEFLAG_ROTATE;
		}
	}

	m_Brush.m_Width = BrushHeight;
	m_Brush.m_Height = BrushWidth;
}

void CEditor2::BrushPaintLayer(int PaintTX, int PaintTY, int LayerID)
{
	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	const int BrushW = m_Brush.m_Width;
	const int BrushH = m_Brush.m_Height;
	const int LayerW = Layer.m_Width;
	const int LayerH = Layer.m_Height;
	array2<CTile>& aLayerTiles = Layer.m_aTiles;
	array2<CTile>& aBrushTiles = m_Brush.m_aTiles;

	for(int ty = 0; ty < BrushH; ty++)
	{
		for(int tx = 0; tx < BrushW; tx++)
		{
			int BrushTid = ty * BrushW + tx;
			int LayerTx = tx + PaintTX;
			int LayerTy = ty + PaintTY;

			if(LayerTx < 0 || LayerTx > LayerW-1 || LayerTy < 0 || LayerTy > LayerH-1)
				continue;

			int LayerTid = LayerTy * LayerW + LayerTx;
			aLayerTiles[LayerTid] = aBrushTiles[BrushTid];
		}
	}
}

void CEditor2::BrushPaintLayerFillRectRepeat(int PaintTX, int PaintTY, int PaintW, int PaintH, int LayerID)
{
	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	const int BrushW = m_Brush.m_Width;
	const int BrushH = m_Brush.m_Height;
	const int LayerW = Layer.m_Width;
	const int LayerH = Layer.m_Height;
	array2<CTile>& aLayerTiles = Layer.m_aTiles;
	array2<CTile>& aBrushTiles = m_Brush.m_aTiles;

	for(int ty = 0; ty < PaintH; ty++)
	{
		for(int tx = 0; tx < PaintW; tx++)
		{
			int BrushTid = (ty % BrushH) * BrushW + (tx % BrushW);
			int LayerTx = tx + PaintTX;
			int LayerTy = ty + PaintTY;

			if(LayerTx < 0 || LayerTx > LayerW-1 || LayerTy < 0 || LayerTy > LayerH-1)
				continue;

			int LayerTid = LayerTy * LayerW + LayerTx;
			aLayerTiles[LayerTid] = aBrushTiles[BrushTid];
		}
	}
}

void CEditor2::TileLayerRegionToBrush(int LayerID, int StartTX, int StartTY, int EndTX, int EndTY)
{
	const CEditorMap2::CLayer& TileLayer = m_Map.m_aLayers[LayerID];
	dbg_assert(TileLayer.IsTileLayer(), "Layer is not a tile layer");

	const int SelStartX = clamp(min(StartTX, EndTX), 0, TileLayer.m_Width-1);
	const int SelStartY = clamp(min(StartTY, EndTY), 0, TileLayer.m_Height-1);
	const int SelEndX = clamp(max(StartTX, EndTX), 0, TileLayer.m_Width-1) + 1;
	const int SelEndY = clamp(max(StartTY, EndTY), 0, TileLayer.m_Height-1) + 1;
	const int Width = SelEndX - SelStartX;
	const int Height = SelEndY - SelStartY;

	array2<CTile> aExtractTiles;
	aExtractTiles.add_empty(Width * Height);

	const int LayerWidth = TileLayer.m_Width;
	const array2<CTile>& aLayerTiles = TileLayer.m_aTiles;
	const int StartTid = SelStartY * LayerWidth + SelStartX;
	const int LastTid = (SelEndY-1) * LayerWidth + SelEndX;

	for(int ti = StartTid; ti < LastTid; ti++)
	{
		const int tx = (ti % LayerWidth) - SelStartX;
		const int ty = (ti / LayerWidth) - SelStartY;
		if(tx >= 0 && tx < Width && ty >= 0 && ty < Height)
			aExtractTiles[ty * Width + tx] = aLayerTiles[ti];
	}

	SetNewBrush(aExtractTiles.base_ptr(), Width, Height);
}

bool CEditor2::LoadMap(const char* pFileName)
{
	if(m_Map.Load(pFileName))
	{
		OnMapLoaded();
		ed_log("map '%s' sucessfully loaded.", pFileName);
		return true;
	}
	ed_log("failed to load map '%s'", pFileName);
	return false;
}

bool CEditor2::SaveMap(const char* pFileName)
{
	if(m_Map.Save(pFileName))
	{
		// send rcon.. if we can
		// TODO: implement / uncomment
		if(Client()->RconAuthed())
		{/*
			CServerInfo CurrentServerInfo;
			Client()->GetServerInfo(&CurrentServerInfo);
			char aMapName[128];
			m_pEditor->ExtractName(pFileName, aMapName, sizeof(aMapName));
			if(!str_comp(aMapName, CurrentServerInfo.m_aMap))
				m_pEditor->Client()->Rcon("reload");
		*/}

		ed_log("map '%s' sucessfully saved.", pFileName);
		return true;
	}
	ed_log("failed to save map '%s'", pFileName);
	return false;
}

void CEditor2::OnMapLoaded()
{
	m_UiSelectedLayerID = m_Map.m_GameLayerID;
	m_UiSelectedGroupID = m_Map.m_GameGroupID;
	mem_zero(m_UiGroupHidden.base_ptr(), sizeof(m_UiGroupHidden[0]) * m_UiGroupHidden.size());
	m_UiGroupOpen.set_size(m_Map.m_aGroups.size());
	mem_zero(m_UiGroupOpen.base_ptr(), sizeof(m_UiGroupOpen[0]) * m_UiGroupOpen.size());
	m_UiGroupOpen[m_Map.m_GameGroupID] = true;
	mem_zero(m_UiLayerHidden.base_ptr(), sizeof(m_UiLayerHidden[0]) * m_UiLayerHidden.size());
	mem_zero(m_UiLayerHovered.base_ptr(), sizeof(m_UiLayerHovered[0]) * m_UiLayerHovered.size());
	mem_zero(&m_UiBrushPaletteState, sizeof(m_UiBrushPaletteState));
	ResetCamera();
	BrushClear();

	// clear history
	if(m_pHistoryEntryCurrent)
	{
		CHistoryEntry* pCurrent = m_pHistoryEntryCurrent->m_pNext;
		while(pCurrent)
		{
			mem_free(pCurrent->m_pSnap);
			mem_free(pCurrent->m_pUiSnap); // TODO: make a HistoryDeleteEntry function
			pCurrent = pCurrent->m_pNext;
		}

		pCurrent = m_pHistoryEntryCurrent;
		while(pCurrent)
		{
			mem_free(pCurrent->m_pSnap);
			mem_free(pCurrent->m_pUiSnap); // TODO: make a HistoryDeleteEntry function
			pCurrent = pCurrent->m_pPrev;
		}
		HistoryClear();
		m_pHistoryEntryCurrent = 0x0;
	}

	// initialize history
	m_pHistoryEntryCurrent = HistoryAllocEntry();
	m_pHistoryEntryCurrent->m_pSnap = m_Map.SaveSnapshot();
	m_pHistoryEntryCurrent->m_pUiSnap = SaveUiSnapshot();
	m_pHistoryEntryCurrent->SetAction("Map loaded");
	m_pHistoryEntryCurrent->SetDescription(m_Map.m_aPath);
}

void CEditor2::EditDeleteLayer(int LyID, int ParentGroupID)
{
	dbg_assert(LyID >= 0 && LyID < m_Map.m_aLayers.size(), "LyID out of bounds");
	dbg_assert(ParentGroupID >= 0 && ParentGroupID < m_Map.m_aGroups.size(), "ParentGroupID out of bounds");
	dbg_assert(LyID != m_Map.m_GameLayerID, "Can't delete game layer");
	dbg_assert(m_Map.m_aLayers.size() > 0, "There should be at least a game layer");

	char aHistoryDesc[64];
	str_format(aHistoryDesc, sizeof(aHistoryDesc), "%s", GetLayerName(LyID));

	const int SwappedLyID = m_Map.m_aLayers.size()-1; // see RemoveByIndex
	CEditorMap2::CGroup& ParentGroup = m_Map.m_aGroups[ParentGroupID];
	const int ParentGroupLayerCount = ParentGroup.m_LayerCount;
	dbg_assert(ParentGroupLayerCount > 0, "Parent group layer count is zero?");
	const int GroupCount = m_Map.m_aGroups.size();

	// remove layer id from parent group
	int GroupLayerID = -1;
	for(int li = 0; li < ParentGroupLayerCount && GroupLayerID == -1; li++)
	{
		if(ParentGroup.m_apLayerIDs[li] == LyID)
			GroupLayerID = li;
	}
	dbg_assert(GroupLayerID != -1, "Layer not found inside parent group");

	memmove(&ParentGroup.m_apLayerIDs[GroupLayerID], &ParentGroup.m_apLayerIDs[GroupLayerID+1],
			(ParentGroup.m_LayerCount-GroupLayerID) * sizeof(ParentGroup.m_apLayerIDs[0]));
	ParentGroup.m_LayerCount--;

	// delete actual layer (swap with last)
	m_Map.m_aLayers.remove_index_fast(LyID);

	// GamelayerID: swap id if needed
	dbg_assert(m_Map.m_GameLayerID != LyID, "Can't delete game layer");
	if(m_Map.m_GameLayerID == SwappedLyID)
		m_Map.m_GameLayerID = LyID;

	// Groups: swap last layer id with deleted id
	for(int gi = 0; gi < GroupCount; gi++)
	{
		CEditorMap2::CGroup& Group = m_Map.m_aGroups[gi];
		const int LayerCount = Group.m_LayerCount;
		for(int li = 0; li < LayerCount; li++)
		{
			if(Group.m_apLayerIDs[li] == SwappedLyID)
			{
				Group.m_apLayerIDs[li] = LyID;
				break;
			}
		}
	}

	// validation checks
#ifdef CONF_DEBUG
	dbg_assert(m_Map.m_GameLayerID >= 0 && m_Map.m_GameLayerID < m_Map.m_aLayers.size(),
		"m_Map.m_GameLayerID is invalid");
	dbg_assert(m_Map.m_GameGroupID >= 0 && m_Map.m_GameGroupID < m_Map.m_aGroups.size(),
		"m_UiSelectedGroupID is invalid");

	for(int gi = 0; gi < GroupCount; gi++)
	{
		CEditorMap2::CGroup& Group = m_Map.m_aGroups[gi];
		const int LayerCount = Group.m_LayerCount;
		for(int li = 0; li < LayerCount; li++)
		{
			dbg_assert(Group.m_apLayerIDs[li] >= 0 && Group.m_apLayerIDs[li] < m_Map.m_aLayers.size(),
				"Group.m_apLayerIDs[li] is invalid");
		}
	}
#endif

	// history entry
	HistoryNewEntry("Deleted layer", aHistoryDesc);
}

void CEditor2::EditDeleteGroup(int GroupID)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");
	dbg_assert(GroupID != m_Map.m_GameGroupID, "Can't delete game group");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];
	while(Group.m_LayerCount > 0)
	{
		EditDeleteLayer(Group.m_apLayerIDs[0], GroupID);
	}

	if(m_Map.m_GameGroupID > GroupID)
		m_Map.m_GameGroupID--; // see RemoveByIndexSlide

	m_Map.m_aGroups.remove_index(GroupID);

	// history entry
	char aBuff[64];
	str_format(aBuff, sizeof(aBuff), "Group %d", GroupID);
	HistoryNewEntry("Deleted group", aBuff);
}

void CEditor2::EditDeleteImage(int ImgID)
{
	dbg_assert(ImgID >= 0 && ImgID <= m_Map.m_Assets.m_ImageCount, "ImgID out of bounds");

	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%s", m_Map.m_Assets.m_aImageNames[ImgID].m_Buff);

	m_Map.AssetsDeleteImage(ImgID);

	// history entry
	HistoryNewEntry("Deleted image", aHistoryEntryDesc);
}

void CEditor2::EditAddImage(const char* pFilename)
{
	bool Success = m_Map.AssetsAddAndLoadImage(pFilename);

	// history entry
	if(Success)
	{
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%s", pFilename);
		HistoryNewEntry("Added image", aHistoryEntryDesc);
	}
}

void CEditor2::EditCreateAndAddGroup()
{
	CEditorMap2::CGroup Group;
	Group.m_OffsetX = 0;
	Group.m_OffsetY = 0;
	Group.m_ParallaxX = 100;
	Group.m_ParallaxY = 100;
	Group.m_LayerCount = 0;
	m_Map.m_aGroups.add(Group);

	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "Group %d", m_Map.m_aGroups.size()-1);
	HistoryNewEntry("New group", aHistoryEntryDesc);
}

int CEditor2::EditCreateAndAddTileLayerUnder(int UnderLyID, int GroupID)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	// base width and height on given layer if it's a tilelayer, else base on game layer
	int LyWidth = m_Map.m_aLayers[m_Map.m_GameLayerID].m_Width;
	int LyHeight = m_Map.m_aLayers[m_Map.m_GameLayerID].m_Height;
	int LyImageID = -1;

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];

	int UnderGrpLyID = -1;
	if(UnderLyID != -1)
	{
		dbg_assert(UnderLyID >= 0 && UnderLyID < m_Map.m_aLayers.size(), "LyID out of bounds");
		const CEditorMap2::CLayer& TopLayer = m_Map.m_aLayers[UnderLyID];

		if(TopLayer.IsTileLayer())
		{
			LyWidth = TopLayer.m_Width;
			LyHeight = TopLayer.m_Height;
			LyImageID = TopLayer.m_ImageID;
		}

		const int ParentGroupLayerCount = Group.m_LayerCount;
		for(int li = 0; li < ParentGroupLayerCount; li++)
		{
			if(Group.m_apLayerIDs[li] == UnderLyID)
			{
				UnderGrpLyID = li;
				break;
			}
		}

		dbg_assert(UnderGrpLyID != -1, "Layer not found in parent group");
	}

	dbg_assert(Group.m_LayerCount < CEditorMap2::MAX_GROUP_LAYERS, "Group is full of layers");

	CEditorMap2::CLayer& Layer = m_Map.NewTileLayer(LyWidth, LyHeight);
	Layer.m_ImageID = LyImageID;

	const int GrpLyID = UnderGrpLyID+1;
	memmove(&Group.m_apLayerIDs[GrpLyID+1], &Group.m_apLayerIDs[GrpLyID],
		(Group.m_LayerCount-GrpLyID) * sizeof(Group.m_apLayerIDs[0]));

	const int LyID = m_Map.m_aLayers.size()-1;
	Group.m_apLayerIDs[GrpLyID] = LyID;
	Group.m_LayerCount++;

	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "Tile %d", LyID);
	HistoryNewEntry(Localize("New tile layer"), aHistoryEntryDesc);
	return LyID;
}

int CEditor2::EditCreateAndAddQuadLayerUnder(int UnderLyID, int GroupID)
{
	dbg_assert(UnderLyID >= 0 && UnderLyID < m_Map.m_aLayers.size(), "LyID out of bounds");
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	/*CEditorMap2::CLayer& Layer =*/ m_Map.NewQuadLayer();
	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];

	int UnderGrpLyID = -1;
	const int ParentGroupLayerCount = Group.m_LayerCount;
	for(int li = 0; li < ParentGroupLayerCount; li++)
	{
		if(Group.m_apLayerIDs[li] == UnderLyID)
		{
			UnderGrpLyID = li;
			break;
		}
	}

	dbg_assert(UnderGrpLyID != -1, "Layer not found in parent group");
	dbg_assert(Group.m_LayerCount < CEditorMap2::MAX_GROUP_LAYERS, "Group is full of layers");

	const int GrpLyID = UnderGrpLyID+1;
	memmove(&Group.m_apLayerIDs[GrpLyID+1], &Group.m_apLayerIDs[GrpLyID],
		(Group.m_LayerCount-GrpLyID) * sizeof(Group.m_apLayerIDs[0]));
	Group.m_LayerCount++;
	const int LyID = m_Map.m_aLayers.size()-1;
	Group.m_apLayerIDs[GrpLyID] = LyID;

	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "Quad %d", LyID);
	HistoryNewEntry(Localize("New Quad layer"), aHistoryEntryDesc);
	return LyID;
}

void CEditor2::EditLayerChangeImage(int LayerID, int NewImageID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");
	dbg_assert(NewImageID >= -1 && NewImageID < m_Map.m_Assets.m_ImageCount, "NewImageID out of bounds");

	const int OldImageID = m_Map.m_aLayers[LayerID].m_ImageID;
	if(OldImageID == NewImageID)
		return;

	m_Map.m_aLayers[LayerID].m_ImageID = NewImageID;

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("%s: changed image"),
		GetLayerName(LayerID));
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%s > %s",
		OldImageID < 0 ? Localize("none") : m_Map.m_Assets.m_aImageNames[OldImageID].m_Buff,
		NewImageID < 0 ? Localize("none") : m_Map.m_Assets.m_aImageNames[NewImageID].m_Buff);
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditGroupUseClipping(int GroupID, bool NewUseClipping)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];
	if(Group.m_UseClipping == NewUseClipping)
		return;

	const bool OldUseClipping = Group.m_UseClipping;
	Group.m_UseClipping = NewUseClipping;

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: use clipping"),
		GroupID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%s > %s",
		OldUseClipping ? Localize("true") : Localize("false"),
		NewUseClipping ? Localize("true") : Localize("false"));
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

int CEditor2::EditGroupOrderMove(int GroupID, int RelativePos)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	const int NewGroupID = clamp(GroupID + RelativePos, 0, m_Map.m_aGroups.size()-1);
	if(NewGroupID == GroupID)
		return GroupID;

	tl_swap(m_Map.m_aGroups[GroupID], m_Map.m_aGroups[NewGroupID]);
	if(m_Map.m_GameGroupID == GroupID)
		m_Map.m_GameGroupID = NewGroupID;
	else if(m_Map.m_GameGroupID == NewGroupID)
		m_Map.m_GameGroupID = GroupID;

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: change order"),
		GroupID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%d > %d",
		GroupID,
		NewGroupID);
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	return NewGroupID;
}

int CEditor2::EditLayerOrderMove(int LayerID, int RelativePos)
{
	// Returns new parent group ID (or the same if it does not change)

	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "GroupID out of bounds");

	int ParentGroupID = -1;
	int LayerPos = -1;
	const int GroupCount = m_Map.m_aGroups.size();
	for(int gi = 0; gi < GroupCount && ParentGroupID == -1; gi++)
	{
		const CEditorMap2::CGroup& Group = m_Map.m_aGroups[gi];

		for(int l = 0; l < Group.m_LayerCount; l++)
		{
			if(Group.m_apLayerIDs[l] == LayerID)
			{
				ParentGroupID = gi;
				LayerPos = l;
				break;
			}
		}
	}

	dbg_assert(ParentGroupID != -1 && LayerPos != -1,
		"Parent group or layer position not found for this LayerID");

	// this assume a relative change of 1
	RelativePos = clamp(RelativePos, -1, 1);
	if(RelativePos == 0)
		return ParentGroupID;

	const int NewPos = LayerPos + RelativePos;
	CEditorMap2::CGroup& ParentGroup = m_Map.m_aGroups[ParentGroupID];
	const bool IsGameLayer = LayerID == m_Map.m_GameLayerID;

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];

	// go up one group
	if(NewPos < 0)
	{
		if(ParentGroupID > 0 && !IsGameLayer)
		{
			CEditorMap2::CGroup& Group = m_Map.m_aGroups[ParentGroupID-1];

			// TODO: make a function GroupAddLayer?
			if(Group.m_LayerCount < CEditorMap2::MAX_GROUP_LAYERS)
				Group.m_apLayerIDs[Group.m_LayerCount++] = LayerID;

			// squash layer from previous group
			ParentGroup.m_LayerCount--;
			memmove(&ParentGroup.m_apLayerIDs[0], &ParentGroup.m_apLayerIDs[1],
				sizeof(ParentGroup.m_apLayerIDs[0]) * ParentGroup.m_LayerCount);

			str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: change group"),
				LayerID);
			str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%d > %d",
				ParentGroupID,
				ParentGroupID-1);
			HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
			return ParentGroupID-1;
		}
	}
	// go down one group
	else if(NewPos >= ParentGroup.m_LayerCount)
	{
		if(ParentGroupID < GroupCount-1 && !IsGameLayer)
		{
			CEditorMap2::CGroup& Group = m_Map.m_aGroups[ParentGroupID+1];

			// move other layers down, put this one first
			if(Group.m_LayerCount < CEditorMap2::MAX_GROUP_LAYERS)
			{
				memmove(&Group.m_apLayerIDs[1], &Group.m_apLayerIDs[0],
					sizeof(Group.m_apLayerIDs[0]) * Group.m_LayerCount);
				Group.m_LayerCount++;
				Group.m_apLayerIDs[0] = LayerID;
			}

			// "remove" layer from previous group
			ParentGroup.m_LayerCount--;

			str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: change group"),
				LayerID);
			str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%d > %d",
				ParentGroupID,
				ParentGroupID+1);
			HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
			return ParentGroupID+1;
		}
	}
	else
	{
		tl_swap(ParentGroup.m_apLayerIDs[LayerPos], ParentGroup.m_apLayerIDs[NewPos]);

		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: change order"),
			LayerID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%d > %d",
			LayerPos,
			NewPos);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
		return ParentGroupID;
	}

	// never reached
	return ParentGroupID;
}

void CEditor2::EditTileSelectionFlipX(int LayerID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");

	TileLayerRegionToBrush(LayerID, m_TileSelection.m_StartTX, m_TileSelection.m_StartTY,
		m_TileSelection.m_EndTX, m_TileSelection.m_EndTY);
	BrushFlipX();
	BrushPaintLayer(m_TileSelection.m_StartTX, m_TileSelection.m_StartTY, LayerID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: tile selection"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "Flipped X");
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditTileSelectionFlipY(int LayerID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");

	TileLayerRegionToBrush(LayerID, m_TileSelection.m_StartTX, m_TileSelection.m_StartTY,
		m_TileSelection.m_EndTX, m_TileSelection.m_EndTY);
	BrushFlipY();
	BrushPaintLayer(m_TileSelection.m_StartTX, m_TileSelection.m_StartTY, LayerID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: tile selection"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "Flipped Y");
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditBrushPaintLayer(int PaintTX, int PaintTY, int LayerID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");
	dbg_assert(m_Map.m_aLayers[LayerID].IsTileLayer(), "Layer is not a tile layer");

	BrushPaintLayer(PaintTX, PaintTY, LayerID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: brush paint"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "at (%d, %d)",
		PaintTX, PaintTY);
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditBrushPaintLayerFillRectRepeat(int PaintTX, int PaintTY, int PaintW, int PaintH, int LayerID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");
	dbg_assert(m_Map.m_aLayers[LayerID].IsTileLayer(), "Layer is not a tile layer");

	BrushPaintLayerFillRectRepeat(PaintTX, PaintTY, PaintW, PaintH, LayerID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: brush paint"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "at (%d, %d)(%d, %d)",
		PaintTX, PaintTY, PaintW, PaintH);
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditBrushPaintLayerAutomap(int PaintTX, int PaintTY, int LayerID, int RulesetID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");
	dbg_assert(m_Map.m_aLayers[LayerID].IsTileLayer(), "Layer is not a tile layer");

	BrushPaintLayer(PaintTX, PaintTY, LayerID);

	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];
	dbg_assert(Layer.m_ImageID >= 0 && Layer.m_ImageID < m_Map.m_Assets.m_ImageCount, "ImageID out of bounds or invalid");

	CTilesetMapper2* pMapper = m_Map.AssetsFindTilesetMapper(Layer.m_ImageID);

	dbg_assert(pMapper != 0, "Tileset mapper not found");
	dbg_assert(Layer.m_aTiles.size() == Layer.m_Width*Layer.m_Height, "Tile count does not match layer size");

	pMapper->AutomapLayerSection(Layer.m_aTiles.base_ptr(), PaintTX, PaintTY, m_Brush.m_Width, m_Brush.m_Height, Layer.m_Width, Layer.m_Height, RulesetID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: brush paint auto"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "at (%d, %d)",
		PaintTX, PaintTY);
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditBrushPaintLayerFillRectAutomap(int PaintTX, int PaintTY, int PaintW, int PaintH, int LayerID, int RulesetID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");
	dbg_assert(m_Map.m_aLayers[LayerID].IsTileLayer(), "Layer is not a tile layer");

	BrushPaintLayerFillRectRepeat(PaintTX, PaintTY, PaintW, PaintH, LayerID);

	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];
	dbg_assert(Layer.m_ImageID >= 0 && Layer.m_ImageID < m_Map.m_Assets.m_ImageCount, "ImageID out of bounds or invalid");

	CTilesetMapper2* pMapper = m_Map.AssetsFindTilesetMapper(Layer.m_ImageID);

	dbg_assert(pMapper != 0, "Tileset mapper not found");
	dbg_assert(Layer.m_aTiles.size() == Layer.m_Width*Layer.m_Height, "Tile count does not match layer size");

	pMapper->AutomapLayerSection(Layer.m_aTiles.base_ptr(), PaintTX, PaintTY, PaintW, PaintH, Layer.m_Width, Layer.m_Height, RulesetID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: brush paint auto"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "at (%d, %d)(%d, %d)",
		PaintTX, PaintTY, PaintW, PaintH);
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditTileLayerResize(int LayerID, int NewWidth, int NewHeight)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");
	dbg_assert(m_Map.m_aLayers[LayerID].IsTileLayer(), "Layer is not a tile layer");
	dbg_assert(NewWidth > 0 && NewHeight > 0, "NewWidth, NewHeight invalid");

	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	if(NewWidth == Layer.m_Width && NewHeight == Layer.m_Height)
		return;

	const int OldWidth = Layer.m_Width;
	const int OldHeight = Layer.m_Height;
	array2<CTile>& aTiles = Layer.m_aTiles;

	CTile* pCopyBuff = (CTile*)mem_alloc(OldWidth * OldHeight * sizeof(CTile), 1); // TODO: use ring buffer?

	dbg_assert(aTiles.size() == OldWidth * OldHeight, "Tile count does not match Width*Height");
	memmove(pCopyBuff, aTiles.base_ptr(), sizeof(CTile) * OldWidth * OldHeight);

	aTiles.clear();
	aTiles.add_empty(NewWidth * NewHeight);

	for(int oty = 0; oty < OldHeight; oty++)
	{
		for(int otx = 0; otx < OldWidth; otx++)
		{
			int oid = oty * OldWidth + otx;
			if(oty > NewHeight-1 || otx > NewWidth-1)
				continue;
			aTiles[oty * NewWidth + otx] = pCopyBuff[oid];
		}
	}

	mem_free(pCopyBuff);

	Layer.m_Width = NewWidth;
	Layer.m_Height = NewHeight;

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: resized"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "(%d, %d) > (%d, %d)",
		OldWidth, OldHeight,
		NewWidth, NewHeight);
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditTileLayerAutoMapWhole(int LayerID, int RulesetID)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");

	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	dbg_assert(Layer.m_ImageID >= 0 && Layer.m_ImageID < m_Map.m_Assets.m_ImageCount, "ImageID out of bounds or invalid");

	CTilesetMapper2* pMapper = m_Map.AssetsFindTilesetMapper(Layer.m_ImageID);

	dbg_assert(pMapper != 0, "Tileset mapper not found");
	dbg_assert(Layer.m_aTiles.size() == Layer.m_Width*Layer.m_Height, "Tile count does not match layer size");

	pMapper->AutomapLayerWhole(Layer.m_aTiles.base_ptr(), Layer.m_Width, Layer.m_Height, RulesetID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: tileset automap"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "Ruleset: %s", pMapper->GetRuleSetName(RulesetID));
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditTileLayerAutoMapSection(int LayerID, int RulesetID, int StartTx, int StartTy, int SectionWidth, int SectionHeight)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");

	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	dbg_assert(Layer.m_ImageID >= 0 && Layer.m_ImageID < m_Map.m_Assets.m_ImageCount, "ImageID out of bounds or invalid");

	CTilesetMapper2* pMapper = m_Map.AssetsFindTilesetMapper(Layer.m_ImageID);

	dbg_assert(pMapper != 0, "Tileset mapper not found");
	dbg_assert(Layer.m_aTiles.size() == Layer.m_Width*Layer.m_Height, "Tile count does not match layer size");

	pMapper->AutomapLayerSection(Layer.m_aTiles.base_ptr(), StartTx, StartTy, SectionWidth, SectionHeight, Layer.m_Width, Layer.m_Height, RulesetID);

	char aHistoryEntryAction[64];
	char aHistoryEntryDesc[64];
	str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: tileset automap section"),
		LayerID);
	str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "Ruleset: %s", pMapper->GetRuleSetName(RulesetID));
	HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
}

void CEditor2::EditHistCondLayerChangeName(int LayerID, const char* pNewName, bool HistoryCondition)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");

	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	// names are indentical, stop
	if(str_comp(Layer.m_aName, pNewName) == 0)
		return;

	char aOldName[sizeof(Layer.m_aName)];
	str_copy(aOldName, Layer.m_aName, sizeof(aOldName));
	str_copy(Layer.m_aName, pNewName, sizeof(Layer.m_aName));

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Layer %d: changed name"),
			LayerID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "'%s' -> '%s'", aOldName, pNewName);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondLayerChangeColor(int LayerID, vec4 NewColor, bool HistoryCondition)
{
	dbg_assert(LayerID >= 0 && LayerID < m_Map.m_aLayers.size(), "LayerID out of bounds");

	CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	// colors are indentical, stop
	if(mem_comp(&Layer.m_Color, &NewColor, sizeof(NewColor)) == 0)
		return;

	Layer.m_Color = NewColor;

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("%s: changed layer color"),
			GetLayerName(LayerID));
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc),
			"(%.2f, %.2f, %.2f, %.2f)", NewColor.r, NewColor.g, NewColor.b, NewColor.a);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondGroupChangeName(int GroupID, const char* pNewName, bool HistoryCondition)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];

	// names are indentical, stop
	if(str_comp(Group.m_aName, pNewName) == 0)
		return;

	char aOldName[sizeof(Group.m_aName)];
	str_copy(aOldName, Group.m_aName, sizeof(aOldName));
	str_copy(Group.m_aName, pNewName, sizeof(Group.m_aName));

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: changed name"),
			GroupID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "'%s' -> '%s'", aOldName, pNewName);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondGroupChangeParallax(int GroupID, int NewParallaxX, int NewParallaxY, bool HistoryCondition)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];
	if(NewParallaxX == Group.m_ParallaxX && NewParallaxY == Group.m_ParallaxY)
		return;

	const int OldParallaxX = Group.m_ParallaxX;
	const int OldParallaxY = Group.m_ParallaxY;
	Group.m_ParallaxX = NewParallaxX;
	Group.m_ParallaxY = NewParallaxY;

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: changed parallax"),
			GroupID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "(%d, %d) > (%d, %d)",
			OldParallaxX, OldParallaxY,
			NewParallaxX, NewParallaxY);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondGroupChangeOffset(int GroupID, int NewOffsetX, int NewOffsetY, bool HistoryCondition)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];
	if(NewOffsetX == Group.m_OffsetX && NewOffsetY == Group.m_OffsetY)
		return;

	const int OldOffsetX = Group.m_OffsetX;
	const int OldOffsetY = Group.m_OffsetY;
	Group.m_OffsetX = NewOffsetX;
	Group.m_OffsetY = NewOffsetY;

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: changed offset"),
			GroupID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "(%d, %d) > (%d, %d)",
			OldOffsetX, OldOffsetY,
			NewOffsetX, NewOffsetY);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondGroupChangeClipX(int GroupID, int NewClipX, bool HistoryCondition)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];

	if(NewClipX == Group.m_ClipX)
		return;

	int OldClipX = Group.m_ClipX;
	int OldClipWidth = Group.m_ClipWidth;
	Group.m_ClipWidth += Group.m_ClipX - NewClipX;
	Group.m_ClipX = NewClipX;
	if(Group.m_ClipWidth < 0)
		Group.m_ClipWidth = 0;

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: changed clip left"), GroupID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "(%d, %d) > (%d, %d)",
			OldClipX, OldClipWidth,
			Group.m_ClipY, Group.m_ClipHeight);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondGroupChangeClipY(int GroupID, int NewClipY, bool HistoryCondition)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];

	if(NewClipY == Group.m_ClipY)
		return;

	int OldClipY = Group.m_ClipY;
	int OldClipHeight = Group.m_ClipHeight;
	Group.m_ClipHeight += Group.m_ClipY - NewClipY;
	Group.m_ClipY = NewClipY;
	if(Group.m_ClipHeight < 0)
		Group.m_ClipHeight = 0;

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: changed clip top"),
			GroupID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "(%d, %d) > (%d, %d)",
			OldClipY, OldClipHeight,
			Group.m_ClipY, Group.m_ClipHeight);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondGroupChangeClipRight(int GroupID, int NewClipRight, bool HistoryCondition)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];

	if(NewClipRight - Group.m_ClipX == Group.m_ClipWidth)
		return;

	int OldClipWidth = Group.m_ClipWidth;
	Group.m_ClipWidth = NewClipRight - Group.m_ClipX;
	if(Group.m_ClipWidth < 0)
		Group.m_ClipWidth = 0;

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: changed clip width"), GroupID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%d > %d",
			OldClipWidth, Group.m_ClipWidth);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::EditHistCondGroupChangeClipBottom(int GroupID, int NewClipBottom, bool HistoryCondition)
{
	dbg_assert(GroupID >= 0 && GroupID < m_Map.m_aGroups.size(), "GroupID out of bounds");

	CEditorMap2::CGroup& Group = m_Map.m_aGroups[GroupID];

	if(NewClipBottom - Group.m_ClipY == Group.m_ClipHeight)
		return;

	int OldClipHeight = Group.m_ClipHeight;
	Group.m_ClipHeight = NewClipBottom - Group.m_ClipY;
	if(Group.m_ClipHeight < 0)
		Group.m_ClipHeight = 0;

	if(HistoryCondition)
	{
		char aHistoryEntryAction[64];
		char aHistoryEntryDesc[64];
		str_format(aHistoryEntryAction, sizeof(aHistoryEntryAction), Localize("Group %d: changed clip height"), GroupID);
		str_format(aHistoryEntryDesc, sizeof(aHistoryEntryDesc), "%d > %d",
			OldClipHeight, Group.m_ClipHeight);
		HistoryNewEntry(aHistoryEntryAction, aHistoryEntryDesc);
	}
}

void CEditor2::HistoryClear()
{
	mem_zero(m_aHistoryEntryUsed, sizeof(m_aHistoryEntryUsed));
}

CEditor2::CHistoryEntry *CEditor2::HistoryAllocEntry()
{
	for(int i = 0; i < MAX_HISTORY; i++)
	{
		if(!m_aHistoryEntryUsed[i])
		{
			m_aHistoryEntryUsed[i] = 1;
			m_aHistoryEntries[i] = CHistoryEntry();
			return &m_aHistoryEntries[i];
		}
	}

	// if not found, get the last one
	CHistoryEntry* pCur = m_pHistoryEntryCurrent;
	while(pCur && pCur->m_pPrev)
	{
		pCur = pCur->m_pPrev;
	}

	pCur->m_pNext->m_pPrev = 0x0;
	*pCur = CHistoryEntry();
	return pCur;
}

void CEditor2::HistoryDeallocEntry(CEditor2::CHistoryEntry *pEntry)
{
	int Index = pEntry - m_aHistoryEntries;
	dbg_assert(Index >= 0 && Index < MAX_HISTORY, "History entry out of bounds");
	dbg_assert(pEntry->m_pSnap != 0x0, "Snapshot is null");
	mem_free(pEntry->m_pSnap);
	mem_free(pEntry->m_pUiSnap); // TODO: dealloc smarter, see above
	m_aHistoryEntryUsed[Index] = 0;
}

void CEditor2::HistoryNewEntry(const char* pActionStr, const char* pDescStr)
{
	CHistoryEntry* pEntry;
	pEntry = HistoryAllocEntry();
	pEntry->m_pSnap = m_Map.SaveSnapshot();
	pEntry->m_pUiSnap = SaveUiSnapshot();
	pEntry->SetAction(pActionStr);
	pEntry->SetDescription(pDescStr);

	// delete all the next entries from current
	CHistoryEntry* pCur = m_pHistoryEntryCurrent->m_pNext;
	while(pCur)
	{
		CHistoryEntry* pToDelete = pCur;
		pCur = pCur->m_pNext;
		HistoryDeallocEntry(pToDelete);
	}

	m_pHistoryEntryCurrent->m_pNext = pEntry;
	pEntry->m_pPrev = m_pHistoryEntryCurrent;
	m_pHistoryEntryCurrent = pEntry;
}

void CEditor2::HistoryRestoreToEntry(CHistoryEntry* pEntry)
{
	dbg_assert(pEntry && pEntry->m_pSnap, "History entry or snapshot invalid");
	m_Map.RestoreSnapshot(pEntry->m_pSnap);
	RestoreUiSnapshot(pEntry->m_pUiSnap);
	m_pHistoryEntryCurrent = pEntry;
}

void CEditor2::HistoryUndo()
{
	dbg_assert(m_pHistoryEntryCurrent != 0x0, "Current history entry is null");
	if(m_pHistoryEntryCurrent->m_pPrev)
		HistoryRestoreToEntry(m_pHistoryEntryCurrent->m_pPrev);
}

void CEditor2::HistoryRedo()
{
	dbg_assert(m_pHistoryEntryCurrent != 0x0, "Current history entry is null");
	if(m_pHistoryEntryCurrent->m_pNext)
		HistoryRestoreToEntry(m_pHistoryEntryCurrent->m_pNext);
}

CEditor2::CUISnapshot* CEditor2::SaveUiSnapshot()
{
	CUISnapshot* pUiSnap = (CUISnapshot*)mem_alloc(sizeof(CUISnapshot), 1); // TODO: alloc this smarter (make a growable array to act like a pool)
	pUiSnap->m_SelectedLayerID = m_UiSelectedLayerID;
	pUiSnap->m_SelectedGroupID = m_UiSelectedGroupID;
	pUiSnap->m_ToolID = m_Tool;
	return pUiSnap;
}

void CEditor2::RestoreUiSnapshot(CUISnapshot* pUiSnap)
{
	m_UiSelectedLayerID = pUiSnap->m_SelectedLayerID;
	m_UiSelectedGroupID = pUiSnap->m_SelectedGroupID;
	m_Tool = pUiSnap->m_ToolID;
	BrushClear(); // TODO: save brush?
	m_TileSelection.Deselect(); // TODO: save selection?
}

const char* CEditor2::GetLayerName(int LayerID)
{
	static char aBuff[64];
	const CEditorMap2::CLayer& Layer = m_Map.m_aLayers[LayerID];

	if(Layer.m_aName[0])
	{
		str_format(aBuff, sizeof(aBuff), "%s %d", Layer.m_aName, LayerID);
		return aBuff;
	}

	if(Layer.IsTileLayer())
		str_format(aBuff, sizeof(aBuff), Localize("Tile %d"), LayerID);
	else if(Layer.IsQuadLayer())
		str_format(aBuff, sizeof(aBuff), Localize("Quad %d"), LayerID);
	else
		dbg_break();
	return aBuff;
}

void CEditor2::ConLoad(IConsole::IResult* pResult, void* pUserData)
{
	CEditor2 *pSelf = (CEditor2 *)pUserData;

	const int InputTextLen = str_length(pResult->GetString(0));

	char aMapPath[256];
	bool AddMapPath = str_comp_nocase_num(pResult->GetString(0), "maps/", 5) != 0;
	bool AddMapExtension = InputTextLen <= 4 ||
		str_comp_nocase_num(pResult->GetString(0)+InputTextLen-4, ".map", 4) != 0;
	str_format(aMapPath, sizeof(aMapPath), "%s%s%s", AddMapPath ? "maps/":"", pResult->GetString(0),
			   AddMapExtension ? ".map":"");

	dbg_msg("editor", "ConLoad(%s)", aMapPath);
	pSelf->LoadMap(aMapPath);
}

void CEditor2::ConSave(IConsole::IResult* pResult, void* pUserData)
{
	CEditor2 *pSelf = (CEditor2 *)pUserData;

	const int InputTextLen = str_length(pResult->GetString(0));

	char aMapPath[256];
	bool AddMapPath = str_comp_nocase_num(pResult->GetString(0), "maps/", 5) != 0;
	bool AddMapExtension = InputTextLen <= 4 ||
		str_comp_nocase_num(pResult->GetString(0)+InputTextLen-4, ".map", 4) != 0;
	str_format(aMapPath, sizeof(aMapPath), "%s%s%s", AddMapPath ? "maps/":"", pResult->GetString(0),
			   AddMapExtension ? ".map":"");

	dbg_msg("editor", "ConSave(%s)", aMapPath);
	pSelf->SaveMap(aMapPath);
}

void CEditor2::ConShowPalette(IConsole::IResult* pResult, void* pUserData)
{
	// CEditor2 *pSelf = (CEditor2 *)pUserData;
	dbg_assert(0, "Implement this");
}

void CEditor2::ConGameView(IConsole::IResult* pResult, void* pUserData)
{
	CEditor2 *pSelf = (CEditor2 *)pUserData;
	pSelf->m_ConfigShowGameEntities = (pResult->GetInteger(0) > 0);
	pSelf->m_ConfigShowExtendedTilemaps = pSelf->m_ConfigShowGameEntities;
}

void CEditor2::ConShowGrid(IConsole::IResult* pResult, void* pUserData)
{
	CEditor2 *pSelf = (CEditor2 *)pUserData;
	pSelf->m_ConfigShowGrid = (pResult->GetInteger(0) > 0);
}

void CEditor2::ConUndo(IConsole::IResult* pResult, void* pUserData)
{
	CEditor2 *pSelf = (CEditor2 *)pUserData;
	pSelf->HistoryUndo();
}

void CEditor2::ConRedo(IConsole::IResult* pResult, void* pUserData)
{
	CEditor2 *pSelf = (CEditor2 *)pUserData;
	pSelf->HistoryRedo();
}

void CEditor2::ConDeleteImage(IConsole::IResult* pResult, void* pUserData)
{
	CEditor2 *pSelf = (CEditor2 *)pUserData;
	pSelf->EditDeleteImage(pResult->GetInteger(0));
}

void CEditor2::CTileSelection::FitLayer(const CEditorMap2::CLayer& TileLayer)
{
	dbg_assert(TileLayer.IsTileLayer(), "Layer is not a tile layer");
	// if either start or end point could be inside layer

	CUIRect SelectRect = {
		(float)m_StartTX,
		(float)m_StartTY,
		(float)m_EndTX + 1 - m_StartTX,
		(float)m_EndTY + 1 - m_StartTY
	};

	CUIRect LayerRect = {
		0, 0,
		(float)TileLayer.m_Width, (float)TileLayer.m_Height
	};

	if(DoRectIntersect(SelectRect, LayerRect))
	{
		m_StartTX = clamp(m_StartTX, 0, TileLayer.m_Width-1);
		m_StartTY = clamp(m_StartTY, 0, TileLayer.m_Height-1);
		m_EndTX = clamp(m_EndTX, 0, TileLayer.m_Width-1);
		m_EndTY = clamp(m_EndTY, 0, TileLayer.m_Height-1);
	}
	else
		Deselect();
}
