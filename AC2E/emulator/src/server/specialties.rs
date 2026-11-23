//! AC2E rules for workshop infusions, fellowship healing and a summoned ward.
use super::{invalid, item_service_transaction, other, Peer, Store};
use crate::{
    content::Object,
    encounters::{Encounters, Over},
    geometry::Geometry,
    inventory::{Command, Inventory, Item, Location},
    inventory_request::Metadata,
    movement::Placement,
    progression,
    scene::{Delivery, Scene},
    script,
};
use std::{collections::BTreeMap, io};

pub(super) const CHARM: u32 = 0x47002fbb;
pub(super) const ROCK: u32 = 0x47003541;
const FOCUS_VISUAL: u32 = 0x1f0001cf;
const SPELLBINDING_WORKSHOP: u32 = 0x6f002ee7;
const AREA_RANGE: f32 = 6.0;
const AREA_TARGETS: usize = 3;
const WARD_COST: u32 = 20;
const WARD_DURATION: u64 = 15_000;
const WARD_INTERVAL: u64 = 3_000;
const WARD_HEALTH: u32 = 5;
const FIEND_RADIUS: f32 = 0.4;
const FIEND_HEIGHT: f32 = 1.5;

fn infusion(
    inventory: &Inventory,
    charm: u64,
    weapon: u64,
    output: u64,
) -> io::Result<Vec<Command>> {
    let original = carried(inventory, weapon)
        .ok_or_else(|| invalid("Examine a crafted weapon in your backpack, or a crafted piece of armor."))?;
    let (kind, base) = crate::totem::base(original)
        .ok_or_else(|| invalid("Examine a crafted weapon in your backpack, or a crafted piece of armor."))?;
    let focus = carried(inventory, charm).ok_or_else(|| invalid("Carry the spellbinding component in your backpack."))?;
    if !(focus.definition == CHARM && focus.visual == FOCUS_VISUAL) && crate::totem::charge(focus) != Some(kind) {
        return Err(invalid("The totem does not match this equipment."));
    }
    let mut roll = original.roll.unwrap();
    let rating = kind.rating(&mut roll);
    if *rating != base || base > 1000 - crate::totem::BONUS {
        return Err(invalid("This item cannot hold another infusion."));
    }
    *rating += crate::totem::BONUS;
    let mut result = original.clone();
    result.id = output;
    result.roll = Some(roll);
    exchange(inventory, &[charm, weapon], result)
}

fn carried(inventory: &Inventory, id: u64) -> Option<&Item> {
    inventory.items().find(|item| {
        item.id == id
            && item.quantity == 1
            && item.stack_limit == 1
            && matches!(item.location, Location::Bag(_))
    })
}

fn exchange(inventory: &Inventory, inputs: &[u64], mut result: Item) -> io::Result<Vec<Command>> {
    let mut plan = Vec::with_capacity(inputs.len() + 1);
    let mut bag = inventory.clone();
    for &item in inputs {
        let consume = Command::ConsumeDense { item };
        bag = bag.apply(&consume).map_err(other)?;
        plan.push(consume);
    }
    result.location = Location::Bag(
        bag.free_slot()
            .ok_or_else(|| invalid("No backpack slot."))?,
    );
    plan.push(Command::Grant(result));
    Ok(plan)
}

pub(super) enum Binding {
    Infuse(u64),
    Extract,
    Transfer(Option<u64>),
}

pub(super) fn bind_weapon(
    peer: &mut Peer,
    character: u64,
    action: Binding,
    weapon: u64,
    store: &mut Store,
    scene: &mut Scene,
    metadata: &BTreeMap<u32, Option<Metadata>>,
    now: u64,
) -> io::Result<Vec<Delivery>> {
    if peer.portal.is_some()
        || peer.recall.is_some()
        || peer.item_motion.is_some()
        || peer
            .last_inventory_ms
            .is_some_and(|last| now.saturating_sub(last) < 1000)
    {
        return Err(invalid("Wait for your current action before spellbinding."));
    }
    if !matches!(action, Binding::Transfer(_)) {
        crate::professions::near_workshop(scene, character, SPELLBINDING_WORKSHOP)?;
    }
    let inventory = &store.characters[&character].inventory;
    let output = store.next_item_id()?;
    let (kind, _) = carried(inventory, weapon).and_then(crate::totem::base)
        .ok_or_else(|| invalid("Examine a crafted weapon in your backpack, or a crafted piece of armor."))?;
    let armor = kind == crate::totem::Infusion::Armor;
    let (items, message) = match action {
        Binding::Infuse(charm) => (
            infusion(inventory, charm, weapon, output)?,
            if armor { "Spellbinding infused the armor with +3 armor." } else { "Spellbinding infused the weapon with +3 damage." },
        ),
        Binding::Extract => {
            if carried(inventory, weapon).and_then(crate::totem::infused).is_none() {
                return Err(invalid("Examine infused equipment in your backpack. Extraction destroys the source item."));
            }
            (exchange(inventory, &[weapon], crate::totem::charged_totem(output, kind))?,
                if armor { "The source armor was destroyed. Its +3 armor infusion is now in a totem." }
                else { "The source weapon was destroyed. Its +3 damage infusion is now in a totem." })
        }
        Binding::Transfer(selected) => {
            let totem = selected.or_else(|| inventory.items().find(|item|
                matches!(item.location, Location::Bag(_)) && crate::totem::charge(item) == Some(kind)
            ).map(|i| i.id)).ok_or_else(|| invalid("Carry a matching charged totem."))?;
            (infusion(inventory, totem, weapon, output)?,
                if armor { "The totem transferred its +3 armor infusion to the armor." }
                else { "The totem transferred its +3 damage infusion to the weapon." })
        }
    };
    let text = script::chat(message, 1).map_err(other)?;
    let updates = item_service_transaction(
        peer,
        character,
        &items,
        &[],
        vec![text],
        store,
        scene,
        metadata,
    )?;
    peer.last_inventory_ms = Some(now);
    Ok(updates)
}

pub(super) struct Area<'a> {
    pub caster: u64,
    pub center: u64,
    pub health: u32,
    pub vigor: u32,
    pub members: Vec<u64>,
    pub scene: &'a Scene,
    pub store: &'a Store,
    pub geometry: Option<&'a mut Geometry>,
}

impl Area<'_> {
    pub(super) fn recipients(self) -> Vec<u64> {
        let Some(center) = self.scene.player_placement(self.center) else {
            return Vec::new();
        };
        if (self.health == 0 && self.vigor == 0) || !self.members.contains(&self.center) {
            return Vec::new();
        }
        let mut geometry = self.geometry;
        let mut nearby: Vec<_> = self
            .members
            .into_iter()
            .filter_map(|member| {
                if member == self.caster
                    || member == self.center
                    || !self.scene.can_observe(self.caster, member)
                    || !self.scene.can_observe(self.center, member)
                {
                    return None;
                }
                let state = &self.store.characters.get(&member)?.progression;
                if !state.alive()
                    || (self.health == 0 || state.health >= state.max_health())
                        && (self.vigor == 0 || state.vigor >= state.max_vigor())
                {
                    return None;
                }
                let point = self.scene.player_placement(member)?;
                let distance = center.distance(point)?;
                if distance > AREA_RANGE {
                    return None;
                }
                if !geometry
                    .as_deref_mut()
                    .map_or(center.cell == point.cell, |world| {
                        world.clear(center, point, 1.0)
                    })
                {
                    return None;
                }
                Some((distance, member))
            })
            .collect();
        nearby.sort_by(|a, b| a.0.total_cmp(&b.0).then_with(|| a.1.cmp(&b.1)));
        nearby
            .into_iter()
            .take(AREA_TARGETS)
            .map(|(_, id)| id)
            .collect()
    }
}

pub(super) fn instant(health: u32, vigor: u32) -> Over {
    Over {
        effect: 0,
        health,
        vigor,
        period_ms: 1000,
        duration_ms: 0,
    }
}

enum Summon {
    Healing { focus: u64 },
    SandFiend { rank: u16, target: Option<u64> },
}
struct Ward {
    summon: Summon,
    expires: u64,
    next: u64,
    follow_at: Option<u64>,
}

#[derive(Default)]
pub(super) struct Wards(BTreeMap<u64, Ward>);

fn ward_id(owner: u64) -> u64 {
    crate::content::SUMMON_NAMESPACE | (owner & 0x0000ffffffffffff)
}

pub(super) fn focus_in_bag(inventory: &Inventory, definition: u32) -> Option<u64> {
    inventory
        .items()
        .find(|item| {
            item.definition == definition
                && item.visual == FOCUS_VISUAL
                && matches!(item.location, Location::Bag(_))
        })
        .map(|item| item.id)
}

fn owns_focus(inventory: &Inventory, focus: u64) -> bool {
    inventory.items().any(|item| {
        item.id == focus
            && item.definition == ROCK
            && item.visual == FOCUS_VISUAL
            && matches!(item.location, Location::Bag(_))
    })
}

impl Wards {
    pub(super) fn prepare_fiend(&self, owner: u64, scene: &mut Scene, geometry: Option<&mut Geometry>) -> io::Result<Object> {
        if self.0.contains_key(&owner) || self.0.len() >= 64 {
            return Err(invalid("No summon slot is available."));
        }
        let feet = scene.player_placement(owner).ok_or_else(|| invalid("Wait for the world to load."))?;
        let world = geometry.ok_or_else(|| invalid("Summoning a Sand Fiend requires world geometry."))?;
        let point = [[1.5, 0., 0.], [-1.5, 0., 0.], [0., 1.5, 0.], [0., -1.5, 0.]].into_iter()
            .filter_map(|offset| feet.translated(offset))
            .find_map(|candidate| {
                let floor = world.ground(candidate, 1., 2.)?;
                (world.clear(feet, floor, 0.75) && world.body_fits(floor, FIEND_RADIUS, FIEND_HEIGHT)).then_some(floor)
            }).ok_or_else(|| invalid("There is no room for a Sand Fiend here."))?;
        // Both supported AC2E archives resolve NPCTable_PetSandFiend to this entity and model.
        let object = Object::summon(ward_id(owner), owner, 0x47001302, 0x1f000752, point, "Sand Fiend")?;
        scene.prepare_cell(point.cell)?;
        scene.check_content_live(&[], std::slice::from_ref(&object))?;
        Ok(object)
    }

    pub(super) fn place_fiend(&mut self, owner: u64, object: Object, rank: u16, scene: &mut Scene, now: u64) -> io::Result<Vec<Delivery>> {
        let updates = scene.insert_content_live(object, true)?;
        self.0.insert(owner, Ward { summon: Summon::SandFiend { rank, target: None },
            expires: u64::MAX, next: now, follow_at: Some(now) });
        Ok(updates)
    }

    pub(super) fn dismiss(&mut self, owner: u64, scene: &mut Scene, encounters: &mut Encounters) -> io::Result<Vec<Delivery>> {
        if !self.0.contains_key(&owner) { return Err(invalid("You have no active summon.")); }
        let updates = scene.remove_content_live(ward_id(owner))?;
        self.0.remove(&owner);
        encounters.clear_pet_strike(owner);
        Ok(updates)
    }

    pub(super) fn summon(
        &mut self,
        peer: &mut Peer,
        owner: u64,
        focus: u64,
        mobile: bool,
        store: &mut Store,
        scene: &mut Scene,
        geometry: Option<&mut Geometry>,
        now: u64,
    ) -> io::Result<Vec<Delivery>> {
        if mobile && geometry.is_none() {
            return Err(invalid("Companions require native world geometry."));
        }
        if self.0.contains_key(&owner) {
            return Err(invalid("Your ward is already active."));
        }
        if self.0.len() >= 64 {
            return Err(invalid("There are too many wards in the world."));
        }
        if peer.portal.is_some() || peer.recall.is_some() || peer.item_motion.is_some() {
            return Err(invalid("Finish your current action before summoning."));
        }
        let saved = &store.characters[&owner];
        if !saved.progression.alive() || !owns_focus(&saved.inventory, focus) {
            return Err(invalid(
                "Summoning requires a living player with a Pet Rock in the backpack.",
            ));
        }
        let feet = scene
            .player_placement(owner)
            .ok_or_else(|| invalid("Wait for the world to load."))?;
        let angle = f32::from(feet.heading) * std::f32::consts::TAU / 256.0;
        let mut geometry = geometry;
        let point = geometry
            .as_deref_mut()
            .and_then(|world| {
                let offset = feet.translated([angle.cos(), angle.sin(), 0.0])?;
                let point = world.ground(offset, 0.5, 2.0)?;
                world.clear(feet, point, 0.5).then_some(point)
            })
            .unwrap_or(feet);
        let point = if mobile {
            let lifted = point
                .translated([0., 0., 0.6])
                .ok_or_else(|| invalid("No room for your companion."))?;
            geometry
                .and_then(|world| world.hover_step(point.translated([0., 0., 0.2])?, lifted))
                .ok_or_else(|| invalid("No room for your companion."))?
        } else {
            point
        };
        let id = ward_id(owner);
        let name = if mobile {
            "Healing Companion"
        } else {
            "Healing Ward"
        };
        let object = Object::summon(id, owner, ROCK, FOCUS_VISUAL, point, name)?;
        scene.prepare_cell(point.cell)?;
        scene.check_content_live(&[], std::slice::from_ref(&object))?;
        let charge = progression::Command::SpendVigor(WARD_COST);
        let state = saved.progression.apply(&charge).map_err(other)?;
        let mut events = state.private_changes(&saved.progression);
        let message = format!(
            "You summon a {name} for 15 seconds. Stay within six meters to receive healing."
        );
        events.push(script::chat(&message, 1).map_err(other)?);
        peer.validate_scene_batch(&events)?;
        store.progression_batch(&peer.session.account, owner, &[charge])?;
        let mut updates = scene.insert_content_live(object, true)?;
        self.0.insert(
            owner,
            Ward {
                summon: Summon::Healing { focus },
                expires: now + WARD_DURATION,
                next: now + WARD_INTERVAL,
                follow_at: mobile.then_some(now),
            },
        );
        updates.extend(scene.player_stats(owner, &state));
        peer.queue_ordered_batch(events)?;
        Ok(updates)
    }

    pub(super) fn tick(
        &mut self,
        scene: &mut Scene,
        store: &Store,
        encounters: &mut Encounters,
        mut geometry: Option<&mut Geometry>,
        ready: impl Fn(u64) -> bool,
        now: u64,
        world_ms: u64,
    ) -> io::Result<Vec<Delivery>> {
        let mut deliveries = Vec::new();
        let mut removed = Vec::new();
        for (&owner, ward) in &mut self.0 {
            let id = ward_id(owner);
            let Some(mut point) = scene.content_placement(id) else {
                removed.push((owner, false));
                continue;
            };
            let Some(feet) = scene.player_placement(owner) else {
                removed.push((owner, true));
                continue;
            };
            let state = store
                .characters
                .get(&owner)
                .filter(|c| {
                    !c.deleted && c.progression.alive() && match ward.summon {
                        Summon::Healing { focus } => owns_focus(&c.inventory, focus),
                        Summon::SandFiend { .. } => c.skills.usable(0x109),
                    }
                })
                .map(|c| &c.progression);
            if now >= ward.expires
                || !ready(owner)
                || !scene.can_observe(owner, id)
                || state.is_none()
            {
                removed.push((owner, true));
                continue;
            }
            if let Summon::SandFiend { rank, target } = &mut ward.summon {
                let Some(world) = geometry.as_deref_mut() else { removed.push((owner, true)); continue };
                if let Some(enemy) = encounters.attacking_target(owner) { *target = Some(enemy); }
                let enemy = target.and_then(|enemy| {
                    let at = scene.content_placement(enemy)?;
                    (encounters.living_target(enemy) && scene.combat_distance(owner, enemy).is_some_and(|d| d <= 30.)
                        && world.clear(feet, at, 1.)).then_some((enemy, at))
                });
                *target = enemy.map(|(enemy, _)| enemy);
                let goal = enemy.map_or(feet, |(_, at)| at);
                let last = ward.follow_at.get_or_insert(now);
                let elapsed = now.saturating_sub(*last);
                if elapsed >= 100 {
                    *last = now;
                    if let Some(next) = crate::encounters::approach(point, goal, 1.5, 3. * elapsed.min(250) as f32 / 1000.)
                        .and_then(|step| world.walk_body(point, step, FIEND_RADIUS, FIEND_HEIGHT)) {
                        deliveries.extend(scene.move_creature(id, next, world_ms)?);
                        point = next;
                    }
                }
                if let Some((enemy, at)) = enemy {
                    if now >= ward.next && point.distance(at).is_some_and(|d| d <= 3.) && world.clear(point, at, 1.) {
                        encounters.queue_pet_strike(owner, id, enemy, *rank);
                        ward.next = now.saturating_add(2000);
                    }
                }
                continue;
            }
            if let (Some(last), Some(world)) = (&mut ward.follow_at, geometry.as_deref_mut()) {
                if now.saturating_sub(*last) >= 100 {
                    let distance = 2.8 * now.saturating_sub(*last).min(250) as f32 / 1000.0;
                    *last = now;
                    if let Some(next) = follow(world, point, feet, distance) {
                        deliveries.extend(scene.move_creature(id, next, world_ms)?);
                        point = next;
                    }
                }
            }
            if now < ward.next {
                continue;
            }
            // Skip missed pulses after a hitch. Never extend the ward's lifetime.
            ward.next += ((now - ward.next) / WARD_INTERVAL + 1) * WARD_INTERVAL;
            if state.is_some_and(|s| s.health < s.max_health())
                && in_ward_range(feet, point, geometry.as_deref_mut())
                && encounters.admit_effects(0, 0, 1, 0).is_ok()
            {
                encounters.mend(owner, 0, owner, instant(WARD_HEALTH, 0), 0, now)?;
            }
        }
        for (owner, placed) in removed {
            if placed {
                deliveries.extend(scene.remove_content_live(ward_id(owner))?);
            }
            self.0.remove(&owner);
            encounters.clear_pet_strike(owner);
        }
        Ok(deliveries)
    }
}

fn follow(
    world: &mut Geometry,
    point: Placement,
    owner: Placement,
    max_step: f32,
) -> Option<Placement> {
    let [x, y, z] = point.offset_to(owner.translated([0., 0., 0.6])?)?;
    let distance = x.hypot(y);
    let step = (distance - 1.5).min(max_step);
    let (dx, dy) = if step > 0.02 {
        (x * step / distance, y * step / distance)
    } else {
        (0., 0.)
    };
    let dz = z.clamp(-0.15, 0.15);
    if dx == 0. && dy == 0. && dz.abs() <= 0.02 {
        return None;
    }
    let mut next = point.translated([dx, dy, dz])?;
    if step > 0.02 {
        next.heading = (x.atan2(y).rem_euclid(std::f32::consts::TAU)
            * (255. / std::f32::consts::TAU))
            .round() as u16 as u8;
    }
    world.hover_step(point, next)
}

fn in_ward_range(owner: Placement, point: Placement, geometry: Option<&mut Geometry>) -> bool {
    owner
        .distance(point)
        .is_some_and(|distance| distance <= AREA_RANGE)
        && geometry.map_or(owner.cell == point.cell, |world| {
            world.clear(owner, point, 1.0)
        })
}
