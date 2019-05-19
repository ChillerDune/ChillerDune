#pragma once
#include <stdint.h>
#include <base/tl/array.h>
#include <engine/graphics.h>
#include <game/duck_collision.h>
#include <generated/protocol.h>

// Bridge between teeworlds and duktape

class CDuktape;
class CRenderTools;
class CGameClient;
class CCharacterCore;

struct CDukBridge
{
	CDuktape* m_pDuktape;
	IGraphics* m_pGraphics;
	CRenderTools* m_pRenderTools;
	CGameClient* m_pGameClient;
	inline CDuktape* Duktape() { return m_pDuktape; }
	inline IGraphics* Graphics() { return m_pGraphics; }
	inline CRenderTools* RenderTools() { return m_pRenderTools; }
	inline CGameClient* GameClient() { return m_pGameClient; }

	struct DrawSpace
	{
		enum Enum
		{
			GAME=0,
			_COUNT
		};
	};

	struct CTeeDrawBodyAndFeetInfo
	{
		float m_Size;
		float m_Angle;
		float m_Pos[2]; // vec2
		bool m_IsWalking;
		bool m_IsGrounded;
		bool m_GotAirJump;
		int m_Emote;
	};

	struct CTeeDrawHand
	{
		float m_Size;
		float m_AngleDir;
		float m_AngleOff;
		float m_Pos[2]; // vec2
		float m_Offset[2]; // vec2
	};

	struct CTeeSkinInfo
	{
		int m_aTextures[NUM_SKINPARTS];
		float m_aColors[NUM_SKINPARTS][4];
	};

	struct CRenderCmd
	{
		enum TypeEnum
		{
			SET_COLOR=0,
			SET_TEXTURE,
			SET_QUAD_SUBSET,
			SET_QUAD_ROTATION,
			SET_TEE_SKIN,
			DRAW_QUAD,
			DRAW_QUAD_CENTERED,
			DRAW_TEE_BODYANDFEET,
			DRAW_TEE_HAND,
		};

		int m_Type;

		union
		{
			float m_Color[4];
			float m_Quad[4]; // POD IGraphics::CQuadItem
			int m_TextureID;
			float m_QuadSubSet[4];
			float m_QuadRotation;

			// TODO: this is kinda big...
			CTeeDrawBodyAndFeetInfo m_TeeBodyAndFeet;
			CTeeDrawHand m_TeeHand;
			CTeeSkinInfo m_TeeSkinInfo;
		};
	};

	struct CRenderSpace
	{
		float m_aWantColor[4];
		float m_aCurrentColor[4];
		float m_aWantQuadSubSet[4];
		float m_aCurrentQuadSubSet[4];
		int m_WantTextureID;
		int m_CurrentTextureID;
		float m_WantQuadRotation;
		float m_CurrentQuadRotation;
		CTeeSkinInfo m_CurrentTeeSkin;

		CRenderSpace()
		{
			mem_zero(m_aWantColor, sizeof(m_aWantColor));
			mem_zero(m_aCurrentColor, sizeof(m_aCurrentColor));
			mem_zero(m_aWantQuadSubSet, sizeof(m_aWantQuadSubSet));
			mem_zero(m_aCurrentQuadSubSet, sizeof(m_aCurrentQuadSubSet));
			m_WantTextureID = -1; // clear by default
			m_CurrentTextureID = 0;
			m_WantQuadRotation = 0; // clear by default
			m_CurrentQuadRotation = -1;

			for(int i = 0; i < NUM_SKINPARTS; i++)
			{
				m_CurrentTeeSkin.m_aTextures[i] = -1;
				m_CurrentTeeSkin.m_aColors[i][0] = 1.0f;
				m_CurrentTeeSkin.m_aColors[i][1] = 1.0f;
				m_CurrentTeeSkin.m_aColors[i][2] = 1.0f;
				m_CurrentTeeSkin.m_aColors[i][3] = 1.0f;
			}
		}
	};

	int m_CurrentDrawSpace;
	array<CRenderCmd> m_aRenderCmdList[DrawSpace::_COUNT];
	CRenderSpace m_aRenderSpace[DrawSpace::_COUNT];

	struct CTextureHashPair
	{
		uint32_t m_Hash;
		IGraphics::CTextureHandle m_Handle;
	};

	array<CTextureHashPair> m_aTextures;

	CDuckCollision m_Collision;

	void DrawTeeBodyAndFeet(const CTeeDrawBodyAndFeetInfo& TeeDrawInfo, const CTeeSkinInfo& SkinInfo);
	void DrawTeeHand(const CTeeDrawHand& Hand, const CTeeSkinInfo& SkinInfo);

	void Init(CDuktape* pDuktape);
	void Reset();

	void QueueSetColor(const float* pColor);
	void QueueSetTexture(int TextureID);
	void QueueSetQuadSubSet(const float* pSubSet);
	void QueueSetQuadRotation(float Angle);
	void QueueSetTeeSkin(const CTeeSkinInfo& SkinInfo);
	void QueueDrawQuad(IGraphics::CQuadItem Quad);
	void QueueDrawQuadCentered(IGraphics::CQuadItem Quad);
	void QueueDrawTeeBodyAndFeet(const CTeeDrawBodyAndFeetInfo& TeeDrawInfo);
	void QueueDrawTeeHand(const CTeeDrawHand& Hand);

	bool LoadTexture(const char* pTexturePath, const char *pTextureName);
	IGraphics::CTextureHandle GetTexture(const char* pTextureName);

	void SetSolidBlock(int BlockId, const CDuckCollision::CStaticBlock& Block);
	void ClearSolidBlock(int BlockId);



	// "entries"
	void RenderDrawSpace(DrawSpace::Enum Space);
	void CharacterCorePreTick(CCharacterCore** apCharCores);
	void CharacterCorePostTick(CCharacterCore** apCharCores);
};
