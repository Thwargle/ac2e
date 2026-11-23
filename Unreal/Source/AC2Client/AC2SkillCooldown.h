#pragma once
#include "CoreMinimal.h"

// Legions gmCWInterface::InqSkillTimeLeft: compare the skill's own reset
// with shared recovery and use the duration of the longer remaining timer.
// Timestamps use the synchronized server clock, never world/game delta time.
struct FAC2SkillCooldown
{
    double Started=-1,Duration=0;
    double Remaining(double Now) const
    {
        if(!FMath::IsFinite(Started) || !FMath::IsFinite(Duration) || !FMath::IsFinite(Now)
            || Started<0 || Duration<=0 || Duration>86400 || Now<0)return 0;
        return FMath::Clamp(Started+Duration-Now,0.,Duration);
    }
    static FAC2SkillCooldown Longest(const FAC2SkillCooldown& Skill,const FAC2SkillCooldown& Shared,double Now)
    {return Skill.Remaining(Now)>Shared.Remaining(Now)?Skill:Shared;}
    float Position(double Now) const
    {return Duration>0?float(1.-Remaining(Now)/Duration):1.f;}
};
