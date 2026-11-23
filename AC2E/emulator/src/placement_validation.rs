//! Read-only authoring verification using the native sphere placement solver.
//! This does not replace runtime walking, reserve living bodies, or edit anchors.
use ac2_physics::{BodyShape,MovementState,PhysicsBody,Position,SetPositionError,Terrain};
use ac2_world::Ac2MotionInterpDesc;
use serde::Serialize;
use std::sync::Arc;

pub const SUPPORT_QUANTA:usize=100;
const POSITION_TOLERANCE:f32=0.05;

#[derive(Debug,Serialize)]
pub struct SupportReport {
    pub accepted:bool,
    pub placement_result:String,
    pub placement_flags:u32,
    pub physics_quanta:usize,
    pub all_samples_supported:bool,
    pub max_displacement_m:f32,
    pub requested_cell:String,
    pub requested_position:[f32;3],
    pub placed:SupportSample,
    pub settled:SupportSample,
}
#[derive(Debug,Serialize)]
pub struct SupportSample {
    pub cell:String,
    pub position:[f32;3],
    pub velocity:[f32;3],
    pub contact:bool,
    pub on_walkable:bool,
}
impl From<MovementState> for SupportSample {
    fn from(s:MovementState)->Self {
        Self{cell:format!("{:08x}",s.cell_id),position:s.origin,velocity:s.velocity,
            contact:s.contact,on_walkable:s.on_walkable}
    }
}

/// Accept only a supported, stationary native body at the requested position,
/// without FORCE_SPF, SLIDE_SPF, shape reduction, or a new floor/room. Inspect
/// every 100ms sample: a body that falls and later finds another floor fails.
pub fn verify_support(terrain:&Terrain,position:Position,shape:BodyShape,
    motion:Arc<Ac2MotionInterpDesc>)->SupportReport {
    let requested=[position.frame.origin.x,position.frame.origin.y,position.frame.origin.z];
    let shape_valid=!shape.spheres.is_empty() && shape.scale.is_finite() && shape.scale>0. &&
        shape.spheres.iter().all(|s|s.radius.is_finite() && s.radius>0. &&
            [s.center.x,s.center.y,s.center.z].iter().all(|v|v.is_finite()));
    let mut body=PhysicsBody::new(position,shape,Some(motion));
    let result=if shape_valid{body.place(position,0,terrain)}else{SetPositionError::GeneralFailure};
    let placed=body.movement_state();
    let mut supported=shape_valid && result==SetPositionError::Ok;
    let mut max_displacement=0.0_f32;
    let mut samples=0;
    for i in 0..=SUPPORT_QUANTA {
        if i>0 {body.step_quantum(terrain);samples+=1;}
        let state=body.movement_state();
        let distance=state.origin.iter().zip(requested).map(|(v,r)|(v-r).powi(2)).sum::<f32>().sqrt();
        max_displacement=max_displacement.max(distance);
        supported &= state.cell_id==position.cell_id && state.contact && state.on_walkable &&
            distance.is_finite() && distance<=POSITION_TOLERANCE &&
            state.velocity.iter().all(|v|v.is_finite() && v.abs()<=0.001);
        // Rejected placements need no simulated recovery. Never step bad shapes.
        if result!=SetPositionError::Ok{break;}
    }
    SupportReport{accepted:supported,placement_result:format!("{result:?}"),placement_flags:0,
        physics_quanta:samples,all_samples_supported:supported,max_displacement_m:max_displacement,
        requested_cell:format!("{:08x}",position.cell_id),requested_position:requested,
        placed:placed.into(),settled:body.movement_state().into()}
}
