
#pragma once

// PPG : Physical Page.
// BPG : Big Page.
// PBLK : Physical Block.
// BBLK : Big Block.
// CHUNK : Map/Memory unit.

#define BYTE_PER_CHUNK		(512)
#define BYTE_PER_PPG		(BYTE_PER_CHUNK)
#define BYTE_PER_SPARE		(8)		///< Chunk 당 spare크기.

#define NUM_PLN				(1)
#define NUM_WL				(32)
#define PBLK_PER_DIE		(32)
#define NUM_DIE				(1)

#define CHUNK_PER_PPG		(BYTE_PER_PPG / BYTE_PER_CHUNK)
#define CHUNK_PER_BPG		(CHUNK_PER_PPG * NUM_PLN)
#define BBLK_PER_DIE		(PBLK_PER_DIE / NUM_PLN)
#define CHUNK_PER_PBLK		(CHUNK_PER_PPG * NUM_WL)

//////////////////////

