//! Narrow training melee presentation from native 7148 physics serializers.
//! DoMode: 00495420; stamped IID: 004946d0; BehaviorParams: 00679220.
//! The human 44000001 combo table resolves one-hand mode + Melee_Auto1.
//! Damage, range and timing remain the authored encounter owner's authority.
use crate::world::WorldError;

pub const DO_MODE: u32 = 0x10082;
pub const DO_BEHAVIOR: u32 = 0x1007f;
/// PhysicsStory on the victim. StreamPack 006ae550; hook pack 006ad9f0.
pub const DO_STORY: u32 = 0x100a8;
pub const PEACE: u32 = 0x40000001;
pub const ONE_HAND: u32 = 0x4100000a;
pub const MARTIAL_ARTS: u32 = 0x4100000f;
// Legions EnumMapper 23000026 / 23000027.
pub const MISSILE: u32 = 0x41000010;
pub const MAGIC: u32 = 0x41000011;
pub const BOULDER: u32 = 0x41000015;
pub const MAGIC_AUTO: u32 = 0x410000b0;
pub const MISSILE_AUTO: u32 = 0x410000e2;
pub const MELEE_AUTO: u32 = 0x41000089;
pub const MELEE_NORMAL: u32 = 0x41000077;
pub const PICKUP: u32 = 0x41000020;
pub const PUTDOWN: u32 = 0x41000021;
pub const RECALL_CHARGE: u32 = 0x410000bf;
pub const LIFESTONE_RELEASE: u32 = 0x410000d8;
pub const PORTAL_RECALL: u32 = 0x410000d5;
pub const ENTER_WORLD_FX: u32 = 0x41000010;

/// Pyreal's Player.LifestoneRecall requests a zero hold-cycle callback threshold
/// and move-to cancellation. CharTemplate 2F000000 independently classifies
/// Magic_Charge1 as a hold; the body motion map defines its loop boundaries.
pub(crate) fn recall_charge_params(wire: &mut Vec<u8>) {
    for field in [0x33, RECALL_CHARGE, MAGIC, 0, 1] {
        wire.extend(field.to_le_bytes());
    }
}

pub fn recall_behavior(owner: u64, instance: u16, stamp: u16, behavior: u32, stop: bool) -> Result<Vec<u8>, WorldError> {
    let mut wire = prefix(if stop { 0x10080 } else { DO_BEHAVIOR }, owner, instance, stamp)?;
    if stop {
        wire.extend(behavior.to_le_bytes());
    } else if behavior == RECALL_CHARGE {
        recall_charge_params(&mut wire);
    } else {
        wire.extend(3u32.to_le_bytes());
        wire.extend(behavior.to_le_bytes());
        wire.extend(MAGIC.to_le_bytes());
    }
    Ok(wire)
}

/// Native BehaviorParams with an explicit peace pose, using the same shared
/// instance/stamp order as combat. This never grants inventory by itself.
pub fn item_motion(owner:u64,instance:u16,stamp:u16,drop:bool,stop:bool)->Result<Vec<u8>,WorldError> {
    let mut wire=prefix(if stop {0x10080}else{DO_BEHAVIOR},owner,instance,stamp)?;
    if !stop {wire.extend(3u32.to_le_bytes());}
    wire.extend((if drop {PUTDOWN}else{PICKUP}).to_le_bytes());
    if !stop {wire.extend(PEACE.to_le_bytes());}
    Ok(wire)
}

/// Body clip periods from installed DATs (both sexes share item action clips).
/// The contact point is an authored presentation midpoint: no inventory hook
/// survives in these clips. Do not describe it as recovered retail timing.
pub fn item_motion_ms(race:u32,drop:bool)->u64 {
    match (race,drop) {(2,false)=>1459,(2|4,true)=>2000,(4,false)=>1875,(_,false)=>1792,(_,true)=>1750}
}
/// Captured result paired with a nonzero health hook. Not a reconstructed crit table.
pub const HEALTH_RESULT: u32 = 0x40000008;

/// Presentation grace, not combat authority or a loot lifetime. Native Legions
/// death graphs: Idol 03000072 = 2.375s; Drudge 0300002a variants <= 4.375s.
/// Uncatalogued profiles receive room for the 10s humanoid death/hold clips.
pub fn death_presentation_ms(entity: u32) -> u64 {
    match entity {
        crate::quests::TARGET_ENTITY => 3500,
        crate::shelter::DIGGER | crate::shelter::SENTINEL => 5500,
        _ => 12000,
    }
}

#[derive(Clone, Copy)]
pub enum IdolAction {
    Combat,
    Swing,
    Peace,
    Death,
}

/// Exact Idol Spawn profile: BehaviorTable 44000018 and retained fight capture.
/// Return the final shared visual stamp, snapshot mode and fresh wire messages.
pub fn idol(
    owner: u64,
    instance: u16,
    current_stamp: u16,
    action: IdolAction,
) -> Result<(u16, u32, Vec<Vec<u8>>), WorldError> {
    let final_stamp = current_stamp
        .checked_add(if matches!(action, IdolAction::Swing) {
            2
        } else {
            1
        })
        .ok_or(WorldError::LimitExceeded)?;
    let mode = match action {
        IdolAction::Combat | IdolAction::Swing => 0x41000012,
        IdolAction::Peace => PEACE,
        IdolAction::Death => 0x41000005,
    };
    let mut events = Vec::new();
    if matches!(action, IdolAction::Death) {
        let mut wire = prefix(DO_BEHAVIOR, owner, instance, final_stamp)?;
        wire.extend(3u32.to_le_bytes()); // behavior and explicit mode, no captured context.
        wire.extend(0x4100001fu32.to_le_bytes());
        wire.extend(mode.to_le_bytes());
        events.push(wire);
    } else {
        let mut wire = prefix(DO_MODE, owner, instance, current_stamp + 1)?;
        wire.extend(mode.to_le_bytes());
        events.push(wire);
        if matches!(action, IdolAction::Swing) {
            events.push(swing(owner, instance, final_stamp)?);
        }
    }
    Ok((final_stamp, mode, events))
}

fn prefix(opcode: u32, owner: u64, instance: u16, stamp: u16) -> Result<Vec<u8>, WorldError> {
    if [0, u64::MAX].contains(&owner) || instance == 0 || stamp == 0 {
        return Err(WorldError::InvalidValue);
    }
    let mut wire = Vec::with_capacity(24);
    wire.extend(opcode.to_le_bytes());
    wire.extend(owner.to_le_bytes());
    wire.extend(instance.to_le_bytes());
    wire.extend(stamp.to_le_bytes());
    Ok(wire)
}
/// Aunmauri's Speed modifies the same two float stats as rank-one Dash.
pub fn obelisk_speed(owner: u64, instance: u16, stamp: u16, active: bool) -> Result<Vec<Vec<u8>>, WorldError> {
    let velocity: f32 = if active { 1.08 } else { 1.0 };
    let secondary: f32 = if active { 1.05 } else { 1.0 };
    let mut motion = prefix(0x10092, owner, instance, stamp)?;
    motion.extend(velocity.to_le_bytes());
    let private_stat = |key: u32, value: f32| {
        [0x1006du32.to_le_bytes(), key.to_le_bytes(), value.to_le_bytes()].concat()
    };
    Ok(vec![motion, private_stat(0x127, velocity), private_stat(0x128, secondary)])
}

pub fn appearance(owner:u64, instance:u16, stamp:u16, visual:&[u8]) -> Result<Vec<u8>, WorldError> {
    if crate::appearance::visual_len(visual) != Some(visual.len()) {return Err(WorldError::InvalidValue);}
    let mut wire=prefix(0x1009f,owner,instance,stamp)?;
    wire.extend(visual); Ok(wire)
}
/// Legions shared Cast_Attack (23000051 / DBFXTable 1e000056) plus racial
/// impacts in 1e000075. The older named *_cast enums have no DAT FX binding.
/// This uses the native shared attack cue; elemental spell-specific sequencing
/// still requires the original skill effect scripts.
pub fn magic_effects(race:u32) -> Option<(u32,u32)> {
    match race {1=>Some((0x810001cd,0x41000248)),2=>Some((0x810001cd,0x410002e5)),4=>Some((0x810001cd,0x41000391)),_=>None}
}
pub fn effect(owner:u64,instance:u16,stamp:u16,fx:u32) -> Result<Vec<u8>,WorldError> {
    if !matches!(fx>>24,0x40|0x41|0x81) {return Err(WorldError::InvalidValue);}
    let mut wire=prefix(0x100a0,owner,instance,stamp)?;wire.extend(fx.to_le_bytes());wire.extend(1f32.to_le_bytes());Ok(wire)
}
pub fn stop_effect(owner:u64,instance:u16,stamp:u16,fx:u32) -> Result<Vec<u8>,WorldError> {
    if !matches!(fx>>24,0x40|0x41|0x81) {return Err(WorldError::InvalidValue);}
    // ECM_Physics::RecvEvt_StopFX: stamped owner, FX key; no scalar.
    let mut wire=prefix(0x100a1,owner,instance,stamp)?;wire.extend(fx.to_le_bytes());Ok(wire)
}

pub fn mode(owner: u64, instance: u16, stamp: u16, fighting: bool) -> Result<Vec<u8>, WorldError> {
    mode_id(owner,instance,stamp,if fighting { ONE_HAND } else { PEACE })
}
pub fn mode_id(owner: u64, instance: u16, stamp: u16, mode: u32) -> Result<Vec<u8>, WorldError> {
    if ![PEACE,ONE_HAND,MARTIAL_ARTS,MISSILE,MAGIC,BOULDER].contains(&mode) {return Err(WorldError::InvalidValue);}
    let mut wire = prefix(DO_MODE, owner, instance, stamp)?;
    wire.extend(mode.to_le_bytes());
    Ok(wire)
}

/// Player basic-attack presentation. Captured sword attacks use an attacker
/// PhysicsStory, whose native StoryManager creates a fresh behavior context.
/// Damage remains in the existing authoritative encounter/victim update path.
pub fn weapon_attack(owner: u64, instance: u16, stamp: u16, mode: u32) -> Result<Vec<u8>, WorldError> {
    attack_behavior(owner,instance,stamp,match mode {MAGIC=>MAGIC_AUTO,MISSILE|BOULDER=>MISSILE_AUTO,_=>MELEE_AUTO})
}
pub fn attack_behavior(owner:u64,instance:u16,stamp:u16,behavior:u32)->Result<Vec<u8>,WorldError> {
    let mut wire = prefix(DO_STORY, owner, instance, stamp)?;
    wire.extend(0x26u32.to_le_bytes()); // Attacker, behavior, basic-attack flag.
    wire.extend(owner.to_le_bytes());
    wire.extend(0x601u32.to_le_bytes()); // Behavior, propagate, send-event.
    wire.extend(behavior.to_le_bytes());
    wire.extend(1u32.to_le_bytes()); // Propagate to the held weapon.
    wire.extend(0u32.to_le_bytes()); // No interpreted completion callback.
    wire.extend(0u32.to_le_bytes()); // No duplicated damage hooks.
    Ok(wire)
}

/// PhysicsStory's native client attack context pairs a special attack with the
/// local request. StoryManager owns start/end notices for its playing behavior;
/// a story without a behavior completes immediately. It is not a basic attack.
pub fn skill_attack(owner:u64,instance:u16,stamp:u16,behavior:u32,request:u32,skill:u32)->Result<Vec<u8>,WorldError> {
    skill_story(owner,instance,stamp,behavior,Some(request),skill)
}
/// A chat-command-triggered cast has no client skill request to acknowledge.
pub fn skill_story(owner:u64,instance:u16,stamp:u16,behavior:u32,request:Option<u32>,skill:u32)->Result<Vec<u8>,WorldError> {
    let mut wire=prefix(DO_STORY,owner,instance,stamp)?;
    wire.extend((0x12u32 | if behavior!=0 {4}else{0} | if request.is_some() {8} else {0}).to_le_bytes());
    wire.extend(owner.to_le_bytes());
    if behavior!=0 {
        wire.extend(0x601u32.to_le_bytes());
        wire.extend(behavior.to_le_bytes());
        wire.extend(1u32.to_le_bytes()); // Propagate to held equipment.
        wire.extend(0u32.to_le_bytes());
    }
    if let Some(request)=request {wire.extend(request.to_le_bytes());}
    wire.extend(skill.to_le_bytes());
    wire.extend(0u32.to_le_bytes()); // No duplicate damage hooks.
    Ok(wire)
}

pub fn swing(owner: u64, instance: u16, stamp: u16) -> Result<Vec<u8>, WorldError> {
    let mut wire = prefix(DO_BEHAVIOR, owner, instance, stamp)?;
    wire.extend(1u32.to_le_bytes()); // Only mBehaviorID; native defaults for all other fields.
    // Legions Idol map 03000072 has Melee_Normal1 (81000043), but no Auto1.
    wire.extend(MELEE_NORMAL.to_le_bytes());
    Ok(wire)
}

/// Victim-owned health story. Bitfield 2 carries the attacker and omits skill,
/// behavior and the basic-attack bit. The single hook is type 1 (SetTypeToDamage,
/// 006ad840) with target, result and signed health. StoryManager::DoStoryHook
/// 00514ff0 presents type 1; it does not replace the separate vital-quality update.
/// Negative health is damage. Positive health is recovery. Zero is not packed.
pub fn damage_story(
    victim: u64,
    instance: u16,
    stamp: u16,
    attacker: u64,
    health_change: i32,
) -> Result<Vec<u8>, WorldError> {
    damage_story_for_skill(victim,instance,stamp,attacker,health_change,None)
}

/// Preserve a known maneuver in the native optional skill field. Retail uses
/// it for IsSkillHarmful / last-attacker selection, even without health loss.
/// A zero-damage hook carries no fabricated health result or blood event.
pub fn damage_story_for_skill(victim:u64,instance:u16,stamp:u16,attacker:u64,
    health_change:i32,skill:Option<u32>)->Result<Vec<u8>,WorldError> {
    if [0, u64::MAX].contains(&attacker) || skill==Some(0) || (health_change==0 && skill.is_none()) {
        return Err(WorldError::InvalidValue);
    }
    let mut wire = prefix(DO_STORY, victim, instance, stamp)?;
    wire.extend((2u32 | if skill.is_some(){0x10}else{0}).to_le_bytes());
    wire.extend(attacker.to_le_bytes());
    if let Some(skill)=skill {wire.extend(skill.to_le_bytes());}
    wire.extend(1u32.to_le_bytes());
    wire.extend((if health_change==0 {0x101u32}else{0x981}).to_le_bytes());
    wire.extend(victim.to_le_bytes());
    if health_change!=0 {
        wire.extend(HEALTH_RESULT.to_le_bytes());
        wire.extend(health_change.to_le_bytes());
    }
    Ok(wire)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn stop_fx_uses_native_stamp_and_has_no_scalar_payload() {
        let wire=stop_effect(0x100000002,3,4,0x41000031).unwrap();
        assert_eq!(wire.len(),20);
        assert_eq!(&wire[..4],&0x100a1u32.to_le_bytes());
        assert_eq!(&wire[4..12],&0x100000002u64.to_le_bytes());
        assert_eq!(&wire[12..16],&[3,0,4,0]);
        assert_eq!(&wire[16..],&0x41000031u32.to_le_bytes());
        assert!(stop_effect(1,1,1,0).is_err());
    }
    #[test]
    fn skill_story_retains_context_and_maneuver_without_basic_attack_flag() {
        // PhysicsStory StreamPack: attacker, optional behavior, optional client
        // context, optional skill, then hooks. Counter zero is valid after wrap.
        let animated=skill_attack(11,2,9,0x410000b6,0,0x59).unwrap();
        assert_eq!(&animated[16..20],&0x1eu32.to_le_bytes());
        assert_eq!(&animated[44..48],&0u32.to_le_bytes());
        assert_eq!(&animated[48..52],&0x59u32.to_le_bytes());
        assert_eq!(animated.len(),56);
        let immediate=skill_attack(11,2,10,0,73,0x55).unwrap();
        assert_eq!(&immediate[16..20],&0x1au32.to_le_bytes());
        assert_eq!(&immediate[28..32],&73u32.to_le_bytes());
        assert_eq!(&immediate[32..36],&0x55u32.to_le_bytes());
        assert_eq!(immediate.len(),40);
    }
    #[test]
    fn unarmed_mode_and_idol_attack_have_native_animation_keys() {
        let wire=mode_id(1,1,1,MARTIAL_ARTS).unwrap();
        assert_eq!(&wire[16..20],&MARTIAL_ARTS.to_le_bytes());
        assert!(mode_id(1,1,1,0).is_err());
        let (_,mode,events)=idol(2,1,1,IdolAction::Swing).unwrap();
        assert_eq!(mode,0x41000012);assert_eq!(events.len(),2);
        assert_eq!(&events[1][20..24],&MELEE_NORMAL.to_le_bytes());
    }

    #[test]
    fn damage_story_matches_the_captured_victim_hook() {
        let wire = damage_story(0x11, 2, 9, 0x22, -20).unwrap();
        assert_eq!(wire.len(), 52);
        assert_eq!(&wire[..4], &DO_STORY.to_le_bytes());
        assert_eq!(&wire[4..12], &0x11u64.to_le_bytes());
        assert_eq!(&wire[12..14], &2u16.to_le_bytes());
        assert_eq!(&wire[14..16], &9u16.to_le_bytes());
        assert_eq!(&wire[16..20], &2u32.to_le_bytes());
        assert_eq!(&wire[20..28], &0x22u64.to_le_bytes());
        assert_eq!(&wire[28..32], &1u32.to_le_bytes());
        assert_eq!(&wire[32..36], &0x981u32.to_le_bytes());
        assert_eq!(&wire[36..44], &0x11u64.to_le_bytes());
        assert_eq!(&wire[44..48], &HEALTH_RESULT.to_le_bytes());
        assert_eq!(&wire[48..52], &(-20i32).to_le_bytes());
        let heal = damage_story(0x11, 2, 10, 0x11, 25).unwrap();
        assert_eq!(&heal[48..52], &25i32.to_le_bytes());
        assert!(damage_story(0x11, 2, 10, 0x22, 0).is_err());
        assert!(damage_story(0, 2, 10, 0x22, -1).is_err());
        assert!(damage_story(0x11, 2, 10, 0, -1).is_err());
    }
    #[test]
    fn known_maneuver_survives_victim_hook_including_missed_attacks() {
        let hit=damage_story_for_skill(0x11,2,9,0x22,-20,Some(6)).unwrap();
        assert_eq!(&hit[16..20],&0x12u32.to_le_bytes());
        assert_eq!(&hit[28..32],&6u32.to_le_bytes());
        assert_eq!(&hit[32..36],&1u32.to_le_bytes());
        assert_eq!(&hit[36..40],&0x981u32.to_le_bytes());
        assert_eq!(&hit[40..48],&0x11u64.to_le_bytes());
        assert_eq!(&hit[52..56],&(-20i32).to_le_bytes());
        let miss=damage_story_for_skill(0x11,2,10,0x22,0,Some(6)).unwrap();
        assert_eq!(&miss[36..40],&0x101u32.to_le_bytes());
        assert_eq!(&miss[40..48],&0x11u64.to_le_bytes());
        assert_eq!(miss.len(),48,"No invented damage result on a miss");
        assert!(damage_story_for_skill(0x11,2,10,0x22,-1,Some(0)).is_err());
    }
}
