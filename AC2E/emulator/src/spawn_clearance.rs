//! Extra spawn reservations for players and known noncombat NPCs. Body sizes
//! come from native CSetup and VisualDesc scale, never mesh/render bounds.
//! This is spawn admission, not player collision or a crowd-navigation solver.
use crate::{content::{Content,Object},creature_motion::{Catalog,Profile},dat::Dat,
    geometry::Occupancy,scene::Scene,services::Service,store::Store};
use std::{collections::{BTreeMap,BTreeSet},io,sync::OnceLock};

#[derive(Clone,Default)]
pub(crate) struct Blockers {
    players:BTreeMap<(u32,u32),Profile>,
    npcs:BTreeMap<u64,Profile>,
}
fn known_npc(object:&Object)->bool {
    static PROTECTED:OnceLock<BTreeSet<u32>>=OnceLock::new();
    object.service==Some(Service::Vendor) || PROTECTED.get_or_init(||
        include_str!("../content/population-protected.txt").lines()
            .filter_map(|s|u32::from_str_radix(s,16).ok()).collect()).contains(&object.entity)
}
impl Blockers {
    pub fn load(content:&Content,dat:&mut Dat,catalog:&mut Catalog,
        creature:impl Fn(u64)->bool)->io::Result<Self> {
        let mut result=Self::default();
        for body in crate::appearance::bodies() {
            let profile=catalog.resolve_scaled(dat,body.visual,1.,Some([1.;3]))?
                .ok_or_else(||io::Error::other("Missing native player body dimensions"))?;
            result.players.insert((body.race,body.sex),profile);
        }
        for object in content.objects().filter(|o|!creature(o.id)&&known_npc(o)) {
            result.add_npc(object,catalog.resolve(dat,object.visual,object.scale)?);
        }
        Ok(result)
    }
    pub fn add_npc(&mut self,object:&Object,profile:Option<Profile>) {
        self.npcs.remove(&object.id);
        if let Some(profile)=profile.filter(|p|p.grounded && known_npc(object)) {
            self.npcs.insert(object.id,profile);
        }
    }
    pub fn remove(&mut self,id:u64) {self.npcs.remove(&id);}
    pub fn reserve(&self,scene:&Scene,store:&Store,active:&[u64],occupied:&mut Occupancy) {
        for (&id,body) in &self.npcs {
            if let Some(p)=scene.content_placement(id) {
                occupied.insert(id,p,body.radius,body.height);
            }
        }
        // Iterate connected players, not the potentially much larger saved roster.
        for &id in active {
            let Some(character)=store.characters.get(&id) else{continue};
            if character.deleted || !character.progression.alive(){continue;}
            let Some(p)=scene.player_placement(id) else{continue};
            let (race,sex)=character.creation.as_ref().map_or((1,0x1000),|c|(c.race,c.sex));
            let Some(body)=self.players.get(&(race,sex)) else{continue};
            let scale=|key|character.creation.as_ref().and_then(|c|c.physical_values.iter().rev()
                .find(|(k,_)|*k==key).map(|(_,bits)|f32::from_bits(*bits))).unwrap_or(1.);
            occupied.insert(id,p,body.radius*scale(1),body.height*scale(2));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{movement::Placement,scene::Body,world};

    pub(crate) fn player(id:u64,p:Placement)->Body {
        let bytes=world::create_stationary_object(&world::StationaryObject {
            character_id:id,parent_visual_did:1,entity_did:2,cell_id:p.cell,
            position:p.coordinates,orientation:p.orientation(),instance_stamp:1,
            visual_ordering_stamp:0,timestamps:[0;4]}).unwrap();
        Body::new(id,p,bytes).unwrap()
    }
    #[test]
    fn reservations_follow_ready_living_players_and_their_build_and_height() {
        let path=std::env::temp_dir().join(format!("ac2-player-clearance-{}",std::process::id()));
        let mut store=Store::open(&path).unwrap();
        let request=world::CharacterCreationRequest {name:"Clearance Tester".into(),entity_did:0x47000001,
            start_area:0,race:4,sex:0x2000,class:0,implement:0,
            physical_values:vec![(1,1.5f32.to_bits()),(2,1.5f32.to_bits())]};
        let id=store.create_native("local",&request).unwrap().id;
        let mut blockers=Blockers::default();
        blockers.players.insert((4,0x2000),Profile{radius:0.5,height:2.,..Profile::default()});
        let home=Placement::HUMAN_START;
        let content=Content::fixture(crate::content::NAMESPACE+7,home,None);
        let mut scene=Scene::with_content(content);
        scene.enter_hidden(player(id,home)).unwrap();scene.preload(id).unwrap();
        let free=|scene:&Scene,store:&Store,active:&[u64],p:Placement|{
            let mut occupied=Occupancy::default();blockers.reserve(scene,store,active,&mut occupied);
            occupied.permits(999,p,p,0.25,1.)
        };
        assert!(free(&scene,&store,&[id],home),"Preloading players have no world body");
        scene.activate(id).unwrap();
        assert!(!free(&scene,&store,&[id],home.translated([1.,0.,0.]).unwrap()),"Player build widens the body");
        assert!(free(&scene,&store,&[id],home.translated([1.2,0.,0.]).unwrap()));
        assert!(!free(&scene,&store,&[id],home.translated([0.,0.,2.5]).unwrap()),"Height reserves the upper body");
        assert!(free(&scene,&store,&[id],home.translated([0.,0.,3.1]).unwrap()),"Other floors remain available");
        assert!(free(&scene,&store,&[],home),"Offline saved characters do not reserve space");
        store.characters.get_mut(&id).unwrap().progression.health=0;
        assert!(free(&scene,&store,&[id],home),"Dead players do not hold a living spawn slot");
        store.characters.get_mut(&id).unwrap().progression.health=100;
        store.characters.get_mut(&id).unwrap().deleted=true;
        assert!(free(&scene,&store,&[id],home));
        store.characters.get_mut(&id).unwrap().deleted=false;
        scene.remove(id);
        assert!(free(&scene,&store,&[id],home),"Disconnected scene bodies cannot leave a reservation");
        drop(store);std::fs::remove_dir_all(path).unwrap();
    }
    #[test]
    fn only_known_grounded_npcs_reserve_space() {
        let id=crate::content::NAMESPACE+7;
        let mut object=Content::fixture(id,Placement::HUMAN_START,None).into_objects().remove(&id).unwrap();
        let mut blockers=Blockers::default();
        blockers.add_npc(&object,Some(Profile::default()));
        assert!(blockers.npcs.is_empty(),"Arbitrary scenery and enemies are not NPCs");
        object.entity=0x47001c27; // Captured Orven, protected from generic creature AI.
        blockers.add_npc(&object,Some(Profile::default()));assert!(blockers.npcs.contains_key(&id));
        blockers.add_npc(&object,None);assert!(blockers.npcs.is_empty(),"Missing native body must not invent one");
        blockers.add_npc(&object,Some(Profile{grounded:false,..Profile::default()}));
        assert!(blockers.npcs.is_empty(),"Hovering/FX props are not grounded NPC bodies");
        object.entity=1;object.service=Some(Service::Vendor);
        blockers.add_npc(&object,Some(Profile::default()));assert!(blockers.npcs.contains_key(&id));
        blockers.remove(id);assert!(blockers.npcs.is_empty());
    }
}
