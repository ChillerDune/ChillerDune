#include "duk_entry.h"
#include "duktape_comp.h"
#include <game/client/animstate.h>
#include <game/client/render.h>
#include <engine/external/pnglite/pnglite.h>
#include <engine/storage.h>
#include <base/hash.h>

void CDukEntry::DrawTeeBodyAndFeet(const CTeeDrawBodyAndFeetInfo& TeeDrawInfo)
{
	CAnimState State;
	State.Set(&g_pData->m_aAnimations[ANIM_BASE], 0);
	CTeeRenderInfo RenderInfo = GameClient()->m_aClients[GameClient()->m_LocalClientID].m_RenderInfo;
	RenderInfo.m_Size = TeeDrawInfo.m_Size;
	RenderInfo.m_GotAirJump = TeeDrawInfo.m_GotAirJump;

	vec2 Direction = direction(TeeDrawInfo.m_Angle);
	vec2 Pos = vec2(TeeDrawInfo.m_Pos[0], TeeDrawInfo.m_Pos[1]);
	int Emote = TeeDrawInfo.m_Emote;

	const float WalkTimeMagic = 100.0f;
	float WalkTime =
		((Pos.x >= 0)
			? fmod(Pos.x, WalkTimeMagic)
			: WalkTimeMagic - fmod(-Pos.x, WalkTimeMagic))
		/ WalkTimeMagic;

	if(TeeDrawInfo.m_IsWalking)
		State.Add(&g_pData->m_aAnimations[ANIM_WALK], WalkTime, 1.0f);
	else
	{
		if(TeeDrawInfo.m_IsGrounded)
			State.Add(&g_pData->m_aAnimations[ANIM_IDLE], 0, 1.0f); // TODO: some sort of time here
		else
			State.Add(&g_pData->m_aAnimations[ANIM_INAIR], 0, 1.0f); // TODO: some sort of time here
	}

	RenderTools()->RenderTee(&State, &RenderInfo, Emote, Direction, Pos);
}

void CDukEntry::DrawTeeHand(const CDukEntry::CTeeDrawHand& Hand)
{
	CTeeRenderInfo RenderInfo = GameClient()->m_aClients[GameClient()->m_LocalClientID].m_RenderInfo;
	RenderInfo.m_Size = Hand.m_Size;
	vec2 Pos(Hand.m_Pos[0], Hand.m_Pos[1]);
	vec2 Offset(Hand.m_Offset[0], Hand.m_Offset[1]);
	vec2 Dir = direction(Hand.m_AngleDir);

	RenderTools()->RenderTeeHand(&RenderInfo, Pos, Dir, Hand.m_AngleOff, Offset);
}

void CDukEntry::Init(CDuktape* pDuktape)
{
	m_pGraphics = pDuktape->Graphics();
	m_pRenderTools = pDuktape->RenderTools();
	m_pGameClient = pDuktape->m_pClient;
	m_CurrentDrawSpace = 0;
}

void CDukEntry::Reset()
{
	for(int i = 0 ; i < m_aTextures.size(); i++)
	{
		Graphics()->UnloadTexture(&m_aTextures[i].m_Handle);
	}

	m_aTextures.clear();
}

void CDukEntry::QueueSetColor(const float* pColor)
{
	CRenderCmd Cmd;
	Cmd.m_Type = CRenderCmd::SET_COLOR;
	mem_move(Cmd.m_Color, pColor, sizeof(Cmd.m_Color));
	m_aRenderCmdList[m_CurrentDrawSpace].add(Cmd);
}

void CDukEntry::QueueSetTexture(int TextureID)
{
	CRenderCmd Cmd;
	Cmd.m_Type = CRenderCmd::SET_TEXTURE;
	Cmd.m_TextureID = TextureID;
	m_aRenderCmdList[m_CurrentDrawSpace].add(Cmd);
}

void CDukEntry::QueueSetQuadSubSet(const float* pSubSet)
{
	CRenderCmd Cmd;
	Cmd.m_Type = CRenderCmd::SET_QUAD_SUBSET;
	mem_move(Cmd.m_QuadSubSet, pSubSet, sizeof(Cmd.m_QuadSubSet));
	m_aRenderCmdList[m_CurrentDrawSpace].add(Cmd);
}

void CDukEntry::QueueSetQuadRotation(float Angle)
{
	CRenderCmd Cmd;
	Cmd.m_Type = CRenderCmd::SET_QUAD_ROTATION;
	Cmd.m_QuadRotation = Angle;
	m_aRenderCmdList[m_CurrentDrawSpace].add(Cmd);
}

void CDukEntry::QueueDrawQuad(IGraphics::CQuadItem Quad)
{
	CRenderCmd Cmd;
	Cmd.m_Type = CDukEntry::CRenderCmd::DRAW_QUAD;
	mem_move(Cmd.m_Quad, &Quad, sizeof(Cmd.m_Quad)); // yep, this is because we don't have c++11
	m_aRenderCmdList[m_CurrentDrawSpace].add(Cmd);
}

void CDukEntry::QueueDrawTeeBodyAndFeet(const CTeeDrawBodyAndFeetInfo& TeeDrawInfo)
{
	CRenderCmd Cmd;
	Cmd.m_Type = CDukEntry::CRenderCmd::DRAW_TEE_BODYANDFEET;
	Cmd.m_TeeBodyAndFeet = TeeDrawInfo;
	m_aRenderCmdList[m_CurrentDrawSpace].add(Cmd);
}

void CDukEntry::QueueDrawTeeHand(const CDukEntry::CTeeDrawHand& Hand)
{
	CRenderCmd Cmd;
	Cmd.m_Type = CDukEntry::CRenderCmd::DRAW_TEE_HAND;
	Cmd.m_TeeHand = Hand;
	m_aRenderCmdList[m_CurrentDrawSpace].add(Cmd);
}

void CDukEntry::RenderDrawSpace(DrawSpace::Enum Space)
{
	const int CmdCount = m_aRenderCmdList[Space].size();
	const CRenderCmd* aCmds = m_aRenderCmdList[Space].base_ptr();

	// TODO: merge CRenderSpace and DrawSpace
	CRenderSpace& RenderSpace = m_aRenderSpace[Space];
	float* pWantColor = RenderSpace.m_aWantColor;
	float* pCurrentColor = RenderSpace.m_aCurrentColor;
	float* pWantQuadSubSet = RenderSpace.m_aWantQuadSubSet;
	float* pCurrentQuadSubSet = RenderSpace.m_aCurrentQuadSubSet;

	for(int i = 0; i < CmdCount; i++)
	{
		const CRenderCmd& Cmd = aCmds[i];

		switch(Cmd.m_Type)
		{
			case CRenderCmd::SET_COLOR: {
				mem_move(pWantColor, Cmd.m_Color, sizeof(Cmd.m_Color));
			} break;

			case CRenderCmd::SET_TEXTURE: {
				RenderSpace.m_WantTextureID = Cmd.m_TextureID;
			} break;

			case CRenderCmd::SET_QUAD_SUBSET: {
				mem_move(pWantQuadSubSet, Cmd.m_QuadSubSet, sizeof(Cmd.m_QuadSubSet));
			} break;

			case CRenderCmd::SET_QUAD_ROTATION: {
				RenderSpace.m_WantQuadRotation = Cmd.m_QuadRotation;
			} break;

			case CRenderCmd::DRAW_QUAD: {
				if(RenderSpace.m_WantTextureID != RenderSpace.m_CurrentTextureID)
				{
					if(RenderSpace.m_WantTextureID < 0)
						Graphics()->TextureClear();
					else
						Graphics()->TextureSet(*(IGraphics::CTextureHandle*)&RenderSpace.m_WantTextureID);
					RenderSpace.m_CurrentTextureID = RenderSpace.m_WantTextureID;
				}

				Graphics()->QuadsBegin();

				if(pWantColor[0] != pCurrentColor[0] ||
				   pWantColor[1] != pCurrentColor[1] ||
				   pWantColor[2] != pCurrentColor[2] ||
				   pWantColor[3] != pCurrentColor[3])
				{
					Graphics()->SetColor(pWantColor[0] * pWantColor[3], pWantColor[1] * pWantColor[3], pWantColor[2] * pWantColor[3], pWantColor[3]);
					mem_move(pCurrentColor, pWantColor, sizeof(float)*4);
				}

				if(pWantQuadSubSet[0] != pCurrentQuadSubSet[0] ||
				   pWantQuadSubSet[1] != pCurrentQuadSubSet[1] ||
				   pWantQuadSubSet[2] != pCurrentQuadSubSet[2] ||
				   pWantQuadSubSet[3] != pCurrentQuadSubSet[3])
				{
					Graphics()->QuadsSetSubset(pWantQuadSubSet[0], pWantQuadSubSet[1], pWantQuadSubSet[2], pWantQuadSubSet[3]);
					mem_move(pCurrentQuadSubSet, pWantQuadSubSet, sizeof(float)*4);
				}

				if(RenderSpace.m_WantQuadRotation != RenderSpace.m_CurrentQuadRotation)
				{
					Graphics()->QuadsSetRotation(RenderSpace.m_WantQuadRotation);
					RenderSpace.m_CurrentQuadRotation = RenderSpace.m_WantQuadRotation;
				}

				Graphics()->QuadsDrawTL((IGraphics::CQuadItem*)&Cmd.m_Quad, 1);
				Graphics()->QuadsEnd();
			} break;

			case CRenderCmd::DRAW_TEE_BODYANDFEET:
				DrawTeeBodyAndFeet(Cmd.m_TeeBodyAndFeet);
				break;

			case CRenderCmd::DRAW_TEE_HAND:
				DrawTeeHand(Cmd.m_TeeHand);
				break;

			default:
				dbg_assert(0, "Render command type not handled");
		}
	}

	m_aRenderCmdList[Space].clear();
	RenderSpace = CRenderSpace();
}

bool CDukEntry::LoadTexture(const char *pTexturePath, const char* pTextureName)
{
	IGraphics::CTextureHandle Handle = Graphics()->LoadTexture(pTexturePath, IStorage::TYPE_SAVE, CImageInfo::FORMAT_AUTO, 0);
	uint32_t Hash = hash_fnv1a(pTextureName, str_length(pTextureName));
	CTextureHashPair Pair = { Hash, Handle };
	m_aTextures.add(Pair);
	return Handle.IsValid();
}

IGraphics::CTextureHandle CDukEntry::GetTexture(const char *pTextureName)
{
	const uint32_t SearchHash = hash_fnv1a(pTextureName, str_length(pTextureName));

	const CTextureHashPair* Pairs = m_aTextures.base_ptr();
	const int PairCount = m_aTextures.size();

	for(int i = 0; i < PairCount; i++)
	{
		if(Pairs[i].m_Hash == SearchHash)
			return Pairs[i].m_Handle;
	}

	return IGraphics::CTextureHandle();
}
