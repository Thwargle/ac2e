#pragma once
#include "AC2Appearance.h"
#include "CoreMinimal.h"

struct FAC2AssetReader;
class FAC2DatArchive;
FString AC2ReadPackedString(FAC2AssetReader &Reader, int32 Width = 1);
FString AC2ReadUIString(FAC2DatArchive &Local, uint32 Table, uint32 Key, const FString *Argument = nullptr);
FString AC2FormatUIString(FAC2DatArchive &Local, uint32 Table, uint32 Key, const TMap<uint32,FString>& Arguments);

// Native CharGenMatrixData (70000390). These are data choices, not server-side character rules.
struct FAC2AvatarChoice
{
    uint32 Race = 1, Sex = 4096, Physics = 0, Appearance = 0;
    // Position in the creation scene, not a starting area in the server world.
    uint32 HoldingLocation = 0;
    TMap<uint32,uint32> PreviewHeldObjects; // native holding location -> entity DID
    TArray<float> Heights, Builds;
    TMap<uint32, TArray<FAC2AppearanceKey>> Choices;
    TMap<uint32, FString> Labels;
    TMap<uint32, FIntPoint> LabelIds;
    TMap<uint32, int32> Defaults() const;
    FAC2Appearance Resolve(const TMap<uint32, int32> &Selections, uint32 Clothing = 0x700) const;
};
// Private creation-screen random stream; never used for server gameplay rolls.
struct FAC2CharacterRandom
{
    int32 Seed = 1, Second = 123456789, Previous = 0;
    int32 Shuffle[32] = {};
    int32 Index(int32 Count);
};
struct FAC2CharacterDraft
{
    TMap<uint32, int32> Appearance;
    int32 Height = 0, Build = 0;
    static FAC2CharacterDraft Create(const FAC2AvatarChoice& Avatar, FAC2CharacterRandom& Random);
    void Randomize(const FAC2AvatarChoice& Avatar, uint32 Mask, FAC2CharacterRandom& Random);
};
struct FAC2ItemDefinition
{
    uint32 Id = 0, Icon = 0, Slots = 0, Preferred = 0, Excludes = 0, Race = 0;
    uint32 NativeClass = 0;
    // gmEntity::IsMultiItemUse reads IntStat 0x2D0, not a fixed item DID.
    int32 UseTargets = 0;
    // Native IntStats::ImplementType (0xFA0), not the entity script class.
    TOptional<uint32> Implement;
    FString Name, Description;
    int32 Armor = 0, Damage = 0, MinLevel = 1;
    bool HasArmor = false, HasDamage = false;
    uint32 Physique = 0;
    bool StartsEquipped = false;
};
class FAC2CharacterCatalog
{
  public:
    bool Open(const FString &Directory, FString &Error);
    void LoadNetworkItem(const FString& Directory, uint32 Id);
    bool DecodeMatrix(TConstArrayView<uint8> Bytes);
    const FAC2AvatarChoice *Avatar(uint32 Race, uint32 Sex) const;
    TArray<FAC2AvatarChoice> Avatars;
    TMap<uint32, TArray<uint32>> Starters;
    TMap<uint32, FAC2ItemDefinition> Items;
    static FString RaceName(uint32 Race);
    static FString SlotName(uint32 Slot);
  private:
    TMap<uint32,uint32> PropertyTypes;
};
struct FAC2InventoryItem
{
    uint32 Definition = 0, Equipped = 0;
    int32 Cell = 0;
    FString NetworkId;
    int32 Quantity = 1;
    uint32 Visual=0;
    FAC2Appearance Aspects;
};
struct FAC2CharacterProfile
{
    FString Id, Name;
    uint32 Race = 1, Sex = 4096;
    float Height = 1, Build = 1;
    TMap<uint32, int32> Appearance;
    int32 Level = 1;
    int64 Experience = 0;
    // Local sandbox vitals, independent of the unimplemented retail server rules.
    float Health = 100, Vigor = 100;
    TArray<FAC2InventoryItem> Items;
    uint32 ClothingMask(const FAC2CharacterCatalog &Catalog) const;
    bool Equip(int32 Index, const FAC2CharacterCatalog &Catalog, FString &Error);
    bool Move(int32 Index, int32 Cell, FString &Error);
    int32 Armor(const FAC2CharacterCatalog &Catalog) const;
    int32 Damage(const FAC2CharacterCatalog &Catalog) const;
};
class FAC2CharacterStore
{
  public:
    TArray<FAC2CharacterProfile> Characters;
    FString ActiveId;
    bool Load(const FString &Path, const FAC2CharacterCatalog &Catalog, FString &Error);
    bool Save(const FString &Path, FString &Error) const;
    bool Create(FString Name, uint32 Race, uint32 Sex, float Height, float Build,
                const FAC2CharacterCatalog &Catalog, FString &Error,
                const TMap<uint32, int32> *Appearance = nullptr);
    FAC2CharacterProfile *Active();
    static bool ValidateName(FString &Name, FString &Error);
    static bool Validate(const FAC2CharacterProfile &Profile, const FAC2CharacterCatalog &Catalog);
};
