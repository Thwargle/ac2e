//! Native locomotion limits, independent of weapon/attack speed.
//! CMotionInterp uses MotionInterpDesc and the body's total Y scale for forward
//! speed; acceleration scales only above unit size. No creature-name heuristics.
use crate::dat::Dat;
use ac2_assets::{Ac2VisualDesc, Ac2SetupPhysics};
use ac2_core::{DataId, QualifiedDataId};
use ac2_db::ResourcePackedDecode;
use ac2_world::Ac2MotionInterpDesc;
use std::{collections::{BTreeMap, BTreeSet}, io};

/// Publication safety bound, not a speed: each segment is collision checked and
/// normal 100ms updates travel far less. Prevent unbounded movement after stalls.
pub const MAX_UPDATE_DISTANCE: f32 = 16.;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Pace {
    pub speed: f32,
    pub acceleration: f32,
}
impl Pace {
    /// Integrate acceleration up to the speed limit, in metres. Independent of
    /// tick subdivision; collision/arrival resets velocity in the caller.
    pub fn distance(self, velocity: &mut f32, seconds: f32) -> f32 {
        if self.speed <= 0. || seconds <= 0. { *velocity=0.; return 0.; }
        let start=velocity.clamp(0.,self.speed);
        let ramp=if self.acceleration>0. {((self.speed-start)/self.acceleration).min(seconds)}else{seconds};
        let end=(start+self.acceleration*ramp).min(self.speed);
        *velocity=end;
        (start+end)*0.5*ramp+end*(seconds-ramp)
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Profile {
    pub descriptor: u32,
    pub scale: f32,
    pub peace: Pace,
    pub combat: Pace,
    pub radius: f32,
    pub height: f32,
    pub grounded: bool,
}
impl Default for Profile {
    // Compatibility for synthetic/no-DAT fixtures only. The live server loads
    // every combat actor from its actual visual before starting simulation.
    fn default() -> Self {
        Self {descriptor:0,scale:1.,peace:Pace{speed:2.,acceleration:12.},combat:Pace{speed:2.,acceleration:12.},radius:0.35,height:2.,grounded:true}
    }
}
#[derive(Default)]
pub struct Catalog {
    visuals: BTreeMap<u32, Ac2VisualDesc>,
    descriptors: BTreeMap<u32, Ac2MotionInterpDesc>,
    setups: BTreeMap<u32, Ac2SetupPhysics>,
}
fn invalid(message:impl Into<String>)->io::Error {io::Error::new(io::ErrorKind::InvalidData,message.into())}
impl Catalog {
    pub fn resolve(&mut self, dat:&mut Dat, visual:u32, instance_scale:f32)->io::Result<Option<Profile>> {
        self.resolve_scaled(dat,visual,instance_scale,None)
    }
    /// An inline player VisualDesc replaces inherited scale, as it does on the
    /// client; build and height must not multiply the parent's scale twice.
    pub(crate) fn resolve_scaled(&mut self, dat:&mut Dat, visual:u32, instance_scale:f32,
        inline_scale:Option<[f32;3]>)->io::Result<Option<Profile>> {
        let mut current=visual;
        let (mut motion,mut scale,mut setup)=(None,inline_scale,None);
        let mut seen=BTreeSet::new();
        while current!=0 {
            if current>>24!=0x1f || seen.len()>=16 || !seen.insert(current) {
                return Err(invalid(format!("Invalid movement VisualDesc chain at {current:08x}")));
            }
            if !self.visuals.contains_key(&current) {
                let desc=Ac2VisualDesc::decode_packed(QualifiedDataId::new(DataId::new(current),0),0,&dat.read(current)?)
                    .map_err(|e|invalid(e.to_string()))?;
                self.visuals.insert(current,desc);
            }
            let desc=&self.visuals[&current];
            motion=motion.or(desc.motion_interp);
            scale=scale.or(desc.scale.map(|s|[s.x,s.y,s.z]));
            setup=setup.or(desc.root_setup());
            current=desc.parent.map_or(0,|d|d.raw());
        }
        let Some(did)=motion else {return Ok(None)};
        let scale=scale.unwrap_or([1.;3]);
        let total=scale[1]*instance_scale;
        if !instance_scale.is_finite() || instance_scale<=0. ||
            scale.iter().any(|s|!s.is_finite() || *s<=0.) || !total.is_finite() {
            return Err(invalid("Invalid creature movement scale"));
        }
        if !self.descriptors.contains_key(&did.raw()) {
            let desc=Ac2MotionInterpDesc::decode_packed(QualifiedDataId::new(did,0),0,&dat.read(did.raw())?)
                .map_err(|e|invalid(e.to_string()))?;
            self.descriptors.insert(did.raw(),desc);
        }
        let desc=&self.descriptors[&did.raw()];
        let pace=|mode|->io::Result<Pace>{
            // Existing server navigation follows walkable ground, including
            // hovering creatures. Flight paths/water navigation are separate.
            let values=desc.desired_motion_values(0x40000001,mode,0,0x80000001,None);
            let speed=values.max_forward_velocity*total;
            let acceleration=values.forward_acceleration*total.max(1.);
            if !speed.is_finite() || speed<0. || !acceleration.is_finite() || acceleration<0. {
                return Err(invalid("Invalid native creature locomotion limits"));
            }
            Ok(Pace{speed,acceleration})
        };
        let mut radius=0.35;let mut height=2.;
        if let Some(setup)=setup {
            if !self.setups.contains_key(&setup.raw()) {
                let body=Ac2SetupPhysics::decode_packed(QualifiedDataId::new(setup,0),0,&dat.read(setup.raw())?)
                    .map_err(|e|invalid(e.to_string()))?;
                self.setups.insert(setup.raw(),body);
            }
            let body=&self.setups[&setup.raw()];
            radius=body.radius*scale[0].max(scale[1])*instance_scale;
            height=body.height*scale[2]*instance_scale;
            if !radius.is_finite() || !height.is_finite() || radius<0. || height<0. {
                return Err(invalid("Invalid native creature body size"));
            }
        }
        let ground=desc.desired_motion_values(0x40000001,crate::combat_visual::PEACE,0,0x80000001,None);
        Ok(Some(Profile{descriptor:did.raw(),scale:total,peace:pace(crate::combat_visual::PEACE)?,combat:pace(0x41000012)?,
            radius:radius.max(0.1),height:height.max(0.2),grounded:ground.gravity && ground.hover_height.abs()<0.001}))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn player_inline_scale_replaces_inherited_scale_on_all_six_bodies() {
        let Ok(dir)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let mut dat=Dat::open(std::path::Path::new(&dir).join("portal.dat")).unwrap();
        let mut catalog=Catalog::default();
        let mut count=0;
        for body in crate::appearance::bodies() {
            let unit=catalog.resolve_scaled(&mut dat,body.visual,1.,Some([1.;3])).unwrap().unwrap();
            let sized=catalog.resolve_scaled(&mut dat,body.visual,1.,Some([1.5,1.5,0.75])).unwrap().unwrap();
            assert!(unit.radius>0.1 && unit.height>0.2);
            assert!((sized.radius-unit.radius*1.5).abs()<0.0001);
            assert!((sized.height-unit.height*0.75).abs()<0.0001);
            assert!((sized.combat.speed-unit.combat.speed*1.5).abs()<0.0001);
            for scale in [[f32::NAN,1.,1.],[1.,1.,-1.],[1.,1.,f32::INFINITY]] {
                assert!(catalog.resolve_scaled(&mut dat,body.visual,1.,Some(scale)).is_err());
            }
            count+=1;
        }
        assert_eq!(count,6);
    }
    #[test]
    fn acceleration_is_time_based_and_respects_stationary_profiles() {
        let p=Pace{speed:6.5,acceleration:12.};
        let mut v=0.;let mut distance=0.;
        for _ in 0..10 {distance+=p.distance(&mut v,0.1);}
        let mut one=0.;let expected=p.distance(&mut one,1.);
        assert!((distance-expected).abs()<0.0001);
        assert_eq!(v,6.5);
        assert_eq!(Pace{speed:0.,acceleration:0.}.distance(&mut v,1.),0.);
        assert_eq!(v,0.);
    }
    #[test]
    fn native_visual_inheritance_and_instance_scale_change_speed() {
        let Ok(dir)=std::env::var("AC2_TEST_GAME_DATA") else {return};
        let mut dat=Dat::open(std::path::Path::new(&dir).join("portal.dat")).unwrap();
        let mut catalog=Catalog::default();
        for (visual,speed) in [(0x1f0003d1,3.9),(0x1f000036,7.8),(0x1f0004e0,7.475),(0x1f000051,6.5)] {
            let p=catalog.resolve(&mut dat,visual,1.).unwrap().unwrap();
            assert!((p.combat.speed-speed).abs()<0.0001,"{visual:08x}: {p:?}");
            let half=catalog.resolve(&mut dat,visual,0.5).unwrap().unwrap();
            assert!(p.radius>0.1 && p.height>0.2,"Native body dimensions: {p:?}");
            assert!((half.radius-p.radius*0.5).abs()<0.001);
            assert!((half.height-p.height*0.5).abs()<0.001);
            assert!((half.combat.speed-speed*0.5).abs()<0.0001);
            assert!(half.combat.acceleration>=12.,"small bodies retain native acceleration");
        }
    }
}
