//! Bounded server-authored placements. DAT presence is checked before publication.
use crate::{
    dat::Dat,
    movement::Placement,
    world::{self, StationaryObject},
};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::File,
    io::{self, Read},
    path::Path,
    sync::Arc,
};

#[cfg(test)]
mod motion_tests {
    use super::*;
    fn spawn(point: Placement, instance:u16) -> Vec<u8> {
        world::create_stationary_object(&StationaryObject {character_id:NAMESPACE+1,
            parent_visual_did:0x1f0003d1,entity_did:crate::quests::TARGET_ENTITY,
            cell_id:point.cell,position:point.coordinates,orientation:point.orientation(),
            instance_stamp:instance,visual_ordering_stamp:0,timestamps:[0;4]}).unwrap()
    }
    #[test]
    fn pursuit_updates_late_observers_and_respawn_returns_to_authored_home() {
        let home=Placement::HUMAN_START;
        let id=NAMESPACE+1;
        let mut content=Content::default();
        content.objects.insert(id,Object {id,entity:crate::quests::TARGET_ENTITY,placement:home,home,
            spawn:spawn(home,1).into(),destroy:vec![0;16].into(),active:true,instance:1,
            visual_stamp:0,position_stamp:0,stats:BTreeMap::new()});
        let mut next=home;next.coordinates[0]+=1.5;next.heading=64;
        content.move_creature(id,next,2500).unwrap();
        assert_eq!(&*content.object(id).unwrap().spawn,spawn(next,1));
        let mut wrong=next;wrong.cell+=1;
        assert!(content.move_creature(id,wrong,2600).is_err());
        assert_eq!(content.object(id).unwrap().placement,next);
        content.despawn(id).unwrap();content.respawn(id).unwrap();
        assert_eq!(&*content.object(id).unwrap().spawn,spawn(home,2));
        assert_eq!(content.object(id).unwrap().placement,home);
    }
}

pub const NAMESPACE: u64 = 0x8140000000000000;
const MAX_BYTES: u64 = 4 * 1024 * 1024;
const MAX_OBJECTS: usize = 16384;
const MAX_CELL_OBJECTS: usize = 64;
fn invalid(message: impl Into<String>) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message.into())
}
pub struct Object {
    pub id: u64,
    pub entity: u32,
    pub placement: Placement,
    home: Placement,
    pub spawn: Arc<[u8]>,
    pub destroy: Arc<[u8]>,
    active: bool,
    instance: u16,
    visual_stamp: u16,
    position_stamp: u16,
    stats: BTreeMap<u32, (u32, Arc<[u8]>)>,
}
impl Object {
    pub fn stat(&self, key: u32) -> Option<u32> {
        self.stats.get(&key).map(|(value, _)| *value)
    }
    pub fn updates(&self) -> impl Iterator<Item = &Arc<[u8]>> {
        self.stats
            .get(&0x107)
            .map(|(_, wire)| wire)
            .into_iter()
            .chain(
                self.stats
                    .iter()
                    .filter(|(key, _)| **key != 0x107)
                    .map(|(_, (_, wire))| wire),
            )
    }
    fn stat_wire(&self, key: u32, value: u32) -> Arc<[u8]> {
        world::visual_int(self.id, self.instance, key, value).into()
    }
}
#[derive(Default)]
pub struct Content {
    objects: BTreeMap<u64, Object>,
    cells: BTreeMap<u32, Vec<u64>>,
}
impl Content {
    /// Bounded same-cell creature pursuit. Retain the new pose in CreateObject
    /// too, so late observers receive the same location as existing observers.
    pub fn move_creature(&mut self, id: u64, placement: Placement, now: u64) -> io::Result<Arc<[u8]>> {
        let object = self.objects.get_mut(&id).filter(|o| o.active && o.entity == crate::quests::TARGET_ENTITY)
            .ok_or_else(|| invalid("unsupported moving creature"))?;
        if !placement.valid() || placement.cell != object.placement.cell {
            return Err(invalid("creature pursuit cannot cross cell boundaries"));
        }
        object.position_stamp = object.position_stamp.checked_add(1).ok_or_else(|| invalid("creature position stamp exhausted"))?;
        Self::set_pose(object, placement)?;
        let update = crate::movement::ClientPosition {
            time: now as f64 / 1000.0, placement, motion: u32::from(placement.heading) << 24,
            cancel_moveto: false, stamps: [object.instance,0,0,0], jump_velocity: None,
        };
        crate::movement::server_position(id,object.instance,object.position_stamp,&update)
            .map(Arc::from).map_err(io::Error::other)
    }
    fn set_pose(object: &mut Object, placement: Placement) -> io::Result<()> {
        // StationaryObject's pinned Idol serializer: mode, one scale slider,
        // then Position. Do not guess offsets for arbitrary visual profiles.
        let spawn = Arc::make_mut(&mut object.spawn);
        if spawn.get(20..24) != Some(0x8082u32.to_le_bytes().as_slice()) || spawn.len() < 96
            || spawn.get(32..36) != Some(1u32.to_le_bytes().as_slice()) {
            return Err(invalid("unsupported creature position layout"));
        }
        // A slider stores its key and two floats; Position starts at byte 48.
        spawn[48..52].copy_from_slice(&placement.cell.to_le_bytes());
        for (i,v) in placement.coordinates.iter().enumerate() { spawn[52+i*4..56+i*4].copy_from_slice(&v.to_le_bytes()); }
        for (i,v) in placement.orientation().iter().enumerate() {spawn[64+i*4..68+i*4].copy_from_slice(&v.to_le_bytes());}
        object.placement=placement;
        Ok(())
    }
    /// The shipped snapshot works independently of the current directory.
    pub fn bundled(game_data: &Path) -> io::Result<Self> {
        Self::parse(include_str!("../content/world.tsv"), game_data)
    }
    pub fn observed(game_data: &Path) -> io::Result<Self> {
        Self::parse(
            include_str!("../content/world-observed-expanded.tsv"),
            game_data,
        )
    }
    pub fn training(game_data: &Path) -> io::Result<Self> {
        let mut combined = String::from("pyrealium-world-v3\n");
        // Keep the retained actor snapshot when enabling the training preset;
        // replacing it with the portal/lifestone-only base leaves known actors
        // absent even though the server reports that world content is enabled.
        for line in include_str!("../content/world-observed-expanded.tsv")
            .lines()
            .skip(1)
        {
            combined.push_str(line);
            combined.push('\n');
        }
        for line in include_str!("../content/training-world.tsv")
            .lines()
            .skip(1)
        {
            combined.push_str(line);
            combined.push('\n');
        }
        for line in include_str!("../content/capture-spawns.tsv")
            .lines()
            .skip(1)
        {
            combined.push_str(line);
            combined.push('\n');
        }
        Self::parse(&combined, game_data)
    }
    pub fn len(&self) -> usize {
        self.objects.len()
    }
    pub fn is_empty(&self) -> bool {
        self.objects.is_empty()
    }
    pub fn cell(&self, id: u64) -> Option<u32> {
        self.objects
            .get(&id)
            .filter(|object| object.active)
            .map(|object| object.placement.cell)
    }
    pub fn objects(&self) -> impl Iterator<Item = &Object> {
        self.objects.values()
    }
    pub fn object(&self, id: u64) -> Option<&Object> {
        self.objects.get(&id)
    }
    pub fn set_stat(&mut self, id: u64, key: u32, value: u32) -> io::Result<Option<Arc<[u8]>>> {
        let object = self
            .objects
            .get_mut(&id)
            .ok_or_else(|| invalid("unknown content object"))?;
        if object.stats.get(&key).is_some_and(|(old, _)| *old == value) {
            return Ok(None);
        }
        if object.stats.len() >= 16 && !object.stats.contains_key(&key) {
            return Err(invalid("content stat limit"));
        }
        let wire = object.stat_wire(key, value);
        object.stats.insert(key, (value, wire.clone()));
        Ok(object.active.then_some(wire))
    }
    pub fn despawn(&mut self, id: u64) -> io::Result<Arc<[u8]>> {
        let object = self
            .objects
            .get_mut(&id)
            .filter(|object| object.active)
            .ok_or_else(|| invalid("content object not active"))?;
        object.active = false;
        Ok(object.destroy.clone())
    }
    pub fn idol_presentation(
        &mut self,
        id: u64,
        action: crate::combat_visual::IdolAction,
    ) -> io::Result<Vec<Arc<[u8]>>> {
        let object = self
            .objects
            .get_mut(&id)
            .filter(|object| object.active)
            .ok_or_else(|| invalid("inactive creature presentation"))?;
        // Idol and the two Shelter Drudges use native behavior table 44000018.
        // Keep unrelated observed creatures out of this validated presentation set.
        let expected_visual=match object.entity {
            crate::quests::TARGET_ENTITY=>0x1f0003d1u32,
            crate::shelter::DIGGER|crate::shelter::SENTINEL=>0x1f000036,
            _=>return Ok(Vec::new()),
        };
        if object.spawn.get(16..20) != Some(expected_visual.to_le_bytes().as_slice()) {
            return Ok(Vec::new());
        }
        if object.spawn.get(20..24) != Some(0x8082u32.to_le_bytes().as_slice()) {
            return Err(invalid("unsupported creature physics presentation"));
        }
        let (stamp, mode, events) =
            crate::combat_visual::idol(id, object.instance, object.visual_stamp, action)
                .map_err(|error| invalid(format!("creature presentation: {error}")))?;
        let spawn = Arc::make_mut(&mut object.spawn);
        spawn[26..28].copy_from_slice(&stamp.to_le_bytes());
        spawn[28..32].copy_from_slice(&mode.to_le_bytes());
        object.visual_stamp = stamp;
        Ok(events.into_iter().map(Arc::from).collect())
    }
    /// Next visual stamp on the victim. The story is presentation only.
    pub fn damage_story(
        &mut self,
        victim: u64,
        attacker: u64,
        health_change: i32,
    ) -> io::Result<Arc<[u8]>> {
        let object = self
            .objects
            .get_mut(&victim)
            .filter(|object| object.active)
            .ok_or_else(|| invalid("inactive creature damage story"))?;
        let stamp = object
            .visual_stamp
            .checked_add(1)
            .ok_or_else(|| invalid("creature visual stamp exhausted"))?;
        let wire = crate::combat_visual::damage_story(
            victim,
            object.instance,
            stamp,
            attacker,
            health_change,
        )
        .map_err(|error| invalid(format!("creature damage story: {error}")))?;
        if object.spawn.len() >= 28 {
            Arc::make_mut(&mut object.spawn)[26..28].copy_from_slice(&stamp.to_le_bytes());
        }
        object.visual_stamp = stamp;
        Ok(wire.into())
    }
    pub fn effect(&mut self,id:u64,fx:u32) -> io::Result<Arc<[u8]>> {
        let object=self.objects.get_mut(&id).filter(|o|o.active).ok_or_else(||invalid("inactive effect owner"))?;
        let stamp=object.visual_stamp.checked_add(1).ok_or_else(||invalid("effect stamp exhausted"))?;
        let wire=crate::combat_visual::effect(id,object.instance,stamp,fx).map_err(|e|invalid(e.to_string()))?;
        Arc::make_mut(&mut object.spawn)[26..28].copy_from_slice(&stamp.to_le_bytes());object.visual_stamp=stamp;Ok(wire.into())
    }
    pub fn respawn(&mut self, id: u64) -> io::Result<()> {
        let object = self
            .objects
            .get_mut(&id)
            .filter(|object| !object.active)
            .ok_or_else(|| invalid("content object already active"))?;
        if object.placement != object.home { Self::set_pose(object,object.home)?; }
        object.position_stamp=0;
        object.instance = object
            .instance
            .checked_add(1)
            .ok_or_else(|| invalid("content lifetime exhausted"))?;
        Arc::make_mut(&mut object.spawn)[24..26].copy_from_slice(&object.instance.to_le_bytes());
        object.visual_stamp = 0;
        Arc::make_mut(&mut object.spawn)[26..28].copy_from_slice(&0u16.to_le_bytes());
        if matches!(object.entity,crate::quests::TARGET_ENTITY|crate::shelter::DIGGER|crate::shelter::SENTINEL)
            && object.spawn.get(20..24) == Some(0x8082u32.to_le_bytes().as_slice())
        {
            Arc::make_mut(&mut object.spawn)[28..32]
                .copy_from_slice(&crate::combat_visual::PEACE.to_le_bytes());
        }
        Arc::make_mut(&mut object.destroy)[12..14].copy_from_slice(&object.instance.to_le_bytes());
        let keys = object
            .stats
            .iter()
            .map(|(&key, (value, _))| (key, *value))
            .collect::<Vec<_>>();
        for (key, value) in keys {
            let wire = object.stat_wire(key, value);
            object.stats.insert(key, (value, wire));
        }
        object.active = true;
        Ok(())
    }
    pub fn at(&self, cell: u32) -> impl Iterator<Item = &Object> {
        self.cells
            .get(&cell)
            .into_iter()
            .flatten()
            .map(|id| &self.objects[id])
            .filter(|object| object.active)
    }
    /// V1 uses IID/entity/visual/cell/X/Y/Z/heading; v2 replaces heading with
    /// quaternion W/X/Y/Z. Numeric identifiers are hex; positions are decimal.
    pub fn load(path: &Path, game_data: &Path) -> io::Result<Self> {
        let mut bytes = Vec::new();
        File::open(path)?
            .take(MAX_BYTES + 1)
            .read_to_end(&mut bytes)?;
        if bytes.len() as u64 > MAX_BYTES {
            return Err(invalid("world content exceeds 4 MiB"));
        }
        let text =
            std::str::from_utf8(&bytes).map_err(|_| invalid("world content is not UTF-8"))?;
        Self::parse(text, game_data)
    }
    fn parse(text: &str, game_data: &Path) -> io::Result<Self> {
        if text.len() as u64 > MAX_BYTES {
            return Err(invalid("world content exceeds 4 MiB"));
        }
        let mut lines = text.lines();
        let version = match lines.next() {
            Some("pyrealium-world-v1") => 1,
            Some("pyrealium-world-v2") => 2,
            Some("pyrealium-world-v3") => 3,
            _ => return Err(invalid("unsupported world content header")),
        };
        let mut content = Self::default();
        let mut portal = Dat::open(game_data.join("portal.dat"))?;
        let mut cells = Dat::open(game_data.join("cell_1.dat"))?;
        let mut assets = BTreeSet::new();
        let mut checked_cells = BTreeSet::new();
        for (line, text) in lines.enumerate() {
            if text.is_empty() || text.starts_with('#') {
                continue;
            }
            let parse = || -> io::Result<(u64, u32, u32, Placement, [f32; 4], Option<f32>)> {
                if text.len() > 512 {
                    return Err(invalid("placement exceeds 512 bytes"));
                }
                let fields: Vec<_> = text.split('\t').collect();
                if fields.len()
                    != match version {
                        1 => 8,
                        2 => 11,
                        _ => 12,
                    }
                {
                    return Err(invalid(
                        "placement field count does not match manifest version",
                    ));
                }
                let hex = |value: &str| {
                    u64::from_str_radix(value.strip_prefix("0x").unwrap_or(value), 16)
                        .map_err(|_| invalid("invalid hex identifier"))
                };
                let narrow = |index: usize| {
                    u32::try_from(hex(fields[index])?)
                        .map_err(|_| invalid("identifier exceeds u32"))
                };
                let id = hex(fields[0])?;
                let entity = narrow(1)?;
                let visual = narrow(2)?;
                let placement = Placement {
                    cell: narrow(3)?,
                    coordinates: [
                        fields[4].parse().map_err(|_| invalid("invalid X"))?,
                        fields[5].parse().map_err(|_| invalid("invalid Y"))?,
                        fields[6].parse().map_err(|_| invalid("invalid Z"))?,
                    ],
                    heading: if version >= 2 {
                        0
                    } else {
                        fields[7].parse().map_err(|_| invalid("invalid heading"))?
                    },
                };
                let orientation = if version >= 2 {
                    let mut rotation = [0.0f32; 4];
                    for (value, text) in rotation.iter_mut().zip(&fields[7..11]) {
                        *value = text.parse().map_err(|_| invalid("invalid quaternion"))?;
                    }
                    rotation
                } else {
                    placement.orientation()
                };
                let norm: f32 = orientation.iter().map(|value| value * value).sum();
                if !orientation.iter().all(|value| value.is_finite())
                    || !norm.is_finite()
                    || (norm - 1.0).abs() > 0.01
                {
                    return Err(invalid("invalid or non-normalized content quaternion"));
                }
                if id >> 48 != NAMESPACE >> 48
                    || id == NAMESPACE
                    || entity >> 24 != 0x47
                    || visual >> 24 != 0x1f
                    || !placement.valid()
                {
                    return Err(invalid(
                        "invalid placement identity, asset type or position",
                    ));
                }
                let scale = if version == 3 && fields[11] != "-" {
                    let scale: f32 = fields[11]
                        .parse()
                        .map_err(|_| invalid("invalid content scale"))?;
                    if !scale.is_finite() || scale <= 0.0 || scale > 100.0 {
                        return Err(invalid("content scale outside local bounds"));
                    }
                    Some(scale)
                } else {
                    None
                };
                Ok((id, entity, visual, placement, orientation, scale))
            };
            let (id, entity, visual, placement, orientation, scale) = parse()
                .map_err(|error| invalid(format!("world content line {}: {error}", line + 2)))?;
            if content.objects.len() >= MAX_OBJECTS || content.objects.contains_key(&id) {
                return Err(invalid("world content object limit or duplicate IID"));
            }
            let cell_ids = content.cells.entry(placement.cell).or_default();
            if cell_ids.len() >= MAX_CELL_OBJECTS {
                return Err(invalid("world content exceeds 64 objects in one cell"));
            }
            for did in [entity, visual] {
                if assets.insert(did) {
                    portal
                        .read(did)
                        .map_err(|error| invalid(format!("content asset {did:#010x}: {error}")))?;
                }
            }
            let cell_record = crate::dat::world_cell_record(placement.cell)?;
            if checked_cells.insert(cell_record) {
                cells.read(cell_record).map_err(|error| {
                    invalid(format!("content cell {:#010x}: {error}", placement.cell))
                })?;
            }
            let spawn = world::create_stationary_object_with_scale(
                &StationaryObject {
                    character_id: id,
                    parent_visual_did: visual,
                    entity_did: entity,
                    cell_id: placement.cell,
                    position: placement.coordinates,
                    orientation,
                    instance_stamp: 1,
                    visual_ordering_stamp: 0,
                    timestamps: [0; 4],
                },
                scale,
            )
            .map_err(|error| invalid(format!("content object: {error}")))?;
            let mut destroy = 0x100c2u32.to_le_bytes().to_vec();
            destroy.extend(id.to_le_bytes());
            destroy.extend(1u16.to_le_bytes());
            destroy.extend(0u16.to_le_bytes());
            content.objects.insert(
                id,
                Object {
                    id,
                    entity,
                    placement,
                    home: placement,
                    spawn: spawn.into(),
                    destroy: destroy.into(),
                    active: true,
                    instance: 1,
                    visual_stamp: 0,
                    position_stamp: 0,
                    stats: BTreeMap::new(),
                },
            );
            cell_ids.push(id);
        }
        for ids in content.cells.values_mut() {
            ids.sort_unstable();
        }
        Ok(content)
    }
}
