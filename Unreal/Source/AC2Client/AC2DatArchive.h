#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformFile.h"

// AC2 DiskFileInfo_t / BTEntry. Deliberately separate from ACE's AC1 reader.
struct FAC2DatHeader
{
    uint32 BlockSize = 0;
    uint32 FileSize = 0;
    uint32 DataSet = 0;
    uint32 Subset = 0;
    uint32 Root = 0;
    uint32 MasterMap = 0;
    uint32 EngineVersion = 0;
    uint32 GameVersion = 0;
};

struct FAC2DatEntry
{
    uint32 Id = 0;
    uint32 Offset = 0;
    uint32 Size = 0;
    uint32 Date = 0;
};

// Single-owner, synchronous archive. Runtime lookup touches only one B-tree path.
// Full enumeration is reserved for inspection. No DAT bytes are ever modified.
class FAC2DatArchive
{
  public:
    bool Open(const FString &Path, uint32 ExpectedDataSet, FString &Error);
    bool Find(uint32 Id, FAC2DatEntry &Entry, FString &Error);
    bool Read(uint32 Id, TArray<uint8> &Bytes, FString &Error);
    uint32 ReadPublicEnumId(uint32 Map, uint32 Key, FString &Error);
    bool Enumerate(TArray<FAC2DatEntry> &Entries, FString &Error);
    const FAC2DatHeader &GetHeader() const
    {
        return Header;
    }
    const FString& GetPath() const { return Filename; }

  private:
    struct FNode
    {
        TArray<FAC2DatEntry> Entries;
        TArray<uint32> Children;
    };
    bool ReadChain(uint32 Offset, uint32 Size, TArray<uint8> &Bytes, FString &Error);
    bool ReadNode(uint32 Offset, FNode &Node, FString &Error);
    bool ValidBlock(uint32 Offset) const;
    TUniquePtr<IFileHandle> File;
    FAC2DatHeader Header;
    FString Filename;
};

namespace AC2
{
uint32 U32(const uint8 *Bytes);
float F32(const uint8 *Bytes);
FString ResolveDatDirectory(const FString &Configured);
bool ParseBlock(const FString &Text, uint32 &Block);
} // namespace AC2
