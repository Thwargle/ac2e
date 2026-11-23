# Native inventory request fixtures

The three `Inv*Desc.txt` files contain hexadecimal packed values produced by
the stock Legions DAT's native WLib constructors, independently of AC2E's
decoder and Unreal network bridge. The constructor oracle outputs were
`bulk-stock-oracle.json` and `move-desc-oracle.json` from the Icefire
interaction investigation.

Only the VM-allocated reference graph ID was normalized to 1, and the packed
value type (`5`) was prefixed to match a script-call argument. Item, container,
and player identities are synthetic. These fixtures exercise native unsigned
field tags without weakening field, owner, price, or replay validation.
