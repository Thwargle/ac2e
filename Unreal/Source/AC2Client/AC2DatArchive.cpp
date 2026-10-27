#include "AC2DatArchive.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
constexpr uint32 HeaderOffset = 300;
constexpr uint32 NodeBytes = 1228; // 62 links, count, 61 x 16-byte BTEntry
constexpr uint32 MaxRecordBytes = 64 * 1024 * 1024;
bool Fail(FString &Error, const FString &Message)
{
    Error = Message;
    return false;
}
} // namespace

uint32 AC2::U32(const uint8 *B)
{
    return uint32(B[0]) | uint32(B[1]) << 8 | uint32(B[2]) << 16 | uint32(B[3]) << 24;
}

float AC2::F32(const uint8 *B)
{
    uint32 Bits = U32(B);
    float Value;
    FMemory::Memcpy(&Value, &Bits, 4);
    return Value;
}

FString AC2::ResolveDatDirectory(const FString &Configured)
{
    FString Result = Configured;
    FParse::Value(FCommandLine::Get(), TEXT("AC2DatDir="), Result);
    if (FPaths::IsRelative(Result))
        Result = FPaths::Combine(FPaths::ProjectDir(), Result);
    return FPaths::ConvertRelativePathToFull(Result);
}

bool AC2::ParseBlock(const FString &Text, uint32 &Block)
{
    FString Hex = Text;
    Hex.RemoveFromStart(TEXT("0x"), ESearchCase::IgnoreCase);
    if (Hex.IsEmpty() || Hex.Len() > 8)
        return false;
    for (TCHAR C : Hex)
        if (!FChar::IsHexDigit(C))
            return false;
    const uint64 Value = FCString::Strtoui64(*Hex, nullptr, 16);
    if (Value <= 0xFFFF)
        Block = uint32(Value);
    else if ((Value & 0xFFFF) == 0xFFFF || (Value & 0xFFFF) == 0)
        Block = uint32(Value >> 16);
    else
        return false;
    return true;
}

bool FAC2DatArchive::ValidBlock(uint32 Offset) const
{
    return Header.BlockSize && Offset >= 1024 && Offset % Header.BlockSize == 0 &&
           uint64(Offset) + Header.BlockSize <= Header.FileSize;
}

bool FAC2DatArchive::Open(const FString &Path, uint32 ExpectedDataSet, FString &Error)
{
    Error.Reset();
    File.Reset();
    Header = {};
    File.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path, true));
    if (!File)
        return Fail(Error, TEXT("Cannot open DAT: ") + Path);
    uint8 Bytes[380];
    if (File->Size() < sizeof(Bytes) || !File->Read(Bytes, sizeof(Bytes)))
        return Fail(Error, TEXT("Truncated DAT header: ") + Path);
    const uint8 *H = Bytes + HeaderOffset;
    if (AC2::U32(H) != 0x5442)
        return Fail(Error, TEXT("Unsupported DAT layout; expected AC2 magic at byte 300: ") + Path);
    Header.BlockSize = AC2::U32(H + 4);
    Header.FileSize = AC2::U32(H + 8);
    Header.DataSet = AC2::U32(H + 12);
    Header.Subset = AC2::U32(H + 16);
    Header.Root = AC2::U32(H + 32);
    Header.MasterMap = AC2::U32(H + 48);
    Header.EngineVersion = AC2::U32(H + 52);
    Header.GameVersion = AC2::U32(H + 56);
    if (Header.BlockSize < 256 || Header.BlockSize > 65536 || !FMath::IsPowerOfTwo(Header.BlockSize) ||
        Header.FileSize != uint64(File->Size()) || Header.FileSize % Header.BlockSize != 0 || !ValidBlock(Header.Root))
        return Fail(Error, TEXT("Invalid DAT size, block size, or B-tree root: ") + Path);
    if (Header.DataSet != ExpectedDataSet)
        return Fail(Error, FString::Printf(TEXT("DAT dataset %u does not match expected %u: %s"), Header.DataSet,
                                           ExpectedDataSet, *Path));
    FNode Root;
    return ReadNode(Header.Root, Root, Error);
}

bool FAC2DatArchive::ReadChain(uint32 Offset, uint32 Size, TArray<uint8> &Bytes, FString &Error)
{
    Bytes.Reset();
    if (!File || Size > MaxRecordBytes || Size > Header.FileSize)
        return Fail(Error, TEXT("DAT record exceeds read budget or archive is closed"));
    TArray<uint8> Result;
    Result.Reserve(Size);
    TSet<uint32> Visited;
    while (Result.Num() < int64(Size))
    {
        if (!ValidBlock(Offset) || Visited.Contains(Offset))
            return Fail(Error, FString::Printf(TEXT("Invalid or cyclic DAT sector 0x%08X"), Offset));
        Visited.Add(Offset);
        uint8 Next[4];
        const uint32 Take = FMath::Min(Size - uint32(Result.Num()), Header.BlockSize - 4);
        const int32 Start = Result.AddUninitialized(Take);
        if (!File->Seek(Offset) || !File->Read(Next, 4) || !File->Read(Result.GetData() + Start, Take))
            return Fail(Error, TEXT("Failed to read DAT sector"));
        Offset = AC2::U32(Next);
        // CLBlockAllocator::Load_Data rejects the free-block high bit, even at the tail.
        if (Offset & 0x80000000u)
            return Fail(Error, TEXT("DAT chain references a free block"));
    }
    if (Offset != 0)
        return Fail(Error, TEXT("DAT chain continues past declared record size"));
    Bytes = MoveTemp(Result);
    return true;
}

bool FAC2DatArchive::ReadNode(uint32 Offset, FNode &Node, FString &Error)
{
    Node = {};
    TArray<uint8> Bytes;
    if (!ReadChain(Offset, NodeBytes, Bytes, Error))
        return false;
    const uint32 Count = AC2::U32(Bytes.GetData() + 248);
    if (Count > 61)
        return Fail(Error, TEXT("Invalid B-tree entry count"));
    uint32 Previous = 0;
    for (uint32 I = 0; I < Count; ++I)
    {
        const uint8 *E = Bytes.GetData() + 252 + I * 16;
        FAC2DatEntry Entry{AC2::U32(E), AC2::U32(E + 4), AC2::U32(E + 8), AC2::U32(E + 12)};
        if ((I && Entry.Id <= Previous) || Entry.Size > MaxRecordBytes || (Entry.Size && !ValidBlock(Entry.Offset)) ||
            (!Entry.Size && Entry.Offset))
            return Fail(Error, TEXT("Invalid, unsorted, or oversized B-tree entry"));
        Previous = Entry.Id;
        Node.Entries.Add(Entry);
    }
    if (AC2::U32(Bytes.GetData()) != 0)
    {
        for (uint32 I = 0; I <= Count; ++I)
        {
            uint32 Child = AC2::U32(Bytes.GetData() + I * 4);
            if (!ValidBlock(Child))
                return Fail(Error, TEXT("Invalid B-tree child"));
            Node.Children.Add(Child);
        }
    }
    return true;
}

bool FAC2DatArchive::Find(uint32 Id, FAC2DatEntry &Entry, FString &Error)
{
    Error.Reset();
    Entry = {};
    uint32 Offset = Header.Root;
    TSet<uint32> Visited;
    while (Offset)
    {
        if (Visited.Num() >= 64 || Visited.Contains(Offset))
            return Fail(Error, TEXT("B-tree cycle or excessive depth"));
        Visited.Add(Offset);
        FNode Node;
        if (!ReadNode(Offset, Node, Error))
            return false;
        int32 I = 0;
        while (I < Node.Entries.Num() && Node.Entries[I].Id < Id)
            ++I;
        if (I < Node.Entries.Num() && Node.Entries[I].Id == Id)
        {
            Entry = Node.Entries[I];
            return true;
        }
        Offset = Node.Children.IsEmpty() ? 0 : Node.Children[I];
    }
    // Missing is distinct from corruption: Find returns false with an empty error.
    return false;
}

bool FAC2DatArchive::Read(uint32 Id, TArray<uint8> &Bytes, FString &Error)
{
    Bytes.Reset();
    FAC2DatEntry Entry;
    if (!Find(Id, Entry, Error))
    {
        if (Error.IsEmpty())
            Error = FString::Printf(TEXT("DAT record 0x%08X not found"), Id);
        return false;
    }
    return ReadChain(Entry.Offset, Entry.Size, Bytes, Error);
}

bool FAC2DatArchive::Enumerate(TArray<FAC2DatEntry> &Entries, FString &Error)
{
    Error.Reset();
    Entries.Reset();
    TArray<uint32> Pending{Header.Root};
    TSet<uint32> Seen;
    TSet<uint32> Ids;
    while (!Pending.IsEmpty())
    {
        uint32 Offset = Pending.Pop(EAllowShrinking::No);
        if (Seen.Contains(Offset) || Seen.Num() >= 1000000)
            return Fail(Error, TEXT("Duplicate B-tree node or enumeration budget exceeded"));
        Seen.Add(Offset);
        FNode Node;
        if (!ReadNode(Offset, Node, Error))
            return false;
        for (const FAC2DatEntry &E : Node.Entries)
        {
            if (Ids.Contains(E.Id))
                return Fail(Error, TEXT("Duplicate DAT record ID"));
            Ids.Add(E.Id);
            Entries.Add(E);
        }
        Pending.Append(Node.Children);
    }
    Entries.Sort([](const FAC2DatEntry &A, const FAC2DatEntry &B) { return A.Id < B.Id; });
    return true;
}
