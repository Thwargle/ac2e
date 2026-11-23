// SPDX-License-Identifier: AGPL-3.0-only
use ac2_net::protocol::*;
use serde_json::{json, Value};

/// Native world exit and the following character list are separate messages.
/// Keep selection controls closed until the fresh list arrives, so an early
/// click cannot race the server's reliable exit/selection transition.
#[derive(Default)]
pub(crate) struct SelectionReturn { waiting: bool, native_inventory: bool }
impl SelectionReturn {
    pub(crate) fn new(native_inventory: bool) -> Self { Self { waiting:false, native_inventory } }
    pub(crate) fn waiting(&self) -> bool { self.waiting }
    pub(crate) fn convert(&mut self, event: ServerEvent) -> Vec<Value> {
        if matches!(event, ServerEvent::CharExitGame) {
            self.waiting = true;
            return vec![json!({"event":"status","phase":"leaving","message":"Loading server characters..."})];
        }
        let returning = self.waiting && matches!(event, ServerEvent::CharacterSet(_));
        let mut output = Vec::new();
        if returning {
            self.waiting = false;
            output.push(json!({"event":"selection"}));
        }
        output.extend(convert_profile(event, self.native_inventory));
        output
    }
}

pub(crate) fn string(s: &StringInfo) -> Value {
    let variables:Vec<_>=s.variables.iter().map(|(key,value)|match value {
        StringVariable::Nested(n)=>json!({"key":key,"kind":9,"text":string(n)}),
        StringVariable::Word{kind,value}=>json!({"key":key,"kind":kind,"value":value}),
        StringVariable::WordsShort{kind,a,b,c}=>json!({"key":key,"kind":kind,"value":a,"grouping":b,"base":c}),
        StringVariable::LongShort{value,c}=>json!({"key":key,"kind":4,"value":value.to_string(),"precision":c}),
        StringVariable::Long5{value,c,d,e}=>json!({"key":key,"kind":5,"value":value.to_string(),"precision":c,"grouping":d,"base":e}),
        StringVariable::Long{kind,value}=>json!({"key":key,"kind":kind,"value":value.to_string()}),
        StringVariable::LongWordShort{kind,value,d,e}=>json!({"key":key,"kind":kind,"value":value.to_string(),"grouping":d,"base":e}),
        StringVariable::Empty{kind}=>json!({"key":key,"kind":kind}),
    }).collect();
    json!({"literal":s.literal,"table":s.table_id,"key":s.string_id,"variables":variables})
}
fn visual(v: &VisualDesc) -> Value {
    let aspects: Vec<Value> = v.global_mods.iter()
        .chain(v.part_groups.iter().map(|(_, g)|g))
        .flat_map(|g| {
            g.appearance.iter().flat_map(|(table, values)| {
                values
                    .iter()
                    .map(move |(key, value)| json!({"table":table,"key":key,"value":value}))
            })
        })
        .collect();
    json!({"did":v.parent.or(v.did).unwrap_or(0),"scale":v.scale.unwrap_or([1.;3]),"aspects":aspects})
}
fn character(c: &CharacterIdentity) -> Value {
    json!({"id":c.id.to_string(),"name":c.name,"disabled":c.seconds_greyed_out!=0,"visual":visual(&c.visual)})
}
fn usage_error(code: u32) -> Value {
    // CUsageSystem::2: both 54 and 91 select this Usage string-table entry.
    let mut event=json!({"event":"error","code":code,"message":format!("Server rejected item use ({code})")});
    if matches!(code,54|91) {
        event["text"]=string(&StringInfo {table_id:0x25000014,string_id:0xca37af06,..Default::default()});
        event["message"]=json!("Item use interrupted.");
    }
    event
}
fn behavior(id: u64, instance: u16, p: &BehaviorParams) -> Value {
    json!({"event":"behavior","id":id.to_string(),"instance":instance,"behavior":p.behavior,
        // hold_cycles controls completion callbacks on a native hold track; zero
        // does not turn an arbitrary attack, jump or cast into a held final pose.
        "mode":p.mode,"rate":p.time_scale,"hold":false,"hold_cycles":p.hold_cycles,"lock":p.lock_actions!=0,
        "impulse":p.impulse,"propagate":p.propagate!=0})
}
fn position(p: &Position) -> Value {
    let [w, x, y, z] = p.rotation;
    // Native Heading is clockwise from +Y; native quaternion is a standard AC z rotation.
    let yaw = (2. * (w * z + x * y))
        .atan2(1. - 2. * (y * y + z * z))
        .to_degrees();
    json!({"cell":p.cell,"position":p.origin,"heading":(-yaw).rem_euclid(360.),"rotation":p.rotation})
}
fn quality(q: QualityUpdate) -> Option<Value> {
    let table = q.value.table();
    let value = match q.value {
        QualityValue::Int(v) | QualityValue::Bool(v) => json!(v),
        QualityValue::Float(v) if v.is_finite() => json!(v),
        QualityValue::Timestamp(v) if v.is_finite() => json!(v),
        QualityValue::LongInt(v) => json!(v.to_string()),
        QualityValue::DataId(v) => json!(v),
        QualityValue::InstanceId(v) => json!(v.to_string()),
        QualityValue::String(v) => json!(String::from_utf8_lossy(&v)),
        QualityValue::Position(v) => position(&v),
        QualityValue::StringInfo(v) => string(&v),
        _ => return None,
    };
    Some(
        json!({"event":"quality","id":q.object.map(|o|o.id.to_string()),"instance":q.object.map(|o|o.instance),"table":table,"key":q.quality,"value":value}),
    )
}
#[cfg(test)]
fn convert(event: ServerEvent) -> Vec<Value> {
    convert_profile(event, false)
}
fn convert_profile(event: ServerEvent, native_inventory: bool) -> Vec<Value> {
    let one = match event {
        ServerEvent::CharacterSet(s) => {
            json!({"event":"characters","characters":s.characters.iter().map(character).collect::<Vec<_>>(),"allowed":s.allowed})
        }
        ServerEvent::CharGenVerification {
            response,
            identity,
            weenie_result,
        } => {
            json!({"event":"created","response":response,"result":weenie_result,"character":character(&identity)})
        }
        ServerEvent::CharacterError(code) => json!({"event":"character_error","code":code}),
        ServerEvent::CharacterDeleted(id) => json!({"event":"deleted","id":id.to_string()}),
        ServerEvent::CreatePlayer { id, region } => {
            json!({"event":"player","id":id.to_string(),"region":region})
        }
        ServerEvent::CreateObject(o) => {
            let mut object=json!({"event":"object","id":o.id.to_string(),"visual":visual(&o.visual),
            "placement":o.physics.position.as_ref().map(position),"instance":o.physics.instance_stamp,"scale":o.weenie.scale.unwrap_or(1.0),
            "position_stamp":o.physics.timestamps[0],"teleport":o.physics.timestamps[1],"force":o.physics.timestamps[2],
            "entity":o.weenie.entity_did,"icon":o.weenie.icon,"quantity":o.weenie.quantity,
            "name":o.weenie.name.as_ref().map(string),"parent":o.physics.parent.map(|p|p.0.to_string()),"holding":o.physics.parent.map(|p|p.1),
            "container":o.weenie.container.map(|p|p.to_string()),"wielder":o.weenie.wielder.map(|p|p.to_string())});
            if o.weenie.data_mask & 0x00800000 != 0 {
                object["selectable"]=json!(o.weenie.bitfield.unwrap_or(0) & 2 != 0);
            }
            if let Some(summoner)=o.weenie.pet_summoner {object["summoner"]=json!(summoner.to_string());}
            let mut out=vec![object];
            // Late observers receive an already moving missile in PhysicsDesc,
            // without a second InitializeMissile event.
            if o.physics.bitfield & 0x80000 != 0 {
                if let (Some(p),Some(velocity))=(o.physics.position,o.physics.velocity) {
                    let mut offset=o.physics.target_position.unwrap_or(Position{cell:0,origin:[0.;3],rotation:[1.,0.,0.,0.]});
                    if o.physics.target.unwrap_or(0)!=0 {offset.origin=o.physics.target_offset.unwrap_or([0.;3]);}
                    out.push(json!({"event":"missile","id":o.id.to_string(),"instance":o.physics.instance_stamp,
                        "placement":position(&p),"velocity":velocity,"target":o.physics.target.unwrap_or(0).to_string(),
                        "offset":position(&offset),"initialized":o.physics.target.is_some() || o.physics.target_position.is_some()}));
                }
            }
            if let Some(mode)=o.physics.mode {out.push(json!({"event":"mode","id":o.id.to_string(),"instance":o.physics.instance_stamp,"mode":mode}));}
            if let Some(target)=o.physics.look_at {out.push(json!({"event":"look","id":o.id.to_string(),"instance":o.physics.instance_stamp,"target":target.to_string()}));}
            for p in &o.physics.behaviors {out.push(behavior(o.id,o.physics.instance_stamp,p));}
            for (fx,scalar,target) in &o.physics.fx {out.push(json!({"event":"fx","id":o.id.to_string(),"instance":o.physics.instance_stamp,"fx":fx,"strength":scalar,"target":target.to_string()}));}
            for (name,value) in [("velocity",o.physics.velocity_scale),("jump",o.physics.jump_scale)] {
                if let Some(value)=value {out.push(json!({"event":"motion","id":o.id.to_string(),"instance":o.physics.instance_stamp,"parameter":name,"value":value}));}
            }
            return out;
        }
        ServerEvent::UpdatePosition(p) | ServerEvent::PositionCell(p) => {
            json!({"event":"position","id":p.sender.id.to_string(),"instance":p.sender.instance,
            "cell":p.position.cell,"position":p.position.origin,"heading":p.heading_degrees,"motion":p.motion,"time":p.time,
            "position_stamp":p.position_stamp,"teleport":p.teleport_stamp,"force":p.force_position_stamp,"impulse":p.impulse,"velocity":p.velocity})
        }
        ServerEvent::DestroyObject(o) => {
            json!({"event":"remove","id":o.id.to_string(),"instance":o.instance})
        }
        // ECM_Physics::RecvEvt_Contain keeps the inventory representation;
        // it is not DestroyObject (unequipping normally sends Contain).
        ServerEvent::Contain(o) => json!({"event":"contain","id":o.id.to_string(),"instance":o.instance,"position_stamp":o.other}),
        ServerEvent::Object(o) => match o.action {
            ObjectAction::LookAt(target) => json!({"event":"look","id":o.sender.id.to_string(),"instance":o.sender.instance,"target":target.to_string()}),
            ObjectAction::LookAtDir {x,y} => json!({"event":"look_direction","id":o.sender.id.to_string(),"instance":o.sender.instance,"x":x,"y":y}),
            ObjectAction::InitializeMissile {position:p,velocity,target} => json!({"event":"missile","id":o.sender.id.to_string(),"instance":o.sender.instance,
                "placement":position(&p),"velocity":velocity,"target":target.target.to_string(),"offset":position(&target.offset),"target_velocity":target.velocity,"initialized":target.initialized!=0}),
            ObjectAction::DoFx {fx,scalar} => json!({"event":"fx","id":o.sender.id.to_string(),"instance":o.sender.instance,"fx":fx,"strength":scalar}),
            ObjectAction::DoFxScript(script) => json!({"event":"fx_script","id":o.sender.id.to_string(),"instance":o.sender.instance,"script":script}),
            ObjectAction::DoTargettedFx {fx,scalar,target} => json!({"event":"fx","id":o.sender.id.to_string(),"instance":o.sender.instance,"fx":fx,"strength":scalar,"target":target.to_string()}),
            ObjectAction::StopFx(fx) => json!({"event":"stop_fx","id":o.sender.id.to_string(),"instance":o.sender.instance,"fx":fx}),
            ObjectAction::DoMode(mode) | ObjectAction::SetMode(mode) => {
                json!({"event":"mode","id":o.sender.id.to_string(),"instance":o.sender.instance,"mode":mode})
            }
            ObjectAction::DoBehavior(p) => behavior(o.sender.id,o.sender.instance,&p),
            ObjectAction::ReleaseBehavior {behavior:released,params} => {
                // ModeTracker::ReleaseBehavior releases the existing track first;
                // a nonzero replacement is a separate, optional DoBehavior.
                let mut out=vec![json!({"event":"release_behavior","id":o.sender.id.to_string(),
                    "instance":o.sender.instance,"behavior":released})];
                if params.behavior!=0 {out.push(behavior(o.sender.id,o.sender.instance,&params));}
                return out;
            }
            ObjectAction::StopLayer(layer) => json!({"event":"stop_layer","id":o.sender.id.to_string(),"instance":o.sender.instance,"layer":layer}),
            ObjectAction::StopBehavior(b) => json!({"event":"stop_behavior","id":o.sender.id.to_string(),"instance":o.sender.instance,"behavior":b}),
            ObjectAction::StopAllWeenieBehaviors => json!({"event":"stop_behavior","id":o.sender.id.to_string(),"instance":o.sender.instance,"behavior":0}),
            ObjectAction::DoStory(story) => {
                // Story identity belongs to its owner, not to an attacker in a
                // victim-owned damage story. Keep the native optional counter.
                let mut out=vec![json!({"event":"story","id":o.sender.id.to_string(),"instance":o.sender.instance,
                    "request":story.client_attack_context,"skill":story.skill,"animated":story.attacker_behavior.is_some()})];
                if let Some(p)=story.attacker_behavior.as_ref() {
                    let attacker=story.attacker.unwrap_or(o.sender.id);
                    let mut v=behavior(attacker,o.sender.instance,p);
                    // The envelope stamp belongs to the story owner, not a different attacker.
                    if attacker!=o.sender.id {v.as_object_mut().unwrap().remove("instance");}
                    else if let Some(request)=story.client_attack_context {v["story_request"]=json!(request);}
                    out.push(v);
                }
                // Retail creates transient local missiles from these hooks;
                // they do not arrive as CreateObject/InitializeMissile messages.
                let missiles:Vec<_>=story.hooks.iter().filter(|h|matches!(h.kind(),2|3)).map(|h|
                    json!({"kind":h.kind(),"anim_hook":h.anim_hook.unwrap_or(0),
                        "attack_hook":h.attack_hook.unwrap_or(0),"target":h.target.unwrap_or(0).to_string(),
                        "weapon":h.weapon.unwrap_or(0).to_string()})).collect();
                if !missiles.is_empty() {
                    out.push(json!({"event":"story_missiles","id":o.sender.id.to_string(),"instance":o.sender.instance,
                        "attacker":story.attacker.unwrap_or(o.sender.id).to_string(),"skill":story.skill,
                        "behavior":story.attacker_behavior.as_ref().map(|p|p.behavior).unwrap_or(0),"hooks":missiles}));
                }
                for hook in story.hooks {
                    if hook.kind()!=1 {continue;}
                    // StoryManager::DoStoryHook notifies the target of a harmful
                    // skill independently of hit damage (misses/debuffs count).
                    // The client checks the skill's native DAT flag. The stamp
                    // belongs to the story owner, never a different attacker.
                    if let (Some(target),Some(attacker),Some(skill))=(hook.target,story.attacker,story.skill) {
                        if target!=0 && attacker!=0 && skill!=0 {
                            out.push(json!({"event":"story_target","id":target.to_string(),
                                "owner":o.sender.id.to_string(),"instance":o.sender.instance,
                                "attacker":attacker.to_string(),"skill":skill}));
                        }
                    }
                    if let Some(health)=hook.target_health_change {
                        if health<0 {
                            let target=hook.target.unwrap_or(o.sender.id);
                            let mut hit=json!({"event":"hit","id":target.to_string(),"attacker":story.attacker.unwrap_or(o.sender.id).to_string(),"damage":-i64::from(health)});
                            if target==o.sender.id {hit["instance"]=json!(o.sender.instance);}
                            out.push(hit);
                        }
                    }
                }
                return out;
            }
            ObjectAction::AddImpulse {direction,..} => json!({"event":"impulse","id":o.sender.id.to_string(),"instance":o.sender.instance,"direction":direction}),
            ObjectAction::SetVelocityScale(value) => json!({"event":"motion","id":o.sender.id.to_string(),"instance":o.sender.instance,"parameter":"velocity","value":value}),
            ObjectAction::SetAccelerationScale(value) => json!({"event":"motion","id":o.sender.id.to_string(),"instance":o.sender.instance,"parameter":"acceleration","value":value}),
            ObjectAction::SetJumpScale(value) => json!({"event":"motion","id":o.sender.id.to_string(),"instance":o.sender.instance,"parameter":"jump","value":value}),
            ObjectAction::SetGlobalModifier(value) => json!({"event":"motion","id":o.sender.id.to_string(),"instance":o.sender.instance,"parameter":"modifier","value":value}),
            ObjectAction::SetMotionInterp(value) => json!({"event":"motion","id":o.sender.id.to_string(),"instance":o.sender.instance,"parameter":"descriptor","value":value}),
            ObjectAction::UpdateVisualDesc(v) => {
                json!({"event":"visual","id":o.sender.id.to_string(),"instance":o.sender.instance,"visual":visual(&v)})
            }
            // Leaving physics is not destruction: a sheathed weapon still
            // exists in inventory and may be parented again without a create.
            ObjectAction::LeaveWorld(stamp) => json!({"event":"world_leave","id":o.sender.id.to_string(),"instance":o.sender.instance,"position_stamp":stamp}),
            ObjectAction::Parent {parent,location,orientation} => json!({"event":"parent","id":o.sender.id.to_string(),"instance":o.sender.instance,
                "parent":parent.id.to_string(),"parent_instance":parent.instance,"position_stamp":parent.other,"holding":location,"orientation":orientation}),
            // DeParent is addressed to the parent, with the child as argument.
            ObjectAction::DeParent(child) => json!({"event":"deparent","id":child.id.to_string(),"instance":child.instance,
                "parent":o.sender.id.to_string(),"parent_instance":o.sender.instance,"position_stamp":child.other}),
            _ => return vec![],
        },
        ServerEvent::PlayerDescription(p) => {
            let mut out = Vec::new();
            // CBaseQualities::UnPack provides the same tables as later private
            // updates. In particular the recovery timestamp must survive login.
            let mut add=|key,value|out.extend(quality(QualityUpdate{object:None,quality:key,value}));
            for (k,v) in p.ints {add(k,QualityValue::Int(v));}
            for (k,v) in p.bools {add(k,QualityValue::Bool(v));}
            for (k,v) in p.floats {add(k,QualityValue::Float(v));}
            for (k,v) in p.timestamps {add(k,QualityValue::Timestamp(v));}
            for (k,v) in p.strings {add(k,QualityValue::String(v));}
            for (k,v) in p.dids {add(k,QualityValue::DataId(v));}
            for (k,v) in p.iids {add(k,QualityValue::InstanceId(v));}
            for (k,v) in p.positions {add(k,QualityValue::Position(v));}
            for (k,v) in p.string_infos {add(k,QualityValue::StringInfo(Box::new(v)));}
            for (k,v) in p.long_ints {add(k,QualityValue::LongInt(v));}
            return out;
        }
        // ECM_Physics::RecvEvt_DoFX_Private addresses the current player, with
        // no object stamp. Native CPhysicsObj plays each key at scalar 1.
        ServerEvent::PrivateFx(effects) => return effects.into_iter().map(|fx|
            json!({"event":"fx","id":null,"fx":fx,"strength":1.0})).collect(),
        ServerEvent::Quality(q) => return quality(q).into_iter().collect(),
        ServerEvent::DisplayStringInfo { kind, text } => {
            json!({"event":"chat","kind":kind,"text":string(&text)})
        }
        ServerEvent::WorldName(name) => json!({"event":"world_name","text":string(&name)}),
        ServerEvent::AccountBooted(reason) => {
            json!({"event":"disconnected","message":reason.literal.unwrap_or("Account disconnected by server".into())})
        }
        ServerEvent::Interp {
            function,
            arguments,
            object,
        } => {
            if object.is_none() && matches!(function, 0x8212001b | 0x8212001f | 0x82120020 | 0x82120021) {
                let values = unpack_values(&arguments);
                let event = match (function, values.as_deref()) {
                    (0x8212001b | 0x82120020, Ok([PackValue::LongFloat(duration)])) if duration.is_finite() =>
                        json!({"event":"portal_space","active":function == 0x82120020}),
                    (0x8212001f, Ok([PackValue::Int(value)])) if *value <= 1 =>
                        json!({"event":"portal_flag","active":*value != 0}),
                    (0x82120021, Ok([PackValue::Int(value)])) if *value <= 1 =>
                        json!({"event":"portal_movement_lock","locked":*value != 0}),
                    _ => json!({"event":"error","message":"Invalid native portal notice"}),
                };
                return vec![event];
            }
            if object.is_none() && function == 0x82100022 {
                return vec![match unpack_values(&arguments).as_deref() {
                    Ok([PackValue::Int(region)]) => json!({"event":"pvp_status","region":region}),
                    _ => json!({"event":"error","message":"Invalid regional conflict status"}),
                }];
            }
            if let Some(event)=crate::status_effects::update(function,&arguments) {return vec![event];}
            if let Some(event)=crate::inventory_bulk::decode(function,&arguments) {return vec![event];}
            if let Some(event)=crate::shop::decode(function,&arguments) {return vec![event];}
            if let Some(event)=crate::journal::decode(function,&arguments) {return vec![event];}
            if let Some(event)=crate::crafting::decode(function,&arguments) {return vec![event];}
            if let Some(event)=crate::groups::decode(function,&arguments) {return vec![event];}
            if let Some(event)=crate::allegiance::decode(function,&arguments) {return vec![event];}
            // WM_Player::26 -> CPlayer::E9: select the supplied object (6A),
            // then CM_UI::SendNotice_OpenExaminationPanel (00910002).
            if function==0x82120026 {
                if let Ok(values)=unpack_values(&arguments) {
                    if let [PackValue::LongInt(id)]=values.as_slice() {
                        if *id!=0 {return vec![json!({"event":"open_examination","id":id.to_string()})];}
                    }
                }
                return vec![json!({"event":"error","message":"Invalid server examination notice"})];
            }
            // Native MessageDialog has a priority followed by StringInfo. The
            // Unreal HUD keeps its readable text in chat instead of discarding
            // the notice as an unhandled script. Retain the diagnostic event.
            if function==0x82120024 {
                if let Ok(values)=unpack_values(&arguments) {
                    if let [PackValue::Int(_),PackValue::Reference(r)]=values.as_slice() {
                        if let [PackedEntry::Heap{object,..}]=r.entries.as_slice() {
                            if let Some(NativeObject::StringInfo(s))=&object.native {
                                return vec![json!({"event":"chat","kind":1,"text":string(s)}),
                                    json!({"event":"script","function":function,"object":null})];
                            }
                        }
                    }
                }
            }
            if function==0x820e0058 {
                if let Ok(values)=unpack_values(&arguments) {
                    if let [PackValue::LongInt(item)]=values.as_slice() {
                        return vec![json!({"event":"inventory_remove","id":item.to_string()})];
                    }
                }
                return vec![json!({"event":"error","message":"Invalid native inventory removal"})];
            }
            // WM_Inventory::5E carries the same InvMoveDesc as MoveItemDone.
            // CPlayer::RecvCEvt_MoveItem_Done forwards the complete descriptor
            // to CInventorySystem, including the actual insertion slot. This
            // is also the corpse-item move reply, not merely a sound cue.
            if function==0x820e005e {
                if let Ok(values)=unpack_values(&arguments) {
                    if let [PackValue::Reference(r)]=values.as_slice() {
                        if let Ok(d)=InvMoveDesc::from_reference(r) {
                            return vec![json!({"event":"inventory_acquired","status":d.status(),
                                "id":(if d.split_item()!=0 {d.split_item()} else {d.item()}).to_string(),
                                "source_item":d.item().to_string(),"owner":d.target().to_string(),"from":d.from().to_string(),
                                "slot":d.actual_target_slot()})];
                        }
                    }
                }
                return vec![json!({"event":"error","message":"Invalid native inventory acquisition"})];
            }
            if let Some(message)=CombatMessage::decode(function,&arguments) {
                return vec![match message {
                    Ok(CombatMessage::UpdateAttackState(enabled))=>json!({"event":"combat","enabled":enabled}),
                    Ok(CombatMessage::EndSpecialAttack(request))=>json!({"event":"skill_finished","request":request}),
                    Ok(CombatMessage::ToggleSpecialAttack{maneuver,toggled})=>json!({"event":"skill_toggled","skill":maneuver,"enabled":toggled}),
                    Err(error)=>json!({"event":"error","message":format!("Invalid native combat notice: {error}")}),
                }];
            }
            if let Some(Ok(DeathMessage::PlayerDeath(dead))) =
                DeathMessage::decode(function, &arguments)
            {
                return vec![json!({"event":"death","dead":u32::from(dead)})];
            }
            if function == 0x82120027 {
                let decoded=crate::journal::unpack_quest_profile(&arguments);
                if let Err(error) = &decoded {
                    return vec![json!({"event":"error","message":format!("Cannot decode player inventory: {error}")})];
                }
                if let Ok(values) = decoded {
                    if values.len() == 14 {
                        let bag = ContainerContents::from_values(&values[9..12]);
                        let equipment = Equipment::from_values(&values[6..9]);
                        if bag.is_err() || equipment.is_err() {
                            return vec![json!({"event":"error","message":"Cannot decode authoritative inventory slots"})];
                        }
                        if let (Ok(bag), Ok(equipment)) = (bag, equipment) {
                            let gold = match values[0] {
                                PackValue::Int(n) => n,
                                _ => 0,
                            };
                            let mut events=vec![
                                json!({"event":"inventory","items":bag.items.iter().map(u64::to_string).collect::<Vec<_>>(),"equipment":equipment.locations().unwrap_or_default().iter().map(|(slot,id)|json!({"slot":slot,"id":id.to_string()})).collect::<Vec<_>>(),"gold":gold}),
                            ];
                            // CPlayer::65 passes the last two LOAD arguments to the
                            // conflict handler as the region and server override.
                            if let (PackValue::Int(region), PackValue::Int(server)) = (&values[12], &values[13]) {
                                events.push(json!({"event":"pvp_status","region":region,"server":server}));
                            }
                            if let Some(e)=crate::status_effects::registry(&values[5]) {events.push(e);}
                            if let Some(q)=crate::journal::quests(&values[2],true) {events.push(q);}
                            if let Some(p)=crate::preferences::profile(&values[3]) {events.push(p);}
                            if let Some(p)=crate::preferences::game_profile(&values[3]) {events.push(p);}
                            if let PackValue::Reference(r)=&values[4] {
                                if let Some(repo)=SkillRepository::decode(r) {events.push(crate::journal::repository(&repo,Some(r)));}
                            }
                            return events;
                        }
                    }
                }
            }
            if function==0x820a0009 {
                return vec![crate::examination::convert(&arguments).unwrap_or_else(||
                    json!({"event":"examination_error","message":"Unsupported examination profile from server"}))];
            }
            match ScriptMessage::decode(function, &arguments) {
                Some(Ok(ScriptMessage::OpenContainerDone { error, packs, items, container })) => {
                    json!({"event":"container_state","id":container.to_string(),"open":true,"status":error,
                        // Pack IDs have their own list. Prepending them changes
                        // every ordinary item's ordinal in a pickup request.
                        "packs":packs.into_iter().flatten().map(|id|id.to_string()).collect::<Vec<_>>(),
                        "items":items.into_iter().flatten().map(|id|id.to_string()).collect::<Vec<_>>()})
                }
                Some(Ok(ScriptMessage::CloseContainerDone { error, container })) => {
                    json!({"event":"container_state","id":container.to_string(),"open":false,"status":error})
                }
                Some(Ok(ScriptMessage::ContainerContents(c))) => {
                    json!({"event":"bag","id":object.map(|o|o.id.to_string()),"items":c.items.iter().map(u64::to_string).collect::<Vec<_>>()})
                }
                Some(Ok(ScriptMessage::Equipment(e))) => {
                    json!({"event":"equipment","slots":e.locations().unwrap_or_default().iter().map(|(slot,id)|json!({"slot":slot,"id":id.to_string()})).collect::<Vec<_>>()})
                }
                Some(Ok(ScriptMessage::MoveItemDone(_))) => unreachable!("MoveItemDone handled above"),
                Some(Ok(ScriptMessage::ReorganizeContentsDone(d))) => {
                    // Native drag ordinals are insertion boundaries in the
                    // pre-removal list. The Unreal view stores final cells.
                    let slot=d.actual_target_slot()-i32::from(d.from()==d.target() && d.actual_from_slot()<d.actual_target_slot());
                    json!({"event":"inventory_result","status":d.status(),"id":d.item().to_string(),"slot":slot})
                }
                Some(Ok(ScriptMessage::EquipItemDone(d))) => {
                    // Legions WM_Inventory::51 -> CPlayer::c8 ->
                    // CInventorySystem::0c is a successful completion. It reads
                    // InvEquipDesc::2f (actual location, slot 15), never slot 0.
                    // AC2E additionally uses slot 0 for its own rejection replies.
                    let actual = d.to_reference().entries.first().and_then(|e|match e {
                        PackedEntry::Heap {object,..} => object.slots.get(15).copied(), _ => None,
                    }).unwrap_or(d.location());
                    json!({"event":"inventory_result","status":if native_inventory {0} else {d.status()},"id":d.item().to_string(),"equipped":actual})
                }
                Some(Ok(ScriptMessage::UnequipItemDone(d))) => {
                    json!({"event":"inventory_result","status":if native_inventory {0} else {d.status()},"id":d.item().to_string(),"equipped":0,"slot":d.slot()})
                }
                Some(Ok(ScriptMessage::LootAllDone(d))) => {
                    json!({"event":"inventory_result","status":d.status(),"id":d.container().to_string()})
                }
                Some(Ok(ScriptMessage::UsageError { error, .. })) => {
                    usage_error(error)
                }
                Some(Ok(ScriptMessage::UsageDone(d))) => {
                    // CUsageSystem::0A shows the outcome unless UsageDesc::1B
                    // marks it silent. A failed use still reports its error.
                    if d.error!=0 {
                        usage_error(d.error)
                    } else if d.flags & USAGE_FLAG_SILENT == 0 &&
                        !(d.message.table_id==0x25000014 && d.message.string_id==0x4e1aec40 && d.message.literal.is_none()) {
                        // Suppress only the native generic success acknowledgement.
                        // Quest/NPC replies carried by UsageDone are still dialogue.
                        json!({"event":"chat","kind":8,"text":string(&d.message)})
                    } else {return vec![];}
                }
                Some(Ok(ScriptMessage::HearSpeech {
                    text, speaker_name, speaker, ..
                })) => {
                    json!({"event":"chat","kind":0x10,"format":"speech","text":string(&text),"speaker":string(&speaker_name),"speaker_id":speaker.to_string()})
                }
                Some(Ok(ScriptMessage::HearTell {
                    text, teller_name, kind, ..
                })) => {
                    json!({"event":"chat","kind":if (0x40000001..=0x40000003).contains(&kind){0x20020}else{0x20},"format":"tell","text":string(&text),"speaker":string(&teller_name)})
                }
                Some(Ok(ScriptMessage::DisplayStringInfo { text, kind, .. })) => {
                    json!({"event":"chat","kind":kind,"text":string(&text)})
                }
                Some(Ok(ScriptMessage::DisplayText { text, kind })) => {
                    json!({"event":"chat","kind":kind,"text":{"literal":text}})
                }
                _ => {
                    json!({"event":"script","function":function,"object":object.map(|o|o.id.to_string())})
                }
            }
        }
        ServerEvent::CharExitGame => json!({"event":"selection"}),
        ServerEvent::Other { opcode, body } =>
            json!({"event":"unsupported","opcode":opcode,"bytes":body.len()+4}),
        _ => return vec![],
    };
    vec![one]
}

#[cfg(test)]
mod tests {
    #[test]
    fn private_region_transitions_preserve_the_native_conflict_value() {
        use super::*;
        let event = |values: &[PackValue]| convert(ServerEvent::Interp {
            function: 0x82100022, arguments: pack_values(values), object: None,
        });
        for region in 0..=5 {
            assert_eq!(event(&[PackValue::Int(region)]), vec![json!({"event":"pvp_status","region":region})]);
        }
        assert_eq!(event(&[])[0]["event"], "error");
    }

    #[test]
    fn unknown_server_events_are_observable_without_exposing_the_payload() {
        use super::*;
        let payload=b"Private dialogue or player data".to_vec();
        let expected=json!({"event":"unsupported","opcode":0x12345678,"bytes":payload.len()+4});
        assert_eq!(convert(ServerEvent::Other{opcode:0x12345678,body:payload}),vec![expected]);
    }
    #[test]
    fn acquisition_preserves_owner_and_failure_without_inventing_a_pickup() {
        use super::*;
        let mut d=InvMoveDesc::new(0x818000000000002a,0,0,0x8140000000000011,3,1);
        for status in [0,54] {
            d.set_status(status);
            let v=convert(ServerEvent::Interp{function:0x820e005e,
                arguments:pack_values(&[PackValue::Reference(d.to_reference())]),object:None});
            assert_eq!(v[0],json!({"event":"inventory_acquired","status":status,
                "id":d.item().to_string(),"source_item":d.item().to_string(),"owner":d.target().to_string(),"from":"0","slot":3}));
        }
        let v=convert(ServerEvent::Interp{function:0x820e005e,arguments:pack_values(&[PackValue::Int(0)]),object:None});
        assert_eq!(v[0]["event"],"error");
    }
    #[test]
    fn owner_portal_callbacks_use_native_double_and_integer_arguments() {
        use super::*;
        let event = |function, value| convert(ServerEvent::Interp {
            function, arguments: pack_values(&[value]), object: None,
        });
        for (function, active) in [(0x82120020, true), (0x8212001b, false)] {
            assert_eq!(event(function, PackValue::LongFloat(0.0)), vec![json!({"event":"portal_space","active":active})]);
            assert_eq!(event(function, PackValue::Int(0))[0]["event"], "error");
        }
        for active in [false, true] {
            assert_eq!(event(0x8212001f, PackValue::Int(active.into())), vec![json!({"event":"portal_flag","active":active})]);
            assert_eq!(event(0x82120021, PackValue::Int(active.into())), vec![json!({"event":"portal_movement_lock","locked":active})]);
        }
        assert_eq!(event(0x82120021, PackValue::Int(2))[0]["event"], "error");
        assert_eq!(event(0x8212001f, PackValue::LongFloat(0.0))[0]["event"], "error");
    }
    #[test]
    fn native_equipment_completion_is_not_a_slot_number_error() {
        use super::*;
        let mut d=InvEquipDesc::equip(42,1,64,1,3);
        d.set_status(64); // Native descriptor metadata, not AC2E's error extension.
        let mut reference=d.to_reference();
        if let PackedEntry::Heap{object,..}=&mut reference.entries[0] {object.slots[15]=128;}
        let d=InvEquipDesc::from_reference(&reference).unwrap();
        for message in [ScriptMessage::EquipItemDone(d.clone()),ScriptMessage::UnequipItemDone(d)] {
            let event=||ServerEvent::Interp{function:message.function(),arguments:pack_values(&message.arguments()),object:None};
            let native=convert_profile(event(),true);
            assert_eq!(native[0]["status"],0);
            assert_eq!(convert(event())[0]["status"],64,"AC2E rejection extension remains intact");
            if matches!(message,ScriptMessage::EquipItemDone(_)) {assert_eq!(native[0]["equipped"],128);}
            else {assert_eq!(native[0]["equipped"],0);assert_eq!(native[0]["slot"],3);}
        }
    }
    use super::*;

    #[test]
    fn server_open_examination_preserves_unsigned_id_and_rejects_malformed_notices() {
        let id=0x800000000000002au64;
        let convert_args=|arguments|convert(ServerEvent::Interp{function:0x82120026,arguments,object:None});
        assert_eq!(convert_args(pack_values(&[PackValue::LongInt(id)])),
            vec![json!({"event":"open_examination","id":id.to_string()})]);
        for values in [vec![],vec![PackValue::Int(42)],vec![PackValue::LongInt(0)],
            vec![PackValue::LongInt(id),PackValue::Int(1)]] {
            assert_eq!(convert_args(pack_values(&values))[0]["event"],"error");
        }
    }

    #[test]
    fn usage_completion_displays_server_message_unless_silent_and_always_reports_failures() {
        let mut d=UsageDesc::new(1,2,0);
        d.message=StringInfo{literal:Some("Read the training stone.".into()),..Default::default()};
        d.flags=0;
        let run=|d|{let m=ScriptMessage::UsageDone(d);convert(ServerEvent::Interp {
            function:m.function(),arguments:pack_values(&m.arguments()),object:None})};
        let events=run(d.clone());
        assert_eq!(events[0]["event"],"chat");assert_eq!(events[0]["kind"],8);
        assert_eq!(events[0]["text"]["literal"],"Read the training stone.");
        d.flags=USAGE_FLAG_SILENT;assert!(run(d.clone()).is_empty());
        d.error=17;assert_eq!(run(d)[0]["event"],"error");
    }

    #[test]
    fn generic_use_success_is_quiet_but_dialogue_and_failure_are_not() {
        let mut d=UsageDesc::new(1,2,0);d.flags=0;
        d.message=StringInfo{table_id:0x25000014,string_id:0x4e1aec40,..Default::default()};
        let run=|d|{let m=ScriptMessage::UsageDone(d);convert(ServerEvent::Interp{
            function:m.function(),arguments:pack_values(&m.arguments()),object:None})};
        assert!(run(d.clone()).is_empty());
        d.message.string_id=0x12345678;assert_eq!(run(d.clone())[0]["event"],"chat");
        d.message.string_id=0x4e1aec40;d.error=54;assert_eq!(run(d)[0]["event"],"error");
    }

    fn selection_list() -> ServerEvent {
        ServerEvent::CharacterSet(CharacterSet {
            characters: Vec::new(), deleted: Vec::new(), status: 0, allowed: 5,
            account: "local".into(), turbine_chat: false, legions: None,
        })
    }

    #[test]
    fn interrupted_use_preserves_native_error_and_localization_for_both_replies() {
        for error in [54,91,17] {
            let mut desc=UsageDesc::new(1,2,0);desc.error=error;
            for message in [ScriptMessage::UsageDone(desc),ScriptMessage::UsageError{error,target:0,item:2}] {
                let out=convert(ServerEvent::Interp{function:message.function(),arguments:pack_values(&message.arguments()),object:None});
                assert_eq!(out[0]["event"],"error");assert_eq!(out[0]["code"],error);
                if error==17 {assert!(out[0].get("text").is_none());}
                else {
                    assert_eq!(out[0]["text"]["table"],0x25000014u32);
                    assert_eq!(out[0]["text"]["key"],0xca37af06u32);
                    assert_eq!(out[0]["message"],"Item use interrupted.");
                }
            }
        }
    }

    #[test]
    fn character_selection_waits_for_fresh_list_after_world_exit() {
        let mut state = SelectionReturn::default();
        let initial = state.convert(selection_list());
        assert_eq!(initial.len(), 1);
        assert_eq!(initial[0]["event"], "characters");
        assert!(!state.waiting());
        for _ in 0..2 {
            let exit = state.convert(ServerEvent::CharExitGame);
            assert_eq!(exit.len(), 1);
            assert_eq!(exit[0]["phase"], "leaving");
            assert!(state.waiting());
        }
        let returned = state.convert(selection_list());
        assert_eq!(returned.len(), 2);
        assert_eq!(returned[0]["event"], "selection");
        assert_eq!(returned[1]["event"], "characters");
        assert!(!state.waiting());
        assert_eq!(state.convert(selection_list()).len(), 1, "No duplicate UI logout on list refresh");
    }
    #[test]
    fn native_notices_are_readable_and_malformed_arguments_are_not_reinterpreted() {
        let text=StringInfo{literal:Some("Overseer: Bring the hammer.".into()),..Default::default()};
        let message=|arguments|ServerEvent::Interp{function:0x82120024,arguments,object:None};
        let args=pack_values(&[PackValue::Int(0),PackValue::Reference(PackedReference::native(NativeObject::StringInfo(text)))]);
        let out=convert(message(args));assert_eq!(out[0]["event"],"chat");
        assert_eq!(out[0]["text"]["literal"],"Overseer: Bring the hammer.");
        assert_eq!(out[1]["event"],"script");
        for args in [vec![],vec![0],pack_values(&[PackValue::Int(0),PackValue::Int(1)])] {
            assert!(!convert(message(args)).iter().any(|e|e["event"]=="chat"));
        }
    }
    #[test]
    fn native_container_open_close_and_failure_keep_identity_and_contents() {
        for error in [0, 17] {
            let message=ScriptMessage::OpenContainerDone{error,packs:Some(vec![0x8130000000000012]),items:Some(vec![0x8130000000000011]),container:0x8140000000010001};
            let events=convert(ServerEvent::Interp{function:message.function(),arguments:pack_values(&message.arguments()),object:None});
            assert_eq!(events[0]["event"],"container_state");assert_eq!(events[0]["open"],true);
            assert_eq!(events[0]["status"],error);assert_eq!(events[0]["id"],0x8140000000010001u64.to_string());
            assert_eq!(events[0]["items"][0],0x8130000000000011u64.to_string());
            assert_eq!(events[0]["items"].as_array().unwrap().len(),1);
            assert_eq!(events[0]["packs"][0],0x8130000000000012u64.to_string());
        }
        let message=ScriptMessage::CloseContainerDone{error:0,container:0x8140000000010001};
        let events=convert(ServerEvent::Interp{function:message.function(),arguments:pack_values(&message.arguments()),object:None});
        assert_eq!(events[0]["open"],false);assert_eq!(events[0]["status"],0);
    }
    #[test]
    fn consumed_item_removal_is_forwarded_without_losing_its_identity() {
        let id=0x813000000000002au64;
        let mut arguments=2u32.to_le_bytes().to_vec();arguments.extend(id.to_le_bytes());
        let message=|arguments|ServerEvent::Interp{function:0x820e0058,arguments,object:None};
        let event=convert(message(arguments.clone()));
        assert_eq!(event[0],json!({"event":"inventory_remove","id":id.to_string()}));
        arguments.pop();assert_eq!(convert(message(arguments))[0]["event"],"error");
    }
    #[test]
    fn global_creation_traits_and_clothing_survive_the_bridge() {
        let v=VisualDesc {parent:Some(0x1f000023),global_mods:Some(PartGroupDesc {
            appearance:vec![(0x20000014,vec![(0x10,1.0),(2,0.25)])],..Default::default()
        }),..Default::default()};
        let result=visual(&v);
        assert_eq!(result["aspects"].as_array().unwrap().len(),2);
        assert_eq!(result["aspects"][0]["key"],0x10);
        assert_eq!(result["aspects"][1]["value"],0.25);
    }
    fn object(action:ObjectAction)->ServerEvent {
        ServerEvent::Object(ObjectEvent {sender:InstanceIdWithStamp {id:0x8140000000030003,instance:7,other:11},action})
    }
    #[test]
    fn private_fx_targets_current_player_at_native_unit_strength() {
        for native in [false,true] {
            let events=convert_profile(ServerEvent::PrivateFx(vec![0x41000002,0x40000015]),native);
            assert_eq!(events,vec![
                json!({"event":"fx","id":null,"fx":0x41000002,"strength":1.0}),
                json!({"event":"fx","id":null,"fx":0x40000015,"strength":1.0})]);
            assert!(convert_profile(ServerEvent::PrivateFx(vec![]),native).is_empty());
        }
    }
    #[test]
    fn release_preserves_original_behavior_and_orders_optional_replacement() {
        for native in [false,true] {for replacement in [0,0x41000021] {
            let events=convert_profile(object(ObjectAction::ReleaseBehavior {
                behavior:0x41000020,params:Box::new(BehaviorParams {behavior:replacement,..Default::default()})}),native);
            assert_eq!(events[0],json!({"event":"release_behavior","id":"9313444029402382339","instance":7,"behavior":0x41000020}));
            assert_eq!(events.len(),if replacement==0 {1}else{2});
            if replacement!=0 {assert_eq!(events[1]["event"],"behavior");assert_eq!(events[1]["behavior"],replacement);}
        }}
        let events=convert(object(ObjectAction::StopLayer(3)));
        assert_eq!(events[0]["event"],"stop_layer");
        assert_eq!(events[0]["layer"],3);assert_eq!(events[0]["instance"],7);
    }
    #[test]
    fn initial_player_qualities_match_updates_without_losing_timestamps_or_ids() {
        let values=vec![(0x12e,QualityValue::Int(12)),(0x270,QualityValue::Bool(1)),
            (0xbb8,QualityValue::Float(2.5)),(0x12f,QualityValue::Timestamp(123456.125)),
            (0x100,QualityValue::String(b"Player".to_vec())),(0x200,QualityValue::DataId(0x47000001)),
            (0x1f9,QualityValue::InstanceId(u64::MAX)),
            (0x300,QualityValue::Position(Position {cell:0x77b80001,origin:[1.,2.,3.],rotation:[1.,0.,0.,0.]})),
            (0x100,QualityValue::StringInfo(Box::new(StringInfo {literal:Some("Player".into()),..Default::default()}))),
            (0x12c,QualityValue::LongInt(i64::MAX))];
        let mut p=Qualities::default();
        for (k,v) in &values {match v {
            QualityValue::Int(v)=>p.ints.push((*k,*v)),QualityValue::Bool(v)=>p.bools.push((*k,*v)),
            QualityValue::Float(v)=>p.floats.push((*k,*v)),QualityValue::Timestamp(v)=>p.timestamps.push((*k,*v)),
            QualityValue::String(v)=>p.strings.push((*k,v.clone())),QualityValue::DataId(v)=>p.dids.push((*k,*v)),
            QualityValue::InstanceId(v)=>p.iids.push((*k,*v)),QualityValue::Position(v)=>p.positions.push((*k,v.clone())),
            QualityValue::StringInfo(v)=>p.string_infos.push((*k,*v.clone())),QualityValue::LongInt(v)=>p.long_ints.push((*k,*v)),
        }}
        let expected:Vec<_>=values.into_iter().flat_map(|(quality,value)|convert(ServerEvent::Quality(QualityUpdate{object:None,quality,value}))).collect();
        for native in [false,true] {
            assert_eq!(convert_profile(ServerEvent::PlayerDescription(Box::new(p.clone())),native),expected);
        }
        assert_eq!(expected[6]["value"],u64::MAX.to_string());
        assert_eq!(expected[9]["value"],i64::MAX.to_string());
        for value in [QualityValue::Float(f32::NAN),QualityValue::Timestamp(f64::INFINITY)] {
            assert!(convert(ServerEvent::Quality(QualityUpdate{object:None,quality:1,value})).is_empty());
        }
    }
    #[test]
    fn motion_changes_keep_zero_scales_and_descriptor_ids() {
        for (action,name,value) in [(ObjectAction::SetVelocityScale(0.),"velocity",0.),
            (ObjectAction::SetAccelerationScale(0.5),"acceleration",0.5),
            (ObjectAction::SetJumpScale(2.),"jump",2.)] {
            let v=convert(object(action));assert_eq!(v[0]["event"],"motion");
            assert_eq!(v[0]["parameter"],name);assert_eq!(v[0]["value"],value);
            assert_eq!(v[0]["instance"],7);
        }
        let v=convert(object(ObjectAction::SetMotionInterp(0x32000000)));
        assert_eq!(v[0]["parameter"],"descriptor");assert_eq!(v[0]["value"],0x32000000u32);
    }
    #[test]
    fn look_events_preserve_instance_targets_clear_and_initial_state() {
        for target in [0x8140000000030043,u64::MAX,0] {
            let v=convert(object(ObjectAction::LookAt(target)));
            assert_eq!(v[0]["event"],"look");assert_eq!(v[0]["instance"],7);
            assert_eq!(v[0]["target"],target.to_string());
        }
        let v=convert(object(ObjectAction::LookAtDir{x:0.5,y:-0.25}));
        assert_eq!(v[0]["event"],"look_direction");assert_eq!(v[0]["x"],0.5);assert_eq!(v[0]["y"],-0.25);
        let v=convert(ServerEvent::CreateObject(Box::new(CreateObject {
            id:42,visual:VisualDesc::default(),weenie:WeenieDesc::default(),
            physics:PhysicsDesc{instance_stamp:12,look_at:Some(123),..Default::default()}})));
        assert_eq!(v[0]["event"],"object");assert_eq!(v[1]["event"],"look");
        assert_eq!(v[1]["instance"],12);assert_eq!(v[1]["target"],"123");
    }
    #[test]
    fn held_objects_keep_parenting_and_world_exit_distinct_from_destruction() {
        let parent=InstanceIdWithStamp {id:0x2000000000000042,instance:3,other:19};
        let v=convert(object(ObjectAction::Parent{parent:parent.clone(),location:4,orientation:0}));
        assert_eq!(v[0]["parent"],parent.id.to_string());assert_eq!(v[0]["parent_instance"],3);
        assert_eq!(v[0]["holding"],4);assert_eq!(v[0]["position_stamp"],19);
        let v=convert(object(ObjectAction::DeParent(parent.clone())));
        assert_eq!(v[0]["id"],parent.id.to_string());assert_eq!(v[0]["instance"],3);
        assert_eq!(v[0]["parent"],"9313444029402382339");assert_eq!(v[0]["parent_instance"],7);
        let v=convert(object(ObjectAction::LeaveWorld(20)));
        assert_eq!(v[0]["event"],"world_leave");assert_eq!(v[0]["position_stamp"],20);
        let v=convert(ServerEvent::Contain(parent.clone()));
        assert_eq!(v[0]["event"],"contain");assert_eq!(v[0]["position_stamp"],19);
        let v=convert(ServerEvent::DestroyObject(parent));
        assert_eq!(v[0]["event"],"remove");
    }
    #[test]
    fn combat_actions_keep_native_behavior_mode_and_rate() {
        let p=BehaviorParams {behavior:0x4100001f,mode:0x41000005,time_scale:0.75,hold_cycles:0,..Default::default()};
        let v=convert(object(ObjectAction::DoBehavior(Box::new(p))));
        assert_eq!(v[0]["id"],"9313444029402382339");assert_eq!(v[0]["instance"],7);
        assert_eq!(v[0]["behavior"],0x4100001fu32);assert_eq!(v[0]["mode"],0x41000005u32);
        assert_eq!(v[0]["rate"],0.75);assert_eq!(v[0]["hold"],false);
        assert_eq!(v[0]["hold_cycles"],0);
        let v=convert(object(ObjectAction::DoMode(0x41000012)));
        assert_eq!(v[0]["mode"],0x41000012u32);
    }
    #[test]
    fn native_attack_notices_keep_request_identity_and_toggle_argument_order() {
        let make=|m:CombatMessage|ServerEvent::Interp{function:m.function(),arguments:pack_values(&m.arguments()),object:None};
        let end=convert(make(CombatMessage::EndSpecialAttack(73)));
        assert_eq!(end[0],json!({"event":"skill_finished","request":73}));
        for enabled in [true,false] {
            let v=convert(make(CombatMessage::ToggleSpecialAttack{maneuver:0x59,toggled:enabled}));
            assert_eq!(v[0],json!({"event":"skill_toggled","skill":0x59,"enabled":enabled}));
        }
        let bad=convert(ServerEvent::Interp{function:0x82040028,arguments:pack_values(&[PackValue::Int(1)]),object:None});
        assert_eq!(bad[0]["event"],"error");
    }
    #[test]
    fn stories_forward_attack_and_damage_without_misattributing_stamps() {
        let s=PhysicsStory {attacker:Some(22),client_attack_context:Some(71),skill:Some(0x59),attacker_behavior:Some(BehaviorParams {behavior:0x41000089,propagate:1,..Default::default()}),
            hooks:vec![StoryHook {bitfield:1,target:Some(33),target_health_change:Some(-9),..Default::default()},
                StoryHook {bitfield:1,target:Some(44),target_health_change:Some(12),..Default::default()}],..Default::default()};
        let v=convert(object(ObjectAction::DoStory(Box::new(s))));
        assert_eq!(v.len(),5);assert_eq!(v[0]["event"],"story");assert_eq!(v[0]["request"],71);
        assert_eq!(v[1]["id"],"22");assert!(v[1].get("instance").is_none());
        assert!(v[1].get("story_request").is_none()); // Cannot activate a different attacker.
        assert_eq!(v[1]["propagate"],true);
        assert_eq!(v[2]["event"],"story_target");assert_eq!(v[2]["id"],"33");
        assert_eq!(v[2]["owner"],"9313444029402382339");assert_eq!(v[2]["instance"],7);
        assert_eq!(v[2]["attacker"],"22");assert_eq!(v[2]["skill"],0x59);
        assert_eq!(v[3]["id"],"33");assert_eq!(v[3]["damage"],9);
        assert_eq!(v[4]["event"],"story_target");assert_eq!(v[4]["id"],"44");
        let s=PhysicsStory{client_attack_context:Some(0),attacker_behavior:Some(BehaviorParams{behavior:0x410000b6,..Default::default()}),..Default::default()};
        let v=convert(object(ObjectAction::DoStory(Box::new(s))));
        assert_eq!(v[1]["story_request"],0); // Wrapped counter is distinct from an absent counter.
        let v=convert(object(ObjectAction::DoStory(Box::new(PhysicsStory::default()))));
        assert_eq!(v.len(),1);assert_eq!(v[0]["animated"],false);assert!(v[0]["request"].is_null());
    }
    #[test]
    fn story_missile_preparation_and_release_keep_native_indices_and_order() {
        let s=PhysicsStory{attacker:Some(22),skill:Some(0x316),attacker_behavior:Some(BehaviorParams{
            behavior:0x4100001f,..Default::default()}),hooks:vec![
                StoryHook{bitfield:2,anim_hook:Some(0),attack_hook:Some(1),weapon:Some(u64::MAX),target:Some(33),..Default::default()},
                StoryHook{bitfield:3,anim_hook:Some(2),attack_hook:Some(1),weapon:Some(u64::MAX),target:Some(33),..Default::default()}],..Default::default()};
        let v=convert(object(ObjectAction::DoStory(Box::new(s))));
        assert_eq!(v[1]["event"],"behavior");
        let m=&v[2];assert_eq!(m["event"],"story_missiles");
        assert_eq!(m["id"],"9313444029402382339");assert_eq!(m["instance"],7);
        assert_eq!(m["attacker"],"22");assert_eq!(m["skill"],0x316);
        assert_eq!(m["hooks"][0]["kind"],2);assert_eq!(m["hooks"][1]["kind"],3);
        assert_eq!(m["hooks"][1]["anim_hook"],2);assert_eq!(m["hooks"][1]["attack_hook"],1);
        assert_eq!(m["hooks"][1]["weapon"],u64::MAX.to_string());assert_eq!(m["hooks"][1]["target"],"33");
    }
    #[test]
    fn story_targets_keep_misses_but_never_infer_an_attacker_or_skill() {
        let story=PhysicsStory{attacker:Some(22),skill:Some(6),hooks:vec![
            StoryHook{bitfield:1,target:Some(33),..Default::default()},
            StoryHook{bitfield:2,target:Some(44),..Default::default()}],..Default::default()};
        let convert_story=|s|convert(object(ObjectAction::DoStory(Box::new(s))));
        let v=convert_story(story.clone());
        let targets:Vec<_>=v.iter().filter(|e|e["event"]=="story_target").collect();
        assert_eq!(targets.len(),1);assert_eq!(targets[0]["id"],"33");
        assert!(!v.iter().any(|e|e["event"]=="hit"));
        for s in [PhysicsStory{attacker:None,..story.clone()},PhysicsStory{attacker:Some(0),..story.clone()},
            PhysicsStory{skill:None,..story.clone()},PhysicsStory{skill:Some(0),..story.clone()}] {
            assert!(!convert_story(s).iter().any(|e|e["event"]=="story_target"));
        }
    }
    #[test]
    fn object_ids_preserve_all_64_bits() {
        let v = convert(ServerEvent::CreatePlayer {
            id: 0x8140000000030003,
            region: 1,
        });
        assert_eq!(v[0]["id"], "9313444029402382339");
    }
    #[test]
    fn object_selectability_distinguishes_absence_false_and_true_and_keeps_pet_owner() {
        for (mask,flags,expected) in [(0,2,None),(0x00800000,0,Some(false)),(0x00800000,2,Some(true))] {
            let out=convert(ServerEvent::CreateObject(Box::new(CreateObject {
                id:21,visual:VisualDesc::default(),physics:PhysicsDesc::default(),
                weenie:WeenieDesc{data_mask:mask,bitfield:Some(flags),pet_summoner:Some(0x8140000000030003),..Default::default()}})));
            assert_eq!(out[0]["selectable"].as_bool(),expected);
            assert_eq!(out[0]["summoner"],"9313444029402382339");
        }
    }
    #[test]
    fn native_missiles_keep_target_offsets_velocity_and_instance() {
        let p=Position{cell:0x02990109,origin:[1.,2.,3.],rotation:[1.,0.,0.,0.]};
        let v=convert(object(ObjectAction::InitializeMissile{position:p,velocity:[10.,0.,0.],target:Box::new(ac2_net::protocol::TargetPosition{target:0x8140000000030043,offset:p,velocity:[0.,1.,0.],initialized:1})}));
        assert_eq!(v[0]["event"],"missile");assert_eq!(v[0]["target"],0x8140000000030043u64.to_string());
        assert_eq!(v[0]["velocity"][0],10.);assert_eq!(v[0]["offset"]["position"][2],3.);assert_eq!(v[0]["initialized"],true);
        let v=convert(ServerEvent::CreateObject(Box::new(CreateObject{id:21,visual:VisualDesc::default(),weenie:WeenieDesc::default(),
            physics:PhysicsDesc{bitfield:0x80000,instance_stamp:3,position:Some(p),velocity:Some([10.,0.,0.]),target:Some(22),target_offset:Some([0.,0.,1.]),..Default::default()}})));
        assert_eq!(v[0]["event"],"object");assert_eq!(v[1]["event"],"missile");assert_eq!(v[1]["offset"]["position"][2],1.);
        assert_eq!(v[1]["instance"],3);assert_eq!(v[1]["target"],"22");
    }
    #[test]
    fn native_position_is_cell_local_and_heading_clockwise() {
        let v = position(&Position {
            cell: 0x02990109,
            origin: [59., 240., -44.5],
            rotation: [1., 0., 0., 0.],
        });
        assert_eq!(v["position"][2], -44.5);
        assert_eq!(v["heading"], 0.0);
        let tilted = position(&Position {cell:0x77b80006, origin:[1.,100.,182.5],
            rotation:[0.9238795,0.3826834,0.,0.]});
        assert_eq!(tilted["rotation"],json!([0.9238795f32,0.3826834f32,0.,0.]));
        let create=convert(ServerEvent::CreateObject(Box::new(CreateObject{id:21,
            visual:VisualDesc::default(),weenie:WeenieDesc::default(),
            physics:PhysicsDesc{timestamps:[100,3,4,5],..Default::default()}})));
        assert_eq!(create[0]["position_stamp"],100);assert_eq!(create[0]["teleport"],3);assert_eq!(create[0]["force"],4);
        let update=convert(ServerEvent::UpdatePosition(ServerPosition{
            sender:InstanceIdWithStamp{id:21,instance:0,other:0},time:1.,
            position:CellPosition{cell:0x03fd0113,origin:[1.,2.,3.]},heading_degrees:45.,motion:[0.;3],contact:true,
            position_stamp:101,teleport_stamp:3,force_position_stamp:4,jump:false,impulse:true,velocity:Some([0.,0.,2.])}));
        assert_eq!(update[0]["position_stamp"],101);assert_eq!(update[0]["instance"],0);
        assert_eq!(update[0]["impulse"],true);assert_eq!(update[0]["velocity"],json!([0.,0.,2.]));
    }
}
