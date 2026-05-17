# DUST 514 PS3 skeletal mesh support implementation notes

This document records the UEViewer changes required for PlayStation 3 skeletal meshes from DUST 514.

For a higher-level hand-off (code map, current integration gaps, repro workflow), see:

- `Docs/PS3_SkeletalMesh_Handoff.md`

The current work is grounded in the uploaded PlayStation Edge geometry sources and the local DUST skeletal reconstruction probes. The relevant observed pipeline is:

1. package skeletal metadata: bones, hierarchy, sockets and material context;
2. streamed `.MSH` wrapper/page layout;
3. page-local Edge compressed index payloads;
4. wrapper packed position stream;
5. wrapper fixed8 skin records.

Current status in UEViewer:

- PS3 DUST skeletal meshes do not deserialize inline UE3 `FStaticLODModel3` data. `USkeletalMesh3::Serialize` skips `LODModels` for `GAME_Dust514`/PS3 (licensee ver 35+) to avoid desync/stopper overruns, and the geometry decode must come from the streamed `.MSH` wrapper instead (`Unreal/UnrealMesh/UnMesh3.cpp`).

The DUST reference exporter identifies three initial targets:

- `CH_1P_CA_Assault_SKM`
- `WP_1P_GA_PlasmaCannon_SKM`
- `WPM_1P_AM_ReaverKnife_SKM`

## Required UEViewer changes

### 1. PS3 archive routing

When UEViewer encounters the reversed package tag for a PS3 package, the archive must keep `ReverseBytes = true` and route downstream mesh loading through the PS3 path.

Implementation target:

- `Unreal/UnPackage.cpp`, or the repository-equivalent package summary serialisation file.

Expected behaviour:

```cpp
if (Tag == PACKAGE_FILE_TAG_REV)
{
    Ar.ReverseBytes = true;
    Tag = PACKAGE_FILE_TAG;
    Ar.Platform = PLATFORM_PS3;
}
```

All PS3 skeletal-mesh reads must then use archive helpers that honour `ReverseBytes`.

### 2. Packed position decode

DUST 514 PS3 skeletal meshes use wrapper/page data, not a simple PC-style contiguous vertex buffer. The PS3 path must decode the packed position stream before the normal UEViewer mesh build path consumes it.

The DUST reconstruction scripts use target-specific component and bounds permutations:

```cpp
// Default weapon-like basis.
static const int DefaultPositionComponentPerm[3] = { 0, 1, 2 };
static const int DefaultPositionBoundsPerm[3]    = { 0, 1, 2 };

// Character body basis for CH_1P_CA_Assault_SKM.
static const int CharacterPositionComponentPerm[3] = { 2, 0, 1 };
static const int CharacterPositionBoundsPerm[3]    = { 2, 0, 1 };
```

Initial implementation may decode the wrapper position stream into standard `FVector`/`CVec3` positions, then allow existing UEViewer export code to operate normally.

### 3. Edge compressed index decode

DUST pages contain page-local Edge compressed index payloads. Do not treat the payload as a plain little-endian index buffer.

The uploaded Edge compressor shows the essential reverse operation:

- indexes are converted into deltas per block;
- previous indexes are tracked in an 8-entry history;
- deltas are bit-packed high-order first;
- a delta offset restores signed deltas.

A UEViewer decoder should mirror the inverse of that logic:

```cpp
static bool DecodeDustEdgeIndexBlock(
    const uint8* Data,
    int DataSize,
    int IndexCount,
    int VertexLimit,
    TArray<uint16>& OutIndices)
{
    // TODO: implement exact inverse of Edge delta block format.
    // The uploaded Edge source shows ConvertIndexesToDeltas and FillDeltaBlockBuffer.
    // Decode must restore first eight seed indexes and then indexes[i] = previous[i % 8] + delta.
    return false;
}
```

This function should be isolated so it can be tested independently against known DUST `.MSH` page payloads.

Current implementation lives in:

- `Unreal/UnrealMesh/UnMesh3_Dust514_PS3.cpp`
- `Unreal/UnrealMesh/UnMesh3_Dust514_PS3.h`

Notes:

- `FDust514Ps3SkeletalMeshDecoder::DecodeEdgeIndexBlock(...)` expects the inline Edge header+seeds format, but will also scan the first 64 bytes for a valid inner header when the wrapper prefixes the payload with page metadata.
- If the `.MSH` page table stores `bitsPerIndex`, `deltaOffset`, and seed indices externally (not inline), use `FDust514Ps3SkeletalMeshDecoder::DecodeEdgeIndexPayload(...)` with the extracted header fields and pass only the bit-packed delta payload.

### 4. Fixed8 skin record decode

DUST skinning uses fixed8 records. Treat each vertex as four influences:

```cpp
struct FDustFixed8SkinRecord
{
    uint8 Weights[4];
    uint8 Indices[4];
};
```

The decode rule is:

```cpp
float Sum = float(W0 + W1 + W2 + W3);
if (Sum <= 0.0f)
{
    Sum = 255.0f;
}
OutWeights[0] = float(W0) / Sum;
OutWeights[1] = float(W1) / Sum;
OutWeights[2] = float(W2) / Sum;
OutWeights[3] = float(W3) / Sum;
```

Bone indices are page-local and must be remapped through the DUST page palette before being attached to the final UEViewer skeletal mesh influence buffer.

### 5. DUST coordinate transforms

The DUST reconstruction scripts use two known target bases.

Default weapon-like target basis:

```cpp
static const float DefaultTargetBasis[3][3] =
{
    { 1.0f,  0.0f, 0.0f },
    { 0.0f,  0.0f, 1.0f },
    { 0.0f, -1.0f, 0.0f },
};
```

Character target basis for `CH_1P_CA_Assault_SKM`:

```cpp
static const float CharacterTargetBasis[3][3] =
{
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 0.0f },
};
```

For positions and directions:

```cpp
Out.X = Basis[0][0] * X + Basis[0][1] * Y + Basis[0][2] * Z;
Out.Y = Basis[1][0] * X + Basis[1][1] * Y + Basis[1][2] * Z;
Out.Z = Basis[2][0] * X + Basis[2][1] * Y + Basis[2][2] * Z;
```

For rotations, convert quaternion to matrix, apply `B * M * inverse(B)`, then convert back to quaternion.

### 6. Initial game switch

Add or extend the DUST game path so the code only activates for:

```text
-game=dust514 -ps3
```

A safe helper name would be:

```cpp
bool IsDust514Ps3Mesh(const FArchive& Ar)
{
    return Ar.Platform == PLATFORM_PS3 && Ar.Game == GAME_Dust514;
}
```

Use the existing UEViewer naming conventions for platform and game constants.

## Suggested new files

If the repository layout permits, add:

```text
Unreal/UnrealMesh/UnMesh3_Dust514_PS3.cpp
Unreal/UnrealMesh/UnMesh3_Dust514_PS3.h
```

These should contain only DUST-specific decode helpers. The main UE3 skeletal mesh path should call into them, not absorb all logic inline.

## Validation targets

Use these meshes first:

```text
CH_1P_CA_Assault_SKM
WP_1P_GA_PlasmaCannon_SKM
WPM_1P_AM_ReaverKnife_SKM
```

Validation command shape:

```bat
umodel.exe -ps3 -game=dust514 -mesh -anim -path="<COOKEDPS3 folder>" <package>.XXX
```

Expected result:

- mesh vertices decode without explosion;
- triangle indices remain page-local during decode, then are appended with page vertex-base offsets;
- fixed8 weights normalise to four influences per vertex;
- bone indices remap through the page palette;
- character and weapon meshes use their correct basis transforms;
- glTF/PSK export does not regress non-DUST UE3 behaviour.

## Notes from uploaded sources

The uploaded Edge geometry toolchain confirms several important constraints:

- endian write helpers explicitly support big-endian output;
- X11Y11Z10N packs normalised components into 11/11/10-bit layout;
- the index compressor uses block delta conversion with an 8-entry previous-index history;
- skinning is consistently treated as four matrix indices and four weights per vertex.

These details should be used as implementation constraints rather than assumptions.
