#include "Core.h"

#if UNREAL3

#include "UnCore.h"
#include "UnObject.h"
#include "UnMesh3_Dust514_PS3.h"
#include "UnMathTools.h"

#include "Mesh/MeshCommon.h"
#include "TypeConvert.h"

static const int DefaultPositionComponentPerm[3]   = { 0, 1, 2 };
static const int CharacterPositionComponentPerm[3] = { 2, 0, 1 };

static const float DefaultTargetBasis[3][3] =
{
	{ 1.0f,  0.0f, 0.0f },
	{ 0.0f,  0.0f, 1.0f },
	{ 0.0f, -1.0f, 0.0f },
};

static const float CharacterTargetBasis[3][3] =
{
	{ 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 1.0f },
	{ 1.0f, 0.0f, 0.0f },
};

bool IsDust514Ps3Mesh(const FArchive& Ar)
{
	return Ar.Game == GAME_Dust514 && Ar.Platform == PLATFORM_PS3;
}

bool IsDust514Ps3Mesh(const UObject* Object)
{
	if (!Object) return false;
	const FArchive* Ar = Object->GetPackageArchive();
	return Ar && IsDust514Ps3Mesh(*Ar);
}

static bool IsDust514Ps3CharacterMesh(const UObject* MeshObject)
{
	return MeshObject && MeshObject->Name && !strncmp(MeshObject->Name, "CH_", 3);
}

static const int* GetDust514Ps3PositionComponentPerm(const UObject* MeshObject)
{
	return IsDust514Ps3CharacterMesh(MeshObject) ? CharacterPositionComponentPerm : DefaultPositionComponentPerm;
}

static const float (*GetDust514Ps3Basis(const UObject* MeshObject))[3]
{
	return IsDust514Ps3CharacterMesh(MeshObject) ? CharacterTargetBasis : DefaultTargetBasis;
}

static FVector PermuteDust514Ps3Vector(const FVector& Src, const int Perm[3])
{
	const float Components[3] = { Src.X, Src.Y, Src.Z };
	FVector Dst;
	Dst.X = Components[Perm[0]];
	Dst.Y = Components[Perm[1]];
	Dst.Z = Components[Perm[2]];
	return Dst;
}

static FVector ApplyDust514Ps3Basis(const FVector& Src, const float Basis[3][3])
{
	FVector Dst;
	Dst.X = Basis[0][0] * Src.X + Basis[0][1] * Src.Y + Basis[0][2] * Src.Z;
	Dst.Y = Basis[1][0] * Src.X + Basis[1][1] * Src.Y + Basis[1][2] * Src.Z;
	Dst.Z = Basis[2][0] * Src.X + Basis[2][1] * Src.Y + Basis[2][2] * Src.Z;
	return Dst;
}

FVector DecodeDust514Ps3PackedPosition(const UObject* MeshObject, const FVector& Position)
{
	if (!IsDust514Ps3Mesh(MeshObject))
		return Position;

	const int* Perm = GetDust514Ps3PositionComponentPerm(MeshObject);
	return ApplyDust514Ps3Basis(PermuteDust514Ps3Vector(Position, Perm), GetDust514Ps3Basis(MeshObject));
}

FVector TransformDust514Ps3Position(const UObject* MeshObject, const FVector& Position)
{
	if (!IsDust514Ps3Mesh(MeshObject))
		return Position;

	return ApplyDust514Ps3Basis(Position, GetDust514Ps3Basis(MeshObject));
}

FVector TransformDust514Ps3Direction(const UObject* MeshObject, const FVector& Direction)
{
	if (!IsDust514Ps3Mesh(MeshObject))
		return Direction;

	return ApplyDust514Ps3Basis(Direction, GetDust514Ps3Basis(MeshObject));
}

FQuat TransformDust514Ps3Rotation(const UObject* MeshObject, const FQuat& Rotation)
{
	if (!IsDust514Ps3Mesh(MeshObject))
		return Rotation;

	CAxis Axis;
	CVT(Rotation).ToAxis(Axis);
	for (int AxisIndex = 0; AxisIndex < 3; AxisIndex++)
	{
		FVector Direction = CVT(Axis[AxisIndex]);
		Direction = TransformDust514Ps3Direction(MeshObject, Direction);
		Axis[AxisIndex] = CVT(Direction);
	}

	CQuat DstQuat;
	DstQuat.FromAxis(Axis);
	return CVT(DstQuat);
}

FRotator TransformDust514Ps3Rotator(const UObject* MeshObject, const FRotator& Rotation)
{
	if (!IsDust514Ps3Mesh(MeshObject))
		return Rotation;

	CAxis Axis;
	RotatorToAxis(Rotation, Axis);
	for (int AxisIndex = 0; AxisIndex < 3; AxisIndex++)
	{
		FVector Direction = CVT(Axis[AxisIndex]);
		Direction = TransformDust514Ps3Direction(MeshObject, Direction);
		Axis[AxisIndex] = CVT(Direction);
	}

	FRotator DstRotation;
	AxisToRotator(Axis, DstRotation);
	return DstRotation;
}

FBoxSphereBounds TransformDust514Ps3Bounds(const UObject* MeshObject, const FBoxSphereBounds& Bounds)
{
	if (!IsDust514Ps3Mesh(MeshObject))
		return Bounds;

	FBoxSphereBounds Dst = Bounds;
	Dst.Origin = TransformDust514Ps3Position(MeshObject, Bounds.Origin);
	FVector Extent = ApplyDust514Ps3Basis(Bounds.BoxExtent, GetDust514Ps3Basis(MeshObject));
	Dst.BoxExtent.X = fabs(Extent.X);
	Dst.BoxExtent.Y = fabs(Extent.Y);
	Dst.BoxExtent.Z = fabs(Extent.Z);
	return Dst;
}

void TransformDust514Ps3Vertex(const UObject* MeshObject, CMeshVertex& Vertex)
{
	if (!IsDust514Ps3Mesh(MeshObject))
		return;

	FVector Position = DecodeDust514Ps3PackedPosition(MeshObject, CVT(Vertex.Position));
	Vertex.Position = CVT(Position);

	CVec3 Normal;
	CVec3 Tangent;
	Unpack(Normal, Vertex.Normal);
	Unpack(Tangent, Vertex.Tangent);

	FVector NormalF  = TransformDust514Ps3Direction(MeshObject, CVT(Normal));
	FVector TangentF = TransformDust514Ps3Direction(MeshObject, CVT(Tangent));

	float BinormalSign = Vertex.Normal.GetW();
	Pack(Vertex.Normal, CVT(NormalF));
	Pack(Vertex.Tangent, CVT(TangentF));
	Vertex.Normal.SetW(BinormalSign);
}

void NormalizeDust514Ps3Fixed8Weights(const byte InWeights[4], byte OutWeights[4])
{
	int Sum = InWeights[0] + InWeights[1] + InWeights[2] + InWeights[3];
	if (Sum <= 0)
	{
		memset(OutWeights, 0, 4);
		return;
	}

	int LastNonZero = -1;
	int OutputSum = 0;
	for (int i = 0; i < 4; i++)
	{
		int Weight = appRound((float)InWeights[i] * 255.0f / Sum);
		if (Weight < 0) Weight = 0;
		if (Weight > 255) Weight = 255;
		OutWeights[i] = Weight;
		if (OutWeights[i] != 0) LastNonZero = i;
		OutputSum += OutWeights[i];
	}

	if (LastNonZero >= 0 && OutputSum != 255)
	{
		int FixedWeight = OutWeights[LastNonZero] + (255 - OutputSum);
		if (FixedWeight < 0) FixedWeight = 0;
		if (FixedWeight > 255) FixedWeight = 255;
		OutWeights[LastNonZero] = FixedWeight;
	}
}

#endif // UNREAL3
