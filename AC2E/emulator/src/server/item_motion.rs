//! Reach first, then commit the existing durable inventory transaction. A
//! disconnect, death, travel or loss of ownership cannot grant a queued item.
use super::*;
pub(super) enum Action {
    Drop(crate::inventory_request::DropRequest),
    Pickup(u64),
    Loot(crate::loot::Request),
}
pub(super) struct Pending {
    character:u64, instance:u16, origin:Placement,
    action:Option<Action>, drop:bool, contact:u64, finish:u64, restore:bool,
}
pub(super) fn begin(peer:&mut Peer,character:u64,action:Action,store:&Store,
    scene:&mut crate::scene::Scene,loot:&crate::loot::Loot,encounters:&mut Option<crate::encounters::Encounters>,now:u64)->io::Result<Vec<crate::scene::Delivery>> {
    if peer.item_motion.as_ref().is_some_and(|p|p.action.is_some()){
        return Err(invalid("Wait for your item animation to reach the item."));
    }
    let saved=&store.characters[&character];
    if !saved.progression.alive(){return Err(invalid("You must be alive to move items."));}
    let origin=scene.player_placement(character).ok_or_else(||invalid("Wait until you arrive."))?;
    match &action {
        Action::Drop(request)=>{
            if request.landblock!=origin.cell&0xffff0000 {return Err(invalid("Your destination has changed."));}
            request.moved.checked_command(character,&saved.inventory).map_err(other)?;
            let drop=store.drop_candidate(&peer.session.account,character,request.moved.item,origin)?;
            scene.validate_drop(&drop)?;
            if saved.professions.1.jobs.get(&request.moved.item).is_some_and(|j|j.point.is_none()) {
                return Err(invalid("Active survey maps cannot be dropped."));
            }
        },
        Action::Pickup(id)=>{
            if !scene.combat_distance(character,*id).is_some_and(|d|d<=4.){return Err(invalid("Move within four meters to pick up the item."));}
            store.pickup_candidate(&peer.session.account,character,*id)?;
        },
        Action::Loot(crate::loot::Request::Take{container,player,item})=>{
            if *player!=character || !scene.combat_distance(character,*container).is_some_and(|d|d<=4.) {
                return Err(invalid("Move within four meters of your loot."));
            }
            let reward=loot.get(*container,character,now).ok_or_else(||invalid("No loot is available to your character."))?;
            if reward.items.is_empty() || item.is_some_and(|id|!reward.items.iter().any(|i|i.id==id)) {
                return Err(invalid("That item is no longer in the container."));
            }
            if saved.inventory.free_slot().is_none(){return Err(invalid("Your backpack is full."));}
        },
        Action::Loot(_)=>return Err(invalid("This action has no pickup animation.")),
    }
    let drop=matches!(action,Action::Drop(_));
    let duration=crate::combat_visual::item_motion_ms(saved.creation.as_ref().map_or(1,|r|r.race),drop);
    let instance=saved.instance;
    // Consecutive loots may replace the recovery half of an already committed
    // reach, but may never replace an uncommitted inventory transaction.
    let restore=peer.combat || peer.item_motion.as_ref().is_some_and(|p|p.restore);
    let mut updates=Vec::new();
    peer.cancel_attack(character,encounters)?;
    if peer.combat {
        peer.queue_ordered_batch(vec![script::private_call(script::COMBAT_STATE,&script::integer(0)).map_err(other)?])?;
        updates.extend(training_combat_visual(peer,character,store,scene,false,false,false)?);
        peer.combat=false;
    }
    let (next,stamp)=peer.emotes.next_visual(instance).map_err(other)?;
    let wire=crate::combat_visual::item_motion(character,instance,stamp,drop,false).map_err(other)?;
    peer.validate_scene_batch(std::slice::from_ref(&wire))?;
    updates.extend(scene.broadcast(character,wire).map_err(other)?);
    scene.save_visual_stamp(character,stamp).map_err(other)?;peer.emotes=next;
    peer.item_motion=Some(Pending{character,instance,origin,action:Some(action),drop,
        contact:now+duration/2,finish:now+duration,restore});
    Ok(updates)
}
pub(super) fn tick(peer:&mut Peer,store:&mut Store,scene:&mut crate::scene::Scene,
    loot:&mut crate::loot::Loot,metadata:&BTreeMap<u32,Option<crate::inventory_request::Metadata>>,now:u64)
    ->io::Result<Vec<crate::scene::Delivery>> {
    let Some(mut pending)=peer.item_motion.take() else {return Ok(Vec::new())};
    let id=pending.character;
    let valid=matches!(peer.session.phase,Phase::InWorld{character} if character==id)
        && store.characters.get(&id).is_some_and(|s|s.instance==pending.instance&&s.progression.alive());
    if !valid {return Ok(Vec::new());} // Old avatar lifetimes never restore stance.
    let interrupted=peer.combat || !scene.player_placement(id).and_then(|p|pending.origin.distance(p)).is_some_and(|d|d<=0.75);
    let mut updates=Vec::new();
    if !interrupted && now>=pending.contact {
        if let Some(action)=pending.action.take() {
            let result=match action {
                Action::Drop(request)=>drop_ground_item(peer,id,&request,store,scene,metadata,now),
                Action::Pickup(item)=>pickup_ground_item(peer,id,item,store,scene,&loot.catalog,metadata,now),
                Action::Loot(request)=>corpse_action(peer,id,request,loot,store,scene,metadata,now),
            };
            match result {Ok(deliveries)=>updates.extend(deliveries),Err(error)=>{
                peer.queue_ordered_batch(vec![script::chat(&format!("Item action rejected: {error}"),1).map_err(other)?])?;
                pending.finish=now;
            }}
        }
    }
    if interrupted || now>=pending.finish {
        if interrupted {
            let (next,stamp)=peer.emotes.next_visual(pending.instance).map_err(other)?;
            updates.extend(scene.broadcast(id,crate::combat_visual::item_motion(id,pending.instance,stamp,pending.drop,true).map_err(other)?).map_err(other)?);
            scene.save_visual_stamp(id,stamp).map_err(other)?;peer.emotes=next;
            if pending.action.is_some(){peer.queue_ordered_batch(vec![script::chat("Item action interrupted.",1).map_err(other)?])?;}
        }
        if pending.restore && !peer.combat {
            peer.queue_ordered_batch(vec![script::private_call(script::COMBAT_STATE,&script::integer(1)).map_err(other)?])?;
            updates.extend(training_combat_visual(peer,id,store,scene,true,false,false)?);peer.combat=true;
        }
    } else {peer.item_motion=Some(pending);}
    Ok(updates)
}
