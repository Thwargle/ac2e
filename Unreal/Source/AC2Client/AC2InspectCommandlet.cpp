#include "AC2InspectCommandlet.h"
#include "AC2Assets.h"
#include "AC2CreatureCatalog.h"
#include "AC2Particle.h"
#include "AC2Scenery.h"
#include "AC2Terrain.h"
#include "AC2UILayout.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

UAC2InspectCommandlet::UAC2InspectCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UAC2InspectCommandlet::Main(const FString &Params)
{
    if (FParse::Param(*Params, TEXT("MaterialsOnly")))
    {
        FString Error;
        if (!PrepareAC2Materials(Error))
        {
            UE_LOG(LogTemp, Error, TEXT("AC2 material preparation: %s"), *Error);
            return 1;
        }
        return 0;
    }
    const FString Directory = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FString Output = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/AC2DatReport.json"));
    FParse::Value(*Params, TEXT("Output="), Output);
    FString BlockText = TEXT("9882");
    FParse::Value(*Params, TEXT("AC2Block="), BlockText);
    uint32 BlockId;
    if (!AC2::ParseBlock(BlockText, BlockId))
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid AC2Block"));
        return 1;
    }
    const bool FullTerrain = FParse::Param(*Params, TEXT("FullTerrain"));
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("dat_directory"), Directory);
    Report->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
    Report->SetStringField(TEXT("validation"),
                           FullTerrain ? TEXT("all terrain records") : TEXT("one terrain record"));
    TArray<TSharedPtr<FJsonValue>> Archives;
    FAC2LandscapeDefs Defs;
    FAC2DatArchive Portal, Cells;
    FString Error;
    TArray<uint8> Bytes;
    if (!Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) ||
        !Portal.Read(0x36000000, Bytes, Error) || !Defs.Decode(Bytes, Error) ||
        !Cells.Open(FPaths::Combine(Directory, TEXT("cell_1.dat")), 2, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("AC2 inspection failed: %s"), *Error);
        return 1;
    }

    const TCHAR *Names[] = {TEXT("portal.dat"), TEXT("cell_1.dat"), TEXT("cell_2.dat"),
                            TEXT("local_English.dat"), TEXT("highres.dat")};
    const uint32 Sets[] = {1, 2, 2, 3, 1};
    TArray<FAC2DatEntry> TerrainEntries;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
    {
        const FString Path = FPaths::Combine(Directory, Names[I]);
        if (I > 1 && !IFileManager::Get().FileExists(*Path))
            continue;
        FAC2DatArchive Archive;
        TArray<FAC2DatEntry> Entries;
        if (!Archive.Open(Path, Sets[I], Error) || !Archive.Enumerate(Entries, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("%s: %s"), Names[I], *Error);
            return 1;
        }
        const FAC2DatHeader &H = Archive.GetHeader();
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("file"), Names[I]);
        Item->SetNumberField(TEXT("bytes"), H.FileSize);
        Item->SetNumberField(TEXT("block_bytes"), H.BlockSize);
        Item->SetNumberField(TEXT("dataset"), H.DataSet);
        Item->SetNumberField(TEXT("subset"), H.Subset);
        Item->SetNumberField(TEXT("engine_pack_version"), H.EngineVersion);
        Item->SetNumberField(TEXT("game_pack_version"), H.GameVersion);
        Item->SetNumberField(TEXT("records"), Entries.Num());
        auto Types = MakeShared<FJsonObject>();
        TMap<uint32, int32> Counts;
        for (const auto &Entry : Entries)
        {
            ++Counts.FindOrAdd(Entry.Id >> 24);
            if (I == 1 && (Entry.Id & 0xFFFF) == 0xFFFF)
                TerrainEntries.Add(Entry);
        }
        TArray<uint32> Keys;
        Counts.GetKeys(Keys);
        Keys.Sort();
        for (uint32 Key : Keys)
            Types->SetNumberField(FString::Printf(TEXT("0x%02X"), Key), Counts[Key]);
        Item->SetObjectField(TEXT("id_high_byte_counts"), Types);
        Archives.Add(MakeShared<FJsonValueObject>(Item));
        UE_LOG(LogTemp, Display, TEXT("AC2 %s: %d records, block=%u, pack=%u/%u"), Names[I], Entries.Num(),
               H.BlockSize, H.EngineVersion, H.GameVersion);
    }
    Report->SetArrayField(TEXT("archives"), Archives);
    int32 Validated = 0;
    for (const FAC2DatEntry &E : TerrainEntries)
    {
        if (!FullTerrain && E.Id != ((BlockId << 16) | 0xFFFF))
            continue;
        FAC2LandBlock Land;
        if (!Cells.Read(E.Id, Bytes, Error) || !Land.Decode(E.Id, Bytes, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("Terrain validation: %s"), *Error);
            return 1;
        }
        ++Validated;
    }
    FAC2LandBlock Sample;
    const uint32 SampleId = (BlockId << 16) | 0xFFFF;
    if (!Cells.Read(SampleId, Bytes, Error) || !Sample.Decode(SampleId, Bytes, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("Sample terrain: %s"), *Error);
        return 1;
    }
    FAC2TerrainMesh Mesh = BuildAC2TerrainMesh(Defs, Sample);
    float Min = MAX_flt, Max = -MAX_flt;
    for (const FVector &V : Mesh.Vertices)
    {
        Min = FMath::Min(Min, float(V.Z / 100));
        Max = FMath::Max(Max, float(V.Z / 100));
    }
    Report->SetNumberField(TEXT("terrain_records"), TerrainEntries.Num());
    Report->SetNumberField(TEXT("terrain_records_validated"), Validated);
    Report->SetNumberField(TEXT("block_size_metres"), Defs.BlockSize());
    auto SampleJson = MakeShared<FJsonObject>();
    SampleJson->SetStringField(TEXT("id"), FString::Printf(TEXT("0x%08X"), SampleId));
    SampleJson->SetNumberField(TEXT("vertices"), Mesh.Vertices.Num());
    SampleJson->SetNumberField(TEXT("triangles"), Mesh.Indices.Num() / 3);
    SampleJson->SetNumberField(TEXT("minimum_height_metres"), Min);
    SampleJson->SetNumberField(TEXT("maximum_height_metres"), Max);
    Report->SetObjectField(TEXT("sample"), SampleJson);
    if (FParse::Param(*Params, TEXT("NativeAssets")))
    {
        TArray<FAC2DatEntry> Entries;
        if (!Portal.Enumerate(Entries, Error))
            return 1;
        int32 Valid = 0, Emitters = 0, Keys = 0, Failed = 0;
        for (const auto &E : Entries)
            if ((E.Id >> 24) == 0x39)
            {
                FAC2ParticlePreset P;
                if (!Portal.Read(E.Id, Bytes, Error) || !P.Decode(E.Id, Bytes))
                {
                    ++Failed;
                    UE_LOG(LogTemp, Warning, TEXT("PSDesc rejected: %08X"), E.Id);
                    continue;
                }
                ++Valid;
                Emitters += P.Emitters.Num();
                for (const auto &Emitter : P.Emitters)
                    Keys += Emitter.Keys.Num();
            }
        Report->SetNumberField(TEXT("particle_descriptors"), Valid);
        Report->SetNumberField(TEXT("particle_emitters"), Emitters);
        Report->SetNumberField(TEXT("particle_keys"), Keys);
        Report->SetNumberField(TEXT("particle_rejected"), Failed);
        FAC2DatArchive Local;
        TArray<TSharedPtr<FJsonValue>> Layouts;
        if (Local.Open(FPaths::Combine(Directory, TEXT("local_English.dat")), 3, Error) &&
            Local.Enumerate(Entries, Error))
            for (const auto &E : Entries)
                if ((E.Id >> 24) == 0x22)
                {
                    FAC2UILayout L;
                    if (!Local.Read(E.Id, Bytes, Error) || !L.Decode(E.Id, Bytes))
                    {
                        ++Failed;
                        UE_LOG(LogTemp, Warning, TEXT("Layout rejected: %08X"), E.Id);
                        continue;
                    }
                    auto J = MakeShared<FJsonObject>();
                    J->SetStringField(TEXT("id"), FString::Printf(TEXT("%08X"), E.Id));
                    J->SetNumberField(TEXT("elements"), L.Elements);
                    TArray<TSharedPtr<FJsonValue>> Art;
                    for (const auto &Image : L.Art)
                    {
                        auto A = MakeShared<FJsonObject>();
                        A->SetStringField(TEXT("element"), FString::Printf(TEXT("%08X"), Image.Element));
                        A->SetStringField(TEXT("image"), FString::Printf(TEXT("%08X"), Image.Image));
                        A->SetBoolField(TEXT("default"), Image.Default);
                        A->SetNumberField(TEXT("w"), Image.Rect.Width());
                        A->SetNumberField(TEXT("h"), Image.Rect.Height());
                        Art.Add(MakeShared<FJsonValueObject>(A));
                    }
                    J->SetArrayField(TEXT("art"), Art);
                    Layouts.Add(MakeShared<FJsonValueObject>(J));
                }
        Report->SetArrayField(TEXT("ui_layouts"), Layouts);
        UE_LOG(LogTemp, Display,
               TEXT("AC2 native asset audit: %d PSDesc / %d emitters / %d keys / %d layouts / %d rejected"),
               Valid, Emitters, Keys, Layouts.Num(), Failed);
        Report->SetNumberField(TEXT("native_assets_rejected"), Failed);
    }
    if (FParse::Param(*Params, TEXT("Environment")))
    {
        TArray<TSharedPtr<FJsonValue>> Environment;
        bool Passed = true;
        for (uint32 Block : {0x5667u, 0xcb4du, 0xf18fu, 0x7f7fu, 0xc654u})
        {
            FAC2LandBlock Land;
            FAC2AssetLibrary Assets;
            if (!Cells.Read((Block << 16) | 0xffff, Bytes, Error) ||
                !Land.Decode((Block << 16) | 0xffff, Bytes, Error) || !Assets.Open(Directory, Error) ||
                !Assets.PrepareBlock(Directory, Defs, Land, BuildAC2TerrainMesh(Defs, Land), Error))
                return 1;
            const auto &S = Assets.Stats();
            auto J = MakeShared<FJsonObject>();
            J->SetStringField(TEXT("block"), FString::Printf(TEXT("%04X"), Block));
            J->SetBoolField(TEXT("highres_available"), S.HighResAvailable);
            J->SetNumberField(TEXT("textures"), S.Textures);
            J->SetNumberField(TEXT("highres_textures"), S.HighResSurfaces);
            J->SetNumberField(TEXT("frills_generated"), S.FrillsGenerated);
            J->SetNumberField(TEXT("frills_resolved"), S.FrillsResolved);
            J->SetNumberField(TEXT("frill_batches"), S.FrillBatches);
            J->SetNumberField(TEXT("world_fx_sources"), Assets.ParticleSources().Num());
            int32 GrassMaterials = 0;
            const bool GrassDepth = Assets.ValidateGrassMaterials(GrassMaterials);
            J->SetNumberField(TEXT("grass_materials"), GrassMaterials);
            J->SetBoolField(TEXT("grass_depth_tested"), GrassDepth);
            Passed &= GrassDepth;
            Environment.Add(MakeShared<FJsonValueObject>(J));
            Passed &= S.HighResAvailable && S.HighResSurfaces > 0 && S.FrillsGenerated > 0 &&
                      S.FrillsResolved == S.FrillsGenerated && S.FrillBatches > 0;
        }
        FAC2AssetLibrary Character;
        if (!Character.Open(Directory, Error))
            return 1;
        for (uint32 Id : {0x31000037u, 0x31000038u, 0x31000843u})
        {
            auto *M = Character.Material(Id);
            const bool Opaque = M && M->GetBlendMode() == BLEND_Opaque;
            Passed &= Opaque;
            UE_LOG(LogTemp, Display, TEXT("AC2 character material %08X: opaque=%d"), Id, Opaque);
        }
        auto *Helper = Character.Material(0x3100034f);
        FLinearColor HelperAlpha;
        const bool HiddenHelper =
            Helper && Helper->GetBlendMode() == BLEND_Translucent &&
            Helper->GetVectorParameterValue(FMaterialParameterInfo(TEXT("AC2Opacity")), HelperAlpha) &&
            FMath::IsNearlyZero(HelperAlpha.R) && FMath::IsNearlyZero(HelperAlpha.G);
        Passed &= HiddenHelper;
        Report->SetBoolField(TEXT("dungeon_zero_opacity_helper_passed"), HiddenHelper);
        auto *Body = Character.Texture(0x4000005e);
        Report->SetStringField(TEXT("body_texture_resolution"),
                               Body ? FString::Printf(TEXT("%dx%d"), Body->GetSizeX(), Body->GetSizeY())
                                    : TEXT("missing"));
        // The top body mip exists ONLY in highres.dat, the next mip ONLY in portal.dat.
        // Direct surface IDs must take the same path as a RenderSurface's mip list.
        auto *Direct = Character.Texture(0x4100034c);
        auto *Fallback = Character.Texture(0x4100034d);
        const bool SurfaceSelection = Body && Body->GetSizeX() == 1024 && Direct &&
                                      Direct->GetSizeX() == 1024 && Fallback && Fallback->GetSizeX() == 512;
        Passed &= SurfaceSelection;
        Report->SetBoolField(TEXT("highres_only_surface_and_portal_fallback_passed"), SurfaceSelection);
        Report->SetArrayField(TEXT("environment"), Environment);
        Report->SetBoolField(TEXT("environment_passed"), Passed);
    }
    if (FParse::Param(*Params, TEXT("Locomotion")))
    {
        FAC2AssetLibrary Assets;
        if (!Assets.Open(Directory, Error))
            return 1;
        TArray<TSharedPtr<FJsonValue>> Creatures;
        bool Passed = true;
        for (const auto &Creature : AC2CreaturePreviews)
        {
            FString Details;
            const bool Valid = Assets.AuditLocomotion(Creature.Visual, Details);
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("species"), Creature.Name);
            Item->SetStringField(TEXT("details"), Details);
            Item->SetBoolField(TEXT("passed"), Valid);
            Creatures.Add(MakeShared<FJsonValueObject>(Item));
            Passed &= Valid;
            UE_LOG(LogTemp, Display, TEXT("AC2 locomotion %s: %s %s"), Creature.Name,
                   Valid ? TEXT("PASS") : TEXT("FAIL"), *Details);
        }
        FString Human;
        Passed &= Assets.AuditLocomotion(0x1f000023, Human);
        Report->SetStringField(TEXT("human_locomotion"), Human);
        Report->SetArrayField(TEXT("creature_locomotion"), Creatures);
        Report->SetBoolField(TEXT("locomotion_passed"), Passed);
    }
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
    if (!FFileHelper::SaveStringToFile(Json, *Output))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot write inspection report: %s"), *Output);
        return 1;
    }
    if (FParse::Param(*Params, TEXT("NativeAssets")) &&
        Report->GetNumberField(TEXT("native_assets_rejected")) > 0)
        return 1;
    if (FParse::Param(*Params, TEXT("Environment")) && !Report->GetBoolField(TEXT("environment_passed")))
        return 1;
    if (FParse::Param(*Params, TEXT("Locomotion")) && !Report->GetBoolField(TEXT("locomotion_passed")))
        return 1;
    UE_LOG(LogTemp, Display, TEXT("AC2 inspection PASS: %d/%d terrain records validated; %s"), Validated,
           TerrainEntries.Num(), *Output);
    return 0;
}
