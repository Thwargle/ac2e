#include "AC2SkillData.h"
#include "AC2AssetReader.h"
#include "AC2CharacterData.h"
namespace {
bool Root(FAC2AssetReader& R,uint32 Id,TArray<uint32>& Fields,int32& References)
{
    if(R.Word()!=Id || R.Word()!=uint32(R.Data.Num()-12))return false;
    References=R.Count(4,8192);
    for(int32 I=0;I<References;++I)if(R.Word()!=uint32(I))return false;
    if(R.Word()!=0x5c000100 || R.Word()!=0 || (R.Word()&65535))return false;
    const int32 Count=R.Count(5,256);
    for(int32 I=0;I<Count;++I)Fields.Add(R.Word());
    R.Take((Count+3)&~3);return R.Good;
}
}
bool FAC2SkillData::Open(const FString& Directory)
{
    Records.Reset();Panels.Reset();Cache.Reset();PanelCache.Reset();Curves.Reset();
    FString Error;TArray<uint8> Bytes;
    if(!Portal.Open(Directory/TEXT("portal.dat"),1,Error) || !Local.Open(Directory/TEXT("local_English.dat"),3,Error) ||
        !Portal.Read(0x700016e2,Bytes,Error))return false;
    FAC2AssetReader R(MoveTemp(Bytes));TArray<uint32> Fields;int32 Refs;
    if(!Root(R,0x700016e2,Fields,Refs) || Fields!=TArray<uint32>({0x41000011,MAX_uint32,1}) ||
        R.Word()!=0x44000100 || R.Word()!=0 || R.Word()!=0xffff0011)return false;
    const uint32 Count=R.Word()&0xffffff;if(Count>8190 || Count!=uint32(Refs-2))return false;
    TMap<uint32,uint32> RefToSkill;
    for(uint32 I=0;I<Count;++I){const uint32 Skill=R.Word(),Ref=R.Word();RefToSkill.Add(Ref,Skill);}
    if(R.Word()!=0)return false;
    for(int32 I=2;I<Refs;++I){if(R.Word()!=0x24000100)return false;const uint32 Did=R.Word();
        if(Did>>24!=0x70 || !RefToSkill.Contains(I))return false;Records.Add(RefToSkill[I],Did);}
    if(!R.Good || !Portal.Read(0x70001976,Bytes,Error))return false;
    FAC2AssetReader P(MoveTemp(Bytes));Fields.Reset();
    if(!Root(P,0x70001976,Fields,Refs) || Fields!=TArray<uint32>({0x4100001d,MAX_uint32,1}) ||
        P.Word()!=0x44000100 || P.Word()!=0 || P.Word()!=0xffff0011)return false;
    const uint32 N=P.Word()&0xffffff;if(N>256 || N!=uint32(Refs-2))return false;
    TMap<uint32,uint32> Keys;
    for(uint32 I=0;I<N;++I){const uint32 Key=P.Word(),Ref=P.Word();if(Keys.Contains(Ref))return false;Keys.Add(Ref,Key);}
    if(P.Word()!=0)return false;
    for(int32 I=2;I<Refs;++I){if(P.Word()!=0x24000100)return false;const uint32 Did=P.Word();
        if(Did>>24!=0x70 || !Keys.Contains(I))return false;Panels.Add(Keys[I],Did);}
    return P.Good;
}

uint32 FAC2SkillData::RootPanel(uint32 Race,int32 Tab)
{
    // Native racial root enums; the panels themselves, including every node,
    // coordinate, parent and specialization link, come from registry 70001976.
    const uint32* Roots=nullptr;
    static const uint32 Human[]={0x0c,0x0d,0x0b,0x0e},Lugian[]={0x17,0x18,0x16,0x19},
        Tumerok[]={0x22,0x23,0x21,0x24},Empyrean[]={0x3b,0x3c,0x3a,0x3d},Drudge[]={0x49,0x49,0x49,0x4a};
    switch(Race){case 1:Roots=Human;break;case 2:Roots=Lugian;break;case 4:Roots=Tumerok;break;
        case 8:Roots=Drudge;break;case 16:Roots=Empyrean;break;default:return 0;}
    return Tab>=0&&Tab<4?0x80000000|Roots[Tab]:0;
}
const FAC2SkillPanel& FAC2SkillData::Panel(uint32 Id)
{
    if(const auto* Found=PanelCache.Find(Id))return *Found;
    auto& Out=PanelCache.Add(Id);FString Error;TArray<uint8> Bytes;const uint32 Did=Panels.FindRef(Id);
    if(!Did || !Portal.Read(Did,Bytes,Error))return Out;
    FAC2AssetReader R(MoveTemp(Bytes));TArray<uint32> F;int32 Refs;
    if(!Root(R,Did,F,Refs)||F.Num()!=6||F[0]!=Id||F[2]!=1||Refs>256)return Out;
    // StreamPack StringInfo. Formatted descriptions can contain typed arguments;
    // consume their native layout even when the UI uses the skill's own text.
    TFunction<FString(int32)> String=[&](int32 Depth)->FString{
        if(Depth>8){R.Good=false;return {};}
        const uint32 Key=R.Word(),Table=R.Word();const uint16 Count=R.Short(),Literal=R.Short();
        if(Count>64){R.Good=false;return {};}
        if(Literal)R.String(2);
        FString Text=!Literal?AC2ReadUIString(Local,Table,Key):FString();
        for(uint16 I=0;I<Count&&R.Good;++I){R.Word();const uint16 Type=R.Short();R.Align();
            switch(Type){case 0:case 1:case 8:break;case 2:case 6:R.Take(4);break;
                case 3:case 4:case 7:R.Take(10);break;case 5:R.Take(16);break;
                case 9:String(Depth+1);break;case 10:case 12:R.Take(8);break;
                case 11:case 13:R.Take(14);break;default:R.Good=false;break;}R.Align();}
        R.Align();return Text;
    };
    TMap<uint32,uint32> NodeKeys;
    for(int32 Ref=1;Ref<Refs&&R.Good;++Ref){
        if(R.Word()!=0x44000100||R.Word()!=0)return Out;
        const uint32 Kind=R.Word();
        if(Kind==0xffff0011&&Ref==1){
            const uint32 N=R.Word()&0xffffff;if(N>128)return Out;
            for(uint32 I=0;I<N;++I){const uint32 Key=R.Word(),Index=R.Word();if(NodeKeys.Contains(Index))return Out;NodeKeys.Add(Index,Key);}
            if(R.Word()!=0)return Out;
        }else if(Kind==0xffff00ae&&NodeKeys.Contains(Ref)){
            FAC2SkillNode N;N.Id=R.Word();N.Kind=R.Word();N.Name=String(0);N.Description=String(0);N.Icon=R.Word();
            N.Cell.X=int32(R.Word());N.Cell.Y=int32(R.Word());
            if(N.Id!=NodeKeys[Ref] || N.Cell.X<0||N.Cell.Y<0||N.Cell.X>64||N.Cell.Y>64)return Out;
            int32 Count=R.Count(4,128);for(int32 I=0;I<Count;++I)N.Parents.Add(R.Word());
            Count=R.Count(4,128);for(int32 I=0;I<Count;++I)N.Dependencies.Add(R.Word());
            const uint32 Passive=R.Word(),Untrainable=R.Word(),Tail=R.Word();
            if(Passive>1||Untrainable>1||Tail>1)return Out;
            N.Passive=Passive!=0;N.Untrainable=Untrainable!=0;
            // 7230 adds a boolean before HeapObject's zero; 7160 ends directly
            // with that zero. Both are bounded by the next graph object marker.
            if(R.Left()>=4&&AC2::U32(R.Data.GetData()+R.Pos)==0){if(R.Word()!=0)return Out;}
            else if(Tail!=0)return Out;
            Out.Nodes.Add(MoveTemp(N));
        }else if(Kind==0xffff006a){
            const FString Text=String(0);if(R.Word()!=0)return Out;
            if(uint32(Ref)==F[3])Out.Description=Text;
            if(uint32(Ref)==F[5]){Out.Name=Text;while(Out.Name.EndsWith(TEXT("]"))){
                int32 Start=INDEX_NONE;if(!Out.Name.FindLastChar('[',Start)||Out.Name.Len()-Start>8)break;Out.Name.LeftInline(Start);Out.Name.TrimEndInline();}}
        }else return Out;
    }
    Out.Icon=F[4];Out.Valid=R.Good&&Out.Nodes.Num()==NodeKeys.Num()&&R.Left()==4&&R.Word()==(Did&0xffffff);
    if(!Out.Valid)Out.Nodes.Reset();return Out;
}
const FAC2SkillInfo& FAC2SkillData::Get(uint32 Id)
{
    if(const auto* Found=Cache.Find(Id))return *Found;
    auto& Info=Cache.Add(Id);Info.Name=FString::Printf(TEXT("Skill %X"),Id);
    FString Error;TArray<uint8> Bytes;const uint32 Did=Records.FindRef(Id);
    if(!Did || !Portal.Read(Did,Bytes,Error))return Info;
    FAC2AssetReader R(MoveTemp(Bytes));TArray<uint32> Fields;int32 Refs;
    if(!Root(R,Did,Fields,Refs) || Fields.Num()<24 || Fields[0]!=Id)return Info;
    Info.Race=Fields[2];Info.Icon=Fields[21]>>24==0x41?Fields[21]:0;
    Info.Level=FMath::Max(1u,Fields[3]);Info.Credits=Fields[20];Info.Cap=Fields[17];Info.Curve=Fields[15];Info.Kind=Fields[23];
    if(Fields.Num()>30)Info.Weapons=Fields[30];
    uint32 Last=FMath::Max(Fields[19],Fields[22]);
    for(int32 Index:{4,6})if(Fields[Index]!=MAX_uint32)Last=FMath::Max(Last,Fields[Index]);
    if(Last>=uint32(Refs) || Last>128)return Info;
    // The bounded prefix contains native StringInfo, dependency hashes and
    // external references. Stop at an unsupported object instead of guessing offsets.
    for(uint32 I=1;I<=Last && R.Good;++I){
        const uint32 Kind=R.Word();if(Kind==0x24000100){R.Word();continue;}
        if(Kind!=0x44000100 || R.Word()!=0)break;
        const uint32 Native=R.Word();
        if(Native==0xffff006a){
            const uint32 Key=R.Word(),Table=R.Word(),Flags=R.Word();if(Flags || R.Word()!=0)break;
            if(I==Fields[19] || I==Fields[22]){
                const auto Text=AC2ReadUIString(Local,Table,Key);
                if(!Text.IsEmpty()){
                    if(I==Fields[22]){
                        Info.Name=Text;
                        // StringTable noun annotations are grammar metadata,
                        // not part of a skill's visible display name.
                        while(Info.Name.EndsWith(TEXT("]"))){
                            int32 Start=INDEX_NONE;if(!Info.Name.FindLastChar('[',Start))break;
                            if(Info.Name.Len()-Start>8)break;
                            Info.Name.LeftInline(Start);Info.Name.TrimEndInline();
                        }
                    }else Info.Description=Text;
                }
            }
        }else if(Native==0xffff0001 || Native==0xffff0011){
            const uint32 Count=R.Word()&0xffffff;if(Count>256)return Info;
            for(uint32 J=0;J<Count;++J){const uint32 Skill=R.Word(),Rank=R.Word();
                if(I==Fields[4] || I==Fields[6]){
                    if(!Skill || Skill>65535 || Rank>10000)return Info;
                    (I==Fields[4]?Info.Forbidden:Info.Required).Add(Skill,Rank);
                }}
            if(R.Word()!=0)return Info;
        }else break;
        if(I==Last)Info.Valid=R.Good;
    }
    return Info;
}

TArray<uint32> FAC2SkillData::All(uint32 Race)
{
    TArray<uint32> Result;
    for(const auto& E:Records){const auto& S=Get(E.Key);if(S.Valid && S.Race==Race && S.Credits>0)Result.Add(E.Key);}
    Result.Sort([&](uint32 A,uint32 B){const auto& X=Get(A);const auto& Y=Get(B);return X.Level!=Y.Level?X.Level<Y.Level:A<B;});
    return Result;
}
const TArray<uint64>& FAC2SkillData::Curve(uint32 Id)
{
    if(const auto* Found=Curves.Find(Id))return *Found;
    auto& Out=Curves.Add(Id);FString Error;TArray<uint8> Bytes;
    if((Id>>24)!=0x6f || !Portal.Read(Id,Bytes,Error))return Out;
    FAC2AssetReader R(MoveTemp(Bytes));TArray<uint32> F;int32 Refs;
    if(!Root(R,Id,F,Refs) || F.Num()!=3 || Refs!=3 || R.Word()!=0x44000100 || R.Word()!=0 || R.Word()!=0xffff0037)return Out;
    const int32 Count=R.Count(8,10001);if(Count<2 || Count!=int32(F[1]+1))return Out;
    for(int32 I=0;I<Count;++I){const uint64 Low=R.Word(),High=R.Word();const uint64 X=Low|(High<<32);
        if((I==0 && X) || (I && X<Out.Last())){Out.Reset();return Out;}Out.Add(X);}
    if(!R.Good || R.Word()!=0)Out.Reset();return Out;
}
int32 FAC2SkillData::Rank(uint32 Id,uint64 Xp)
{
    const auto& C=Curve(Get(Id).Curve);int32 Level=0;
    while(Level+1<C.Num() && C[Level+1]<=Xp)++Level;return Level;
}
uint64 FAC2SkillData::NextCost(uint32 Id,uint64 Xp)
{
    const auto& C=Curve(Get(Id).Curve);const int32 Next=Rank(Id,Xp)+1;
    return Next<=int32(Get(Id).Cap) && C.IsValidIndex(Next) && C[Next]>Xp?C[Next]-Xp:0;
}
