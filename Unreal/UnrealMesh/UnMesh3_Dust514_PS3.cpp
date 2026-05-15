#include "UnMesh3_Dust514_PS3.h"

#include <string.h>

namespace Dust514Ps3
{
	static void SetError(FDecodeError* Error, const std::string& Message)
	{
		if (Error)
		{
			Error->Message = Message;
		}
	}

	static uint16_t ReadU16BE(const uint8_t* P)
	{
		return uint16_t((uint16_t(P[0]) << 8) | uint16_t(P[1]));
	}

	static uint32_t ReadU32BE(const uint8_t* P)
	{
		return
			(uint32_t(P[0]) << 24) |
			(uint32_t(P[1]) << 16) |
			(uint32_t(P[2]) << 8) |
			uint32_t(P[3]);
	}

	static uint32_t ReadU32LE(const uint8_t* P)
	{
		return
			(uint32_t(P[3]) << 24) |
			(uint32_t(P[2]) << 16) |
			(uint32_t(P[1]) << 8) |
			uint32_t(P[0]);
	}

	static int32_t SignExtend(uint32_t Value, uint32_t Bits)
	{
		const uint32_t Shift = 32u - Bits;
		return int32_t(Value << Shift) >> Shift;
	}

	static float DecodeSignedNormal(uint32_t Raw, uint32_t Bits)
	{
		const int32_t Signed = SignExtend(Raw, Bits);
		const float MaxPositive = float((1u << (Bits - 1u)) - 1u);
		const float V = float(Signed) / MaxPositive;
		return std::max(-1.0f, std::min(1.0f, V));
	}

	static FVector3 DecodeX11Y11Z10N(uint32_t Packed)
	{
		// Edge names this X11Y11Z10N, with:
		// X = bits 0..10
		// Y = bits 11..21
		// Z = bits 22..31
		//
		// This mirrors the uploaded Edge vertex conversion code.
		const uint32_t XRaw = (Packed >> 0) & 0x7FFu;
		const uint32_t YRaw = (Packed >> 11) & 0x7FFu;
		const uint32_t ZRaw = (Packed >> 22) & 0x3FFu;

		FVector3 V;
		V.X = DecodeSignedNormal(XRaw, 11);
		V.Y = DecodeSignedNormal(YRaw, 11);
		V.Z = DecodeSignedNormal(ZRaw, 10);
		return V;
	}

	static float Determinant3x3(const float B[3][3])
	{
		return
			B[0][0] * (B[1][1] * B[2][2] - B[1][2] * B[2][1]) -
			B[0][1] * (B[1][0] * B[2][2] - B[1][2] * B[2][0]) +
			B[0][2] * (B[1][0] * B[2][1] - B[1][1] * B[2][0]);
	}

	static void GetBasis(EBasisKind BasisKind, float Out[3][3])
	{
		if (BasisKind == EBasisKind::CharacterAssault)
		{
			// CH_1P_CA_Assault_SKM
			Out[0][0] = 0.0f; Out[0][1] = 1.0f; Out[0][2] = 0.0f;
			Out[1][0] = 0.0f; Out[1][1] = 0.0f; Out[1][2] = 1.0f;
			Out[2][0] = 1.0f; Out[2][1] = 0.0f; Out[2][2] = 0.0f;
			return;
		}

		// Default weapon-like basis:
		// X -> X, Y -> -Z, Z -> Y
		Out[0][0] = 1.0f; Out[0][1] = 0.0f;  Out[0][2] = 0.0f;
		Out[1][0] = 0.0f; Out[1][1] = 0.0f;  Out[1][2] = 1.0f;
		Out[2][0] = 0.0f; Out[2][1] = -1.0f; Out[2][2] = 0.0f;
	}

	static void QuatToMat3(const FQuat& Q, float M[3][3])
	{
		const float X = Q.X;
		const float Y = Q.Y;
		const float Z = Q.Z;
		const float W = Q.W;

		const float XX = X * X;
		const float YY = Y * Y;
		const float ZZ = Z * Z;
		const float XY = X * Y;
		const float XZ = X * Z;
		const float YZ = Y * Z;
		const float WX = W * X;
		const float WY = W * Y;
		const float WZ = W * Z;

		M[0][0] = 1.0f - 2.0f * (YY + ZZ);
		M[0][1] = 2.0f * (XY - WZ);
		M[0][2] = 2.0f * (XZ + WY);

		M[1][0] = 2.0f * (XY + WZ);
		M[1][1] = 1.0f - 2.0f * (XX + ZZ);
		M[1][2] = 2.0f * (YZ - WX);

		M[2][0] = 2.0f * (XZ - WY);
		M[2][1] = 2.0f * (YZ + WX);
		M[2][2] = 1.0f - 2.0f * (XX + YY);
	}

	static void Mat3Mul(const float A[3][3], const float B[3][3], float Out[3][3])
	{
		float R[3][3];
		for (int Row = 0; Row < 3; ++Row)
		{
			for (int Col = 0; Col < 3; ++Col)
			{
				R[Row][Col] =
					A[Row][0] * B[0][Col] +
					A[Row][1] * B[1][Col] +
					A[Row][2] * B[2][Col];
			}
		}
		memcpy(Out, R, sizeof(R));
	}

	static void Mat3Transpose(const float M[3][3], float Out[3][3])
	{
		for (int Row = 0; Row < 3; ++Row)
		{
			for (int Col = 0; Col < 3; ++Col)
			{
				Out[Row][Col] = M[Col][Row];
			}
		}
	}

	static FQuat Mat3ToQuat(const float M[3][3])
	{
		FQuat Q;
		const float Trace = M[0][0] + M[1][1] + M[2][2];

		if (Trace > 0.0f)
		{
			const float S = std::sqrt(Trace + 1.0f) * 2.0f;
			Q.W = 0.25f * S;
			Q.X = (M[2][1] - M[1][2]) / S;
			Q.Y = (M[0][2] - M[2][0]) / S;
			Q.Z = (M[1][0] - M[0][1]) / S;
		}
		else if (M[0][0] > M[1][1] && M[0][0] > M[2][2])
		{
			const float S = std::sqrt(std::max(0.0f, 1.0f + M[0][0] - M[1][1] - M[2][2])) * 2.0f;
			Q.W = (M[2][1] - M[1][2]) / S;
			Q.X = 0.25f * S;
			Q.Y = (M[0][1] + M[1][0]) / S;
			Q.Z = (M[0][2] + M[2][0]) / S;
		}
		else if (M[1][1] > M[2][2])
		{
			const float S = std::sqrt(std::max(0.0f, 1.0f + M[1][1] - M[0][0] - M[2][2])) * 2.0f;
			Q.W = (M[0][2] - M[2][0]) / S;
			Q.X = (M[0][1] + M[1][0]) / S;
			Q.Y = 0.25f * S;
			Q.Z = (M[1][2] + M[2][1]) / S;
		}
		else
		{
			const float S = std::sqrt(std::max(0.0f, 1.0f + M[2][2] - M[0][0] - M[1][1])) * 2.0f;
			Q.W = (M[1][0] - M[0][1]) / S;
			Q.X = (M[0][2] + M[2][0]) / S;
			Q.Y = (M[1][2] + M[2][1]) / S;
			Q.Z = 0.25f * S;
		}

		const float Len = std::sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
		if (Len > 0.000001f)
		{
			Q.X /= Len;
			Q.Y /= Len;
			Q.Z /= Len;
			Q.W /= Len;
		}
		else
		{
			Q.X = Q.Y = Q.Z = 0.0f;
			Q.W = 1.0f;
		}

		return Q;
	}

	static bool ReadBitsHighFirst(
		const uint8_t* Data,
		size_t DataSize,
		size_t& BitOffset,
		uint32_t BitCount,
		uint32_t& OutValue
	)
	{
		if (BitCount == 0)
		{
			OutValue = 0;
			return true;
		}

		if (BitOffset + BitCount > DataSize * 8)
		{
			return false;
		}

		uint32_t Value = 0;
		for (uint32_t Bit = 0; Bit < BitCount; ++Bit)
		{
			const size_t AbsoluteBit = BitOffset + Bit;
			const size_t ByteIndex = AbsoluteBit / 8;
			const uint32_t BitInByte = 7u - uint32_t(AbsoluteBit % 8);
			const uint32_t B = (Data[ByteIndex] >> BitInByte) & 1u;
			Value = (Value << 1) | B;
		}

		BitOffset += BitCount;
		OutValue = Value;
		return true;
	}

	FVector3 FDust514Ps3SkeletalMeshDecoder::ApplyBasis(EBasisKind BasisKind, const FVector3& V)
	{
		float B[3][3];
		GetBasis(BasisKind, B);

		FVector3 Out;
		Out.X = B[0][0] * V.X + B[0][1] * V.Y + B[0][2] * V.Z;
		Out.Y = B[1][0] * V.X + B[1][1] * V.Y + B[1][2] * V.Z;
		Out.Z = B[2][0] * V.X + B[2][1] * V.Y + B[2][2] * V.Z;
		return Out;
	}

	FQuat FDust514Ps3SkeletalMeshDecoder::ApplyBasisToQuat(EBasisKind BasisKind, const FQuat& Q)
	{
		float B[3][3];
		GetBasis(BasisKind, B);

		float Binv[3][3];
		Mat3Transpose(B, Binv);

		float M[3][3];
		QuatToMat3(Q, M);

		float BM[3][3];
		float Result[3][3];

		Mat3Mul(B, M, BM);
		Mat3Mul(BM, Binv, Result);

		return Mat3ToQuat(Result);
	}

	bool FDust514Ps3SkeletalMeshDecoder::DecodePackedPositions(
		const uint8_t* Data,
		size_t DataSize,
		uint32_t VertexCount,
		const FDecodeOptions& Options,
		std::vector<FVector3>& OutPositions,
		FDecodeError* OutError
	)
	{
		if (!Data && VertexCount > 0)
		{
			SetError(OutError, "DecodePackedPositions: null position data.");
			return false;
		}

		const size_t BytesPerVertex = Options.bPositionsAreX11Y11Z10N ? 4u : 12u;
		if (DataSize < size_t(VertexCount) * BytesPerVertex)
		{
			SetError(OutError, "DecodePackedPositions: position data is smaller than expected.");
			return false;
		}

		OutPositions.clear();
		OutPositions.reserve(VertexCount);

		for (uint32_t VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const uint8_t* P = Data + size_t(VertexIndex) * BytesPerVertex;

			FVector3 Position;

			if (Options.bPositionsAreX11Y11Z10N)
			{
				const uint32_t Packed = Options.bPositionsAreBigEndian ? ReadU32BE(P) : ReadU32LE(P);
				Position = DecodeX11Y11Z10N(Packed);
			}
			else
			{
				uint32_t XI = Options.bPositionsAreBigEndian ? ReadU32BE(P + 0) : ReadU32LE(P + 0);
				uint32_t YI = Options.bPositionsAreBigEndian ? ReadU32BE(P + 4) : ReadU32LE(P + 4);
				uint32_t ZI = Options.bPositionsAreBigEndian ? ReadU32BE(P + 8) : ReadU32LE(P + 8);

				float X, Y, Z;
				memcpy(&X, &XI, sizeof(float));
				memcpy(&Y, &YI, sizeof(float));
				memcpy(&Z, &ZI, sizeof(float));

				Position.X = X;
				Position.Y = Y;
				Position.Z = Z;
			}

			Position.X = Position.X * Options.PositionScale.X + Options.PositionBias.X;
			Position.Y = Position.Y * Options.PositionScale.Y + Options.PositionBias.Y;
			Position.Z = Position.Z * Options.PositionScale.Z + Options.PositionBias.Z;

			if (Options.bApplyBasisTransform)
			{
				Position = ApplyBasis(Options.BasisKind, Position);
			}

			OutPositions.push_back(Position);
		}

		return true;
	}

	bool FDust514Ps3SkeletalMeshDecoder::DecodeFixed8SkinRecords(
		const uint8_t* Data,
		size_t DataSize,
		uint32_t VertexCount,
		const std::vector<uint16_t>& BonePalette,
		std::vector<FSkinInfluence4>& OutSkin,
		FDecodeError* OutError
	)
	{
		if (!Data && VertexCount > 0)
		{
			SetError(OutError, "DecodeFixed8SkinRecords: null skin data.");
			return false;
		}

		const size_t BytesPerRecord = 8u;
		if (DataSize < size_t(VertexCount) * BytesPerRecord)
		{
			SetError(OutError, "DecodeFixed8SkinRecords: skin data is smaller than expected.");
			return false;
		}

		OutSkin.clear();
		OutSkin.reserve(VertexCount);

		for (uint32_t VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const uint8_t* R = Data + size_t(VertexIndex) * BytesPerRecord;

			const uint8_t W[4] = { R[0], R[1], R[2], R[3] };
			const uint8_t I[4] = { R[4], R[5], R[6], R[7] };

			float Sum = float(uint32_t(W[0]) + uint32_t(W[1]) + uint32_t(W[2]) + uint32_t(W[3]));
			if (Sum <= 0.000001f)
			{
				Sum = 255.0f;
			}

			FSkinInfluence4 Skin;

			for (int InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
			{
				const uint8_t LocalBone = I[InfluenceIndex];

				if (!BonePalette.empty())
				{
					if (LocalBone >= BonePalette.size())
					{
						SetError(OutError, "DecodeFixed8SkinRecords: local bone index exceeds page bone palette.");
						return false;
					}
					Skin.BoneIndex[InfluenceIndex] = BonePalette[LocalBone];
				}
				else
				{
					Skin.BoneIndex[InfluenceIndex] = uint16_t(LocalBone);
				}

				Skin.Weight[InfluenceIndex] = float(W[InfluenceIndex]) / Sum;
			}

			OutSkin.push_back(Skin);
		}

		return true;
	}

	static bool ParseInlineEdgeIndexHeader(
		const uint8_t* Data,
		size_t DataSize,
		uint32_t IndexCount,
		uint32_t VertexLimit,
		FEdgeIndexBlockHeader& OutHeader,
		size_t& OutDeltaByteOffset,
		FDecodeError* OutError
	)
	{
		OutHeader = {};
		OutDeltaByteOffset = 0;

		if (IndexCount == 0)
		{
			return true;
		}

		const uint32_t SeedCount = std::min<uint32_t>(IndexCount, 8u);

		if (!Data)
		{
			SetError(OutError, "DecodeEdgeIndexBlock: null index data.");
			return false;
		}

		if (DataSize < 1u + 4u)
		{
			SetError(OutError, "DecodeEdgeIndexBlock: index block too small for header.");
			return false;
		}

		uint32_t Cursor = 0;
		const uint32_t BitsPerIndex = Data[Cursor++];
		const uint32_t DeltaOffset = ReadU32BE(Data + Cursor);
		Cursor += 4;

		if (BitsPerIndex > 31u)
		{
			SetError(OutError, "DecodeEdgeIndexBlock: invalid bits-per-index.");
			return false;
		}

		const size_t SeedBytes = size_t(SeedCount) * 2u;
		if (DataSize < size_t(Cursor) + SeedBytes)
		{
			SetError(OutError, "DecodeEdgeIndexBlock: index block too small for seed indices.");
			return false;
		}

		for (uint32_t i = 0; i < SeedCount; ++i)
		{
			const uint32_t Seed = ReadU16BE(Data + Cursor);
			Cursor += 2;

			if (Seed >= VertexLimit)
			{
				SetError(OutError, "DecodeEdgeIndexBlock: seed index exceeds vertex limit.");
				return false;
			}

			OutHeader.Seeds[i] = Seed;
		}

		const uint32_t DeltaCount = (IndexCount > 8u) ? (IndexCount - 8u) : 0u;
		const size_t RequiredBits = size_t(DeltaCount) * size_t(BitsPerIndex);
		const size_t RequiredBytes = (RequiredBits + 7u) / 8u;

		if (DataSize < size_t(Cursor) + RequiredBytes)
		{
			SetError(OutError, "DecodeEdgeIndexBlock: index block is smaller than expected for deltas.");
			return false;
		}

		OutHeader.BitsPerIndex = BitsPerIndex;
		OutHeader.DeltaOffset = DeltaOffset;
		OutHeader.SeedCount = SeedCount;
		OutDeltaByteOffset = size_t(Cursor);
		return true;
	}

	bool FDust514Ps3SkeletalMeshDecoder::DecodeEdgeIndexPayload(
		const uint8_t* DeltaData,
		size_t DeltaDataSize,
		uint32_t IndexCount,
		uint32_t VertexLimit,
		const FEdgeIndexBlockHeader& Header,
		std::vector<uint32_t>& OutIndices,
		FDecodeError* OutError
	)
	{
		OutIndices.clear();

		if (IndexCount == 0)
		{
			return true;
		}

		if (!DeltaData && Header.BitsPerIndex != 0 && IndexCount > 8u)
		{
			SetError(OutError, "DecodeEdgeIndexPayload: null delta data.");
			return false;
		}

		if (Header.SeedCount == 0)
		{
			SetError(OutError, "DecodeEdgeIndexPayload: missing seed indices.");
			return false;
		}

		if (Header.BitsPerIndex > 31u)
		{
			SetError(OutError, "DecodeEdgeIndexPayload: invalid bits-per-index.");
			return false;
		}

		OutIndices.resize(IndexCount);

		uint32_t Previous[8] = { 0,0,0,0,0,0,0,0 };

		const uint32_t SeedCount = std::min<uint32_t>(Header.SeedCount, std::min<uint32_t>(IndexCount, 8u));
		if (IndexCount > 8u && SeedCount < 8u)
		{
			SetError(OutError, "DecodeEdgeIndexPayload: insufficient seed indices for delta decode.");
			return false;
		}
		for (uint32_t i = 0; i < SeedCount; ++i)
		{
			const uint32_t Seed = Header.Seeds[i];

			if (Seed >= VertexLimit)
			{
				SetError(OutError, "DecodeEdgeIndexPayload: seed index exceeds vertex limit.");
				return false;
			}

			OutIndices[i] = Seed;
			Previous[i] = Seed;
		}

		size_t BitOffset = 0;

		for (uint32_t i = 8; i < IndexCount; ++i)
		{
			uint32_t EncodedDelta = 0;

			if (Header.BitsPerIndex == 0)
			{
				EncodedDelta = 0;
			}
			else if (!ReadBitsHighFirst(DeltaData, DeltaDataSize, BitOffset, Header.BitsPerIndex, EncodedDelta))
			{
				SetError(OutError, "DecodeEdgeIndexPayload: ran out of bits while reading deltas.");
				return false;
			}

			const int32_t Delta = int32_t(EncodedDelta) - int32_t(Header.DeltaOffset);
			const int32_t Restored = int32_t(Previous[i % 8u]) + Delta;

			if (Restored < 0 || uint32_t(Restored) >= VertexLimit)
			{
				SetError(OutError, "DecodeEdgeIndexPayload: restored index is outside vertex range.");
				return false;
			}

			const uint32_t FinalIndex = uint32_t(Restored);
			OutIndices[i] = FinalIndex;
			Previous[i % 8u] = FinalIndex;
		}

		return true;
	}

	bool FDust514Ps3SkeletalMeshDecoder::DecodeEdgeIndexBlock(
		const uint8_t* Data,
		size_t DataSize,
		uint32_t IndexCount,
		uint32_t VertexLimit,
		std::vector<uint32_t>& OutIndices,
		FDecodeError* OutError
	)
	{
		OutIndices.clear();

		if (IndexCount == 0)
		{
			return true;
		}

		if (!Data)
		{
			SetError(OutError, "DecodeEdgeIndexBlock: null index data.");
			return false;
		}

		// Decoder format used here:
		//
		// byte 0      : bits per index
		// bytes 1..4  : big-endian delta offset
		// next 16     : first eight seed indices, big-endian uint16
		// remaining   : bit-packed deltas, high-order first
		//
		// This is intentionally isolated. If DUST's wrapper uses an outer table/header,
		// pass this function the inner Edge delta block. If the wrapper prefixes
		// the inner block with page metadata, the decoder will attempt to locate
		// a valid inner header within the first 64 bytes.

		FEdgeIndexBlockHeader Header = {};
		size_t DeltaOffsetBytes = 0;

		{
			FDecodeError LocalError;
			if (ParseInlineEdgeIndexHeader(Data, DataSize, IndexCount, VertexLimit, Header, DeltaOffsetBytes, &LocalError))
			{
				return DecodeEdgeIndexPayload(
					Data + DeltaOffsetBytes,
					DataSize - DeltaOffsetBytes,
					IndexCount,
					VertexLimit,
					Header,
					OutIndices,
					OutError
				);
			}
		}

		const uint32_t SeedCount = std::min<uint32_t>(IndexCount, 8u);
		const size_t MinHeaderBytes = 1u + 4u + size_t(SeedCount) * 2u;
		const size_t MaxSearch = std::min<size_t>(64u, (DataSize > MinHeaderBytes) ? (DataSize - MinHeaderBytes) : 0u);

		for (size_t Offset = 1; Offset <= MaxSearch; ++Offset)
		{
			FDecodeError LocalError;
			FEdgeIndexBlockHeader Candidate = {};
			size_t CandidateDeltaOffsetBytes = 0;

			if (!ParseInlineEdgeIndexHeader(
				Data + Offset,
				DataSize - Offset,
				IndexCount,
				VertexLimit,
				Candidate,
				CandidateDeltaOffsetBytes,
				&LocalError))
			{
				continue;
			}

			if (DecodeEdgeIndexPayload(
				(Data + Offset) + CandidateDeltaOffsetBytes,
				(DataSize - Offset) - CandidateDeltaOffsetBytes,
				IndexCount,
				VertexLimit,
				Candidate,
				OutIndices,
				nullptr))
			{
				return true;
			}
		}

		// No valid header was found; try again at offset 0 to provide the original error message.
		FDecodeError LocalError;
		(void)ParseInlineEdgeIndexHeader(Data, DataSize, IndexCount, VertexLimit, Header, DeltaOffsetBytes, &LocalError);
		SetError(OutError, LocalError.Message.empty() ? "DecodeEdgeIndexBlock: failed to locate a valid Edge index header." : LocalError.Message);
		return false;
	}

	bool FDust514Ps3SkeletalMeshDecoder::Decode(
		const std::vector<FPageDesc>& Pages,
		const FDecodeOptions& Options,
		FDecodedMesh& OutMesh,
		FDecodeError* OutError
	)
	{
		OutMesh.Positions.clear();
		OutMesh.Indices.clear();
		OutMesh.SkinInfluences.clear();

		uint32_t VertexBase = 0;

		float Basis[3][3];
		GetBasis(Options.BasisKind, Basis);
		const bool bMirroredBasis = Determinant3x3(Basis) < 0.0f;

		for (size_t PageIndex = 0; PageIndex < Pages.size(); ++PageIndex)
		{
			const FPageDesc& Page = Pages[PageIndex];

			std::vector<FVector3> PagePositions;
			std::vector<uint32_t> PageIndices;
			std::vector<FSkinInfluence4> PageSkin;

			if (!DecodePackedPositions(
				Page.PositionData,
				Page.PositionDataSize,
				Page.VertexCount,
				Options,
				PagePositions,
				OutError))
			{
				return false;
			}

			if (!DecodeEdgeIndexBlock(
				Page.IndexData,
				Page.IndexDataSize,
				Page.IndexCount,
				Page.VertexCount,
				PageIndices,
				OutError))
			{
				return false;
			}

			if (!DecodeFixed8SkinRecords(
				Page.SkinData,
				Page.SkinDataSize,
				Page.VertexCount,
				Page.BonePalette,
				PageSkin,
				OutError))
			{
				return false;
			}

			OutMesh.Positions.insert(
				OutMesh.Positions.end(),
				PagePositions.begin(),
				PagePositions.end()
			);

			OutMesh.SkinInfluences.insert(
				OutMesh.SkinInfluences.end(),
				PageSkin.begin(),
				PageSkin.end()
			);

			if (Options.bFlipWindingWhenBasisMirrored && bMirroredBasis)
			{
				if ((PageIndices.size() % 3u) != 0u)
				{
					SetError(OutError, "Decode: index count is not divisible by 3.");
					return false;
				}

				for (size_t i = 0; i < PageIndices.size(); i += 3)
				{
					OutMesh.Indices.push_back(VertexBase + PageIndices[i + 0]);
					OutMesh.Indices.push_back(VertexBase + PageIndices[i + 2]);
					OutMesh.Indices.push_back(VertexBase + PageIndices[i + 1]);
				}
			}
			else
			{
				for (uint32_t LocalIndex : PageIndices)
				{
					OutMesh.Indices.push_back(VertexBase + LocalIndex);
				}
			}

			VertexBase += Page.VertexCount;
		}

		return true;
	}
}
