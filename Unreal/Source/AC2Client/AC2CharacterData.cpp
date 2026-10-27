#include "AC2CharacterData.h"
#include "AC2AssetReader.h"
#include "AC2UILayout.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString ReadString(FAC2AssetReader &R, int32 Width = 1)
{
    uint32 N = R.Short();
    if (N == 65535)
        N = R.Word();
    const int32 P = R.Pos;
    if (!R.Take(int64(N) * Width))
        return {};
    TArray<uint8> B;
    B.Append(R.Data.GetData() + P, N * Width);
    R.Align();
    const uint32 Keys[] = {0xe5849238, 0xeb2d767d, 0xfc0fdd03, 0xcb45a6a6, 0x7f21e6bb, 0xf5a6cdc8,
                           0x22d9ad12, 0xb2a3d4de, 0x3b834db7, 0xc406c91c, 0x8392305e, 0x0a9f0099,
                           0x0943b90c, 0xa66d953c, 0x0d929e1f, 0x0e2e9ad4};
    for (int32 I = 0; I + 4 <= B.Num(); I += 4)
    {
        uint32 V = AC2::U32(B.GetData() + I) ^ Keys[(I / 4) & 15];
        FMemory::Memcpy(B.GetData() + I, &V, 4);
    }
    for (uint8 &V : B)
        V = uint8((V << 4) | (V >> 4));
    B.AddZeroed(2);
    return Width == 2 ? FString(int32(N), reinterpret_cast<const TCHAR *>(B.GetData()))
                      : FString(UTF8_TO_TCHAR(reinterpret_cast<const char *>(B.GetData())));
}
struct FNode
{
    TMap<uint32, uint32> Pairs;
    TArray<uint32> Values, Slots;
    FString Text;
    uint32 Physics = 0, Appearance = 0, StringKey = 0, StringTable = 0;
};
float AsFloat(uint32 W)
{
    float V;
    FMemory::Memcpy(&V, &W, 4);
    return V;
}
FString ItemLabel(FString Source)
{
    Source.RemoveFromStart(TEXT("wsl_"));
    Source.RemoveFromEnd(TEXT(".entity"));
    FString Out;
    for (int32 I = 0; I < Source.Len(); ++I)
    {
        if (I && FChar::IsUpper(Source[I]) && FChar::IsLower(Source[I - 1]))
            Out += TEXT(" ");
        Out += Source[I];
    }
    return Out;
}
FString StringTable(FAC2DatArchive &Local, uint32 Table, uint32 Key, int32 Depth = 0,
                   const FString *Argument = nullptr)
{
    FString Error;
    TArray<uint8> Bytes;
    if (Depth > 8 || !Table || !Local.Read(Table, Bytes, Error))
        return {};
    FAC2AssetReader R(MoveTemp(Bytes));
    if (R.Word() != Table)
        return {};
    R.Take(12);
    R.Align();
    const int32 N = R.Count(10);
    for (int32 I = 0; I < N && R.Good; ++I)
    {
        uint32 K = R.Word();
        uint32 PrivateTable = R.Word();
        uint16 Header = R.Short();
        FString Text;
        TArray<FString> Segments;
        for (int32 J = 0; J < (Header & 32767) && R.Good; ++J)
        {
            uint16 Present = R.Short();
            R.Align();
            Segments.Add(Present ? ReadString(R, 2) : FString());
            Text += Segments.Last();
        }
        R.Align();
        uint16 Variables = R.Short();
        R.Take(int64(Variables) * 4);
        R.Align();
        if (Header & 32768)
        {
            int32 Names = R.Count();
            for (int32 J = 0; J < Names && R.Good; ++J)
                ReadString(R, 2);
            R.Align();
        }
        // Map notes contain one coordinate argument between two localized string segments.
        const bool Formatted = Argument && Variables == 1 && Segments.Num() == 2;
        if (Formatted) Text = Segments[0] + *Argument + Segments[1];
        if (K == Key && R.Good && (!Variables || Formatted))
            return Text.IsEmpty() && PrivateTable && PrivateTable != Table
                       ? StringTable(Local, PrivateTable, Key, Depth + 1, Argument)
                       : Text;
    }
    return {};
}
// Entity overrides have typed properties. Unknown script defaults remain unknown instead of fabricated stats.
void ReadItem(FAC2DatArchive &Portal, FAC2DatArchive &Local, const TMap<uint32, uint32> &Types,
              FAC2ItemDefinition &Item)
{
    FString Error;
    TArray<uint8> Bytes;
    if (!Portal.Read(Item.Id, Bytes, Error))
        return;
    FAC2AssetReader R(MoveTemp(Bytes));
    uint32 Bits = R.Word();
    if (Bits & ~511u)
        return;
    if (Bits & 1)
        R.Word();
    if (Bits & 2)
        R.Word();
    if (Bits & 4)
        R.Take(8);
    if (Bits & 8)
        R.Word();
    if (Bits & 16)
        R.Take(64);
    if (Bits & 32)
        R.Take(12);
    FAC2ItemDefinition Result = Item;
    if (Bits & 128)
    {
        int32 NG = R.Count(8);
        for (int32 G = 0; G < NG && R.Good; ++G)
        {
            R.Word();
            int32 NP = R.Count();
            for (int32 I = 0; I < NP && R.Good; ++I)
            {
                uint32 K = R.Word(), T = Types.FindRef(K) & 65535, V = 0;
                switch (T)
                {
                case 1:
                case 2:
                case 3:
                case 5:
                case 7:
                case 8:
                case 11:
                    V = R.Word();
                    break;
                case 4:
                    R.Take(12);
                    break;
                case 6:
                    ReadString(R);
                    break;
                case 9:
                    R.Take(44);
                    break;
                case 13:
                    R.Take(8);
                    break;
                case 14:
                    R.Take(32);
                    break;
                case 10: {
                    uint32 Key = R.Word(), Table = R.Word(), Flags = R.Word();
                    FString S = Flags >> 16 ? ReadString(R, 2) : StringTable(Local, Table, Key);
                    if (Flags & 65535)
                        R.Good = false;
                    if (!S.IsEmpty() && K == 0x41000143)
                    {
                        // StringTable nouns carry trailing grammatical annotations, e.g. [v], [E], [sp].
                        while (S.EndsWith(TEXT("]")))
                        {
                            int32 Start;
                            if (!S.FindLastChar('[', Start) || S.Len() - Start > 8)
                                break;
                            S.LeftInline(Start);
                        }
                        Result.Name = S;
                    }
                    if (!S.IsEmpty() && K == 0x41000144)
                        Result.Description = S;
                    break;
                }
                default:
                    R.Good = false;
                    break;
                }
                switch (K)
                {
                case 0x4100020a:
                    Result.Icon = V;
                    break;
                case 0x4100008a:
                    Result.Preferred = V;
                    break;
                case 0x4100008b:
                    Result.Slots = V;
                    break;
                case 0x4100008c:
                    Result.Excludes = V;
                    break;
                case 0x41000132:
                    Result.Race = V;
                    break;
                case 0x41000001:
                    Result.MinLevel = FMath::Max(1, int32(V));
                    break;
                case 0x410000a7:
                    Result.Armor = V;
                    Result.HasArmor = true;
                    break;
                case 0x41000196:
                    Result.Damage = V;
                    Result.HasDamage = true;
                    break;
                }
            }
        }
    }
    if (Bits & 64)
        R.Word();
    if (Bits & 256)
        ReadString(R);
    if (R.End())
        Item = MoveTemp(Result);
}
bool ChoiceContains(const TArray<float> &Values, float Value)
{
    return FMath::IsFinite(Value) &&
           Values.ContainsByPredicate([&](float V) { return FMath::IsNearlyEqual(V, Value, .0001f); });
}
} // namespace
FString AC2ReadPackedString(FAC2AssetReader &Reader, int32 Width)
{
    return ReadString(Reader, Width);
}
FString AC2ReadUIString(FAC2DatArchive &Local, uint32 Table, uint32 Key, const FString *Argument)
{
    return StringTable(Local, Table, Key, 0, Argument);
}

bool FAC2CharacterCatalog::DecodeMatrix(TConstArrayView<uint8> Bytes)
{
    Avatars.Reset();
    Starters.Reset();
    Items.Reset();
    FAC2AssetReader R(Bytes);
    if (R.Word() != 0x70000390)
        return false;
    uint32 Size = R.Word();
    if (Size > uint32(R.Left()))
        return false;
    int32 End = R.Pos + Size, Count = R.Count(4, 10000);
    TArray<uint32> Ids;
    for (int32 I = 0; I < Count; ++I)
        Ids.Add(R.Word());
    TMap<uint32, FNode> Nodes;
    for (uint32 Id : Ids)
    {
        if (!R.Good || Nodes.Contains(Id))
            return false;
        FNode &N = Nodes.Add(Id);
        uint32 Flags = R.Word();
        if ((Flags & 0x300) != 0x100 || (Flags & 0x20000000))
        {
            R.Word();
            continue;
        }
        if ((Flags & 0x08000000) && !(Flags & 0x10000000))
            continue;
        R.Word();
        uint32 Kind = R.Word() & 65535;
        auto Pairs = [&]() {
            uint32 C = R.Word() & 0xffffff;
            if (C > uint32(R.Left() / 8))
            {
                R.Good = false;
                return;
            }
            for (uint32 I = 0; I < C; ++I)
            {
                uint32 K = R.Word(), V = R.Word();
                N.Pairs.Add(K, V);
            }
        };
        switch (Kind)
        {
        case 0:
            break;
        case 1:
        case 3:
        case 0x11:
            Pairs();
            break;
        case 5:
        case 0x0a:
        case 0x61:
        case 0x65: {
            int32 C = R.Count();
            for (int32 I = 0; I < C; ++I)
                N.Values.Add(R.Word());
            break;
        }
        case 0x6a:
            N.StringKey = R.Word();
            N.StringTable = R.Word();
            if (R.Word())
                R.Good = false;
            break;
        case 0xa2:
            N.Physics = R.Word();
            N.Appearance = R.Word();
            R.Word();
            Pairs();
            break;
        case 0x76:
            N.Text = ReadString(R, 2);
            break;
        case 0x5a:
            R.Take(32);
            break;
        default:
            return false;
        }
        int32 C = R.Count(5);
        for (int32 I = 0; I < C; ++I)
            N.Slots.Add(R.Word());
        R.Take(C);
        R.Align();
    }
    if (!R.Good || R.Pos != End || R.Word() != 912 || !R.End())
        return false;
    const FNode *Root = Nodes.Find(0);
    if (!Root || Root->Slots.Num() != 10)
        return false;
    auto Get = [&](uint32 Id) -> const FNode & {
        static const FNode Empty;
        const auto *N = Nodes.Find(Id);
        return N ? *N : Empty;
    };
    auto Find = [&](uint32 Id, uint32 K) {
        const auto *V = Get(Id).Pairs.Find(K);
        return V ? *V : MAX_uint32;
    };
    for (uint32 Race : {1u, 2u, 4u, 8u, 16u})
    {
        for (uint32 Sex : Get(Find(Root->Slots[2], Race)).Values)
        {
            const FNode &Info = Get(Find(Root->Slots[4], Race | Sex));
            if (Info.Physics >> 24 != 0x47)
                return false;
            FAC2AvatarChoice A;
            A.Race = Race;
            A.Sex = Sex;
            A.Physics = Info.Physics;
            A.Appearance = Info.Appearance;
            uint32 Phys = Find(Find(Root->Slots[0], Race), Sex);
            auto Values = [&](uint32 Kind, TArray<float> &Out) {
                for (uint32 Ref : Get(Find(Phys, Kind)).Values)
                    if (Get(Ref).Slots.Num())
                    {
                        float V = AsFloat(Get(Ref).Slots[0]);
                        if (FMath::IsFinite(V) && V >= .25f && V <= 3.f)
                            Out.Add(V);
                    }
                if (Out.IsEmpty())
                    Out.Add(1.f);
            };
            Values(1, A.Builds);
            Values(2, A.Heights);
            for (const auto &Kind : Get(Phys).Pairs)
            {
                if (Kind.Key <= 2)
                    continue;
                auto &Options = A.Choices.Add(Kind.Key);
                for (uint32 Ref : Get(Kind.Value).Values)
                {
                    const auto &S = Get(Ref).Slots;
                    if (S.Num() != 3 || (S[2] >> 24) != 0x20 || !FMath::IsFinite(AsFloat(S[0])))
                        return false;
                    Options.Add({S[2], S[1], AsFloat(S[0])});
                }
                const auto &Label = Get(Find(Root->Slots[1], (Kind.Key << 16) | Race | Sex));
                A.LabelIds.Add(Kind.Key, Label.Text.IsEmpty()
                                             ? FIntPoint(Label.StringTable, Label.StringKey)
                                             : FIntPoint(0x41000003, FAC2UILayout::Token(Label.Text)));
            }
            Avatars.Add(MoveTemp(A));
        }
        uint32 List = Find(Find(Find(Root->Slots[3], Race), 0), 0);
        for (uint32 Ref : Get(List).Values)
        {
            const auto &S = Get(Ref).Slots;
            if (S.Num() != 7 || S[6] >> 24 != 0x47)
                return false;
            FAC2ItemDefinition I;
            I.Id = S[6];
            I.Physique = S[0];
            I.StartsEquipped = S[1] != 0;
            I.Name = ItemLabel(Get(S[4]).Text);
            I.Race = Race;
            // StartInvData's physique selector identifies starter garment placement; weapons use primary
            // hand.
            I.Preferred = S[0] == 256 ? 2 : S[0] == 512 ? 8 : S[0] == 1024 ? 16 : 64;
            I.Slots = I.Preferred;
            if (I.Name.Contains(TEXT("Robe")))
            {
                I.Slots = I.Preferred = 2;
                I.Excludes = 8;
            }
            Items.Add(I.Id, I);
            Starters.FindOrAdd(Race).Add(I.Id);
        }
    }
    return Avatars.Num() == 10 && Items.Num() >= 20;
}
bool FAC2CharacterCatalog::Open(const FString &Directory, FString &Error)
{
    FAC2DatArchive Portal, Local;
    TArray<uint8> Bytes;
    if (!Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) ||
        !Portal.Read(0x70000390, Bytes, Error))
        return false;
    if (!DecodeMatrix(Bytes))
    {
        Error = TEXT("Unsupported native character matrix");
        return false;
    }
    if (!Local.Open(FPaths::Combine(Directory, TEXT("local_English.dat")), 3, Error) ||
        !Portal.Read(0x34000000, Bytes, Error))
        return false;
    FAC2AssetReader R(MoveTemp(Bytes));
    R.Word();
    R.Take(8);
    int32 N = R.Count(6);
    for (int32 I = 0; I < N && R.Good; ++I)
    {
        R.Word();
        ReadString(R);
    }
    TMap<uint32, uint32> Types;
    uint32 C = R.Word() & 65535;
    if (C > uint32(R.Left() / 24))
        return false;
    for (uint32 I = 0; I < C; ++I)
    {
        uint32 K = R.Word(), V = R.Word();
        Types.Add(K, V);
        R.Take(16);
    }
    if (!R.Good)
        return false;
    // String-table enum Chargen, rather than the general UI table used by LayoutDesc.
    PropertyTypes = Types;
    uint32 ChargenStrings = 0;
    if (Portal.Read(0x28000005, Bytes, Error))
    {
        FAC2AssetReader Tables(MoveTemp(Bytes));
        Tables.Word();
        uint32 Count = Tables.Word() & 65535;
        for (uint32 I = 0; I < Count && Tables.Good; ++I)
        {
            uint32 Key = Tables.Word(), Id = Tables.Word();
            if (Key == 0x41000003)
                ChargenStrings = Id;
        }
    }
    for (auto &A : Avatars)
        for (const auto &Label : A.LabelIds)
            A.Labels.Add(Label.Key, StringTable(Local,
                                                uint32(Label.Value.X) == 0x41000003 ? ChargenStrings
                                                                                    : uint32(Label.Value.X),
                                                uint32(Label.Value.Y)));
    // EnumMapper HoldingLocation (base chain): gmAvatarSelectionUI::InitEntities.
    uint32 Mapper = 0x2300001a;
    for (int32 Depth = 0; Mapper && Depth < 8; ++Depth)
    {
        if (!Portal.Read(Mapper, Bytes, Error))
            return false;
        FAC2AssetReader Names(MoveTemp(Bytes));
        if (Names.Word() != Mapper)
            return false;
        Mapper = Names.Word();
        int32 Count = Names.Count(6);
        for (int32 I = 0; I < Count && Names.Good; ++I)
        {
            uint32 Key = Names.Word();
            FString Name = ReadString(Names);
            for (auto &A : Avatars)
                if (Name.Equals((A.Sex == 4096 ? TEXT("Male") : TEXT("Female")) + RaceName(A.Race),
                                ESearchCase::IgnoreCase))
                    A.HoldingLocation = Key;
        }
        if (!Names.End())
            return false;
    }
    for (auto &Pair : Items)
        ReadItem(Portal, Local, Types, Pair.Value);
    UE_LOG(LogTemp, Display, TEXT("AC2 character data: %d native avatars / %d starter items"), Avatars.Num(),
           Items.Num());
    return true;
}
void FAC2CharacterCatalog::LoadNetworkItem(const FString& Directory,uint32 Id)
{
    if(Items.Contains(Id) || Id>>24!=0x47) return;
    FAC2DatArchive Portal,Local;FString Error;
    if(!Portal.Open(Directory/TEXT("portal.dat"),1,Error)||!Local.Open(Directory/TEXT("local_English.dat"),3,Error))return;
    FAC2ItemDefinition Item;Item.Id=Id;Item.Name=FString::Printf(TEXT("Item %08X"),Id);
    ReadItem(Portal,Local,PropertyTypes,Item);Items.Add(Id,MoveTemp(Item));
}
TMap<uint32, int32> FAC2AvatarChoice::Defaults() const
{
    TMap<uint32, int32> Result;
    for (const auto &C : Choices)
        if (!C.Value.IsEmpty())
            Result.Add(C.Key, 0);
    return Result;
}
FAC2Appearance FAC2AvatarChoice::Resolve(const TMap<uint32, int32> &Selections, uint32 Clothing) const
{
    FAC2Appearance Result;
    TArray<uint32> Types;
    Choices.GetKeys(Types);
    Types.Sort();
    for (uint32 Type : Types)
    {
        const auto &Values = Choices[Type];
        int32 Index = Selections.FindRef(Type);
        if (Values.IsValidIndex(Index))
        {
            // Starter garment tables pair colour key 2 with mesh activation key 0x10 (intensity 1).
            if ((Type & (256 | 512 | 1024)) && (Type & Clothing))
                Result.Add({Values[Index].Table, 0x10, 1.f});
            Result.Add(Values[Index]);
        }
    }
    return Result;
}
const FAC2AvatarChoice *FAC2CharacterCatalog::Avatar(uint32 Race, uint32 Sex) const
{
    return Avatars.FindByPredicate([&](const auto &A) { return A.Race == Race && A.Sex == Sex; });
}
FString FAC2CharacterCatalog::RaceName(uint32 Race)
{
    switch (Race)
    {
    case 1:
        return TEXT("Human");
    case 2:
        return TEXT("Lugian");
    case 4:
        return TEXT("Tumerok");
    case 8:
        return TEXT("Drudge");
    case 16:
        return TEXT("Empyrean");
    default:
        return TEXT("Unknown");
    }
}
FString FAC2CharacterCatalog::SlotName(uint32 Slot)
{
    const TCHAR *Names[] = {TEXT("Head"),   TEXT("Torso"),   TEXT("Back"),      TEXT("Legs"),
                            TEXT("Feet"),   TEXT("Hands"),   TEXT("Main hand"), TEXT("Off hand"),
                            TEXT("Ring I"), TEXT("Ring II"), TEXT("Neck")};
    for (int32 I = 0; I < 11; ++I)
        if (Slot == (1u << I))
            return Names[I];
    return TEXT("Backpack");
}
bool FAC2CharacterProfile::Move(int32 Index, int32 Cell, FString &Error)
{
    if (!Items.IsValidIndex(Index) || Cell < 0 || Cell >= 96)
    {
        Error = TEXT("Invalid inventory slot.");
        return false;
    }
    if (Items[Index].Equipped)
    {
        Error = TEXT("Unequip this item first.");
        return false;
    }
    int32 Old = Items[Index].Cell;
    for (int32 I = 0; I < Items.Num(); ++I)
        if (I != Index && !Items[I].Equipped && Items[I].Cell == Cell)
            Items[I].Cell = Old;
    Items[Index].Cell = Cell;
    return true;
}
uint32 FAC2CharacterProfile::ClothingMask(const FAC2CharacterCatalog &Catalog) const
{
    uint32 Mask = 0;
    for (const auto &Item : Items)
        if (Item.Equipped)
            if (const auto *D = Catalog.Items.Find(Item.Definition))
                Mask |= D->Physique;
    return Mask;
}
bool FAC2CharacterProfile::Equip(int32 Index, const FAC2CharacterCatalog &Catalog, FString &Error)
{
    if (!Items.IsValidIndex(Index))
        return false;
    auto Proposed = Items;
    auto FreeCell = [&]() -> int32 {
        for (int32 C = 0; C < 96; ++C)
            if (!Proposed.ContainsByPredicate([&](const auto &I) { return !I.Equipped && I.Cell == C; }))
                return C;
        return INDEX_NONE;
    };
    if (Proposed[Index].Equipped)
    {
        int32 C = FreeCell();
        if (C == INDEX_NONE)
        {
            Error = TEXT("Your backpacks are full.");
            return false;
        }
        Proposed[Index].Equipped = 0;
        Proposed[Index].Cell = C;
    }
    else
    {
        const auto *D = Catalog.Items.Find(Proposed[Index].Definition);
        if (!D || !D->Preferred || !(D->Slots & D->Preferred) || (D->Race && !(D->Race & Race)) ||
            Level < D->MinLevel)
        {
            Error = TEXT("This character cannot equip that item.");
            return false;
        }
        Proposed[Index].Equipped = D->Preferred;
        Proposed[Index].Cell = INDEX_NONE;
        for (int32 I = 0; I < Proposed.Num(); ++I)
        {
            if (I == Index || !Proposed[I].Equipped)
                continue;
            const auto *Other = Catalog.Items.Find(Proposed[I].Definition);
            if (Other &&
                ((Proposed[I].Equipped & (D->Preferred | D->Excludes)) || (Other->Excludes & D->Preferred)))
            {
                int32 C = FreeCell();
                if (C == INDEX_NONE)
                {
                    Error = TEXT("Make room in your backpacks first.");
                    return false;
                }
                Proposed[I].Equipped = 0;
                Proposed[I].Cell = C;
            }
        }
    }
    Items = MoveTemp(Proposed);
    return true;
}
int32 FAC2CharacterProfile::Armor(const FAC2CharacterCatalog &Catalog) const
{
    int32 Total = 0;
    for (const auto &I : Items)
        if (I.Equipped)
            if (auto *D = Catalog.Items.Find(I.Definition))
                Total += D->Armor;
    return Total;
}
int32 FAC2CharacterProfile::Damage(const FAC2CharacterCatalog &Catalog) const
{
    for (const auto &I : Items)
        if (I.Equipped == 64)
            if (auto *D = Catalog.Items.Find(I.Definition))
                if (D->HasDamage)
                    return D->Damage;
    return 25; // Existing sandbox strike until the item's server script defaults are available.
}
bool FAC2CharacterStore::ValidateName(FString &Name, FString &Error)
{
    Name.TrimStartAndEndInline();
    if (Name.Len() < 2 || Name.Len() > 24)
    {
        Error = TEXT("Use a name of 2 to 24 letters.");
        return false;
    }
    bool Separator = false;
    for (int32 I = 0; I < Name.Len(); ++I)
    {
        TCHAR C = Name[I];
        bool Letter = (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z');
        if (!Letter && (Separator || I == 0 || I == Name.Len() - 1 || (C != ' ' && C != '\'' && C != '-')))
        {
            Error = TEXT("Use letters with single spaces, apostrophes or hyphens between them.");
            return false;
        }
        Separator = !Letter;
    }
    return true;
}
FAC2CharacterProfile *FAC2CharacterStore::Active()
{
    return Characters.FindByPredicate([&](const auto &C) { return C.Id == ActiveId; });
}
bool FAC2CharacterStore::Create(FString Name, uint32 Race, uint32 Sex, float Height, float Build,
                                const FAC2CharacterCatalog &Catalog, FString &Error,
                                const TMap<uint32, int32> *Appearance)
{
    if (!ValidateName(Name, Error))
        return false;
    if (Name.Len() > 16)
    {
        Error = TEXT("Use a name of up to 16 letters.");
        return false;
    }
    if (Characters.Num() >= 8)
    {
        Error = TEXT("All eight local character slots are occupied.");
        return false;
    }
    if (Characters.ContainsByPredicate(
            [&](const auto &C) { return C.Name.Equals(Name, ESearchCase::IgnoreCase); }))
    {
        Error = TEXT("That name is already used by a local character.");
        return false;
    }
    FAC2CharacterProfile P;
    P.Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    P.Name = Name;
    P.Race = Race;
    P.Sex = Sex;
    P.Height = Height;
    P.Build = Build;
    if (const auto *A = Catalog.Avatar(Race, Sex))
        P.Appearance = Appearance ? *Appearance : A->Defaults();
    if (const auto *Ids = Catalog.Starters.Find(Race))
        for (uint32 Id : *Ids)
            P.Items.Add({Id, 0, P.Items.Num()});
    for (int32 Index = 0; Index < P.Items.Num(); ++Index)
        if (Catalog.Items[P.Items[Index].Definition].StartsEquipped && !P.Equip(Index, Catalog, Error))
            return false;
    if (!Validate(P, Catalog))
    {
        Error = TEXT("That appearance is not in the native character matrix.");
        return false;
    }
    ActiveId = P.Id;
    Characters.Add(MoveTemp(P));
    return true;
}
bool FAC2CharacterStore::Validate(const FAC2CharacterProfile &P, const FAC2CharacterCatalog &Catalog)
{
    const auto *A = Catalog.Avatar(P.Race, P.Sex);
    FString Name = P.Name, Error;
    FGuid Guid;
    if (!FGuid::ParseExact(P.Id, EGuidFormats::Digits, Guid) || !ValidateName(Name, Error) ||
        Name != P.Name || !A || !ChoiceContains(A->Heights, P.Height) ||
        !ChoiceContains(A->Builds, P.Build) || P.Level < 1 || P.Level > 150 || P.Experience < 0 ||
        !FMath::IsFinite(P.Health) || P.Health < 0 || P.Health > 100 || !FMath::IsFinite(P.Vigor) ||
        P.Vigor < 0 || P.Vigor > 100 || P.Items.Num() > 107)
        return false;
    if (P.Appearance.Num() != A->Defaults().Num())
        return false;
    for (const auto &Choice : P.Appearance)
    {
        const auto *Values = A->Choices.Find(Choice.Key);
        if (!Values || !Values->IsValidIndex(Choice.Value))
            return false;
    }
    TSet<int32> Cells;
    uint32 Slots = 0, Excludes = 0;
    for (const auto &I : P.Items)
    {
        const auto *D = Catalog.Items.Find(I.Definition);
        if (!D)
            return false;
        if (I.Equipped)
        {
            if ((I.Equipped & (I.Equipped - 1)) || !(D->Slots & I.Equipped) ||
                (I.Equipped & (Slots | Excludes)) || (D->Excludes & Slots) ||
                (D->Race && !(D->Race & P.Race)) || P.Level < D->MinLevel || I.Cell != INDEX_NONE)
                return false;
            Slots |= I.Equipped;
            Excludes |= D->Excludes;
        }
        else
        {
            if (I.Cell < 0 || I.Cell >= 96 || Cells.Contains(I.Cell))
                return false;
            Cells.Add(I.Cell);
        }
    }
    return true;
}
bool FAC2CharacterStore::Save(const FString &Path, FString &Error) const
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), 2);
    Root->SetStringField(TEXT("active"), ActiveId);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto &P : Characters)
    {
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("id"), P.Id);
        O->SetStringField(TEXT("name"), P.Name);
        O->SetNumberField(TEXT("race"), P.Race);
        O->SetNumberField(TEXT("sex"), P.Sex);
        O->SetNumberField(TEXT("height"), P.Height);
        O->SetNumberField(TEXT("build"), P.Build);
        auto Appearance = MakeShared<FJsonObject>();
        for (const auto &Choice : P.Appearance)
            Appearance->SetNumberField(FString::FromInt(Choice.Key), Choice.Value);
        O->SetObjectField(TEXT("appearance"), Appearance);
        O->SetNumberField(TEXT("level"), P.Level);
        O->SetNumberField(TEXT("xp"), P.Experience);
        O->SetNumberField(TEXT("health"), P.Health);
        O->SetNumberField(TEXT("vigor"), P.Vigor);
        TArray<TSharedPtr<FJsonValue>> Inventory;
        for (const auto &I : P.Items)
        {
            auto V = MakeShared<FJsonObject>();
            V->SetNumberField(TEXT("definition"), I.Definition);
            V->SetNumberField(TEXT("equipped"), I.Equipped);
            V->SetNumberField(TEXT("cell"), I.Cell);
            Inventory.Add(MakeShared<FJsonValueObject>(V));
        }
        O->SetArrayField(TEXT("items"), Inventory);
        Rows.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("characters"), Rows);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveStringToFile(Json, *(Path + TEXT(".tmp")),
                                       FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*Path, *(Path + TEXT(".tmp")), true, true, false, true))
    {
        Error = TEXT("Unable to save the character. Check the Saved/Characters folder.");
        return false;
    }
    return true;
}
bool FAC2CharacterStore::Load(const FString &Path, const FAC2CharacterCatalog &Catalog, FString &Error)
{
    if (!IFileManager::Get().FileExists(*Path))
        return true;
    FString Json;
    TSharedPtr<FJsonObject> Root;
    FAC2CharacterStore Loaded;
    auto Fail = [&]() {
        Error = TEXT("Character save could not be read. The original file has been preserved.");
        return false;
    };
    if (IFileManager::Get().FileSize(*Path) > 1024 * 1024 || !FFileHelper::LoadFileToString(Json, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
        return Fail();
    int32 Version;
    const TArray<TSharedPtr<FJsonValue>> *Rows;
    if (!Root->TryGetNumberField(TEXT("version"), Version) || (Version != 1 && Version != 2) ||
        !Root->TryGetStringField(TEXT("active"), Loaded.ActiveId) ||
        !Root->TryGetArrayField(TEXT("characters"), Rows) || Rows->Num() > 8)
        return Fail();
    TSet<FString> Ids, Names;
    for (const auto &Row : *Rows)
    {
        const TSharedPtr<FJsonObject> *O;
        if (!Row->TryGetObject(O) || !O->IsValid())
            return Fail();
        FAC2CharacterProfile P;
        const TArray<TSharedPtr<FJsonValue>> *Inventory;
        if (!(*O)->TryGetStringField(TEXT("id"), P.Id) || !(*O)->TryGetStringField(TEXT("name"), P.Name) ||
            !(*O)->TryGetNumberField(TEXT("race"), P.Race) || !(*O)->TryGetNumberField(TEXT("sex"), P.Sex) ||
            !(*O)->TryGetNumberField(TEXT("height"), P.Height) ||
            !(*O)->TryGetNumberField(TEXT("build"), P.Build) ||
            !(*O)->TryGetNumberField(TEXT("level"), P.Level) ||
            !(*O)->TryGetNumberField(TEXT("xp"), P.Experience) ||
            !(*O)->TryGetNumberField(TEXT("health"), P.Health) ||
            !(*O)->TryGetNumberField(TEXT("vigor"), P.Vigor) ||
            !(*O)->TryGetArrayField(TEXT("items"), Inventory) || Inventory->Num() > 107)
            return Fail();
        const auto *Avatar = Catalog.Avatar(P.Race, P.Sex);
        if (!Avatar)
            return Fail();
        P.Appearance = Avatar->Defaults(); // v1 profiles retain their body and receive valid native defaults.
        if (Version == 2)
        {
            const TSharedPtr<FJsonObject> *Appearance;
            if (!(*O)->TryGetObjectField(TEXT("appearance"), Appearance) || !Appearance->IsValid() ||
                (*Appearance)->Values.Num() != P.Appearance.Num())
                return Fail();
            for (auto &Choice : P.Appearance)
            {
                double Index;
                if (!(*Appearance)->TryGetNumberField(FString::FromInt(Choice.Key), Index) ||
                    !FMath::IsFinite(Index) || Index < 0 || Index > 10000 ||
                    Index != FMath::FloorToDouble(Index))
                    return Fail();
                Choice.Value = int32(Index);
            }
        }
        for (const auto &V : *Inventory)
        {
            const TSharedPtr<FJsonObject> *Obj;
            FAC2InventoryItem I;
            if (!V->TryGetObject(Obj) || !Obj->IsValid() ||
                !(*Obj)->TryGetNumberField(TEXT("definition"), I.Definition) ||
                !(*Obj)->TryGetNumberField(TEXT("equipped"), I.Equipped) ||
                !(*Obj)->TryGetNumberField(TEXT("cell"), I.Cell))
                return Fail();
            P.Items.Add(I);
        }
        if (!Validate(P, Catalog) || Ids.Contains(P.Id) || Names.Contains(P.Name.ToLower()))
            return Fail();
        Ids.Add(P.Id);
        Names.Add(P.Name.ToLower());
        Loaded.Characters.Add(MoveTemp(P));
    }
    if (!Loaded.ActiveId.IsEmpty() && !Loaded.Active())
        return Fail();
    *this = MoveTemp(Loaded);
    return true;
}
