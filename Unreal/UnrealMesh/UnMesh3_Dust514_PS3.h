#ifndef __UNMESH3_DUST514_PS3_H__
#define __UNMESH3_DUST514_PS3_H__

#if UNREAL3

class UObject;
class FArchive;
struct FVector;
struct FQuat;
struct FRotator;
struct FBoxSphereBounds;
struct CMeshVertex;

bool IsDust514Ps3Mesh(const FArchive& Ar);
bool IsDust514Ps3Mesh(const UObject* Object);

FVector DecodeDust514Ps3PackedPosition(const UObject* MeshObject, const FVector& Position);
FVector TransformDust514Ps3Position(const UObject* MeshObject, const FVector& Position);
FVector TransformDust514Ps3Direction(const UObject* MeshObject, const FVector& Direction);
FQuat   TransformDust514Ps3Rotation(const UObject* MeshObject, const FQuat& Rotation);
FRotator TransformDust514Ps3Rotator(const UObject* MeshObject, const FRotator& Rotation);
FBoxSphereBounds TransformDust514Ps3Bounds(const UObject* MeshObject, const FBoxSphereBounds& Bounds);

void TransformDust514Ps3Vertex(const UObject* MeshObject, CMeshVertex& Vertex);
void NormalizeDust514Ps3Fixed8Weights(const byte InWeights[4], byte OutWeights[4]);

#endif // UNREAL3

#endif // __UNMESH3_DUST514_PS3_H__
