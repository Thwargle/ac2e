#include "AC2RadarData.h"
#include "AC2AssetReader.h"
#include "Misc/Paths.h"

AC2Radar::FLook AC2Radar::Resolve(FLook Look,int64 Health,TOptional<uint32> Shape,TOptional<bool> Selectable,TOptional<uint32> Color)
{
    if(Health==0 && Look.Category==Monsters){
        const auto Previous=Look;Look=DefaultLook(Items);Look.Selectable=Previous.Selectable;
        if(Previous.Shape==0x4000000b)Look.Shape=Previous.Shape;
    }
    if(Shape.IsSet())Look.Shape=*Shape;
    if(Selectable.IsSet())Look.Selectable=*Selectable;
    if(Color.IsSet()){const uint32 V=*Color;
        Look.Color=FLinearColor(float((V>>16)&255)/255,float((V>>8)&255)/255,float(V&255)/255,float(V>>24)/255);}
    return Look;
}

AC2Radar::FLook AC2Radar::DefaultLook(uint32 Category)
{
    FLook L;L.Category=Category;
    switch(Category){
    case Monsters:L.Shape=0x40000004;L.Color=FLinearColor(1,0,0);break;
    case NPCs:L.Shape=0x40000007;L.Color=FLinearColor(1,1,0);break;
    case Players:L.Shape=0x40000002;break;
    case Portals:L.Shape=0x40000005;L.Color=FLinearColor(.75f,.27f,1);break;
    case Lifestones:L.Shape=0x40000006;L.Color=FLinearColor(0,.4f,1);break;
    case Items:L.Shape=0x40000001;break;
    }
    return L;
}
TArray<FIntPoint> AC2Radar::Pixels(uint32 Shape,bool Selected)
{
    TArray<FIntPoint> Result;
    if(Shape<0x40000001 || Shape>0x4000000a)return Result;
    const uint32 S=Shape&255;
    for(int32 Y=-1;Y<=1;++Y)for(int32 X=-1;X<=1;++X){
        const bool Center=X==0&&Y==0,Edge=(X==0)!=(Y==0),Corner=X!=0&&Y!=0;
        if((S==1&&Center)||S==2||(S==3&&(Center||Edge))||(S==4&&(Center||Corner))||
           (S==5&&!Center)||(S==6&&(X==-1||(X==0&&Y==1)))||(S==7&&Edge)||
           (S==8&&Corner)||(S==9&&(Y==-1||X==0))||(S==10&&((Y==-1&&X!=0)||Y==1)))
            Result.Add(FIntPoint(X,Y));
    }
    if(Selected)for(int32 I=-2;I<=2;++I){
        Result.Add(FIntPoint(I,-3));Result.Add(FIntPoint(I,3));
        Result.Add(FIntPoint(-3,I));Result.Add(FIntPoint(3,I));
    }
    return Result;
}
bool FAC2RadarData::DecodeClasses(TConstArrayView<uint8> Bytes)
{
    Parents.Reset();FAC2AssetReader R(Bytes);
    if(R.Word()!=0x56000005 || R.Word()!=0x1a015342 || R.Word()!=15)return false;
    TArray<FIntPoint> Exports;
    while(R.Good && R.Left()){
        const uint32 Tag=R.Word(),Length=R.Word();const int32 Start=R.Pos;
        if(!R.Take(Length))return false;
        if(Tag==4){
            FAC2AssetReader E(MakeArrayView(R.Data.GetData()+Start,int32(Length)));
            const int32 Count=E.Count(32);
            for(int32 I=0;I<Count && E.Good;++I){
                E.String();E.String();E.Take(12);const uint32 Id=E.Word(),Parent=E.Word();
                Exports.Add(FIntPoint(Id,Parent));
                const int32 Fields=E.Count(9);
                for(int32 J=0;J<Fields && E.Good;++J){E.String();E.Take(5);E.Align();}
                const int32 Functions=E.Count(24);
                for(int32 J=0;J<Functions && E.Good;++J){E.String();E.Take(16);const int32 N=E.Count();E.Take(int64(N)*4);}
            }
            if(!E.End())return false;
        }
        R.Align();
    }
    if(!R.End() || Exports.IsEmpty())return false;
    for(const auto& E:Exports)if(Exports.IsValidIndex(E.Y))Parents.Add(E.X,Exports[E.Y].X);
    return !Parents.IsEmpty();
}
uint32 FAC2RadarData::Category(uint32 Package) const
{
    // Native package IDs, not a list of individual creature/item definitions.
    TSet<uint32> Seen;
    while(Package && !Seen.Contains(Package)){
        Seen.Add(Package);
        switch(Package){
        case 4457:return AC2Radar::Monsters; // MonsterTemplate::InqRadarLook
        case 5028:return AC2Radar::NPCs; // NPCTemplate
        case 6820:return AC2Radar::Players; // CPlayer
        case 403:return AC2Radar::Portals;
        case 400:return AC2Radar::Lifestones;
        case 6825:case 391:return AC2Radar::Items; // CItem / GameplayContainer
        }
        Package=Parents.FindRef(Package);
    }
    return 0;
}
bool FAC2RadarData::Open(const FString& Directory,FString& Error)
{
    Looks.Reset();Types.Reset();Parents.Reset();TArray<uint8> Bytes;
    if(!Portal.Open(FPaths::Combine(Directory,TEXT("portal.dat")),1,Error) ||
       !Portal.Read(0x56000005,Bytes,Error) || !DecodeClasses(Bytes) ||
       !Portal.Read(0x34000000,Bytes,Error))return false;
    FAC2AssetReader R(MoveTemp(Bytes));if(R.Word()!=0x34000000)return false;R.Take(8);
    const int32 Names=R.Count(6);for(int32 I=0;I<Names && R.Good;++I){R.Word();R.String();}
    const uint32 Count=R.Word()&65535;if(Count>uint32(R.Left()/24))return false;
    for(uint32 I=0;I<Count;++I){const uint32 Key=R.Word(),Type=R.Word();Types.Add(Key,Type&65535);R.Take(16);}
    return R.Good;
}
AC2Radar::FLook FAC2RadarData::Look(uint32 Entity)
{
    if(const auto* L=Looks.Find(Entity))return *L;
    TSet<uint32> Seen;const auto L=ReadEntity(Entity,Seen);Looks.Add(Entity,L);return L;
}
AC2Radar::FLook FAC2RadarData::ReadEntity(uint32 Entity,TSet<uint32>& Seen)
{
    if(Entity>>24!=0x47)return AC2Radar::DefaultLook(Category(Entity));
    if(Seen.Contains(Entity)||Seen.Num()>=32)return {};
    Seen.Add(Entity);TArray<uint8> Bytes;FString Error;
    if(!Portal.Read(Entity,Bytes,Error))return {};
    FAC2AssetReader R(MoveTemp(Bytes));const uint32 Flags=R.Word();if(Flags&~511u)return {};
    if(Flags&1)R.Word();if(Flags&2)R.Word();if(Flags&4)R.Take(8);
    const uint32 Base=(Flags&8)?R.Word():0;
    auto L=ReadEntity(Base,Seen);
    if(Flags&16)R.Take(64);if(Flags&32)R.Take(12);
    if(Flags&128){
        const int32 Groups=R.Count(8);
        for(int32 G=0;G<Groups && R.Good;++G){
            R.Word();const int32 Count=R.Count(8);
            for(int32 I=0;I<Count && R.Good;++I){
                const uint32 Key=R.Word(),Type=Types.FindRef(Key);uint32 V=0;
                switch(Type){
                case 1:case 2:case 3:case 5:case 7:case 8:case 11:V=R.Word();break;
                case 4:R.Take(12);break;
                case 6:R.String();break;
                case 9:R.Take(44);break;
                case 10:{R.Take(8);const uint32 F=R.Word();if(F>>16)R.String(2);if(F&65535)R.Good=false;break;}
                case 13:V=R.Word();R.Word();break;
                case 14:R.Take(32);break;
                default:R.Good=false;break;
                }
                if(Key==0x41000292)L.Shape=V; // RadarBlip -> quality 0x268
                if(Key==0x410000c8)L.Selectable=V!=0;
                if(Key==0x410002bb)L.Color=FLinearColor(float((V>>16)&255)/255,float((V>>8)&255)/255,float(V&255)/255,float(V>>24)/255);
            }
        }
    }
    if(Flags&64)R.Word();if(Flags&256)R.String();
    return R.End()?L:AC2Radar::FLook();
}
