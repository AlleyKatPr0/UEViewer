#include "Core.h"

#if DUST514

#include "UnMesh3_Dust514_PS3.h"

static inline void SetError(const char** OutError, const char* Message)
{
	if (OutError) *OutError = Message;
}

static inline uint16 ReadU16BE(const byte* P)
{
	return uint16((uint16(P[0]) << 8) | uint16(P[1]));
}

static inline uint32 ReadU32BE(const byte* P)
{
	return (uint32(P[0]) << 24) | (uint32(P[1]) << 16) | (uint32(P[2]) << 8) | uint32(P[3]);
}

static inline int32 SignExtend(uint32 Value, uint32 Bits)
{
	const uint32 Shift = 32u - Bits;
	return int32(Value << Shift) >> Shift;
}

static inline float ClampFloat(float V, float MinV, float MaxV)
{
	if (V < MinV) return MinV;
	if (V > MaxV) return MaxV;
	return V;
}

static inline float DecodeSignedNormal(uint32 Raw, uint32 Bits)
{
	const int32 Signed = SignExtend(Raw, Bits);
	const float MaxPositive = float((1u << (Bits - 1u)) - 1u);
	float V = float(Signed) / MaxPositive;
	return ClampFloat(V, -1.0f, 1.0f);
}

static inline void DecodeX11Y11Z10N(uint32 Packed, float& OutX, float& OutY, float& OutZ)
{
	const uint32 XRaw = (Packed >> 0) & 0x7FFu;
	const uint32 YRaw = (Packed >> 11) & 0x7FFu;
	const uint32 ZRaw = (Packed >> 22) & 0x3FFu;

	OutX = DecodeSignedNormal(XRaw, 11);
	OutY = DecodeSignedNormal(YRaw, 11);
	OutZ = DecodeSignedNormal(ZRaw, 10);
}

static bool ReadBitsHighFirst(const byte* Data, int DataSize, int& BitOffset, int BitCount, uint32& OutValue)
{
	if (BitCount <= 0)
	{
		OutValue = 0;
		return true;
	}

	const int TotalBits = DataSize * 8;
	if (BitOffset + BitCount > TotalBits)
	{
		return false;
	}

	uint32 Value = 0;
	for (int Bit = 0; Bit < BitCount; ++Bit)
	{
		const int AbsoluteBit = BitOffset + Bit;
		const int ByteIndex = AbsoluteBit / 8;
		const int BitInByte = 7 - (AbsoluteBit % 8);
		const uint32 B = (Data[ByteIndex] >> BitInByte) & 1u;
		Value = (Value << 1) | B;
	}

	BitOffset += BitCount;
	OutValue = Value;
	return true;
}

bool DecodeDust514Ps3PackedPositions_X11Y11Z10N(
	const byte* Data,
	int DataSize,
	int VertexCount,
	const FVector& PositionScale,
	const FVector& PositionBias,
	TArray<FVector>& OutPositions,
	const char** OutError
)
{
	if (VertexCount <= 0)
	{
		OutPositions.Empty();
		return true;
	}

	if (!Data)
	{
		SetError(OutError, "null position data");
		return false;
	}

	const int BytesPerVertex = 4;
	const int ExpectedSize = VertexCount * BytesPerVertex;
	if (DataSize < ExpectedSize)
	{
		SetError(OutError, "position data is smaller than expected");
		return false;
	}

	OutPositions.Empty(VertexCount);
	OutPositions.AddUninitialized(VertexCount);

	for (int i = 0; i < VertexCount; i++)
	{
		const byte* P = Data + i * BytesPerVertex;
		const uint32 Packed = ReadU32BE(P);

		float X, Y, Z;
		DecodeX11Y11Z10N(Packed, X, Y, Z);

		FVector& V = OutPositions[i];
		V.X = X * PositionScale.X + PositionBias.X;
		V.Y = Y * PositionScale.Y + PositionBias.Y;
		V.Z = Z * PositionScale.Z + PositionBias.Z;
	}

	return true;
}

bool DecodeDust514Ps3Fixed8SkinRecords(
	const byte* Data,
	int DataSize,
	int VertexCount,
	TArray<FDust514Ps3Fixed8SkinRecord>& OutSkin,
	const char** OutError
)
{
	if (VertexCount <= 0)
	{
		OutSkin.Empty();
		return true;
	}

	if (!Data)
	{
		SetError(OutError, "null skin data");
		return false;
	}

	const int BytesPerRecord = 8;
	const int ExpectedSize = VertexCount * BytesPerRecord;
	if (DataSize < ExpectedSize)
	{
		SetError(OutError, "skin data is smaller than expected");
		return false;
	}

	OutSkin.Empty(VertexCount);
	OutSkin.AddUninitialized(VertexCount);

	for (int i = 0; i < VertexCount; i++)
	{
		const byte* R = Data + i * BytesPerRecord;
		FDust514Ps3Fixed8SkinRecord& S = OutSkin[i];

		const int Sum = int(R[0]) + int(R[1]) + int(R[2]) + int(R[3]);
		const int Den = (Sum > 0) ? Sum : 255;

		int Total = 0;
		int MaxI = 0;
		for (int k = 0; k < 4; k++)
		{
			const int Wi = int(R[k]) * 255 / Den;
			S.Weight[k] = (byte)Wi;
			S.Index[k] = R[4 + k];
			Total += Wi;
			if (S.Weight[k] > S.Weight[MaxI]) MaxI = k;
		}

		// Force sum to 255 by adjusting largest component.
		int Delta = 255 - Total;
		int NewW = int(S.Weight[MaxI]) + Delta;
		if (NewW < 0) NewW = 0;
		if (NewW > 255) NewW = 255;
		S.Weight[MaxI] = (byte)NewW;
	}

	return true;
}

static bool ParseInlineEdgeIndexHeader(
	const byte* Data,
	int DataSize,
	int IndexCount,
	int VertexLimit,
	FDust514Ps3EdgeIndexBlockHeader& OutHeader,
	int& OutDeltaByteOffset,
	const char** OutError
)
{
	OutHeader.BitsPerIndex = 0;
	OutHeader.DeltaOffset = 0;
	OutHeader.SeedCount = 0;
	for (int i = 0; i < 8; ++i) OutHeader.Seeds[i] = 0;
	OutDeltaByteOffset = 0;

	if (IndexCount <= 0)
	{
		return true;
	}

	if (!Data)
	{
		SetError(OutError, "null index data");
		return false;
	}

	if (DataSize < 5)
	{
		SetError(OutError, "index block too small for header");
		return false;
	}

	int Cursor = 0;
	const uint32 BitsPerIndex = Data[Cursor++];
	const uint32 DeltaOffset = ReadU32BE(Data + Cursor);
	Cursor += 4;

	if (BitsPerIndex > 31)
	{
		SetError(OutError, "invalid bits-per-index");
		return false;
	}

	const int SeedCount = (IndexCount < 8) ? IndexCount : 8;
	if (DataSize < Cursor + SeedCount * 2)
	{
		SetError(OutError, "index block too small for seed indices");
		return false;
	}

	for (int i = 0; i < SeedCount; i++)
	{
		const uint32 Seed = ReadU16BE(Data + Cursor);
		Cursor += 2;
		if (Seed >= (uint32)VertexLimit)
		{
			SetError(OutError, "seed index exceeds vertex limit");
			return false;
		}
		OutHeader.Seeds[i] = Seed;
	}

	const int DeltaCount = (IndexCount > 8) ? (IndexCount - 8) : 0;
	const int64 RequiredBits = int64(DeltaCount) * int64(BitsPerIndex);
	const int RequiredBytes = int((RequiredBits + 7) / 8);
	if (DataSize < Cursor + RequiredBytes)
	{
		SetError(OutError, "index block is smaller than expected for deltas");
		return false;
	}

	OutHeader.BitsPerIndex = BitsPerIndex;
	OutHeader.DeltaOffset = DeltaOffset;
	OutHeader.SeedCount = SeedCount;
	OutDeltaByteOffset = Cursor;
	return true;
}

bool DecodeDust514Ps3EdgeIndexPayload(
	const byte* DeltaData,
	int DeltaDataSize,
	int IndexCount,
	int VertexLimit,
	const FDust514Ps3EdgeIndexBlockHeader& Header,
	TArray<uint32>& OutIndices,
	const char** OutError
)
{
	OutIndices.Empty();

	if (IndexCount <= 0)
	{
		return true;
	}

	if (!DeltaData && Header.BitsPerIndex != 0 && IndexCount > 8)
	{
		SetError(OutError, "null delta data");
		return false;
	}

	if (Header.SeedCount <= 0)
	{
		SetError(OutError, "missing seed indices");
		return false;
	}

	if (Header.BitsPerIndex > 31)
	{
		SetError(OutError, "invalid bits-per-index");
		return false;
	}

	OutIndices.Empty(IndexCount);
	OutIndices.AddUninitialized(IndexCount);

	uint32 Previous[8] = { 0,0,0,0,0,0,0,0 };
	int SeedCount = Header.SeedCount;
	if (SeedCount > IndexCount) SeedCount = IndexCount;
	if (SeedCount > 8) SeedCount = 8;
	if (IndexCount > 8 && SeedCount < 8)
	{
		SetError(OutError, "insufficient seed indices for delta decode");
		return false;
	}

	for (int i = 0; i < SeedCount; i++)
	{
		const uint32 Seed = Header.Seeds[i];
		if (Seed >= (uint32)VertexLimit)
		{
			SetError(OutError, "seed index exceeds vertex limit");
			return false;
		}
		OutIndices[i] = Seed;
		Previous[i] = Seed;
	}

	int BitOffset = 0;
	for (int i = 8; i < IndexCount; i++)
	{
		uint32 EncodedDelta = 0;
		if (Header.BitsPerIndex > 0)
		{
			if (!ReadBitsHighFirst(DeltaData, DeltaDataSize, BitOffset, (int)Header.BitsPerIndex, EncodedDelta))
			{
				SetError(OutError, "ran out of bits while reading deltas");
				return false;
			}
		}

		const int32 Delta = int32(EncodedDelta) - int32(Header.DeltaOffset);
		const int32 Restored = int32(Previous[i & 7]) + Delta;
		if (Restored < 0 || Restored >= VertexLimit)
		{
			SetError(OutError, "restored index is outside vertex range");
			return false;
		}

		const uint32 FinalIndex = (uint32)Restored;
		OutIndices[i] = FinalIndex;
		Previous[i & 7] = FinalIndex;
	}

	return true;
}

bool DecodeDust514Ps3EdgeIndexBlock(
	const byte* Data,
	int DataSize,
	int IndexCount,
	int VertexLimit,
	TArray<uint32>& OutIndices,
	const char** OutError
)
{
	OutIndices.Empty();

	if (IndexCount <= 0)
	{
		return true;
	}

	if (!Data)
	{
		SetError(OutError, "null index data");
		return false;
	}

	FDust514Ps3EdgeIndexBlockHeader Header;
	int DeltaOffsetBytes = 0;
	if (ParseInlineEdgeIndexHeader(Data, DataSize, IndexCount, VertexLimit, Header, DeltaOffsetBytes, NULL))
	{
		return DecodeDust514Ps3EdgeIndexPayload(
			Data + DeltaOffsetBytes,
			DataSize - DeltaOffsetBytes,
			IndexCount,
			VertexLimit,
			Header,
			OutIndices,
			OutError
		);
	}

	const int SeedCount = (IndexCount < 8) ? IndexCount : 8;
	const int MinHeaderBytes = 1 + 4 + SeedCount * 2;
	int MaxSearch = DataSize - MinHeaderBytes;
	if (MaxSearch < 0) MaxSearch = 0;
	if (MaxSearch > 64) MaxSearch = 64;
	for (int Offset = 1; Offset <= MaxSearch; ++Offset)
	{
		FDust514Ps3EdgeIndexBlockHeader Candidate;
		int CandidateDeltaOffset = 0;
		if (!ParseInlineEdgeIndexHeader(
			Data + Offset,
			DataSize - Offset,
			IndexCount,
			VertexLimit,
			Candidate,
			CandidateDeltaOffset,
			NULL))
		{
			continue;
		}

		if (DecodeDust514Ps3EdgeIndexPayload(
			(Data + Offset) + CandidateDeltaOffset,
			(DataSize - Offset) - CandidateDeltaOffset,
			IndexCount,
			VertexLimit,
			Candidate,
			OutIndices,
			NULL))
		{
			return true;
		}
	}

	if (!ParseInlineEdgeIndexHeader(Data, DataSize, IndexCount, VertexLimit, Header, DeltaOffsetBytes, OutError))
	{
		return false;
	}

	SetError(OutError, "failed to locate a valid Edge index header");
	return false;
}

#endif // DUST514
