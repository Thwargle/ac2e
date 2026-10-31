#include "AC2CharacterData.h"

int32 FAC2CharacterRandom::Index(int32 Count)
{
    if (Count <= 1) return 0;
    // Random::rand / RollDice(long,long). Use unsigned intermediate arithmetic
    // to reproduce native wrapping without signed-overflow undefined behaviour.
    Seed = int32(uint32(Seed) * 40014u - uint32(Seed / 53668) * 2147483563u);
    if (Seed < 0) Seed += 2147483563;
    Second = int32(uint32(Second) * 40692u - uint32(Second / 52774) * 2147483399u);
    if (Second < 0) Second += 2147483399;
    const int32 Slot = Previous / 67108862;
    Previous = Shuffle[Slot] - Second;
    Shuffle[Slot] = Seed;
    if (Previous < 1) Previous += 2147483562;
    const double Roll = FMath::Min(Previous * 4.656613057391769e-10, .99999988);
    return int32(Count * Roll);
}

void FAC2CharacterDraft::Randomize(const FAC2AvatarChoice& Avatar, uint32 Mask, FAC2CharacterRandom& Random)
{
    // CharacterGenSystem::RandomizeCGR (0096000f): visit physique bits in order,
    // and choose an inclusive integer index into the DAT's valid-values list.
    for (uint32 Type = 1; Type <= 4096; Type <<= 1)
    {
        if (!(Mask & Type)) continue;
        if (Type == 1) Build = Random.Index(Avatar.Builds.Num());
        else if (Type == 2) Height = Random.Index(Avatar.Heights.Num());
        else if (const auto* Values = Avatar.Choices.Find(Type); Values && !Values->IsEmpty())
            Appearance.Add(Type, Random.Index(Values->Num()));
    }
}

FAC2CharacterDraft FAC2CharacterDraft::Create(const FAC2AvatarChoice& Avatar, FAC2CharacterRandom& Random)
{
    FAC2CharacterDraft Draft;
    Draft.Appearance = Avatar.Defaults();
    Draft.Randomize(Avatar, MAX_uint32, Random);
    // SetCGRDefaults (0096000e) randomizes first, then applies ceil(N/2)-1
    // to build and height. The middle native value is not necessarily 1.0.
    Draft.Build = FMath::Max(0, (Avatar.Builds.Num() - 1) / 2);
    Draft.Height = FMath::Max(0, (Avatar.Heights.Num() - 1) / 2);
    return Draft;
}
