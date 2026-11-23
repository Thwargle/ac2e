use ac2_net::protocol::{Build, ServerEvent};
use pyrealium::{appearance, status_fx::{Effect,Runtime}, world};

#[test]
fn native_decoder_reads_lasting_effects_without_changing_actor_state() {
    for visual in [0x1f000001,0x1f000deb,0x1f00031e] {
        let object=world::StationaryObject {
            character_id:0x8120000000000001,parent_visual_did:visual,entity_did:0x47000001,
            cell_id:0x03fc0101,position:[12.,23.,-44.],orientation:[1.,0.,0.,0.],
            instance_stamp:17,visual_ordering_stamp:23,timestamps:[31,32,33,34],
        };
        let base=world::create_named_stationary_object(&object,Some(1.25),Some("Test actor")).unwrap();
        for full_appearance in [false,true] {
            let mut spawn=base.clone();
            if full_appearance {
                let visual=appearance::body(appearance::TUMEROK,appearance::FEMALE).unwrap().visual_desc(&[]).unwrap();
                spawn.splice(12..20,visual);
            }
            let ServerEvent::CreateObject(before)=ServerEvent::decode(&spawn,Build::Legions7160).unwrap() else {panic!()};
            let mut runtime=Runtime::default();
            let a=Effect{fx:0x41000031,duration_ms:1000};
            let b=Effect{fx:0x41000032,duration_ms:2000};
            runtime.apply(7,object.character_id,9,&[a,b],0).unwrap();
            runtime.apply(8,object.character_id,10,&[a],0).unwrap();
            for now in [0,1000,2000] {
                let effects=runtime.snapshot(object.character_id,now);
                let wire=world::with_physics_fx(&spawn,&effects).unwrap();
                let ServerEvent::CreateObject(mut after)=ServerEvent::decode(&wire,Build::Legions7160).unwrap() else {panic!()};
                assert_eq!(after.physics.fx,effects.iter().map(|&fx|(fx,1.,0)).collect::<Vec<_>>());
                after.physics.fx.clear();after.physics.bitfield&=!0x200;
                assert_eq!(before,after,"FX snapshots must preserve appearance, position, stamps and item metadata");
            }
            for n in 0..spawn.len().min(68) {assert!(world::with_physics_fx(&spawn[..n],&[a.fx]).is_err());}
            assert!(world::with_physics_fx(&spawn,&[1]).is_err());
            let mut unsupported=spawn.clone();
            let offset=12+appearance::visual_len(&spawn[12..]).unwrap();
            unsupported[offset..offset+4].copy_from_slice(&0x8200u32.to_le_bytes());
            assert!(world::with_physics_fx(&unsupported,&[a.fx]).is_err());
        }
    }
}
