#pragma once

#if DUST514

#include "UnCore.h" // FVector, byte

struct FDust514Ps3Fixed8SkinRecord
{
	byte Weight[4];
	byte Index[4];
};

struct FDust514Ps3EdgeIndexBlockHeader
{
	uint32 BitsPerIndex;
	uint32 DeltaOffset;
	uint32 Seeds[8];
	int SeedCount;
};

bool DecodeDust514Ps3PackedPositions_X11Y11Z10N(
	const byte* Data,
	int DataSize,
	int VertexCount,
	const FVector& PositionScale,
	const FVector& PositionBias,
	TArray<FVector>& OutPositions,
	const char** OutError = NULL
);

bool DecodeDust514Ps3Fixed8SkinRecords(
	const byte* Data,
	int DataSize,
	int VertexCount,
	TArray<FDust514Ps3Fixed8SkinRecord>& OutSkin,
	const char** OutError = NULL
);

bool DecodeDust514Ps3EdgeIndexBlock(
	const byte* Data,
	int DataSize,
	int IndexCount,
	int VertexLimit,
	TArray<uint32>& OutIndices,
	const char** OutError = NULL
);

bool DecodeDust514Ps3EdgeIndexPayload(
	const byte* DeltaData,
	int DeltaDataSize,
	int IndexCount,
	int VertexLimit,
	const FDust514Ps3EdgeIndexBlockHeader& Header,
	TArray<uint32>& OutIndices,
	const char** OutError = NULL
);

#endif // DUST514
