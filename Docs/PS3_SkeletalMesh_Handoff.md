# PlayStation 3 Skeletal Mesh Development (Hand-off)

This document is a practical hand-off for ongoing PlayStation 3 (PS3) skeletal mesh work in UEViewer. It focuses on where the PS3-specific behavior lives, how to reproduce/validate, and what is *missing* for full support.

For deep DUST 514 specifics (Edge index compression, fixed8 skin records, basis transforms), also read:

- `Docs/DUST514_PS3_SkeletalMesh_Implementation.md`

## Scope

- UE3-era PS3 packages (big-endian archives, `PACKAGE_FILE_TAG_REV`).
- Skeletal mesh work with special attention to DUST 514, which stores geometry outside normal UE3 `USkeletalMesh3` inline `LODModels`.

## Quickstart (build + smoke)

Linux dependencies are described in `README.md` and are required for building/running `umodel`:

- SDL2 (`libsdl2-dev` / `libsdl2-2.0-0`)
- zlib (`zlib1g-dev`)
- libpng (`libpng-dev`)

Build:

```sh
cd /path/to/UEViewer
./build.sh
```

Build just one translation unit (fast iteration):

```sh
./build.sh --file Unreal/UnrealMesh/UnMesh3_Dust514_PS3.cpp
```

Basic smoke:

```sh
./umodel -version
./umodel -help
```

## How PS3 routing works today

### 1) Platform selection

PS3 is selected in a few ways:

- CLI: `umodel -ps3 ...` sets the startup platform to `PLATFORM_PS3` (`UmodelTool/Main.cpp`).
- Cooked-path inference: when the root path includes `CookedPS3`, UEViewer sets `GForcePlatform = PLATFORM_PS3` (`Unreal/FileSystem/GameFileSystem.cpp`).

### 2) Big-endian packages and `ReverseBytes`

PS3 UE3 packages commonly use the reversed tag `PACKAGE_FILE_TAG_REV`. UEViewer sets `Ar.ReverseBytes = true` when it sees the reversed tag and continues parsing (`Unreal/UnrealPackage/UnPackage.cpp`).

Important nuance:

- For “fully compressed packages”, `Unreal/UnrealPackage/UnPackageReader.cpp` currently treats `ReverseBytes` as “XBox360 unless forced”. If PS3 decoding depends on platform-specific behavior, ensure `-ps3` is provided (or `CookedPS3` path inference fires) so platform is not ambiguous.

## UE3 skeletal mesh touch-points relevant to PS3

### Packed positions (UE3 GPU skin stream)

UE3 skeletal meshes can encode GPU skin positions using “packed position”. UEViewer enables packed-position handling when platform is XBox360 or PS3:

- `AllowPackedPosition = true` when `Ar.Platform == PLATFORM_XBOX360 || Ar.Platform == PLATFORM_PS3` (`Unreal/UnrealMesh/UnMesh3.cpp`).

This affects `FSkeletalMeshVertexBuffer3` deserialization and is a common “platform gate” for PS3 work.

### DUST 514 PS3: `LODModels` are *not* inline

For DUST 514 on PS3 (licensee version 35+), `USkeletalMesh3::Serialize` deliberately *skips* `LODModels` and drops remaining data to avoid desync:

- `Unreal/UnrealMesh/UnMesh3.cpp` prints `DUST514/PS3: ... uses streamed .MSH wrapper geometry; skipping inline LODModels`

Consequence:

- A DUST 514 PS3 skeletal mesh may load “metadata” (skeleton, bounds, materials), but it will not have usable geometry unless the streamed `.MSH` wrapper is decoded and then bridged into UEViewer’s runtime mesh structures.

## DUST 514 PS3 geometry decode (current state)

### What exists

There is a standalone decoder for DUST 514 PS3 wrapper streams:

- `Unreal/UnrealMesh/UnMesh3_Dust514_PS3.h`
- `Unreal/UnrealMesh/UnMesh3_Dust514_PS3.cpp`

Entry-point:

- `Dust514Ps3::FDust514Ps3SkeletalMeshDecoder::Decode(...)`

It is designed around a wrapper “page” abstraction (`Dust514Ps3::FPageDesc`) containing:

- packed positions
- Edge-compressed index payload
- fixed8 skin records (+ optional per-page bone palette)

And a set of decode knobs (`Dust514Ps3::FDecodeOptions`) to account for observed per-asset variance:

- big-endian vs little-endian packed positions
- X11Y11Z10N packed position format assumption
- basis transform selection (weapon-like vs character basis)
- optional winding flip when basis is mirrored

### What is missing

The decoder is currently not wired into the main UEViewer load path (no call sites outside the decoder itself).

To make DUST 514 PS3 skeletal meshes fully usable, you still need:

1) `.MSH` wrapper discovery and loading
   - Locate wrapper files/pages on disk relative to the package being loaded.
   - Parse wrapper page tables and slice out page payloads for positions, indices, and skin records.

2) Bridge decoded data into UEViewer’s mesh structures
   - Convert decoded positions/indices/skin influences into `CSkeletalMesh` / `CSkelMeshLod` buffers used by exporters/viewers.
   - Apply per-page vertex base offsets when concatenating indices.
   - Remap page-local bone indices through the page palette into final skeleton bone indices.

3) Integrate into a safe activation gate
   - Only activate for `-game=dust514 -ps3` (or equivalent detected game+platform).
   - Avoid regressions for normal UE3 PC/XBox skeletal mesh paths.

## Repro/validation workflow

### Minimum command shape

```text
umodel -ps3 -game=dust514 -path="<COOKEDPS3 folder>" <package>
```

Useful switches:

- `-log=umodel.log` for capture
- `-dump` / `-pkginfo` to sanity-check the package before chasing mesh decode

### Known initial targets

The implementation notes track these meshes as good first validation targets:

- `CH_1P_CA_Assault_SKM`
- `WP_1P_GA_PlasmaCannon_SKM`
- `WPM_1P_AM_ReaverKnife_SKM`

See `Docs/DUST514_PS3_SkeletalMesh_Implementation.md` for expectations and Edge/fixed8 details.

## Debugging checklist

- Confirm platform is PS3 (`-ps3` or a `CookedPS3` root path).
- Confirm byte order is reversed (`PACKAGE_FILE_TAG_REV` => `ReverseBytes` enabled).
- If the mesh is DUST 514 PS3 (licensee 35+), expect `LODModels` to be skipped; geometry must come from `.MSH`.
- Use `./build.sh --file Unreal/UnrealMesh/UnMesh3_Dust514_PS3.cpp` to iterate on the decoder quickly.

