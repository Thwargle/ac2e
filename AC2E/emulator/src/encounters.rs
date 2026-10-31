//! Server-authoritative development encounters. Balance rules remain authored.
//! This module does not claim original combat balance, collision or animation parity.
use crate::{
    content::Content,
    movement::Placement,
    progression::{Command, Progression},
    scene::Scene,
    store::Store,
};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::File,
    io::{self, Read},
    path::Path,
};
fn invalid(text: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, text)
}

#[cfg(test)]
mod skill_tests;

#[cfg(test)]
mod locomotion_tests {
    use super::*;
    #[test]
    fn different_speeds_preserve_collision_steps_and_attack_standoff() {
        let from=Placement::HUMAN_START;
        let goal=from.translated([10.,0.,0.]).unwrap();
        for speed in [3.9,6.5,7.8] {
            let mut route=Vec::new();
            let next=advance(from,goal,2.5,speed*0.1,&mut route,|a,b| {
                assert!(a.distance(b).unwrap()<0.201);Some(b)
            }).unwrap();
            assert!((from.distance(next).unwrap()-speed*0.1).abs()<0.0001);
            let stop=advance(from,goal,2.5,20.,&mut Vec::new(),|_,b|Some(b)).unwrap();
            assert!((stop.distance(goal).unwrap()-2.5).abs()<0.0001);
        }
        // A wall encountered partway through a fast tick cannot be skipped.
        let limited=advance(from,goal,0.,2.,&mut Vec::new(),|_,b|
            (b.coordinates[0]<=from.coordinates[0]+0.41).then_some(b)).unwrap();
        assert!((from.distance(limited).unwrap()-0.4).abs()<0.0001);
    }
    #[test]
    fn return_spends_time_budget_across_waypoints_without_cutting_corners() {
        let home=Placement::HUMAN_START;
        let mut route=Vec::new();
        let corner=advance(home,home.translated([1.,0.,0.]).unwrap(),0.,1.,&mut route,|_,b|Some(b)).unwrap();
        let end=advance(corner,corner.translated([0.,1.,0.]).unwrap(),0.,1.,&mut route,|_,b|Some(b)).unwrap();
        let next=retreat(end,1.5,&mut route,|a,b| {
            let [x,y,_]=a.offset_to(b).unwrap();
            assert!(x.abs()<0.001 || y.abs()<0.001,"Must follow recorded right-angle path");
            assert!(a.distance(b).unwrap()<0.201);Some(b)
        }).unwrap();
        let [x,y,_]=home.offset_to(next).unwrap();assert!((x-0.5).abs()<0.001 && y.abs()<0.001);
        let back=retreat(next,1.5,&mut route,|_,b|Some(b)).unwrap();
        assert!(home.distance(back).unwrap()<0.001);assert!(route.is_empty());
    }
}

#[cfg(test)]
mod spawner_tests {
    use super::*;
    #[test]
    fn policy_overrides_initial_delay_without_moving_the_observed_anchor() {
        let id=crate::content::NAMESPACE+7;let home=Placement::HUMAN_START;
        let content=Content::fixture(id,home,None);
        let text=format!("{}\nspawner\t{:08x}\t2000\t5000\n",
            include_str!("../content/development-encounters.tsv"),crate::quests::TARGET_ENTITY);
        let mut runtime=Encounters::parse(&text,&content).unwrap();
        let mut scene=Scene::with_content(content);
        runtime.initialize_spawners(&mut scene,100).unwrap();
        assert!(scene.content_placement(id).is_none());
        assert!(runtime.spawners.poll(2099).is_empty());
        assert_eq!(runtime.spawners.poll(2100),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        assert_eq!(scene.content_placement(id),Some(home));
        let mut moved=home;moved.coordinates[0]+=0.5;
        scene.move_creature(id,moved,2200).unwrap();
        assert!(runtime.spawners.poll(10000).is_empty(),"Pursuit is not a vacant population slot");
        scene.despawn_content(id).unwrap();runtime.removed(id,10000);
        assert!(runtime.spawners.poll(14999).is_empty());
        assert_eq!(runtime.spawners.poll(15000),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        assert_eq!(scene.content_placement(id),Some(home));
        assert!(runtime.spawners.poll(100000).is_empty());
    }
    #[test]
    fn policy_cannot_make_a_fixed_service_attackable_or_create_an_unplaced_creature() {
        let content=Content::fixture(crate::content::NAMESPACE+7,Placement::HUMAN_START,None);
        let text=include_str!("../content/development-encounters.tsv");
        for row in ["spawner\t47000001\t0\t1000", "spawner\t470007d1\t0\t0",
            "spawner\t470007d1\t86400001\t1000",
            "spawner\t470007d1\t0\t1000\nspawner\t470007d1\t0\t2000"] {
            assert!(Encounters::parse(&format!("{text}\n{row}\n"),&content).is_err());
        }
        let service=Content::fixture(crate::content::NAMESPACE+8,Placement::HUMAN_START,Some(crate::services::Service::Vendor));
        assert!(Encounters::parse(text,&service).is_err());
    }
}
#[derive(Clone, Copy)]
struct Rule {
    health: u32,
    level: Option<u32>,
    vigor: Option<u32>,
    damage: u32,
    armor: u32,
    interval: u64,
    range: f32,
    respawn: u64,
    xp: u64,
    gold: u32,
    leash: f32,
}
struct Actor {
    entity: u32,
    name: Option<String>,
    home: Placement,
    pursuit: bool,
    movement: crate::creature_motion::Profile,
    velocity: f32,
    // Verified steps retraced on disengagement, without inventing a path home.
    route: Vec<Placement>,
    rule: Rule,
    health: u32,
    target: Option<u64>,
    next_attack: u64,
    ai: crate::creature_ai::Profile,
    next_idle: u64,
    wander_goal: Option<Placement>,
    evading: bool,
    random: u64,
}
#[derive(Clone, Copy)]
struct Attack {
    target: u64,
    next: u64,
    repeat: bool,
    category: usize,
    stage: u8,
    started: u64,
    skill: bool,
    strike: Option<Strike>,
    periodic: bool,
}
/// One skill hit: weapon damage plus `bonus`, spending up to `cost` vigor.
/// `behavior` and `fx` are the skill record's story behavior and FX (0 = none).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Strike {
    pub ratings: Option<(crate::combat::Ratings,u64)>,
    pub skill: u32,
    pub bonus: u32,
    pub weapon_damage: bool,
    pub cost: u32,
    pub range: f32,
    pub behavior: u32,
    pub fx: u32,
    pub burn: Option<Over>,
}
/// A vital change applied every `period_ms` for `duration_ms`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Over {
    pub health: u32,
    pub vigor: u32,
    pub period_ms: u64,
    pub duration_ms: u64,
}
/// Regeneration multipliers from a timed buff (1.0 = unchanged).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Regen {
    pub health: f32,
    pub vigor: f32,
    pub duration_ms: u64,
}
struct Burn {
    skill: u32,
    source: u64,
    target: u64,
    per_tick: u32,
    period: u64,
    next: u64,
    until: u64,
}
struct Mend {
    instant: bool,
    skill: u32,
    source: u64,
    player: u64,
    health: u32,
    vigor: u32,
    period: u64,
    next: u64,
    until: u64,
    /// FX shown on the player when the first restoration lands (0 = none).
    fx: u32,
}
const MAX_TIMED: usize = 512;
pub enum Event {
    SkillSwing { player: u64, behavior: u32 },
    Fx { target: u64, fx: u32 },
    Loot { actor:u64, player:u64, entity:u32, ai:crate::creature_ai::Profile },
    Move { actor: u64, placement: Placement },
    IdolVisual(u64, crate::combat_visual::IdolAction),
    Swing(u64),
    Projectile { player: u64, target: u64, profile: crate::projectiles::Profile, flight_ms: u64 },
    /// Signed health on the victim. Negative is damage. The vital update is separate.
    Hit {
        victim: u64,
        attacker: u64,
        health_change: i32,
    },
    Item {
        player: u64,
        item: crate::inventory::Item,
        instance: u16,
    },
    Quest {
        player: u64,
        book: crate::quests::Book,
    },
    Health {
        actor: u64,
        health: u32,
    },
    Despawn(u64),
    Respawn {
        actor: u64,
        health: u32,
    },
    Owner {
        player: u64,
        previous: Progression,
        state: Progression,
    },
    Note {
        player: u64,
        text: String,
    },
    Stop(u64),
}
pub struct Encounters {
    dice: crate::combat::Dice,
    burns: Vec<Burn>,
    mends: Vec<Mend>,
    regen: BTreeMap<(u64,u32), (Regen, u64)>,
    ratings: Vec<(u64,u32,crate::combat::Ratings,u64)>,
    actors: BTreeMap<u64, Actor>,
    spawners: crate::spawners::Spawners,
    attacks: BTreeMap<u64, Attack>,
    cooldowns: BTreeMap<u64, u64>,
    damage: u32,
    cost: u32,
    interval: u64,
    range: f32,
    next_tick: u64,
    regen_health: u32,
    regen_vigor: u32,
    regen_interval: u64,
    next_regen: u64,
    training_loot: bool,
}
fn in_range(a: Option<Placement>, b: Option<Placement>, range: f32) -> bool {
    a.zip(b).and_then(|(a,b)|a.distance(b)).is_some_and(|d|d<=range)
}
fn facing(from:Placement,target:Placement)->Option<Placement> {
    let [dx,dy,_]=from.offset_to(target)?;
    if dx.hypot(dy)<0.001 {return Some(from);}
    // Native Heading::ToUInt8 truncates, using the f32 1/360 constant.
    let degrees=dx.atan2(dy).to_degrees().rem_euclid(360.0);
    Some(Placement{heading:(f64::from(degrees)*f64::from(1f32/360.)*255.).trunc() as u8,..from})
}
fn approach(from:Placement,target:Placement,stop:f32,budget:f32)->Option<Placement> {
    let [dx,dy,_]=from.offset_to(target)?;
    let distance=dx.hypot(dy);
    if distance<=stop+0.01{return None;}
    let step=budget.min(distance-stop);
    if step<=0.0001{return None;}
    let next=from.translated([dx/distance*step,dy/distance*step,0.])?;
    facing(next,target)
}
/// Spend a distance budget through the same short, collision-checked segments
/// used before variable speed. Retain every segment for safe homeward retracing.
fn advance(from:Placement,target:Placement,stop:f32,mut budget:f32,route:&mut Vec<Placement>,
    mut walk:impl FnMut(Placement,Placement)->Option<Placement>)->Option<Placement> {
    let mut point=from;
    for _ in 0..128 {
        if route.len()>=4096 {break;}
        let Some(proposed)=approach(point,target,stop,budget.min(0.2)) else {break};
        let Some(next)=walk(point,proposed) else {break};
        if from.distance(next).is_none_or(|d|d>crate::creature_motion::MAX_UPDATE_DISTANCE) {break;}
        let [x,y,_]=point.offset_to(next)?;let distance=x.hypot(y);
        if distance<0.0001 {break;}
        budget=(budget-distance).max(0.);route.push(point);point=next;
    }
    (point!=from).then_some(point)
}
fn retreat(from:Placement,mut budget:f32,route:&mut Vec<Placement>,
    mut walk:impl FnMut(Placement,Placement)->Option<Placement>)->Option<Placement> {
    let mut point=from;
    for _ in 0..128 {
        while route.last().is_some_and(|p|point.offset_to(*p).is_some_and(|[x,y,z]|x.hypot(y)<0.01 && z.abs()<=0.65)) {route.pop();}
        let Some(&goal)=route.last() else {break};
        let Some(proposed)=approach(point,goal,0.,budget.min(0.2)) else {break};
        let Some(next)=walk(point,proposed) else {break};
        if from.distance(next).is_none_or(|d|d>crate::creature_motion::MAX_UPDATE_DISTANCE) {break;}
        let [x,y,_]=point.offset_to(next)?;let distance=x.hypot(y);
        if distance<0.0001 {break;}
        budget=(budget-distance).max(0.);point=next;
    }
    (point!=from).then_some(point)
}
impl Encounters {
    pub fn load_motion(&mut self,content:&Content,directory:&Path)->io::Result<()> {
        let mut dat=crate::dat::Dat::open(directory.join("portal.dat"))?;
        let mut catalog=crate::creature_motion::Catalog::default();
        for (&id,actor) in &mut self.actors {
            let object=content.object(id).ok_or_else(||invalid("Movement actor missing content"))?;
            actor.movement=catalog.resolve(&mut dat,object.visual,object.scale)?
                .ok_or_else(||io::Error::other(format!("Missing native movement for creature {id:016x}, visual {:08x}",object.visual)))?;
        }
        Ok(())
    }
    pub fn movement_profile(&self,id:u64)->Option<crate::creature_motion::Profile> {
        self.actors.get(&id).map(|a|a.movement)
    }
    pub fn load(path: &Path, content: &Content) -> io::Result<Self> {
        if path == Path::new("training") {
            return Self::parse_profile(
                include_str!("../content/development-encounters.tsv"),
                content,
                true,
            );
        }
        let mut bytes = Vec::new();
        File::open(path)?.take(65537).read_to_end(&mut bytes)?;
        if bytes.len() > 65536 {
            return Err(invalid("encounter profile exceeds 64 KiB"));
        }
        let text =
            std::str::from_utf8(&bytes).map_err(|_| invalid("encounter profile encoding"))?;
        Self::parse(text, content)
    }
    fn parse(text: &str, content: &Content) -> io::Result<Self> {
        Self::parse_profile(text,content,false)
    }
    fn parse_profile(text: &str, content: &Content, regional_native: bool) -> io::Result<Self> {
        if text.len() > 65536 {
            return Err(invalid("encounter profile exceeds 64 KiB"));
        }
        let mut lines = text.lines();
        if lines.next() != Some("pyrealium-encounters-v1") {
            return Err(invalid("encounter profile header"));
        }
        let mut player = None;
        let mut regen = None;
        let mut rules = BTreeMap::new();
        let mut spawn_policies = BTreeMap::new();
        let mut spawn_groups=Vec::new();
        for line in lines {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if line.len() > if line.starts_with("spawn-group\t") { 2048 } else { 512 } {
                return Err(invalid("encounter row length"));
            }
            let f: Vec<_> = line.split('\t').collect();
            let number = |i: usize| {
                f[i].parse::<u64>()
                    .map_err(|_| invalid("invalid encounter integer"))
            };
            let small = |i: usize| {
                u32::try_from(number(i)?).map_err(|_| invalid("encounter integer overflow"))
            };
            let range = |i: usize| -> io::Result<f32> {
                let value = f[i]
                    .parse::<f32>()
                    .map_err(|_| invalid("encounter range"))?;
                if !value.is_finite() || !(0.5..=30.0).contains(&value) {
                    return Err(invalid("encounter range bounds"));
                }
                Ok(value)
            };
            match f.as_slice() {
                ["spawn-group",_,_,_] => {
                    let id=u64::from_str_radix(f[1],16).map_err(|_|invalid("spawner group identity"))?;
                    let capacity=number(2)? as usize;
                    let members=f[3].split(',').map(|id|u64::from_str_radix(id,16).map_err(|_|invalid("spawner child identity")))
                        .collect::<io::Result<Vec<_>>>()?;
                    spawn_groups.push((id,capacity,members));
                }
                ["spawner", _, _, _] => {
                    let entity=u32::from_str_radix(f[1].trim_start_matches("0x"),16)
                        .map_err(|_|invalid("spawner entity"))?;
                    let policy=crate::spawners::Policy{initial_delay_ms:number(2)?,regeneration_ms:number(3)?};
                    if entity>>24!=0x47 || !policy.valid() || spawn_policies.len()>=4096
                        || spawn_policies.insert(entity,policy).is_some() {
                        return Err(invalid("invalid or duplicate spawner policy"));
                    }
                }
                ["player", _, _, _, _] if player.is_none() => {
                    player = Some((small(1)?, small(2)?, number(3)?, range(4)?));
                }
                ["regen", _, _, _] if regen.is_none() => {
                    regen = Some((small(1)?, small(2)?, number(3)?));
                }
                ["npc", _, _, _, _, _, _, _, _] => {
                    let entity = u32::from_str_radix(f[1].trim_start_matches("0x"), 16)
                        .map_err(|_| invalid("encounter entity"))?;
                    let rule = Rule {
                        health: small(2)?,
                        level: None,
                        vigor: None,
                        damage: small(3)?,
                        armor: 0,
                        interval: number(4)?,
                        range: range(5)?,
                        respawn: number(6)?,
                        xp: number(7)?,
                        gold: small(8)?,
                        leash: 18.,
                    };
                    if entity >> 24 != 0x47
                        || rule.health == 0
                        || rule.health > 1_000_000
                        || rule.damage == 0
                        || rule.damage > 1_000_000
                        || !(250..=60000).contains(&rule.interval)
                        || !(1000..=86400000).contains(&rule.respawn)
                        || rule.xp > i64::MAX as u64
                        || rule.gold > i32::MAX as u32
                        || rules.len() >= 256
                        || rules.insert(entity, rule).is_some()
                    {
                        return Err(invalid("invalid or duplicate encounter rule"));
                    }
                }
                _ => return Err(invalid("unrecognized encounter row")),
            }
        }
        let (damage, cost, interval, range) =
            player.ok_or_else(|| invalid("missing player attack profile"))?;
        let (regen_health, regen_vigor, regen_interval) = regen.unwrap_or((0, 0, 1000));
        if damage == 0
            || damage > 1_000_000
            || cost == 0
            || cost > 1_000_000
            || !(250..=60000).contains(&interval)
            || regen_health > 1_000_000
            || regen_vigor > 1_000_000
            || !(1000..=60000).contains(&regen_interval)
        {
            return Err(invalid("player or regeneration profile bounds"));
        }
        // An authored row wins: the retail Idol Spawn is level 24 and one-hits a
        // level 1 character. Interval 0 means the record had no natural weapon
        // time. A catalog entry alone is not evidence that an NPC is attackable.
        // Only explicit encounter rules or recovered combat records qualify.
        let mut native_rules=BTreeMap::new();
        for line in include_str!("../content/creature-combat.tsv").lines()
            .chain(include_str!("../content/inferred-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/regional-creature-combat-v1.tsv").lines()) {
            if line.is_empty() || line.starts_with('#') || line.starts_with("pyrealium-") {
                continue;
            }
            let f: Vec<_> = line.split('\t').collect();
            if ![7,13,14].contains(&f.len()) {
                return Err(invalid("creature combat field count"));
            }
            let entity = u32::from_str_radix(f[0], 16).map_err(|_| invalid("creature combat entity"))?;
            let number = |i: usize| {
                f[i].parse::<u64>()
                    .map_err(|_| invalid("creature combat integer"))
            };
            let small = |i: usize| {
                u32::try_from(number(i)?).map_err(|_| invalid("creature combat overflow"))
            };
            let health = small(1)?;
            let damage = small(2)?;
            let mut interval = number(3)?;
            if interval == 0 {
                interval = 1500;
            }
            let armor = small(4)?;
            let xp = number(5)?;
            if entity >> 24 != 0x47
                || health == 0
                || health > 5_000_000
                || damage == 0
                || damage > 5_000_000
                || armor > 1_000_000
                || !(250..=60000).contains(&interval)
                || xp > i64::MAX as u64
            {
                return Err(invalid("creature combat bounds"));
            }
            let rule = Rule {
                health,
                level: Some(small(6)?).filter(|level| *level>0),
                vigor: if f.len()==14 && !f[13].is_empty(){Some(small(13)?)}else{None},
                damage,
                armor,
                interval,
                range: 3.0,
                respawn: 30_000,
                xp,
                gold: 2,
                leash: if f.len()>=13 {let value=f[11].parse::<f32>().map_err(|_|invalid("native homesick radius"))?;
                    if !value.is_finite() || !(0.0..=500.0).contains(&value){return Err(invalid("native homesick radius bounds"));}
                    if value>0. {value}else{18.}}else{18.},
            };
            native_rules.entry(entity).or_insert(rule);
            if rules.contains_key(&entity) {
                continue;
            }
            if rules.len() >= 4096 || rules.insert(entity, rule).is_some() {
                return Err(invalid("creature combat rule limit"));
            }
        }
        // Catalog labels are presentation only; unlike the old fallback list,
        // they must never confer health, rewards or attackability on an NPC.
        let labels=include_str!("../content/definitions.tsv").lines().filter_map(|line|{
            let mut fields=line.split('\t');let entity=u32::from_str_radix(fields.next()?,16).ok()?;
            fields.next()?;let name=fields.next()?;
            Some((entity,name.split('[').next().unwrap_or(name).trim()))
        }).collect::<BTreeMap<_,_>>();
        let protected=include_str!("../content/population-protected.txt").lines()
            .filter_map(|line|u32::from_str_radix(line,16).ok()).collect::<std::collections::BTreeSet<_>>();
        let ai_profiles=crate::creature_ai::catalog()?;
        let actors = content
            .objects()
            .filter_map(|object| {
                // Authored dialogue roles override inherited combat data: the
                // training mentor's template also carries a monster profile.
                // Use the same identities as the interaction dispatcher.
                if object.service.is_some()
                    || protected.contains(&object.entity)
                    || object.entity == crate::training_dialogue::MENTOR_ENTITY
                    || crate::shelter::supported(object.entity)
                {
                    return None;
                }
                // The built-in training profile weakens several high-level
                // templates for practice. New regional wilderness must retain
                // native stats. Explicit external encounter overrides still win.
                let regional=object.id&0xffffffffff000000==0x8140c00000000000;
                let selected=if regional_native && regional {&native_rules}else{&rules};
                selected.get(&object.entity).map(|&rule| {
                    let ai=ai_profiles.get(&object.entity).copied().unwrap_or_default();
                    let mut rule=rule;
                    if ai.homesick.is_none(){rule.leash=rule.leash.max(ai.perception);}
                    (
                        object.id,
                        Actor {
                            entity: object.entity,
                            name: object.name.clone().or_else(||labels.get(&object.entity).map(|name|(*name).to_owned())),
                            home: object.placement,
                            pursuit: object.supports_pose_updates(),
                            movement: crate::creature_motion::Profile::default(),
                            velocity: 0.,
                            route: Vec::new(),
                            rule,
                            health: rule.health,
                            target: None,
                            next_attack: 0,
                            ai, next_idle:object.id%1000, wander_goal:None,evading:false,random:object.id,
                        },
                    )
                })
            })
            .collect::<BTreeMap<_, _>>();
        if actors.is_empty() || actors.len() > 16384 {
            return Err(invalid("encounters require 1..16384 matching placed actors"));
        }
        if spawn_policies.keys().any(|entity|!actors.values().any(|actor|actor.entity==*entity)) {
            return Err(invalid("spawner policy must name a placed combat creature"));
        }
        // Captures keep one point spawner per observation. The separate inferred
        // pack caps its authored rosters as groups; services remain excluded.
        let mut spawners=crate::spawners::Spawners::new(actors.iter().map(|(&id,actor)|{
            (id,spawn_policies.get(&actor.entity).copied().unwrap_or(crate::spawners::Policy{
                initial_delay_ms:0,regeneration_ms:
                    if id&0xffffffffff000000==0x8140c00000000000 {180000}
                    else if id&0xffffff0000000000==0x8140a00000000000 {120000}
                    else {actor.rule.respawn}}))
        }))?;
        for (id,capacity,members) in spawn_groups {spawners.group(id,capacity,members)?;}
        for source in [include_str!("../content/inferred-world-v1.report.json"),
            include_str!("../content/regional-world-v1.report.json")] {
        let inferred:serde_json::Value=serde_json::from_str(source).map_err(io::Error::other)?;
        for group in inferred["groups"].as_array().ok_or_else(||invalid("inferred spawn groups"))? {
            let members=group["members"].as_array().ok_or_else(||invalid("inferred roster"))?.iter().filter_map(|v|v.as_u64()).collect::<Vec<_>>();
            // External content can select only part of the authored pack.
            let members=members.into_iter().filter(|id|actors.contains_key(id)).collect::<Vec<_>>();
            if !members.is_empty(){let capacity=usize::try_from(group["capacity"].as_u64().ok_or_else(||invalid("inferred capacity"))?)
                .map_err(|_|invalid("inferred capacity overflow"))?.min(members.len());
                spawners.group(group["id"].as_u64().ok_or_else(||invalid("inferred group identity"))?,capacity,members)?;}
        }
        }
        Ok(Self {
            actors,
            spawners,
            attacks: BTreeMap::new(),
            cooldowns: BTreeMap::new(),
            damage,
            cost,
            interval,
            range,
            next_tick: 0,
            regen_health,
            regen_vigor,
            regen_interval,
            next_regen: 0,
            burns: Vec::new(), mends: Vec::new(), regen: BTreeMap::new(), ratings: Vec::new(),
            training_loot: false,
            dice:crate::combat::Dice::seeded(),
        })
    }
    pub fn initial_health(&self) -> impl Iterator<Item = (u64, u32)> + '_ {
        self.actors
            .iter()
            .map(|(&id, actor)| (id, actor.rule.health))
    }
    pub fn inspection_stats(&self) -> impl Iterator<Item = (u64, u32, u32)> + '_ {
        self.actors.iter().flat_map(|(&id, actor)| {
            [(0x12e,actor.rule.level),(0x108,actor.rule.vigor),(0x103,actor.rule.vigor)]
                .into_iter().filter_map(move |(stat,value)|value.map(|v|(id,stat,v)))
        })
    }
    pub fn spawners(&self) -> &crate::spawners::Spawners { &self.spawners }
    /// Called before any client can observe the scene. Only managed monsters
    /// are withdrawn; vendors, dialogue actors and world objects remain placed.
    pub fn initialize_spawners(&mut self,scene:&mut Scene,now:u64)->io::Result<()> {
        for (&id,actor) in &mut self.actors {
            if scene.content_placement(id).is_some(){scene.despawn_content(id)?;}
            actor.health=0;actor.target=None;actor.route.clear();
        }
        self.spawners.initialize(now);
        Ok(())
    }
    /// Acknowledge only after Scene has created the new network lifetime.
    pub fn spawned(&mut self,id:u64)->io::Result<()> {
        if !self.spawners.spawned(id){return Err(invalid("unexpected spawner child"));}
        let actor=self.actors.get_mut(&id).ok_or_else(||invalid("unknown spawner child"))?;
        actor.health=actor.rule.health;actor.target=None;actor.route.clear();actor.next_attack=0;
        actor.velocity=0.;
        actor.evading=false;actor.wander_goal=None;
        self.burns.retain(|b|b.target!=id);
        self.ratings.retain(|(who,_,_,_)|*who!=id);
        Ok(())
    }
    pub fn removed(&mut self,id:u64,now:u64) { self.spawners.removed(id,now); }
    pub fn stop(&mut self, player: u64) {
        self.attacks.remove(&player);
    }
    pub fn attacking_target(&self, player: u64) -> Option<u64> {
        self.attacks.get(&player).map(|attack| attack.target)
    }
    pub fn is_creature(&self, id: u64) -> bool {
        self.actors.contains_key(&id)
    }
    /// Local test navigation to actual runtime actors, never a spawn generator.
    /// Use current positions and exclude dead/despawned actors and all rooms.
    pub fn outdoor_survey(&self, scene: &Scene, origin: Placement) -> Vec<String> {
        if !(1..=64).contains(&(origin.cell & 0xffff)) {
            return vec!["Go outdoors to locate imported world creatures.".into()];
        }
        let mut nearby=self.actors.iter().filter_map(|(&id,actor)| {
            if actor.health==0 {return None;}
            let point=scene.content_placement(id)?;
            if !(1..=64).contains(&(point.cell & 0xffff)) {return None;}
            Some((origin.distance(point)?,id,point,actor.name.as_deref().unwrap_or("Creature")))
        }).collect::<Vec<_>>();
        nearby.sort_by(|a,b|a.0.total_cmp(&b.0).then(a.1.cmp(&b.1)));
        let close=nearby.iter().filter(|row|row.0<=300.).count();
        let mut lines=vec![format!("Imported outdoor creatures: {} alive; {close} within 300 m. {} managed point spawners ({} live across all areas). Nearest locations (local testing):",nearby.len(),self.spawners.len(),self.spawners.live())];
        for (distance,_,point,name) in nearby.into_iter().take(5) {
            // Same native land-cell coordinates as the client map (20 m per
            // cell, 1020 at the origin, 10.2 cells per compass unit).
            let east=((point.cell>>24) as f32*8.+point.coordinates[0]/20.-1020.)/10.2;
            let north=(((point.cell>>16)&255) as f32*8.+point.coordinates[1]/20.-1020.)/10.2;
            lines.push(format!("{name}: {:.1}{}, {:.1}{} ({distance:.0} m away; cell {:08X}).",
                north.abs(),if north>=0. {'N'}else{'S'},east.abs(),if east>=0. {'E'}else{'W'},point.cell));
        }
        lines
    }
    pub fn includes_entity(&self, entity: u32) -> bool {
        self.actors.values().any(|actor| actor.entity == entity)
    }
    /// Call only after the server validates the authored potion's DAT profile.
    pub fn enable_training_loot(&mut self) {
        self.training_loot = true;
    }
    pub fn start(
        &mut self,
        player: u64,
        target: u64,
        now: u64,
        scene: &Scene,
        store: &Store,
    ) -> io::Result<()> {
        let actor = self
            .actors
            .get(&target)
            .filter(|actor| actor.health > 0)
            .ok_or_else(|| invalid("target is not an attackable creature"))?;
        if actor.evading {
            return Err(invalid("This creature is returning to its home."));
        }
        if !store
            .characters
            .get(&player)
            .is_some_and(|c| !c.deleted && c.progression.alive())
        {
            return Err(invalid("dead or unavailable character cannot attack"));
        }
        let category = crate::starter_gear::equipped_category(&store.characters[&player].inventory);
        let range = if category == 0 { self.range } else { 18.0 };
        let distance = scene.combat_distance(player, target)
            .ok_or_else(|| invalid("Target is not in a visible, shared area."))?;
        if distance > range {
            return Err(invalid(&format!(
                "Target is out of attack range ({distance:.1} m away; reach {range:.1} m). Move closer, or return to peace with Z to equip a ranged weapon."
            )));
        }
        if self.attacks.len() >= 64 && !self.attacks.contains_key(&player)
        {
            return Err(invalid("encounter attack capacity"));
        }
        self.cooldowns.retain(|_, until| *until > now);
        if self.cooldowns.len() >= 128 && !self.cooldowns.contains_key(&player) {
            return Err(invalid("attack cooldown capacity"));
        }
        let next = self.cooldowns.get(&player).copied().unwrap_or(now);
        // Repeated attack input must not restart a windup or an airborne shot.
        if let Some(queued) = self.attacks.get_mut(&player).filter(|a| a.target == target && a.category == category) {
            queued.repeat = true;
            return Ok(());
        }
        self.attacks.insert(
            player,
            Attack {
                target,
                next,
                repeat: true,
                category,
                stage: 0,
                started: next,
                skill: false,
                strike: None,
                periodic: false,
            },
        );
        Ok(())
    }
    /// A racial basic attack starts the ordinary repeat chain. Only its first
    /// strike carries the requested skill; follow-ups retain normal cooldowns.
    pub fn basic_skill(
        &mut self,
        player: u64,
        target: u64,
        now: u64,
        scene: &Scene,
        store: &Store,
    ) -> io::Result<()> {
        if self.attacks.get(&player).is_some_and(|a| a.stage != 0) {
            return Err(invalid("An attack is already in progress."));
        }
        self.start(player, target, now, scene, store)?;
        self.attacks.get_mut(&player).unwrap().skill = true;
        Ok(())
    }
    fn stopped(&mut self, player: u64, events: &mut Vec<Event>) {
        self.attacks.remove(&player);
        events.push(Event::Stop(player));
    }
    pub fn tick(
        &mut self,
        now: u64,
        scene: &Scene,
        store: &mut Store,
        active: impl Iterator<Item = u64>,
        mut geometry: Option<&mut crate::geometry::Geometry>,
    ) -> io::Result<Vec<Event>> {
        if now < self.next_tick {
            return Ok(Vec::new());
        }
        // Use elapsed time rather than assuming every scheduled 100ms tick ran
        // on time. Bound catch-up after a stall so no actor leaps across a zone.
        let elapsed=if self.next_tick==0 {0.1}else{(now.saturating_sub(self.next_tick.saturating_sub(100)).min(250) as f32)/1000.};
        self.next_tick = now.saturating_add(100);
        self.ratings.retain(|(_,_,_,until)|*until>now);
        self.regen.retain(|(who,_),(_,until)|*until>now && store.characters.get(who).is_some_and(|c|c.progression.alive()) && scene.player_placement(*who).is_some());
        self.ratings.retain(|(who,_,_,_)|store.characters.get(who).map_or_else(||self.actors.get(who).is_some_and(|a|a.health>0 && !a.evading),|c|c.progression.alive()));
        self.burns.retain(|b| b.next<=b.until && now<=b.until && self.actors.get(&b.target).is_some_and(|a|a.health>0 && !a.evading) && store.characters.get(&b.source).is_some_and(|c|!c.deleted && c.progression.alive()) && scene.player_placement(b.source).is_some());
        let mut events = Vec::new();
        let active=active.collect::<Vec<_>>();
        let mut due = self
            .attacks
            .iter()
            .filter(|(_, attack)| attack.next <= now)
            .map(|(&id, &attack)| (id, attack))
            .collect::<Vec<_>>();
        for burn in &mut self.burns {
            if burn.next<=now {
                burn.next=now.saturating_add(burn.period);
                due.push((burn.source,Attack{target:burn.target,next:now,repeat:false,category:0,stage:2,started:now,skill:false,periodic:true,
                    strike:Some(Strike{ratings:None,skill:burn.skill,bonus:burn.per_tick,weapon_damage:false,cost:0,range:0.,behavior:0,fx:0,burn:None})}));
            }
        }
        for (player, attack) in due {
            if !attack.periodic && !self.attacks.contains_key(&player) {
                continue;
            }
            let available = self
                .actors
                .get(&attack.target)
                .is_some_and(|actor| actor.health > 0 && !actor.evading);
            let alive = store
                .characters
                .get(&player)
                .is_some_and(|c| !c.deleted && c.progression.alive());
            if !available
                || !alive
                || (!attack.periodic && !scene.combat_distance(player, attack.target).is_some_and(|distance|
                    distance <= attack.strike.map_or(if attack.category == 0 { self.range } else { 18.0 }, |s|s.range.max(self.range))))
            {
                if alive {
                    events.push(Event::Note {
                        player,
                        text: if available {
                            "Attack stopped: move closer to your target."
                        } else {
                            "Attack stopped: your target is no longer available."
                        }.into(),
                    });
                }
                if !attack.periodic { self.stopped(player, &mut events); }
                continue;
            }
            if !attack.periodic { if let Some(world)=geometry.as_deref_mut() {
                let clear=scene.player_placement(player).zip(scene.content_placement(attack.target))
                    .is_some_and(|(a,b)|world.clear(a,b,1.));
                if !clear {events.push(Event::Note{player,text:"Attack stopped: your target is obstructed.".into()});
                    self.stopped(player,&mut events);continue;}
            }
            }
            if !attack.periodic && attack.strike.is_some_and(|s|s.cost>0) && store.characters[&player].progression.vigor==0 {
                events.push(Event::Note{player,text:"Attack stopped: insufficient vigor.".into()});self.stopped(player,&mut events);continue;
            }
            if !attack.periodic && attack.category != 0 && attack.stage == 0 {
                let cost = attack.strike.map_or(if attack.category == 2 { self.cost.max(6) } else { self.cost }, |s|s.cost.min(store.characters[&player].progression.vigor));
                if cost > 0 && store.characters[&player].progression.apply(&Command::SpendVigor(cost)).is_err() {
                    events.push(Event::Note { player, text: "Attack stopped: insufficient vigor.".into() });
                    self.stopped(player, &mut events);
                    continue;
                }
                if let Some(strike)=attack.strike { events.push(Event::SkillSwing{player,behavior:strike.behavior}); } else { events.push(Event::Swing(player)); }
                let queued = self.attacks.get_mut(&player).unwrap();
                queued.started = now;
                queued.stage = 1;
                queued.next = now.saturating_add(crate::projectiles::RELEASE_MS);
                continue;
            }
            if attack.strike.is_none() && attack.category != 0 && attack.stage == 1 {
                let race = store.characters[&player].creation.as_ref().map_or(1, |r| r.race);
                if let Some(profile) = crate::projectiles::profile(race, attack.category, attack.skill) {
                    let distance = scene.combat_distance(player, attack.target).unwrap();
                    let flight_ms = crate::projectiles::flight_ms(distance);
                    events.push(Event::Projectile { player, target: attack.target, profile, flight_ms });
                    let queued = self.attacks.get_mut(&player).unwrap();
                    queued.stage = 2;
                    queued.next = now.saturating_add(flight_ms);
                    continue;
                }
            }
            let actor = &self.actors[&attack.target];
            // Authored starter balance; the client sends only a learned skill/target.
            let base = if attack.category == 2 { self.damage.saturating_add(4) } else { self.damage };
            let damage = attack.strike.map_or(base, |s|s.bonus.saturating_add(if s.weapon_damage {base} else {0}));
            let armor=if attack.periodic {0} else {crate::combat::adjusted(actor.rule.armor,self.ratings_for(attack.target).armor)};
            let cost = attack.strike.map_or(if attack.category == 2 { self.cost.max(6) } else { self.cost }, |s|s.cost.min(store.characters[&player].progression.vigor));
            let school=match attack.category {1=>crate::combat::School::Missile,2=>crate::combat::School::Magic,_=>crate::combat::School::Melee};
            let caster=&store.characters[&player];
            let rating=crate::combat::adjusted(crate::combat::player_attack(caster.progression.level().number,&caster.skills,school),self.ratings_for(player).attack(school));
            let defense=crate::combat::adjusted(actor.rule.level.unwrap_or(1)*8,self.ratings_for(attack.target).creature_defense());
            let hit=attack.periodic || self.dice.roll(crate::combat::hit_chance(rating,defense));
            let dealt = if !hit || damage == 0 {0} else {damage
                .saturating_sub(armor)
                .max(1)
                .min(actor.health)};
            let killed = dealt == actor.health;
            let mut commands = if cost > 0 { vec![Command::SpendVigor(cost)] } else { Vec::new() };
            if killed && actor.rule.xp > 0 {
                commands.push(Command::AwardXp(actor.rule.xp));
            }
            if killed && actor.rule.gold > 0 {
                commands.push(Command::AwardGold(actor.rule.gold));
            }
            let character = &store.characters[&player];
            let candidate = commands
                .iter()
                .try_fold(character.progression.clone(), |state, command| {
                    state.apply(command)
                });
            if candidate.is_err() {
                events.push(Event::Note {
                    player,
                    text: "Attack stopped: insufficient vigor or reward capacity.".into(),
                });
                if !attack.periodic { self.stopped(player, &mut events); }
                continue;
            }
            let account = character.account.clone();
            let previous = character.progression.clone();
            let previous_quests = character.quests.clone();
            let quest_drop=crate::shelter::drop(&character.quests,&character.inventory,actor.entity);
            let loot = if killed && (quest_drop.is_some() || (self.training_loot && matches!(actor.entity,crate::shelter::DIGGER|crate::shelter::SENTINEL))) {
                character
                    .inventory
                    .free_slot()
                    .filter(|_| character.inventory.dense_bags())
                    .and_then(|slot| {
                        store
                            .next_item_id()
                            .ok()
                            .map(|id| quest_drop.and_then(|d|crate::shelter::item(d,id,slot)).or_else(||matches!(actor.entity,crate::shelter::DIGGER|crate::shelter::SENTINEL).then(||crate::apparel::drop(&character.inventory,character.creation.as_ref().map_or(1,|r|r.race),id,slot)).flatten()).unwrap_or_else(||crate::item_services::potion(id, slot)))
                    })
                    .and_then(|item| {
                        scene
                            .admit_inventory_grant(player, item.id)
                            .ok()
                            .map(|instance| (item, instance))
                    })
            } else {
                None
            };
            if let Some((item, instance)) = &loot {
                store.encounter_transaction(
                    &account,
                    player,
                    actor.entity,
                    &[crate::inventory::Command::Grant(item.clone())],
                    &commands,
                )?;
                events.push(Event::Item {
                    player,
                    item: item.clone(),
                    instance: *instance,
                });
            } else if killed {
                store.encounter_reward(&account, player, actor.entity, &commands)?;
            } else if !commands.is_empty() {
                store.progression_batch(&account, player, &commands)?;
            }
            if killed && quest_drop.is_some() && loot.is_none() {
                events.push(Event::Note{player,text:"Quest item not collected: free a backpack slot and defeat another Drudge.".into()});
            }
            let corpse_loot=killed && self.training_loot && quest_drop.is_none() && loot.is_none();
            if corpse_loot {events.push(Event::Loot{actor:attack.target,player,entity:actor.entity,ai:actor.ai});}
            if store.characters[&player].quests != previous_quests {
                events.push(Event::Quest {
                    player,
                    book: store.characters[&player].quests.clone(),
                });
            }
            if !attack.periodic {
            let until = if attack.category == 0 { now.saturating_add(self.interval) }
                else { attack.started.saturating_add(self.interval).max(now.saturating_add(100)) };
            self.cooldowns.insert(player, until);
            self.attacks.get_mut(&player).unwrap().next = until;
            self.attacks.get_mut(&player).unwrap().stage = 0;
            self.attacks.get_mut(&player).unwrap().skill = false;
                self.attacks.get_mut(&player).unwrap().strike=None;
            }
            if let Some(strike)=attack.strike {
                if !attack.periodic {
                    if strike.fx!=0 {events.push(Event::Fx{target:attack.target,fx:strike.fx});}
                    if hit { if let Some((ratings,ms))=strike.ratings {self.add_ratings(attack.target,strike.skill,ratings,ms,now)?;} }
                    if let Some(burn)=strike.burn.filter(|b|hit && b.health>0 && b.duration_ms>0) {
                        self.burns.retain(|b|!(b.source==player && b.target==attack.target && b.skill==strike.skill));
                        self.burns.push(Burn{source:player,target:attack.target,skill:strike.skill,per_tick:burn.health,
                            period:burn.period_ms.max(250),next:now.saturating_add(burn.period_ms.max(250)),until:now.saturating_add(burn.duration_ms)});
                    }
                }
            }
            let actor = self.actors.get_mut(&attack.target).unwrap();
            actor.health -= dealt;
            if !attack.periodic && attack.category == 0 { if let Some(strike)=attack.strike {events.push(Event::SkillSwing{player,behavior:strike.behavior});} else {events.push(Event::Swing(player));} }
            if let Some(dealt) = i32::try_from(dealt).ok().filter(|value| *value > 0) {
                events.push(Event::Hit {
                    victim: attack.target,
                    attacker: player,
                    health_change: -dealt,
                });
            }
            events.push(Event::Health {
                actor: attack.target,
                health: actor.health,
            });
            events.push(Event::Owner {
                player,
                previous,
                state: store.characters[&player].progression.clone(),
            });
            events.push(Event::Note {
                player,
                text: if !hit {"Your attack missed.".into()} else if dealt == 0 {"Your skill takes effect.".into()} else {format!("You hit for {dealt} damage.")},
            });
            if killed {
                actor.target = None;
                actor.route.clear();
                events.push(Event::Note {
                    player,
                    text: format!(
                        "Creature defeated. Earned {} XP and {} gold.",
                        actor.rule.xp, actor.rule.gold
                    ),
                });
                // Death mode/behavior is shared by the native creature tables;
                // the receiving client selects that creature's own DAT graph.
                events.push(Event::IdolVisual(
                    attack.target,
                    crate::combat_visual::IdolAction::Death,
                ));
                let grace=crate::combat_visual::death_presentation_ms(actor.entity);
                if !self.spawners.died(attack.target,now,if corpse_loot{60000}else{grace}) {
                    return Err(invalid("defeated creature has no occupied spawner"));
                }
                let stopped = self
                    .attacks
                    .iter()
                    .filter(|(_, queued)| queued.target == attack.target)
                    .map(|(&player, _)| player)
                    .collect::<Vec<_>>();
                for player in stopped {
                    self.stopped(player, &mut events);
                }
            } else if actor.target.is_none() {
                actor.target = Some(player);
                actor.next_attack = now.saturating_add(actor.rule.interval);
                events.push(Event::IdolVisual(
                    attack.target,
                    crate::combat_visual::IdolAction::Combat,
                ));
            }
            if !attack.repeat && !attack.periodic {
                self.attacks.remove(&player);
            }
        }
        let mut spell_ratings=BTreeMap::<u64,crate::combat::Ratings>::new();
        for (who,_,rating,_) in &self.ratings {spell_ratings.entry(*who).or_default().add(*rating);}
        for (&id, actor) in &mut self.actors {
            // A real removal may also come from a script. Refill the owned slot;
            // leaving the client's PVS never removes content from the server.
            if actor.health>0 && scene.content_placement(id).is_none() {
                actor.health=0;actor.target=None;actor.route.clear();
                self.spawners.removed(id,now);
            }
            if actor.health == 0 {
                actor.velocity=0.;
                continue;
            }
            if actor.target.is_none() && !actor.evading && now>=actor.next_idle {
                // Native detection radius, with a staggered 1s scan. Do not
                // load collision or roam distant, unobserved regions.
                actor.next_idle=now.saturating_add(1000);
                let nearby=active.iter().filter_map(|&player| {
                    let alive=store.characters.get(&player).is_some_and(|c|!c.deleted && c.progression.alive());
                    alive.then(||scene.combat_distance(player,id).map(|d|(player,d))).flatten()
                }).filter(|(_,distance)|*distance<=actor.ai.perception.max(actor.ai.wander_range+20.))
                  .collect::<Vec<_>>();
                if actor.ai.aggressive() {
                    let mut candidates=nearby.iter().filter(|(_,d)|*d<=actor.ai.perception)
                        .filter(|(p,_)|in_range(Some(actor.home),scene.player_placement(*p),actor.rule.leash))
                        .copied().collect::<Vec<_>>();
                    candidates.sort_by(|a,b|a.1.total_cmp(&b.1).then(a.0.cmp(&b.0)));
                    // A nearer player behind a wall must not mask a visible
                    // player who is also within the native perception radius.
                    let target=candidates.into_iter().find(|(player,_)|
                        scene.player_placement(*player).zip(scene.content_placement(id))
                            .is_some_and(|(a,b)|geometry.as_deref_mut().is_some_and(|world|world.clear(a,b,1.))));
                    if let Some((player,_))=target {
                        actor.target=Some(player);actor.wander_goal=None;
                        actor.next_attack=now.saturating_add(actor.rule.interval);
                        events.push(Event::IdolVisual(id,crate::combat_visual::IdolAction::Combat));
                    }
                }
                if actor.target.is_none() && actor.wander_goal.is_none() && actor.route.is_empty()
                    && actor.pursuit && actor.ai.wanders() && !nearby.is_empty()
                    && crate::creature_ai::random(&mut actor.random)<actor.ai.probability.unwrap_or(0.2) {
                    // Native range/probability; uniform disk destinations and
                    // 1s opportunities are explicitly AC2E scheduling policy.
                    let angle=crate::creature_ai::random(&mut actor.random)*std::f32::consts::TAU;
                    let radius=crate::creature_ai::random(&mut actor.random).sqrt()*actor.ai.wander_range.min(actor.rule.leash);
                    actor.wander_goal=actor.home.translated([angle.cos()*radius,angle.sin()*radius,0.]);
                }
            }
            let Some(mut target) = actor.target else {
                if let Some(point)=scene.content_placement(id) {
                    if let Some(goal)=actor.wander_goal {
                        if actor.route.len()<2048 {
                            let budget=actor.movement.peace.distance(&mut actor.velocity,elapsed);
                            if let Some(next)=advance(point,goal,0.2,budget,&mut actor.route,|from,to|
                                geometry.as_deref_mut().and_then(|world|world.walk_step(from,to,0.35))) {
                                events.push(Event::Move{actor:id,placement:next});continue;
                            }
                        }
                        actor.wander_goal=None;
                        actor.velocity=0.;
                    }
                    // A blocked return waits for geometry; never teleport or
                    // walk through an obstacle to restore an authored spawn.
                    let budget=actor.movement.peace.distance(&mut actor.velocity,elapsed);
                    if let Some(next)=retreat(point,budget,&mut actor.route,|from,to|
                        geometry.as_deref_mut().and_then(|world|world.walk_step(from,to,0.35))) {
                        events.push(Event::Move{actor:id,placement:next});
                    } else {actor.velocity=0.;}
                    if actor.route.is_empty(){actor.evading=false;actor.velocity=0.;}
                }
                continue;
            };
            // Engagement and melee reach are separate. In particular, a ranged
            // hit across visible cells or a slope must not heal the victim on
            // the same tick merely because our bounded pursuit cannot reach it.
            let eligible = |player| {
                store
                    .characters
                    .get(&player)
                    .is_some_and(|c| !c.deleted && c.progression.alive())
                    && actor.route.len()<4096
                    && in_range(Some(actor.home),scene.player_placement(player),actor.rule.leash)
                    && scene.combat_distance(player, id).is_some_and(|distance| distance <= actor.rule.leash)
            };
            if !eligible(target) {
                if let Some((&other, _)) = self
                    .attacks
                    .iter()
                    .find(|(player, attack)| attack.target == id && eligible(**player))
                {
                    target = other;
                    actor.target = Some(other);
                    actor.next_attack = now.saturating_add(actor.rule.interval);
                } else {
                    actor.target = None;
                    actor.evading=true;actor.wander_goal=None;
                    events.push(Event::IdolVisual(
                        id,
                        crate::combat_visual::IdolAction::Peace,
                    ));
                    if actor.health != actor.rule.health {
                        actor.health = actor.rule.health;
                        events.push(Event::Health {
                            actor: id,
                            health: actor.health,
                        });
                    }
                    continue;
                }
            }
            // Apply homesickness before movement. Shared displacement rebases
            // adjacent blocks; native collision resolves the destination cell.
            if actor.pursuit {
                if let (Some(from),Some(player))=(scene.content_placement(id),scene.player_placement(target)) {
                    let budget=actor.movement.combat.distance(&mut actor.velocity,elapsed);
                    let step=advance(from,player,(actor.rule.range-0.5).max(0.5),budget,&mut actor.route,|from,to|
                        geometry.as_deref_mut().and_then(|world|world.walk_step(from,to,0.35)));
                    if let Some(next)=step {
                        events.push(Event::Move{actor:id,placement:next});
                    } else {
                        actor.velocity=0.;
                        if let Some(turned)=facing(from,player).filter(|p|p.heading!=from.heading) {
                        // Targets can circle inside melee range. Turn in place
                        // without adding a fake walking step to the home route.
                        events.push(Event::Move{actor:id,placement:turned});
                        }
                    }
                }
            }
            if !scene.combat_distance(target, id).is_some_and(|distance| distance <= actor.rule.range)
                || now < actor.next_attack {
                continue;
            }
            if let Some(world)=geometry.as_deref_mut() {
                if !scene.player_placement(target).zip(scene.content_placement(id)).is_some_and(|(a,b)|world.clear(a,b,1.)){continue;}
            }
            let account = store.characters[&target].account.clone();
            let previous = store.characters[&target].progression.clone();
            // ArmorRating from Pyreal_DB_Insert_PlayerLevelData.sql. At least 1
            // damage remains so a hit is still a hit.
            let armor = previous.level().armor.saturating_add(crate::apparel::armor(&store.characters[&target].inventory,store.characters[&target].creation.as_ref().map_or(1,|r|r.race)));
            let offense=spell_ratings.get(&id).copied().unwrap_or_default();
            let protection=spell_ratings.get(&target).copied().unwrap_or_default();
            let armor=crate::combat::adjusted(armor,protection.armor);
            let attack=crate::combat::adjusted(actor.rule.level.unwrap_or(1)*8,offense.creature_attack());
            let defense=crate::combat::adjusted(crate::combat::player_defense(previous.level().number,&store.characters[&target].skills,crate::combat::School::Melee),protection.defense(crate::combat::School::Melee));
            if !self.dice.roll(crate::combat::hit_chance(attack,defense)) {
                actor.next_attack=now.saturating_add(actor.rule.interval);
                events.push(Event::IdolVisual(id,crate::combat_visual::IdolAction::Swing));
                events.push(Event::Note{player:target,text:"The creature's attack misses you.".into()});continue;
            }
            let dealt = actor.rule.damage.saturating_sub(armor).max(1);
            store.progression_command(&account, target, Command::Damage(dealt))?;
            actor.next_attack = now.saturating_add(actor.rule.interval);
            events.push(Event::IdolVisual(
                id,
                crate::combat_visual::IdolAction::Swing,
            ));
            if let Ok(damage) = i32::try_from(dealt) {
                events.push(Event::Hit {
                    victim: target,
                    attacker: id,
                    health_change: -damage,
                });
            }
            let state = store.characters[&target].progression.clone();
            events.push(Event::Note {
                player: target,
                text: format!("Creature hits you for {dealt} damage."),
            });
            if !state.alive() {
                self.attacks.remove(&target);
                events.push(Event::Stop(target));
                events.push(Event::Note {
                    player: target,
                    text: "You have fallen. Use Resurrect to recover.".into(),
                });
            }
            events.push(Event::Owner {
                player: target,
                previous,
                state,
            });
        }
        self.tick_mends(now,scene,store,&mut events)?;
        if now >= self.next_regen {
            self.next_regen = now.saturating_add(self.regen_interval);
            let busy: BTreeSet<_> = self
                .attacks
                .keys()
                .copied()
                .chain(self.actors.values().filter_map(|actor| actor.target))
                .collect();
            for player in active {
                if scene.player_placement(player).is_none() || busy.contains(&player) {
                    continue;
                }
                let Some(character) = store
                    .characters
                    .get(&player)
                    .filter(|c| !c.deleted && c.progression.alive())
                else {
                    continue;
                };
                let state = &character.progression;
                let level = state.level();
                let mut commands = Vec::new();
                let (h,v)=self.regen.iter().filter(|((who,_),_)|*who==player).fold((1f32,1f32),|(h,v),(_, (r,_))|((h*r.health).clamp(0.,100.),(v*r.vigor).clamp(0.,100.)));
                let health = (self.regen_health as f32*h).round() as u32;
                let vigor = (self.regen_vigor as f32*v).round() as u32;
                if health > 0 && state.health < level.health {
                    commands.push(Command::Heal(health));
                }
                if vigor > 0 && state.vigor < level.vigor {
                    commands.push(Command::RecoverVigor(vigor));
                }
                if commands.is_empty() {
                    continue;
                }
                let account = character.account.clone();
                let previous = character.progression.clone();
                store.progression_batch(&account, player, &commands)?;
                events.push(Event::Owner {
                    player,
                    previous,
                    state: store.characters[&player].progression.clone(),
                });
            }
        }
        for action in self.spawners.poll(now) {
            match action {
                crate::spawners::Action::Remove(id)=>events.push(Event::Despawn(id)),
                crate::spawners::Action::Spawn(id)=>events.push(Event::Respawn{actor:id,health:self.actors[&id].rule.health}),
            }
        }
        Ok(events)
    }
    /// Restore a player's vitals: once on the next tick when `duration_ms` is 0,
    /// otherwise every period until it expires. Recasting refreshes the effect.
    pub fn mend(
        &mut self,
        source: u64,
        skill: u32,
        player: u64,
        over: Over,
        fx: u32,
        now: u64,
    ) -> io::Result<()> {
        if over.health == 0 && over.vigor == 0 {
            return Err(invalid("empty restoration"));
        }
        let period = over.period_ms.max(250);
        if over.duration_ms > 0 {
            self.mends.retain(|m| !(m.source == source && m.skill == skill && m.player == player && !m.instant));
        }
        if self.mends.len() >= MAX_TIMED {
            return Err(invalid("timed effect capacity"));
        }
        self.mends.push(Mend {
            instant: over.duration_ms == 0,
            source,
            skill,
            player,
            health: over.health,
            vigor: over.vigor,
            period,
            next: if over.duration_ms == 0 {now} else {now.saturating_add(period)},
            until: now.saturating_add(over.duration_ms),
            fx,
        });
        Ok(())
    }

    pub fn add_ratings(&mut self, entity: u64, skill: u32, ratings: crate::combat::Ratings, duration_ms: u64, now: u64) -> io::Result<()> {
        if ratings.is_empty() || entity == 0 {
            return Ok(());
        }
        self.ratings.retain(|(who, id, _, until)| !(*who == entity && *id == skill) && *until > now);
        if self.ratings.len() >= MAX_TIMED {
            return Err(invalid("timed effect capacity"));
        }
        self.ratings.push((entity, skill, ratings, now.saturating_add(duration_ms.max(1000))));
        Ok(())
    }
    fn ratings_for(&self, entity: u64) -> crate::combat::Ratings {
        let mut total = crate::combat::Ratings::default();
        for (who, _, ratings, _) in &self.ratings {
            if *who == entity {
                total.add(*ratings);
            }
        }
        total
    }
    /// Multiply a player's regeneration until the buff expires.
    pub fn boost_regen(&mut self, player: u64, skill:u32, regen: Regen, now: u64) -> io::Result<()> {
        if !(regen.health.is_finite() && regen.vigor.is_finite())
            || regen.health < 0.0
            || regen.vigor < 0.0
            || regen.health > 100.0
            || regen.vigor > 100.0
        {
            return Err(invalid("invalid regeneration buff"));
        }
        if self.regen.len() >= MAX_TIMED && !self.regen.contains_key(&(player,skill)) {return Err(invalid("timed effect capacity"));}
        self.regen.insert((player,skill),(regen,now.saturating_add(regen.duration_ms)));
        Ok(())
    }
    fn tick_mends(
        &mut self,
        now: u64,
        scene: &Scene,
        store: &mut Store,
        events: &mut Vec<Event>,
    ) -> io::Result<()> {
        for mend in &mut self.mends {
            if mend.next > now || mend.next > mend.until || !mend.instant && now > mend.until {
                continue;
            }
            mend.next = now.saturating_add(mend.period);
            let Some(character) = store
                .characters
                .get(&mend.player)
                .filter(|c| !c.deleted && c.progression.alive())
            else {
                mend.until = 0;
                continue;
            };
            if scene.player_placement(mend.player).is_none() {
                continue;
            }
            if mend.fx != 0 {
                events.push(Event::Fx {
                    target: mend.player,
                    fx: std::mem::take(&mut mend.fx),
                });
            }
            let level = character.progression.level();
            let mut commands = Vec::new();
            if mend.health > 0 && character.progression.health < level.health {
                commands.push(Command::Heal(mend.health));
            }
            if mend.vigor > 0 && character.progression.vigor < level.vigor {
                commands.push(Command::RecoverVigor(mend.vigor));
            }
            if commands.is_empty() {
                continue;
            }
            let account = character.account.clone();
            let previous = character.progression.clone();
            store.progression_batch(&account, mend.player, &commands)?;
            let state = store.characters[&mend.player].progression.clone();
            let healed = state.health.saturating_sub(previous.health);
            if healed > 0 {
                if let Ok(healed) = i32::try_from(healed) {
                    events.push(Event::Hit {
                        victim: mend.player,
                        attacker: mend.source,
                        health_change: healed,
                    });
                }
            }
            events.push(Event::Owner {
                player: mend.player,
                previous,
                state,
            });
        }
        self.mends.retain(|mend| mend.next <= mend.until && (mend.instant || now <= mend.until));
        Ok(())
    }
    /// Capacity is checked before any progression persistence or queue mutation.
    pub fn admit_effects(&self) -> io::Result<()> {
        let pending = self.attacks.values().filter(|a|a.strike.is_some()).count();
        if self.burns.len()+pending+2>MAX_TIMED || self.mends.len()+2>MAX_TIMED || self.ratings.len()+pending+2>MAX_TIMED || self.regen.len()+1>MAX_TIMED {
            return Err(invalid("Timed effect capacity; try again shortly."));
        } Ok(())
    }
    pub fn strike(&mut self, player:u64, target:u64, now:u64, scene:&Scene, store:&Store, strike:Strike) -> io::Result<()> {
        if !strike.range.is_finite() || strike.range<0. || !self.actors.get(&target).is_some_and(|a|a.health>0 && !a.evading) {
            return Err(invalid("Select a living attackable creature."));
        }
        if !store.characters.get(&player).is_some_and(|c|!c.deleted && c.progression.alive()) {
            return Err(invalid("The caster must be alive."));
        }
        if !scene.combat_distance(player,target).is_some_and(|d|d<=strike.range.max(self.range)) {
            return Err(invalid("Your target is out of skill range."));
        }
        if self.attacks.get(&player).is_some_and(|a|a.stage!=0) { return Err(invalid("Finish the current attack before casting.")); }
        self.admit_effects()?;
        if self.attacks.len()>=64 && !self.attacks.contains_key(&player) {return Err(invalid("Attack capacity."));}
        let repeat=self.attacks.get(&player).is_some_and(|a|a.repeat && a.target==target);
        let category=crate::starter_gear::equipped_category(&store.characters[&player].inventory);
        self.attacks.insert(player,Attack{target,next:now,repeat,category,stage:0,started:now,skill:true,strike:Some(strike),periodic:false});
        Ok(())
    }

}
