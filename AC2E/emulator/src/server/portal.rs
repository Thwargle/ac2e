//! Four retail object-portal captures show the same two avatar effects and the
//! portal's Activate effect, a roughly 1.5 s departure, then StopFX(Enter_World)
//! when the owner acknowledges that the destination is rendered.
use super::*;
use crate::combat_visual::ENTER_WORLD_FX;
use crate::scene::{Delivery, Scene};

const PORTAL_USE: u32 = 0x410004fa;
const ACTIVATE: u32 = 0x41000044;
const TRANSIT_FLAG: u32 = 0x8212001f;
const ENTER_SPACE: u32 = 0x82120020;
const MOVEMENT_LOCK: u32 = 0x82120021;
const EXIT_SPACE: u32 = 0x8212001b;

fn flag(function: u32, active: bool) -> io::Result<Vec<u8>> {
    script::private_call(function, &script::integer(u32::from(active))).map_err(other)
}

fn space(function: u32) -> io::Result<Vec<u8>> {
    // Tag 4 is PackLFloat. All four recorded portals pass a double of zero.
    let mut arguments = 4u32.to_le_bytes().to_vec();
    arguments.extend(0f64.to_le_bytes());
    script::private_call(function, &arguments).map_err(other)
}

pub(super) fn exit_space() -> io::Result<Vec<Vec<u8>>> {
    Ok(vec![space(EXIT_SPACE)?, flag(TRANSIT_FLAG, false)?])
}

pub(super) enum Passage {
    Route { route: crate::portals::Route, distance: f32 },
    Glyph { item: u64, completion: Vec<u8> },
}

pub(super) enum Transit {
    ActivatingShard { character: u64, instance: u16, target: u64, passage: Passage, due: u64 },
    Departing { character: u64, instance: u16, target: u64, passage: Passage, due: u64 },
    AwaitingScene { character: u64, instance: u16 },
}

pub(super) fn begin(
    peer: &mut Peer, character: u64, instance: u16, target: u64, passage: Passage,
    scene: &mut Scene, now: u64,
) -> io::Result<Vec<Delivery>> {
    if peer.portal.is_some() || peer.recall.is_some() || peer.pending_skill.is_some() || peer.item_motion.is_some() {
        return Err(invalid("Wait for your current action to finish before using a portal."));
    }
    // Prepare the independent portal visual first; if the target disappeared,
    // publishing nothing leaves the avatar and its visual stamp untouched.
    let activation = scene.content_effect(target, ACTIVATE)?;
    if crate::starters::shard_route(target).is_some() {
        // All three recorded shards play Activate for ten seconds before Act starts.
        peer.portal = Some(Transit::ActivatingShard { character, instance, target, passage, due: now.saturating_add(10_000) });
        return Ok(activation);
    }
    let mut events = depart(peer, character, instance, target, passage, scene, now, false)?;
    events.extend(activation);
    Ok(events)
}

fn depart(peer: &mut Peer, character: u64, instance: u16, target: u64, passage: Passage,
          scene: &mut Scene, now: u64, shard: bool) -> io::Result<Vec<Delivery>> {
    let mut notices = Vec::new();
    if shard {
        let mut act = script::integer(0);
        act.extend(script::integer(1));
        act.extend(script::integer(0x40000021));
        notices.push(script::private_call(0x82130023, &act).map_err(other)?);
        notices.push(flag(MOVEMENT_LOCK, true)?);
        notices.push(flag(TRANSIT_FLAG, true)?);
        notices.push(script::private_call(0x82130021, &crate::ui::menhir_story()).map_err(other)?);
    }
    notices.extend([flag(TRANSIT_FLAG, true)?, flag(MOVEMENT_LOCK, true)?, space(ENTER_SPACE)?]);
    peer.queue_ordered_batch(notices)?;
    let mut events = Vec::new();
    if shard {
        events.extend(player_fx(peer, scene, character, instance, 0x4100000f));
        events.extend(player_fx(peer, scene, character, instance, 0x8100028e));
    }
    events.extend(player_fx(peer, scene, character, instance, PORTAL_USE));
    events.extend(player_fx(peer, scene, character, instance, ENTER_WORLD_FX));
    peer.entering_world_fx = false;
    peer.portal = Some(Transit::Departing { character, instance, target, passage, due: now.saturating_add(1500) });
    Ok(events)
}

pub(super) fn tick(
    peer: &mut Peer, store: &mut Store, scene: &mut Scene,
    metadata: &BTreeMap<u32, Option<crate::inventory_request::Metadata>>,
    encounters: &mut Option<crate::encounters::Encounters>, bootstrap: &mut Option<crate::bootstrap::Bootstrap>,
    now: u64,
) -> io::Result<Vec<Delivery>> {
    if !matches!(peer.portal.as_ref(), Some(Transit::ActivatingShard { due, .. } | Transit::Departing { due, .. }) if now >= *due) {
        return Ok(Vec::new());
    }
    let (activating, character, instance, target, passage) = match peer.portal.take() {
        Some(Transit::ActivatingShard { character, instance, target, passage, .. }) => (true, character, instance, target, passage),
        Some(Transit::Departing { character, instance, target, passage, .. }) => (false, character, instance, target, passage),
        _ => unreachable!(),
    };
    if peer.session.phase != (Phase::InWorld { character })
        || !store.characters.get(&character).is_some_and(|s| s.instance == instance) {
        return Ok(Vec::new());
    }
    if activating {
        let radius = crate::starters::use_radius(target);
        if peer.combat || !store.characters[&character].progression.alive()
            || !scene.combat_distance(character, target).is_some_and(|d| d <= radius) {
            peer.queue_ordered_batch(vec![script::chat("Menhir Shard activation interrupted.", 1).map_err(other)?])?;
            return Ok(Vec::new());
        }
        return depart(peer, character, instance, target, passage, scene, now, true);
    }
    let result = if peer.combat || !store.characters[&character].progression.alive() {
        Err(invalid("Portal travel interrupted."))
    } else {
        match passage {
            Passage::Route { route, distance } =>
                finish_portal(peer, character, target, route, distance, store, scene, encounters, bootstrap, now),
            Passage::Glyph { item, completion } =>
                finish_shelter_glyph(peer, character, item, target, completion, store, scene, metadata, encounters, bootstrap, now),
        }
    };
    match result {
        Ok(events) => {
            peer.portal = Some(Transit::AwaitingScene { character, instance });
            Ok(events)
        }
        Err(error) => {
            let mut messages = exit_space()?;
            messages.push(flag(MOVEMENT_LOCK, false)?);
            messages.push(script::chat(&error.to_string(), 1).map_err(other)?);
            peer.queue_ordered_batch(messages)?;
            Ok(player_fx_action(peer, scene, character, instance, ENTER_WORLD_FX, true))
        }
    }
}

pub(super) fn scene_ready(peer: &mut Peer, owner: u64, scene: &mut Scene) -> io::Result<Vec<Delivery>> {
    match peer.portal {
        Some(Transit::AwaitingScene { character, instance }) if character == owner => {
            peer.queue_ordered_batch(vec![flag(MOVEMENT_LOCK, false)?])?;
            peer.portal = None;
            Ok(player_fx_action(peer, scene, character, instance, ENTER_WORLD_FX, true))
        }
        _ => Ok(Vec::new()),
    }
}
