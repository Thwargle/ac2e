#include "AC2ShortcutKeys.h"
#include "AC2AssetReader.h"
#include "Misc/Paths.h"

namespace
{
FKey KeyboardKey(uint32 Semantic)
{
    const FKey Digits[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine,EKeys::Zero};
    if(Semantic>=0x81000402 && Semantic<=0x8100040b)return Digits[Semantic-0x81000402];
    if(Semantic==0x8100040c)return EKeys::Hyphen;
    if(Semantic==0x8100040d)return EKeys::Equals;
    return EKeys::Invalid;
}
}

bool FAC2ShortcutKeys::IsAction(uint32 Action)
{
    return (Action>=0x4100002b && Action<=0x41000040) || (Action>=0x4100005f && Action<=0x41000068);
}
bool FAC2ShortcutKeys::Decode(TConstArrayView<uint8> Bytes,uint32 Id,bool Shift,bool Control,TArray<FAC2ShortcutKey>& Out)
{
    Out.Reset();FAC2AssetReader R(Bytes);
    if((Id>>24)!=0x1d || R.Word()!=Id)return false;
    // CKeyMap::StreamPack: DID, packed name, GUID, action/semantic/name list.
    // Names are not needed to dispatch; skip the same bounded packed strings.
    R.String();R.Take(16);const int32 Count=R.Count(12);
    TArray<FAC2ShortcutKey> Parsed;TSet<uint32> Seen;
    for(int32 I=0;I<Count && R.Good;++I){
        const uint32 Action=R.Word(),Semantic=R.Word();R.String();
        if(Seen.Contains(Semantic))continue;Seen.Add(Semantic); // retail keeps the first mapping
        if(!IsAction(Action))continue;
        const FKey Key=KeyboardKey(Semantic);if(!Key.IsValid())return false;
        Parsed.Add({FInputChord(Key,Shift,Control,false,false),Action});
    }
    if(!R.End())return false;
    Out=MoveTemp(Parsed);return true;
}
bool FAC2ShortcutKeys::Load(const FString& Directory,TArray<FAC2ShortcutKey>& Out,FString& Error)
{
    Out.Reset();FAC2DatArchive Portal;
    if(!Portal.Open(FPaths::Combine(Directory,TEXT("portal.dat")),1,Error))return false;
    const uint32 Group=Portal.ReadPublicEnumId(Portal.GetHeader().MasterMap,0x4000000f,Error);
    TArray<FAC2ShortcutKey> Parsed;
    // gmClient::InitKeymap defaults: main, control, shift. Resolve the enum
    // group rather than assuming that every installed build uses the same DIDs.
    for(uint32 I=0;I<3;++I){
        const uint32 Id=Portal.ReadPublicEnumId(Group,0x41000001+I,Error);
        TArray<uint8> Bytes;TArray<FAC2ShortcutKey> Map;
        if(!Id || !Portal.Read(Id,Bytes,Error) || !Decode(Bytes,Id,I==2,I==1,Map)){
            Error=FString::Printf(TEXT("Cannot decode default shortcut keymap enum %08X (DID %08X)"),0x41000001+I,Id);return false;
        }
        Parsed.Append(MoveTemp(Map));
    }
    Out=MoveTemp(Parsed);return true;
}
