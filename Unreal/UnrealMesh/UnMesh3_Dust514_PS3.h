#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#ifndef DUST514_PS3_ENABLE_EXCEPTIONS
#define DUST514_PS3_ENABLE_EXCEPTIONS 0
#endif

namespace Dust514Ps3
{
	struct FEdgeIndexBlockHeader
	{
		uint32_t BitsPerIndex = 0;
		uint32_t DeltaOffset = 0;
		uint32_t Seeds[8] = { 0,0,0,0,0,0,0,0 };
		uint32_t SeedCount = 0;
	};

	struct FVector3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
	};

	struct FQuat
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 1.0f;
	};

	struct FSkinInfluence4
	{
		uint16_t BoneIndex[4] = { 0, 0, 0, 0 };
		float Weight[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	};

	struct FPageDesc
	{
		const uint8_t* PositionData = nullptr;
		size_t PositionDataSize = 0;

		const uint8_t* IndexData = nullptr;
		size_t IndexDataSize = 0;

		const uint8_t* SkinData = nullptr;
		size_t SkinDataSize = 0;

		uint32_t VertexCount = 0;
		uint32_t IndexCount = 0;

		// Optional page-local bone palette.
		// If empty, fixed8 indices are treated as final skeleton indices.
		std::vector<uint16_t> BonePalette;
	};

	enum class EBasisKind
	{
		DefaultWeapon,
		CharacterAssault
	};

	struct FDecodeOptions
	{
		EBasisKind BasisKind = EBasisKind::DefaultWeapon;

		// Position format assumptions for DUST wrapper streams.
		// Keep these adjustable because DUST assets are not all identical.
		bool bPositionsAreX11Y11Z10N = true;
		bool bPositionsAreBigEndian = true;

		// For fixed-point positions, decoded value is:
		// Final = DecodeNormalisedSignedComponent(...) * PositionScale + PositionBias.
		FVector3 PositionScale = { 1.0f, 1.0f, 1.0f };
		FVector3 PositionBias = { 0.0f, 0.0f, 0.0f };

		bool bApplyBasisTransform = true;
		bool bFlipWindingWhenBasisMirrored = true;
	};

	struct FDecodedMesh
	{
		std::vector<FVector3> Positions;
		std::vector<uint32_t> Indices;
		std::vector<FSkinInfluence4> SkinInfluences;
	};

	struct FDecodeError
	{
		std::string Message;
	};

	class FDust514Ps3SkeletalMeshDecoder
	{
	public:
		static bool Decode(
			const std::vector<FPageDesc>& Pages,
			const FDecodeOptions& Options,
			FDecodedMesh& OutMesh,
			FDecodeError* OutError = nullptr
		);

		static FVector3 ApplyBasis(EBasisKind BasisKind, const FVector3& V);
		static FQuat ApplyBasisToQuat(EBasisKind BasisKind, const FQuat& Q);

		static bool DecodeEdgeIndexBlock(
			const uint8_t* Data,
			size_t DataSize,
			uint32_t IndexCount,
			uint32_t VertexLimit,
			std::vector<uint32_t>& OutIndices,
			FDecodeError* OutError = nullptr
		);

		// Decode a bit-packed delta payload when the wrapper supplies the Edge header separately.
		// DeltaData points at the first delta bit (high-order packed), not at the inline header.
		static bool DecodeEdgeIndexPayload(
			const uint8_t* DeltaData,
			size_t DeltaDataSize,
			uint32_t IndexCount,
			uint32_t VertexLimit,
			const FEdgeIndexBlockHeader& Header,
			std::vector<uint32_t>& OutIndices,
			FDecodeError* OutError = nullptr
		);

		static bool DecodeFixed8SkinRecords(
			const uint8_t* Data,
			size_t DataSize,
			uint32_t VertexCount,
			const std::vector<uint16_t>& BonePalette,
			std::vector<FSkinInfluence4>& OutSkin,
			FDecodeError* OutError = nullptr
		);

		static bool DecodePackedPositions(
			const uint8_t* Data,
			size_t DataSize,
			uint32_t VertexCount,
			const FDecodeOptions& Options,
			std::vector<FVector3>& OutPositions,
			FDecodeError* OutError = nullptr
		);
	};
}
