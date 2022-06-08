
#pragma once
#include "types.h"
#include "config.h"

#define NUM_META_BLK		(7)
#define NUM_USER_BLK		(PBLK_PER_DIE - NUM_META_BLK)

#define BASE_META_BLK		(NUM_USER_BLK)

#define LPN_PER_USER_BLK	(CHUNK_PER_PBLK)

#define NUM_LPN				((NUM_USER_BLK - 12) * (LPN_PER_USER_BLK))

#define SIZE_REQ_QUE		(16)
#define INV_BN				(0xFF)
#define INV_LPN				(0xFFFF)
#define INV_PPO				(0xFF)
#define MARK_ERS			(0xFFFFFFFF)

#define NUM_DATA_PAGE		(NUM_WL)


enum Cmd
{
	CMD_WRITE,
	CMD_READ,
	CMD_SHUTDOWN,
	NUM_CMD,
};

enum ShutdownOpt
{
	SD_Fast,	///< running IO만 처리. 
	SD_Safe,	///< Meta data저장. (user scan불필요)
	SD_Full,	///< Full meta data저장 (Jnl replay불필요)
};

struct ReqInfo
{
	Cmd eCmd;
	union
	{
		struct
		{
			uint32 nLPN;
			uint16 nBuf;
			uint32 nSeqNo;
		};
		struct
		{
			ShutdownOpt eOpt;
		};
	};
};

typedef void (*CbfReq)(ReqInfo* pReq);

uint32 FTL_GetNumLPN(CbfReq pfCbf);
void FTL_Request(ReqInfo* pReq);

void FTL_Main(void* pParam);
