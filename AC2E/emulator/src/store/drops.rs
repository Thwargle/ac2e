//! Ground items share the character journal: removal/publication and pickup/grant
//! are single durable transactions, including across a server restart.
use super::*;
use crate::inventory::{Command, Inventory, Item, Location};
pub const MAX_DROPS: usize = 512;
// AC2E ground-item retention. The retail lifetime for player drops is unknown.
const GROUND_LIFETIME_SECONDS: u64 = 15 * 60;

#[derive(Debug, Clone, PartialEq)]
pub struct DroppedItem {
    pub item: Item,
    pub placement: Placement,
    // Legacy wd1 records had no timestamp. Their first maintenance pass grants
    // a full lifetime and journals it so subsequent restarts never renew it.
    expires_at: Option<u64>,
}
impl DroppedItem {
    pub fn id(&self) -> u64 { 0x8180000000000000 | (self.item.id & 0x0000ffffffffffff) }
}
fn remove(inventory: &Inventory, item: u64) -> io::Result<(Inventory, Item)> {
    let owned = inventory.items().find(|v| v.id == item).cloned()
        .ok_or_else(|| invalid("Item is not in your backpack."))?;
    if !matches!(owned.location, Location::Bag(_)) {return Err(invalid("Unequip the item before dropping it."));}
    let next = inventory.apply(&Command::ConsumeStackDense { item, quantity: owned.quantity }).map_err(io::Error::other)?;
    Ok((next, owned))
}
fn grant(inventory: &Inventory, drop: &DroppedItem, new_id: u64) -> io::Result<(Inventory, Item)> {
    let mut item = drop.item.clone();
    item.id = new_id;
    item.location = Location::Bag(inventory.free_slot().ok_or_else(|| invalid("Your backpack is full."))?);
    let next = inventory.apply(&Command::Grant(item.clone())).map_err(io::Error::other)?;
    Ok((next, item))
}
impl Store {
    pub fn drop_candidate(&self, account: &str, character: u64, item: u64, placement: Placement) -> io::Result<DroppedItem> {
        let saved=self.characters.get(&character).filter(|c|!c.deleted && c.account==account && c.progression.alive())
            .ok_or_else(|| invalid("You must be alive to drop items."))?;
        if !placement.valid() || self.drops.len()>=MAX_DROPS {return Err(invalid("No room for more ground items."));}
        let (_,item)=remove(&saved.inventory,item)?;
        if item.definition==crate::warder_notebook::NOTEBOOK {
            return Err(invalid("The Warder Scout's Notebook belongs to your quest and cannot be dropped or traded."));
        }
        if item.definition==crate::esper_camp::BONE {
            return Err(invalid("The Drudge Cadre Bone belongs to your quest and cannot be dropped or traded."));
        }
        let expires_at = crate::recovered_quests::now().saturating_add(GROUND_LIFETIME_SECONDS);
        let dropped=DroppedItem {item,placement,expires_at:Some(expires_at)};
        if self.drops.contains_key(&dropped.id()) {return Err(invalid("Item is already on the ground."));}
        Ok(dropped)
    }
    pub fn drop_item(&mut self, account: &str, character: u64, item: u64, placement: Placement) -> io::Result<DroppedItem> {
        let dropped=self.drop_candidate(account,character,item,placement)?;
        let (next,_)=remove(&self.characters[&character].inventory,item)?;
        self.append_record(&format!("wd2\t{character}\t{item}\t{}\t{}",encode_placement(placement),dropped.expires_at.unwrap()))?;
        self.characters.get_mut(&character).unwrap().inventory=next;
        self.drops.insert(dropped.id(),dropped.clone());
        Ok(dropped)
    }
    pub fn pickup_candidate(&self, account: &str, character: u64, id: u64) -> io::Result<Item> {
        let saved=self.characters.get(&character).filter(|c|!c.deleted && c.account==account && c.progression.alive())
            .ok_or_else(||invalid("You must be alive to pick up items."))?;
        let now = crate::recovered_quests::now();
        let dropped=self.drops.get(&id).filter(|d| !d.expires_at.is_some_and(|end| end <= now))
            .ok_or_else(||invalid("Item is no longer on the ground."))?;
        grant(&saved.inventory,dropped,self.next_item_id()?).map(|(_,item)|item)
    }
    pub fn pickup_item(&mut self, account: &str, character: u64, id: u64) -> io::Result<Item> {
        let item=self.pickup_candidate(account,character,id)?;
        let next=self.characters[&character].inventory.apply(&Command::Grant(item.clone())).map_err(io::Error::other)?;
        self.append_record(&format!("wp1\t{character}\t{id}\t{}",item.id))?;
        self.characters.get_mut(&character).unwrap().inventory=next;
        self.drops.remove(&id);
        Ok(item)
    }
    pub fn expire_ground_items(&mut self, now: u64) -> io::Result<Vec<u64>> {
        if self.drops.values().all(|d| d.expires_at.is_some_and(|end| end > now)) {
            return Ok(Vec::new());
        }
        let legacy_deadline = now.saturating_add(GROUND_LIFETIME_SECONDS);
        self.append_record(&format!("wx1\t{now}\t{legacy_deadline}"))?;
        Ok(apply_expiration(&mut self.drops, now, legacy_deadline))
    }
}
fn apply_expiration(drops: &mut BTreeMap<u64, DroppedItem>, now: u64, legacy_deadline: u64) -> Vec<u64> {
    let mut expired = Vec::new();
    drops.retain(|id, dropped| {
        if *dropped.expires_at.get_or_insert(legacy_deadline) <= now {
            expired.push(*id);
            return false;
        }
        true
    });
    expired
}
pub(super) fn replay(fields: &[&str], characters: &mut BTreeMap<u64,Character>, drops: &mut BTreeMap<u64,DroppedItem>) -> io::Result<()> {
    if fields[0] == "wx1" {
        let now = fields[1].parse::<u64>().map_err(|_| invalid("Invalid expiration timestamp"))?;
        let deadline = fields[2].parse::<u64>().map_err(|_| invalid("Invalid migration deadline"))?;
        if deadline <= now { return Err(invalid("Invalid migration deadline")); }
        apply_expiration(drops, now, deadline);
        return Ok(());
    }
    let character=fields[1].parse::<u64>().map_err(|_|invalid("Invalid ground-item owner"))?;
    let id=fields[2].parse::<u64>().map_err(|_|invalid("Invalid ground-item identity"))?;
    let next_id=characters.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1);
    let saved=characters.get_mut(&character).filter(|c|!c.deleted && c.progression.alive()).ok_or_else(||invalid("Invalid ground-item character"))?;
    if matches!(fields[0], "wd1" | "wd2") {
        let placement=decode_placement(fields[3])?;
        let (next,item)=remove(&saved.inventory,id)?;
        let expires_at = fields.get(4).map(|s| s.parse::<u64>().map_err(|_| invalid("Invalid ground-item deadline"))).transpose()?;
        if expires_at == Some(0) { return Err(invalid("Invalid ground-item deadline")); }
        let dropped=DroppedItem {item,placement,expires_at};
        if drops.len()>=MAX_DROPS || drops.contains_key(&dropped.id()) {return Err(invalid("Invalid ground-item publication"));}
        saved.inventory=next;drops.insert(dropped.id(),dropped);
    } else {
        let allocated=fields[3].parse::<u64>().map_err(|_|invalid("Invalid pickup allocation"))?;
        if Some(allocated)!=next_id {return Err(invalid("Invalid pickup identity progression"));}
        let dropped=drops.get(&id).ok_or_else(||invalid("Ground item already taken"))?;
        saved.inventory=grant(&saved.inventory,dropped,allocated)?.0;
        drops.remove(&id);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn expiration_migrates_legacy_drops_once_and_removes_them_durably() {
        let dir = std::env::temp_dir().join(format!("ac2-expiring-drops-{}",std::process::id()));
        let mut store=Store::open(&dir).unwrap();
        let character=store.create("owner","Owner").unwrap().id;
        let potion=crate::item_services::potion(store.next_item_id().unwrap(),0);
        store.inventory_command("owner",character,Command::Grant(potion.clone())).unwrap();
        store.append_record(&format!("wd1\t{character}\t{}\t{}",potion.id,encode_placement(Placement::HUMAN_START))).unwrap();
        drop(store);
        let mut store=Store::open(&dir).unwrap();
        let legacy=*store.drops.keys().next().unwrap();
        let now=crate::recovered_quests::now();
        assert!(store.expire_ground_items(now-GROUND_LIFETIME_SECONDS-1).unwrap().is_empty());
        let potion=crate::item_services::potion(store.next_item_id().unwrap(),0);
        store.inventory_command("owner",character,Command::Grant(potion.clone())).unwrap();
        let fresh=store.drop_item("owner",character,potion.id,Placement::HUMAN_START).unwrap();
        store.compact_journal().unwrap();
        drop(store);
        let mut store=Store::open(&dir).unwrap();
        assert!(store.pickup_candidate("owner",character,legacy).is_err());
        assert_eq!(store.expire_ground_items(now).unwrap(),vec![legacy]);
        assert!(store.pickup_candidate("owner",character,fresh.id()).is_ok());
        assert_eq!(store.expire_ground_items(fresh.expires_at.unwrap()).unwrap(),vec![fresh.id()]);
        drop(store);
        let store=Store::open(&dir).unwrap();
        assert!(store.drops.is_empty());
        assert!(store.characters[&character].inventory.items().next().is_none());
        drop(store);
        std::fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn drop_pickup_restart_and_compaction_are_atomic() {
        let dir=std::env::temp_dir().join(format!("ac2-drop-{}",std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let (a,b,ground);
        {
            let mut s=Store::open(&dir).unwrap();
            a=s.create("alice","Alice").unwrap().id;b=s.create("bob","Bob").unwrap().id;
            let item=crate::item_services::potion(s.next_item_id().unwrap(),0);
            s.inventory_command("alice",a,Command::Grant(item.clone())).unwrap();
            assert!(s.drop_item("bob",a,item.id,Placement::HUMAN_START).is_err());
            ground=s.drop_item("alice",a,item.id,Placement::HUMAN_START).unwrap().id();
            assert_eq!(s.characters[&a].inventory.items().count(),0);
            assert!(s.drop_item("alice",a,item.id,Placement::HUMAN_START).is_err());
            s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();assert!(s.drops.contains_key(&ground));
            let item=s.pickup_item("bob",b,ground).unwrap();
            assert_eq!(item.definition,crate::item_services::POTION_DEFINITION);
            assert!(s.pickup_item("alice",a,ground).is_err());
            s.compact_journal().unwrap();
        }
        {let s=Store::open(&dir).unwrap();assert!(s.drops.is_empty());assert_eq!(s.characters[&b].inventory.items().count(),1);}
        std::fs::remove_dir_all(dir).unwrap();
    }
}
