//! Retail CObjCell::LOSCheck over native terrain, interior BSPs and static physics.
//! This validates contacts/reach; it does not claim an authoritative movement solver.
use crate::movement::Placement;
use ac2_core::{CellId,DataId};
use ac2_physics::{Frame,LosParameters,Position,Quaternion,Ray,Terrain,Vector};
use ac2_world::Ac2WorldSource;
use std::{collections::BTreeMap,io,path::Path,sync::Arc};

/// Per-tick living-body reservations. Outdoor coordinates cross block borders;
/// dungeon blocks have independent spaces. Earlier moves reserve their endpoint
/// immediately so two actors cannot choose the same space in one server tick.
#[derive(Default)]
pub struct Occupancy {
    buckets:BTreeMap<(u32,i32,i32),BTreeMap<u64,(Placement,f32,f32)>>,
    max_radius:f32,
}
impl Occupancy {
    fn point(p:Placement)->(u32,f32,f32) {
        if (1..=64).contains(&(p.cell&65535)) {
            (0,(p.cell>>24) as f32*160.+p.coordinates[0],((p.cell>>16)&255) as f32*160.+p.coordinates[1])
        } else {(p.cell>>16,p.coordinates[0],p.coordinates[1])}
    }
    fn key(p:Placement)->(u32,i32,i32) {let (s,x,y)=Self::point(p);(s,(x/16.).floor() as i32,(y/16.).floor() as i32)}
    pub fn insert(&mut self,id:u64,p:Placement,radius:f32,height:f32) {
        self.max_radius=self.max_radius.max(radius);
        self.buckets.entry(Self::key(p)).or_default().insert(id,(p,radius,height));
    }
    pub fn reserve(&mut self,id:u64,from:Placement,to:Placement,radius:f32,height:f32) {
        if let Some(bucket)=self.buckets.get_mut(&Self::key(from)){bucket.remove(&id);}
        self.insert(id,to,radius,height);
    }
    pub fn permits(&self,id:u64,from:Placement,to:Placement,radius:f32,height:f32)->bool {
        let Some([dx,dy,_])=from.offset_to(to) else{return false};
        let (space,x,y)=Self::point(to);let reach=radius+self.max_radius+dx.hypot(dy)+0.1;
        for bx in ((x-reach)/16.).floor() as i32..=((x+reach)/16.).floor() as i32 {
            for by in ((y-reach)/16.).floor() as i32..=((y+reach)/16.).floor() as i32 {
                let Some(bucket)=self.buckets.get(&(space,bx,by)) else{continue};
                for (&other,&(p,r,h)) in bucket {
                    if other==id || to.coordinates[2]>=p.coordinates[2]+h || to.coordinates[2]+height<=p.coordinates[2]{continue;}
                    let Some([ox,oy,_])=from.offset_to(p) else{continue};
                    let old=ox.hypot(oy);let end=(ox-dx).hypot(oy-dy);let required=radius+r+0.1;
                    let t=if dx*dx+dy*dy>0.000001 {((ox*dx+oy*dy)/(dx*dx+dy*dy)).clamp(0.,1.)}else{0.};
                    let closest=(ox-dx*t).hypot(oy-dy*t);
                    // Let an already overlapping capture separate; never make
                    // the overlap worse or cross through another actor.
                    if closest<required && !(old<required && end>old+0.0001 && closest>=old-0.0001){return false;}
                }
            }
        }
        true
    }
}

pub struct Geometry {
    world: Arc<Ac2WorldSource>,
    terrain: Terrain,
    blocks: BTreeMap<u32,(u64,bool)>,
    clock: u64,
    loads: u32,
}
fn error(e:impl std::fmt::Display)->io::Error {io::Error::other(e.to_string())}
impl Geometry {
    pub fn open(directory:&Path)->io::Result<Self> {
        let dataset=ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(directory)).map_err(error)?;
        let store=Arc::new(ac2_db::ResourceStore::new(dataset));
        let world=Ac2WorldSource::open(store).map_err(error)?.ok_or_else(||error("Missing AC2 world definitions"))?;
        Ok(Self{world,terrain:Terrain::new(),blocks:BTreeMap::new(),clock:0,loads:0})
    }
    fn resident(&mut self,cell:u32)->bool {
        let block=cell&0xffff0000;self.clock+=1;
        if let Some(entry)=self.blocks.get_mut(&block){entry.0=self.clock;return entry.1;}
        // The pinned Terrain keeps vacant object slots after remove_block.
        // Periodically rebuild this query-only cache so long journeys do not
        // accumulate an unbounded slot array. No live simulation state is here.
        if self.loads>=64 {self.terrain=Terrain::new();self.blocks.clear();self.loads=0;}
        self.loads+=1;
        if self.blocks.len()>=16 {
            if let Some((&old,_))=self.blocks.iter().min_by_key(|(_,e)|e.0){self.terrain.remove_block(old);self.blocks.remove(&old);}
        }
        let id=CellId::new(block|0xffff);
        let result=(||{
            // Dungeon-only blocks can have interior geometry without an outdoor mesh.
            let land=ac2_physics::load_terrain_block(&self.world,&mut self.terrain,id)?;
            let rooms=ac2_physics::load_block_interiors(&self.world,&mut self.terrain,id)?;
            ac2_physics::load_block_objects(&self.world,&mut self.terrain,id)?;
            Ok::<_,ac2_db::ResourceError>(land||rooms>0)
        })();
        let ready=result.unwrap_or_else(|e|{eprintln!("Native collision unavailable for {block:08x}: {e}");false});
        if !ready{self.terrain.remove_block(block);}
        self.blocks.insert(block,(self.clock,ready));ready
    }
    fn position(p:Placement,up:f32)->Position {
        Position::new(p.cell,Frame::new(Vector::new(p.coordinates[0],p.coordinates[1],p.coordinates[2]+up),Quaternion::new(1.,0.,0.,0.)))
    }
    /// Verified near-foot contact, including upper floors and bridges.
    pub fn contact(&mut self,p:Placement)->bool {
        if !p.valid() || !self.resident(p.cell){return false;}
        let mut ray=LosParameters::new();
        ray.set_from_position_and_offset(&Self::position(p,0.25),Vector::new(0.,0.,-0.55));
        self.terrain.los_check(&mut ray);
        // Native primitive rays return a zero-distance hit when starting inside
        // a prop. That is penetration, not supporting ground (as in ground()).
        ray.trace_result()==1 && ray.delta()>0.001 && ray.impact_normal().z>=0.57 &&
            (ray.end_point().z-p.coordinates[2]).abs()<=0.3
    }
    /// Authoring guard for terrestrial actors; underwater floors are valid
    /// collision surfaces but must not become land-creature spawn anchors.
    pub fn dry_ground(&self,p:Placement)->bool {
        self.water_height(p.cell).is_ok_and(|water|
            water.is_none_or(|height|p.coordinates[2]>=height+0.15))
    }
    /// Explicit 74A9 shoreline repair, shared with AC2Water.cpp.
    /// A native plane still wins if later DATs restore the missing water cell.
    pub fn water_height(&self,cell:u32)->Result<Option<f32>,ac2_db::ResourceError> {
        let native=self.world.water_surface_height(CellId::new(cell))?;
        if native.is_none() && (cell >> 16)==0x74a9 && (cell & 0xffff)<0x100 {
            return self.world.water_surface_height(CellId::new(0x75a90001));
        }
        Ok(native)
    }
    /// No hit is only clear when the whole segment stayed in resident geometry.
    pub fn clear(&mut self,a:Placement,b:Placement,height:f32)->bool {
        self.traverse(a,b,height).is_some()
    }
    fn traverse(&mut self,a:Placement,b:Placement,height:f32)->Option<Placement> {
        if !a.valid()||!b.valid()||!self.resident(a.cell)||!self.resident(b.cell){return None;}
        let mut ray=LosParameters::new();
        ray.set_from_begin_and_end_position(&Self::position(a,height),&Self::position(b,height));
        self.terrain.los_check(&mut ray);
        if ray.trace_result()!=0{return None;}
        let end=ray.end_point();
        Some(Placement{cell:ray.final_cell_id(),coordinates:[end.x,end.y,end.z-height],heading:b.heading})
    }
    pub fn ground(&mut self,p:Placement,up:f32,down:f32)->Option<Placement>{
        if !p.valid()||!up.is_finite()||!down.is_finite()||up<0.||down<0.||!self.resident(p.cell){return None;}
        let mut ray=LosParameters::new();
        ray.set_ray(Ray::from_offset(Self::position(p,up).frame.origin,Vector::new(0.,0.,-up-down)),p.cell);
        self.terrain.los_check(&mut ray);
        if ray.trace_result()!=1||ray.impact_normal().z<0.57{return None;}
        let hit=ray.end_point();
        // A ray starting inside solid geometry can report its start as a hit.
        // That is not a floor and must never lift an actor onto a wall/ceiling.
        if hit.z>=p.coordinates[2]+up-0.001{return None;}
        Some(Placement{cell:ray.final_cell_id(),coordinates:[hit.x,hit.y,hit.z],heading:p.heading})
    }
    /// Conservative native-body clearance for terrestrial spawn authoring and
    /// walking. Check the footprint, not just the pivot over a ledge. Authored
    /// flying actors/static attachments must not be grounded by this policy.
    pub fn body_fits(&mut self,p:Placement,radius:f32,height:f32)->bool {
        if !radius.is_finite()||!height.is_finite()||!(0.05..=32.).contains(&radius)||!(0.1..=100.).contains(&height){return false;}
        if !self.contact(p){return false;}
        let Some(top)=p.translated([0.,0.,height-0.05]) else{return false};
        if !self.clear(p,top,0.05){return false;}
        // The normal threshold permits slopes up to ~55 degrees. Peripheral
        // feet can follow that slope, but a missing supporting surface fails.
        let span=radius*1.45+0.15;
        for i in 0..8 {
            let a=i as f32*std::f32::consts::FRAC_PI_4;
            let Some(edge)=p.translated([a.cos()*radius,a.sin()*radius,0.]) else{return false};
            let Some(floor)=self.ground(edge,span,span) else{return false};
            if !self.contact(floor) || (floor.coordinates[2]-p.coordinates[2]).abs()>span {return false;}
            // Rays at matching height above each floor avoid mistaking a
            // walkable hillside for a wall through the lower half of the body.
            if !self.clear(p,floor,(height*0.35).max(0.2)) || !self.clear(p,floor,(height-0.1).max(0.1)){return false;}
        }
        true
    }
    /// Authoring fallback for platforms narrower than the conservative flat
    /// footprint. Use unmodified DAT spheres and require stable native contact;
    /// no relocation or bypass of the separate dry-ground/occupancy guards.
    /// Runtime walking deliberately retains its conservative swept checks.
    pub fn native_body_fits(&mut self,p:Placement,visual:u32,scale:f32,descriptor:u32)->io::Result<bool> {
        Ok(self.native_support(p,visual,scale,descriptor)?.is_some_and(|r|r.accepted))
    }
    pub fn native_support(&mut self,p:Placement,visual:u32,scale:f32,descriptor:u32)
        ->io::Result<Option<crate::placement_validation::SupportReport>> {
        if !p.valid() || !scale.is_finite() || scale<=0. {return Ok(None);}
        // Keep the complete neighbourhood resident throughout the sphere test,
        // including static objects shadowing cells from an adjacent landblock.
        if self.loads>55 {self.terrain=Terrain::new();self.blocks.clear();self.loads=0;}
        let (x,y)=((p.cell>>24) as i32,((p.cell>>16)&255) as i32);
        if (1..=64).contains(&(p.cell&65535)) {
            for bx in (x-1).max(0)..=(x+1).min(255) {for by in (y-1).max(0)..=(y+1).min(255) {
                if !self.resident((bx as u32)<<24|(by as u32)<<16|1){return Ok(None);}
            }}
        }
        if !self.resident(p.cell){return Ok(None);}
        let store=self.world.resource_store();
        let Some(mut shape)=ac2_physics::load_body_shape(store,DataId::new(visual)).map_err(error)? else{return Ok(None)};
        shape.scale*=scale;
        let Some(motion)=ac2_world::load_ac2_motion_interp_desc(store,DataId::new(descriptor)).map_err(error)? else{return Ok(None)};
        let mut position=Self::position(p,0.);let q=p.orientation();
        position.frame=Frame::new(position.frame.origin,Quaternion::new(q[0],q[1],q[2],q[3]));
        Ok(Some(crate::placement_validation::verify_support(&self.terrain,position,shape,Arc::new((*motion).clone()))))
    }
    /// Sweep a small hovering body through native rooms and outdoor boundaries.
    pub(crate) fn hover_step(&mut self, from: Placement, to: Placement) -> Option<Placement> {
        if !from.distance(to).is_some_and(|distance| distance <= 0.75) {
            return None;
        }
        let reached = self.traverse(from, to, 0.0)?;
        for offset in [[0.12, 0., 0.], [-0.12, 0., 0.], [0., 0.12, 0.],
                       [0., -0.12, 0.], [0., 0., 0.12], [0., 0., -0.12]] {
            if !self.clear(from.translated(offset)?, reached.translated(offset)?, 0.0) {
                return None;
            }
        }
        Some(reached)
    }

    /// Bounded walking step through native cell portals and outdoor boundaries.
    /// Three body rays reject wall edges; floor queries reject ledges and steps.
    /// This is conservative reach checking, not a full capsule movement solver.
    pub fn walk_step(&mut self,from:Placement,to:Placement,radius:f32)->Option<Placement>{
        self.walk_body(from,to,radius,2.)
    }
    pub fn walk_body(&mut self,from:Placement,to:Placement,radius:f32,height:f32)->Option<Placement>{
        if !radius.is_finite() || !(0.05..=32.).contains(&radius) || !height.is_finite() || !(0.1..=100.).contains(&height)
            || from.distance(to).is_none_or(|d|d>1.) {return None;}
        let reached=self.traverse(from,to,0.6)?;
        let floor=self.ground(reached,0.45,0.65)?;
        if (floor.coordinates[2]-from.coordinates[2]).abs()>0.65{return None;}
        let [dx,dy,_]=from.offset_to(floor)?;let length=dx.hypot(dy);
        if length<=0.0001{return None;}
        for side in [-radius,0.,radius] {
            let offset=[-dy/length*side,dx/length*side,0.];
            let a=from.translated(offset)?;let b=floor.translated(offset)?;
            if !self.clear(a,b,(height*0.35).max(0.2))||!self.clear(a,b,(height-0.1).max(0.1)){return None;}
        }
        if !self.body_fits(floor,radius,height){return None;}
        Some(floor)
    }
}
