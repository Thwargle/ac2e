//! Deterministic development encounters. Rules are explicit server-authored data.
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
    fn world_rules_override_stats_respawn_and_plan_patrol_legs() {
        let id=crate::content::NAMESPACE+7;let home=Placement::HUMAN_START;
        let content=Content::fixture(id,home,None);
        let rules=crate::world_rules::Rules::parse(&format!("{}\nstats\t{id:016x}\t999\t-\t-\t30\t-\t-\t-\t-\t-\t-\n\
            group\t5\t1\t4000\t{id:016x}\npatrol\t{id:016x}\t0\t3,0\n",crate::world_rules::HEADER)).unwrap();
        let mut runtime=Encounters::parse_with_rules(include_str!("../content/development-encounters.tsv"),&content,&rules).unwrap();
        assert_eq!(runtime.initial_health().collect::<Vec<_>>(),vec![(id,999)]);
        assert!(runtime.inspection_stats().any(|row|row==(id,0x12e,30)));
        let mut scene=Scene::with_content(content);
        runtime.initialize_spawners(&mut scene,0).unwrap();
        assert_eq!(runtime.spawners.poll(0),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        scene.despawn_content(id).unwrap();runtime.removed(id,1000);
        assert!(runtime.spawners.poll(4999).is_empty());
        assert_eq!(runtime.spawners.poll(5000),vec![crate::spawners::Action::Spawn(id)],"group respawn wins");
        let actor=&runtime.actors[&id];
        assert_eq!(actor.patrol.as_ref().unwrap().waypoints,vec![[3.,0.]]);
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
#[derive(Clone, Copy, PartialEq)]
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
    /// Operator patrol route. Patrol legs are not recorded in `route`; only a
    /// pursuit is retraced, back to the patrol point where it began.
    patrol: Option<crate::world_rules::Patrol>,
    patrol_next: usize,
    patrol_wait: u64,
    /// Patrolling creatures leash around the point where they engaged.
    engaged_at: Option<Placement>,
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
}
pub enum Event {
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
    /// A live rules edit changed a creature's vitals or level.
    Stats { actor: u64, max_health: u32, health: u32, level: Option<u32> },
}
pub struct Encounters {
    actors: BTreeMap<u64, Actor>,
    spawners: crate::spawners::Spawners,
    bestiary: Bestiary,
    /// Events from live edits, returned by the next tick at once.
    pending: Vec<Event>,
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
fn approach(from:Placement,target:Placement,stop:f32)->Option<Placement> {
    let [dx,dy,_]=from.offset_to(target)?;
    let distance=dx.hypot(dy);
    if distance<=stop+0.01{return None;}
    let step=0.2f32.min(distance-stop);
    let next=from.translated([dx/distance*step,dy/distance*step,0.])?;
    facing(next,target)
}
/// Spawn groups from world rules carry this bit, so a live edit can replace
/// exactly them and leave captured and inferred groups alone.
pub const OPERATOR_GROUP: u64 = 1 << 63;

/// Native creature tables plus the operator rules: everything needed to build or
/// re-rule a creature after startup.
struct Bestiary {
    rules: BTreeMap<u32, Rule>,
    ai: BTreeMap<u32, crate::creature_ai::Profile>,
    labels: BTreeMap<u32, String>,
    protected: BTreeSet<u32>,
    spawn_policies: BTreeMap<u32, crate::spawners::Policy>,
    world: std::sync::Arc<crate::world_rules::Rules>,
}
impl Bestiary {
    /// The rule, AI and patrol a placement gets, or None when it is no creature.
    fn ruled(&self, entity: u32, id: u64) -> Option<(Rule, crate::creature_ai::Profile, Option<crate::world_rules::Patrol>)> {
        let custom = self.world.stats_for(entity, id);
        // Operator stats can make any placed non-service object a creature.
        let mut rule = self.rules.get(&entity).copied().or_else(|| custom.and_then(|s| Some(Rule {
            health: s.health?, damage: s.damage?, level: None, vigor: None, armor: 0, interval: 1500,
            range: 3.0, respawn: 30_000, xp: 0, gold: 0, leash: 18. })))?;
        let behaviour = self.world.behaviour_for(entity, id);
        let ai = behaviour.apply(self.ai.get(&entity).copied().unwrap_or_default());
        if ai.homesick.is_none() { rule.leash = rule.leash.max(ai.perception); }
        if let Some(radius) = behaviour.homesick.filter(|r| *r > 0.) { rule.leash = radius; }
        if let Some(s) = custom { rule = apply_stats(rule, s); }
        Some((rule, ai, self.world.patrols.get(&id).cloned()))
    }
    fn actor(&self, object: &crate::content::Object) -> Option<Actor> {
        // Authored dialogue roles override inherited combat data: the
        // training mentor's template also carries a monster profile.
        // Use the same identities as the interaction dispatcher.
        if object.service.is_some()
            || self.protected.contains(&object.entity)
            || object.entity == crate::training_dialogue::MENTOR_ENTITY
            || crate::shelter::supported(object.entity)
        {
            return None;
        }
        let (rule, ai, patrol) = self.ruled(object.entity, object.id)?;
        Some(Actor {
            entity: object.entity,
            name: object.name.clone().or_else(|| self.labels.get(&object.entity).cloned()),
            home: object.placement,
            pursuit: object.supports_pose_updates(),
            route: Vec::new(),
            rule,
            health: rule.health,
            target: None,
            next_attack: 0,
            ai, next_idle: object.id % 1000, wander_goal: None, evading: false, random: object.id,
            patrol, patrol_next: 0, patrol_wait: 0, engaged_at: None,
        })
    }
    /// Operator groups own their members' timers; operator respawn values win
    /// over development policies for the targeted type or placement.
    fn policy(&self, id: u64, actor: &Actor) -> crate::spawners::Policy {
        let custom = self.world.stats_for(actor.entity, id).and_then(|s| s.respawn_ms);
        let group = self.world.groups.iter().find(|g| g.members.contains(&id)).and_then(|g| g.regeneration_ms);
        let mut policy = self.spawn_policies.get(&actor.entity).copied().unwrap_or(crate::spawners::Policy {
            initial_delay_ms: 0,
            regeneration_ms: if id & 0xffffff0000000000 == 0x8140a00000000000 { 120000 } else { actor.rule.respawn } });
        if let Some(ms) = group.or(custom) { policy.regeneration_ms = ms; }
        policy
    }
    fn operator_groups(&self, actors: &BTreeMap<u64, Actor>) -> Vec<(u64, usize, Vec<u64>)> {
        // Members that are not placed creatures (deleted or services) are skipped.
        self.world.groups.iter().map(|g| (g.id, g.capacity,
            g.members.iter().copied().filter(|m| actors.contains_key(m)).collect())).collect()
    }
}

fn apply_stats(mut rule: Rule, s: crate::world_rules::Stats) -> Rule {
    if let Some(v) = s.health { rule.health = v; }
    if let Some(v) = s.damage { rule.damage = v; }
    if let Some(v) = s.armor { rule.armor = v; }
    if let Some(v) = s.level { rule.level = Some(v).filter(|level| *level > 0); }
    if let Some(v) = s.xp { rule.xp = v; }
    if let Some(v) = s.gold { rule.gold = v; }
    if let Some(v) = s.interval_ms { rule.interval = v; }
    if let Some(v) = s.range { rule.range = v; }
    if let Some(v) = s.leash { rule.leash = v; }
    if let Some(v) = s.respawn_ms { rule.respawn = v; }
    rule
}
impl Encounters {
    pub fn load(path: &Path, content: &Content) -> io::Result<Self> {
        Self::load_with_rules(path, content, &crate::world_rules::Rules::default())
    }
    pub fn load_with_rules(path: &Path, content: &Content, rules: &crate::world_rules::Rules) -> io::Result<Self> {
        if path == Path::new("training") {
            return Self::parse_with_rules(
                include_str!("../content/development-encounters.tsv"),
                content,
                rules,
            );
        }
        let mut bytes = Vec::new();
        File::open(path)?.take(65537).read_to_end(&mut bytes)?;
        if bytes.len() > 65536 {
            return Err(invalid("encounter profile exceeds 64 KiB"));
        }
        let text =
            std::str::from_utf8(&bytes).map_err(|_| invalid("encounter profile encoding"))?;
        Self::parse_with_rules(text, content, rules)
    }
    #[cfg(test)]
    fn parse(text: &str, content: &Content) -> io::Result<Self> {
        Self::parse_with_rules(text, content, &crate::world_rules::Rules::default())
    }
    fn parse_with_rules(text: &str, content: &Content, world_rules: &crate::world_rules::Rules) -> io::Result<Self> {
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
        for line in include_str!("../content/creature-combat.tsv").lines().chain(include_str!("../content/inferred-creature-combat-v1.tsv").lines()) {
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
            Some((entity,name.split('[').next().unwrap_or(name).trim().to_owned()))
        }).collect::<BTreeMap<_,_>>();
        let protected=include_str!("../content/population-protected.txt").lines()
            .filter_map(|line|u32::from_str_radix(line,16).ok()).collect::<std::collections::BTreeSet<_>>();
        let bestiary=Bestiary{rules,ai:crate::creature_ai::catalog()?,labels,protected,spawn_policies,
            world:std::sync::Arc::new(world_rules.clone())};
        let actors=content.objects().filter_map(|object|bestiary.actor(object).map(|actor|(object.id,actor)))
            .collect::<BTreeMap<_,_>>();
        if actors.is_empty() || actors.len() > 16384 {
            return Err(invalid("encounters require 1..16384 matching placed actors"));
        }
        if bestiary.spawn_policies.keys().any(|entity|!actors.values().any(|actor|actor.entity==*entity)) {
            return Err(invalid("spawner policy must name a placed combat creature"));
        }
        // Captures keep one point spawner per observation. The separate inferred
        // pack caps its authored rosters as groups; services remain excluded.
        let mut spawners=crate::spawners::Spawners::new(actors.iter().map(|(&id,actor)|(id,bestiary.policy(id,actor))))?;
        let authored=world_rules.groups.iter().flat_map(|g|g.members.iter().copied()).collect::<BTreeSet<_>>();
        for (id,capacity,members) in spawn_groups {
            let members=members.into_iter().filter(|m|!authored.contains(m)).collect::<Vec<_>>();
            if !members.is_empty() {spawners.group(id,capacity.min(members.len()),members)?;}
        }
        spawners.replace_groups(OPERATOR_GROUP,bestiary.operator_groups(&actors))
            .map_err(|error|invalid(&format!("world rule groups: {error}")))?;
        let inferred:serde_json::Value=serde_json::from_str(include_str!("../content/inferred-world-v1.report.json")).map_err(io::Error::other)?;
        for group in inferred["groups"].as_array().ok_or_else(||invalid("inferred spawn groups"))? {
            let members=group["members"].as_array().ok_or_else(||invalid("inferred roster"))?.iter().filter_map(|v|v.as_u64()).collect::<Vec<_>>();
            // External content can select only part of the authored pack.
            let members=members.into_iter().filter(|id|actors.contains_key(id)&&!authored.contains(id)).collect::<Vec<_>>();
            if !members.is_empty(){let capacity=(group["capacity"].as_u64().unwrap() as usize).min(members.len());
                spawners.group(group["id"].as_u64().unwrap(),capacity,members)?;}
        }
        Ok(Self {
            actors,
            spawners,
            bestiary,
            pending: Vec::new(),
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
            training_loot: false,
        })
    }
    /// Live edit: whether a newly placed object is a creature under the current rules.
    pub fn live_is_creature(&self, object: &crate::content::Object) -> bool {
        self.bestiary.actor(object).is_some()
    }
    /// Live edit: the operator rules every later live change is built from.
    pub fn live_set_rules(&mut self, rules: std::sync::Arc<crate::world_rules::Rules>) {
        self.bestiary.world = rules;
    }
    /// Live edit: a new creature placement. Content holds it inactive; its point
    /// spawner shows it through the normal spawn path on the next poll.
    pub fn live_add(&mut self, object: &crate::content::Object, now: u64) -> io::Result<()> {
        let actor = self.bestiary.actor(object).ok_or_else(|| invalid("object is not a creature"))?;
        if self.actors.len() >= 16384 { return Err(invalid("creature limit reached")); }
        self.spawners.add_point(object.id, self.bestiary.policy(object.id, &actor), now)?;
        self.actors.insert(object.id, actor);
        Ok(())
    }
    /// The stats a new creature's content object carries (as at startup).
    pub fn live_stats(&self, id: u64) -> Vec<(u32, u32)> {
        let Some(actor) = self.actors.get(&id) else { return Vec::new() };
        let rule = actor.rule;
        [(0x107, Some(rule.health)), (0x102, Some(rule.health)), (0x12e, rule.level), (0x108, rule.vigor), (0x103, rule.vigor)]
            .into_iter().filter_map(|(stat, value)| value.map(|v| (stat, v))).collect()
    }
    /// Live edit: the creature leaves the world. Attackers stop at once.
    pub fn live_remove(&mut self, id: u64) {
        if self.actors.remove(&id).is_none() { return; }
        self.spawners.remove_point(id);
        let attackers = self.attacks.iter().filter(|(_, a)| a.target == id).map(|(&p, _)| p).collect::<Vec<_>>();
        for player in attackers {
            self.attacks.remove(&player);
            self.pending.push(Event::Stop(player));
            self.pending.push(Event::Note { player, text: "Attack stopped: your target was removed by a world edit.".into() });
        }
    }
    /// Live edit: re-rule the creatures a rules change touches (`None`: all of them),
    /// then rebuild the operator spawn groups.
    pub fn live_rules(&mut self, affected: Option<&BTreeSet<crate::world_rules::Target>>) -> io::Result<()> {
        use crate::world_rules::Target;
        for (&id, actor) in &mut self.actors {
            if affected.is_some_and(|set| !set.contains(&Target::Actor(id)) && !set.contains(&Target::Entity(actor.entity))) {
                continue;
            }
            let Some((rule, ai, patrol)) = self.bestiary.ruled(actor.entity, id) else { continue };
            if rule != actor.rule {
                let full = actor.health == actor.rule.health;
                if actor.health > 0 {
                    actor.health = if full { rule.health } else { actor.health.min(rule.health) };
                    self.pending.push(Event::Stats { actor: id, max_health: rule.health, health: actor.health, level: rule.level });
                }
                actor.rule = rule;
            }
            // Made peaceful: drop the fight. A creature that was already peaceful
            // keeps defending itself against whoever attacked it.
            if actor.target.is_some() && actor.ai.aggressive() && !ai.aggressive() {
                actor.target = None;
                actor.evading = true;
                actor.wander_goal = None;
                self.pending.push(Event::IdolVisual(id, crate::combat_visual::IdolAction::Peace));
            }
            actor.ai = ai;
            if patrol != actor.patrol {
                actor.patrol = patrol;
                actor.patrol_next = 0;
                actor.patrol_wait = 0;
                if actor.target.is_none() { actor.wander_goal = None; }
            }
            self.spawners.set_policy(id, self.bestiary.policy(id, actor));
        }
        self.spawners.replace_groups(OPERATOR_GROUP, self.bestiary.operator_groups(&self.actors))
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
    #[cfg(test)]
    pub(crate) fn spawners_mut_for_test(&mut self) -> &mut crate::spawners::Spawners { &mut self.spawners }
    /// Called before any client can observe the scene. Only managed monsters
    /// are withdrawn; vendors, dialogue actors and world objects remain placed.
    pub fn initialize_spawners(&mut self,scene:&mut Scene,now:u64)->io::Result<()> {
        for (&id,actor) in &mut self.actors {
            if scene.content_placement(id).is_some(){scene.despawn_content(id)?;}
            actor.health=0;actor.target=None;actor.route.clear();
            actor.patrol_next=0;actor.patrol_wait=0;actor.engaged_at=None;
        }
        self.spawners.initialize(now);
        Ok(())
    }
    /// Acknowledge only after Scene has created the new network lifetime.
    pub fn spawned(&mut self,id:u64)->io::Result<()> {
        if !self.spawners.spawned(id){return Err(invalid("unexpected spawner child"));}
        let actor=self.actors.get_mut(&id).ok_or_else(||invalid("unknown spawner child"))?;
        actor.health=actor.rule.health;actor.target=None;actor.route.clear();actor.next_attack=0;
        actor.evading=false;actor.wander_goal=None;
        actor.patrol_next=0;actor.patrol_wait=0;actor.engaged_at=None;
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
            return Ok(std::mem::take(&mut self.pending));
        }
        self.next_tick = now.saturating_add(100);
        let mut events = std::mem::take(&mut self.pending);
        let active=active.collect::<Vec<_>>();
        let due = self
            .attacks
            .iter()
            .filter(|(_, attack)| attack.next <= now)
            .map(|(&id, &attack)| (id, attack))
            .collect::<Vec<_>>();
        for (player, attack) in due {
            if !self.attacks.contains_key(&player) {
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
                || !scene.combat_distance(player, attack.target).is_some_and(|distance|
                    distance <= if attack.category == 0 { self.range } else { 18.0 })
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
                self.stopped(player, &mut events);
                continue;
            }
            if let Some(world)=geometry.as_deref_mut() {
                let clear=scene.player_placement(player).zip(scene.content_placement(attack.target))
                    .is_some_and(|(a,b)|world.clear(a,b,1.));
                if !clear {events.push(Event::Note{player,text:"Attack stopped: your target is obstructed.".into()});
                    self.stopped(player,&mut events);continue;}
            }
            if attack.category != 0 && attack.stage == 0 {
                let cost = if attack.category == 2 { self.cost.max(6) } else { self.cost };
                if store.characters[&player].progression.apply(&Command::SpendVigor(cost)).is_err() {
                    events.push(Event::Note { player, text: "Attack stopped: insufficient vigor.".into() });
                    self.stopped(player, &mut events);
                    continue;
                }
                events.push(Event::Swing(player));
                let queued = self.attacks.get_mut(&player).unwrap();
                queued.started = now;
                queued.stage = 1;
                queued.next = now.saturating_add(crate::projectiles::RELEASE_MS);
                continue;
            }
            if attack.category != 0 && attack.stage == 1 {
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
            let damage = if attack.category == 2 { self.damage.saturating_add(4) } else { self.damage };
            let cost = if attack.category == 2 { self.cost.max(6) } else { self.cost };
            let dealt = damage
                .saturating_sub(actor.rule.armor)
                .max(1)
                .min(actor.health);
            let killed = dealt == actor.health;
            let mut commands = vec![Command::SpendVigor(cost)];
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
                self.stopped(player, &mut events);
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
            } else {
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
            let until = if attack.category == 0 { now.saturating_add(self.interval) }
                else { attack.started.saturating_add(self.interval).max(now.saturating_add(100)) };
            self.cooldowns.insert(player, until);
            self.attacks.get_mut(&player).unwrap().next = until;
            self.attacks.get_mut(&player).unwrap().stage = 0;
            self.attacks.get_mut(&player).unwrap().skill = false;
            let actor = self.actors.get_mut(&attack.target).unwrap();
            actor.health -= dealt;
            if attack.category == 0 { events.push(Event::Swing(player)); }
            if let Ok(dealt) = i32::try_from(dealt) {
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
                text: format!("You hit for {dealt} damage."),
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
                if actor.patrol.is_some() {actor.engaged_at=scene.content_placement(attack.target);}
                actor.next_attack = now.saturating_add(actor.rule.interval);
                events.push(Event::IdolVisual(
                    attack.target,
                    crate::combat_visual::IdolAction::Combat,
                ));
            }
            if !attack.repeat {
                self.attacks.remove(&player);
            }
        }
        for (&id, actor) in &mut self.actors {
            // A real removal may also come from a script. Refill the owned slot;
            // leaving the client's PVS never removes content from the server.
            if actor.health>0 && scene.content_placement(id).is_none() {
                actor.health=0;actor.target=None;actor.route.clear();
                self.spawners.removed(id,now);
            }
            if actor.health == 0 {
                continue;
            }
            if actor.target.is_none() && !actor.evading && now>=actor.next_idle {
                // Native detection radius, with a staggered 1s scan. Do not
                // load collision or roam distant, unobserved regions.
                actor.next_idle=now.saturating_add(1000);
                let nearby=active.iter().filter_map(|&player| {
                    let alive=store.characters.get(&player).is_some_and(|c|!c.deleted && c.progression.alive());
                    alive.then(||scene.combat_distance(player,id).map(|d|(player,d))).flatten()
                }).filter(|(_,distance)|*distance<=actor.ai.perception.max(actor.ai.wander_range+20.)
                    .max(if actor.patrol.is_some() {80.} else {0.}))
                  .collect::<Vec<_>>();
                // A patrolling creature leashes around where it currently is.
                let center=if actor.patrol.is_some() {scene.content_placement(id)} else {Some(actor.home)};
                if actor.ai.aggressive() {
                    let mut candidates=nearby.iter().filter(|(_,d)|*d<=actor.ai.perception)
                        .filter(|(p,_)|in_range(center,scene.player_placement(*p),actor.rule.leash))
                        .copied().collect::<Vec<_>>();
                    candidates.sort_by(|a,b|a.1.total_cmp(&b.1).then(a.0.cmp(&b.0)));
                    // A nearer player behind a wall must not mask a visible
                    // player who is also within the native perception radius.
                    let target=candidates.into_iter().find(|(player,_)|
                        scene.player_placement(*player).zip(scene.content_placement(id))
                            .is_some_and(|(a,b)|geometry.as_deref_mut().is_some_and(|world|world.clear(a,b,1.))));
                    if let Some((player,_))=target {
                        actor.target=Some(player);actor.wander_goal=None;
                        if actor.patrol.is_some() {actor.engaged_at=center;}
                        actor.next_attack=now.saturating_add(actor.rule.interval);
                        events.push(Event::IdolVisual(id,crate::combat_visual::IdolAction::Combat));
                    }
                }
                if let Some(patrol)=actor.patrol.as_ref().filter(|_|actor.target.is_none() && actor.wander_goal.is_none()
                    && actor.route.is_empty() && actor.pursuit && !nearby.is_empty() && now>=actor.patrol_wait) {
                    // Waypoints in order, then back to the spawn point; repeat.
                    let leg=actor.patrol_next%(patrol.waypoints.len()+1);
                    let [dx,dy]=patrol.waypoints.get(leg).copied().unwrap_or([0.,0.]);
                    actor.wander_goal=actor.home.translated([dx,dy,0.]);
                } else if actor.patrol.is_none() && actor.target.is_none() && actor.wander_goal.is_none() && actor.route.is_empty()
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
                            if let Some(next)=approach(point,goal,0.2).and_then(|proposed|
                                geometry.as_deref_mut().and_then(|world|world.walk_step(point,proposed,0.35))) {
                                if actor.patrol.is_none() {actor.route.push(point);}
                                events.push(Event::Move{actor:id,placement:next});continue;
                            }
                        }
                        actor.wander_goal=None;
                        if let Some(patrol)=&actor.patrol {
                            // Arrived, or blocked: either way continue with the next leg.
                            actor.patrol_next=(actor.patrol_next+1)%(patrol.waypoints.len()+1);
                            actor.patrol_wait=now.saturating_add(patrol.pause_ms);
                        }
                    }
                    // A blocked return waits for geometry; never teleport or
                    // walk through an obstacle to restore an authored spawn.
                    while actor.route.last().is_some_and(|homeward|point.offset_to(*homeward)
                        .is_some_and(|[x,y,z]|x.hypot(y)<0.04 && z.abs()<=0.65)) {actor.route.pop();}
                    if actor.route.is_empty(){actor.evading=false;}
                    if let Some(&homeward)=actor.route.last() {
                        if let Some(mut next)=geometry.as_deref_mut().and_then(|world|world.walk_step(point,homeward,0.35)) {
                            if actor.route.len()>1 {
                                if let Some([x,y,_])=point.offset_to(next){next.heading=(x.atan2(y).to_degrees().rem_euclid(360.)/360.*255.).round() as u8;}
                            }
                            events.push(Event::Move{actor:id,placement:next});
                        }
                    }
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
                    && in_range(Some(actor.engaged_at.unwrap_or(actor.home)),scene.player_placement(player),actor.rule.leash)
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
                    actor.evading=true;actor.wander_goal=None;actor.engaged_at=None;
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
                    let step=approach(from,player,(actor.rule.range-0.5).max(0.5))
                        .and_then(|proposed|geometry.as_deref_mut().and_then(|world|world.walk_step(from,proposed,0.35)));
                    if let Some(next)=step {
                        actor.route.push(from);
                        events.push(Event::Move{actor:id,placement:next});
                    } else if let Some(turned)=facing(from,player).filter(|p|p.heading!=from.heading) {
                        // Targets can circle inside melee range. Turn in place
                        // without adding a fake walking step to the home route.
                        events.push(Event::Move{actor:id,placement:turned});
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
                if self.regen_health > 0 && state.health < level.health {
                    commands.push(Command::Heal(self.regen_health));
                }
                if self.regen_vigor > 0 && state.vigor < level.vigor {
                    commands.push(Command::RecoverVigor(self.regen_vigor));
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
}
