#pragma once
#include "CoreMinimal.h"
#include "Misc/Base64.h"

// Retail has 100 bindings. Extra visible rows on page ten are disabled padding,
// not another set of bindings and not a wrap back to page one.
struct FAC2ShortcutGrid
{
    static constexpr int32 Columns=10, Count=100, MaxRows=5;
    static int32 Rows(int32 Value) {return FMath::Clamp(Value,1,MaxRows);}
    static int32 Index(int32 Page,int32 VisibleSlot,int32 Height)
    {
        if(Page<0 || Page>=Count/Columns || VisibleSlot<0 || VisibleSlot>=Rows(Height)*Columns)return INDEX_NONE;
        const int32 Result=Page*Columns+VisibleSlot;
        return Result<Count?Result:INDEX_NONE;
    }
};

// UIElement_ListBox::StartAnimation / Global_Loop. Coordinates are native
// pixels; changing the goal mid-animation resumes from the current view over
// half the configured duration instead of jumping back to a page boundary.
struct FAC2ShortcutScroll
{
    int32 From=0,Goal=0;
    double Started=0,Duration=0;
    bool Moving(double Now) const {return Duration>0 && Now<Started+Duration && From!=Goal;}
    int32 View(double Now) const
    {
        if(!Moving(Now))return Goal;
        const double Fraction=FMath::Clamp((Now-Started)/Duration,0.,1.);
        return From+int32(double(Goal-From)*Fraction);
    }
    void Snap(int32 Value) {From=Goal=Value;Duration=0;}
    void Start(int32 Value,double Now,double Seconds)
    {
        if(Value==Goal)return;
        if(!FMath::IsFinite(Seconds) || Seconds<=0){Snap(Value);return;}
        const bool Retarget=Moving(Now);const int32 Current=View(Now);
        Goal=Value;Duration=Seconds;
        From=Retarget?Current*2-Goal:Current;
        Started=Now-(Retarget?Duration*.5:0.);
    }
};

// Retail ShortcutInfo distinguishes skill enums from 64-bit item instances.
// Keep instance IDs as decimal strings through local storage and bridge JSON.
struct FAC2Shortcut
{
    uint32 Skill=0;
    FString Item;
    uint32 Recipe=0;
    FString Text;
    uint32 Enumeration=0;
    bool IsEnumeration=false;
    bool IsEmpty() const {return !Skill && Item.IsEmpty() && !Recipe && Text.IsEmpty() && !IsEnumeration;}
    static FAC2Shortcut ForEnumeration(uint32 Value) {FAC2Shortcut Result;Result.IsEnumeration=true;Result.Enumeration=Value;return Result;}
    static FAC2Shortcut ForSkill(uint32 Id) {return {Id,{}};}
    static FAC2Shortcut ForItem(const FString& Id) {return {0,Id};}
    static FAC2Shortcut ForRecipe(uint32 Id) {return {0,{},Id};}
    static FAC2Shortcut ForText(const FString& Value)
    {
        if(Value.IsEmpty() || Value.Len()>512 || Value.TrimStartAndEnd().IsEmpty())return {};
        for(TCHAR C:Value)if(C<32 || (C>=127 && C<=159))return {};
        FAC2Shortcut Result;Result.Text=Value;return Result;
    }
    bool operator==(const FAC2Shortcut& Other) const {return Skill==Other.Skill && Item==Other.Item && Recipe==Other.Recipe && Text==Other.Text && Enumeration==Other.Enumeration && IsEnumeration==Other.IsEnumeration;}
    // UTF-8 Base64 keeps commas, quotes and non-ASCII text out of the INI's
    // comma-delimited slot list. Existing skill/item/recipe encodings stay valid.
    FString Encode() const {return IsEnumeration?FString::Printf(TEXT("e:%u"),Enumeration):!Text.IsEmpty()?TEXT("t:")+FBase64::Encode(Text):!Item.IsEmpty()?TEXT("i:")+Item:Recipe?FString::Printf(TEXT("r:%u"),Recipe):Skill?FString::Printf(TEXT("s:%u"),Skill):TEXT("0");}
    static FAC2Shortcut Decode(const FString& Text)
    {
        if(Text.StartsWith(TEXT("t:"))){
            FString Value;const FString Encoded=Text.Mid(2);
            if(Encoded.Len()>2732 || !FBase64::Decode(Encoded,Value) || FBase64::Encode(Value)!=Encoded)return {};
            return ForText(Value);
        }
        const bool Item=Text.StartsWith(TEXT("i:"));
        const bool Recipe=Text.StartsWith(TEXT("r:"));
        const bool Enumeration=Text.StartsWith(TEXT("e:"));
        const FString Digits=Item||Recipe||Enumeration||Text.StartsWith(TEXT("s:"))?Text.Mid(2):Text;
        if(Digits.IsEmpty() || Digits.Len()>20)return {};
        uint64 Value=0;
        for(TCHAR C:Digits){
            if(C<TEXT('0') || C>TEXT('9') || Value>(MAX_uint64-uint64(C-TEXT('0')))/10)return {};
            Value=Value*10+uint64(C-TEXT('0'));
        }
        if((!Value && !Enumeration) || (!Item && Value>MAX_uint32))return {};
        if(Enumeration)return ForEnumeration(uint32(Value));
        if(Recipe && Value>>24!=0x6f)return {};
        return Item?ForItem(FString::Printf(TEXT("%llu"),Value)):Recipe?ForRecipe(uint32(Value)):ForSkill(uint32(Value));
    }
};
