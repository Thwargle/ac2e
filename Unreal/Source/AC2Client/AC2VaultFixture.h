#pragma once
#include "AC2CharacterData.h"

// Authored Vault fixtures only. The two clients assign different entity DIDs
// to the same named scripts. Draw their PhysObj templates (with native scale).
namespace AC2VaultFixture
{
struct FProps
{
    static constexpr uint32 Portal = 0x47000297; // GenericVault's physical entity in both archives
    uint32 Shard = 0, Scout = 0;
};

inline FProps Resolve(const FString& Directory, FAC2CharacterCatalog& Catalog)
{
    struct FRevision { uint32 Shard, ShardPhys, Scout, ScoutPhys; };
    for (const FRevision& R : {FRevision{0x47002448, 0x47002449, 0x4700157f, 0x4700158f},
                               FRevision{0x47002f9e, 0x47002f93, 0x47002e01, 0x47002e02}})
    {
        Catalog.LoadNetworkItem(Directory, R.Shard);
        Catalog.LoadNetworkItem(Directory, R.Scout);
        const auto* Shard = Catalog.Items.Find(R.Shard);
        const auto* Scout = Catalog.Items.Find(R.Scout);
        if (Shard && Shard->Name == TEXT("Master Osteth Menhir Shard") && Shard->NativeClass == 0x13a3 &&
            Scout && Scout->Name == TEXT("Advance Shelter Scout") && Scout->NativeClass == 0x13a4)
            return {R.ShardPhys, R.ScoutPhys};
    }
    return {};
}
}
