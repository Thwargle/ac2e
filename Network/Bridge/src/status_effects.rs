//! The native EffectRegistry and its live WM_Effect record changes.
use ac2_net::protocol::*;
use serde_json::{json, Value};
use std::collections::HashMap;

fn entry(graph: &PackedReference, id: u32) -> Option<&PackedEntry> {
    graph
        .entries
        .get(graph.ids.iter().position(|candidate| *candidate == id)?)
}

fn object(graph: &PackedReference, id: u32) -> Option<&HeapObject> {
    match entry(graph, id)? {
        PackedEntry::Heap { object, .. } => Some(object),
        _ => None,
    }
}

fn record(graph: &PackedReference, id: u32, eid: u32) -> Option<Value> {
    let obj = object(graph, id)?;
    if obj.pkg_id != 0x14e || obj.slots.len() < 21 {
        return None;
    }
    let slots = &obj.slots;
    let PackedEntry::Singleton { did, .. } = entry(graph, slots[14])? else {
        return None;
    };
    let cast = f64::from_bits((u64::from(slots[3]) << 32) | u64::from(slots[2]));
    let duration = f32::from_bits(slots[6]);
    if !cast.is_finite() || !duration.is_finite() {
        return None;
    }
    Some(json!({"eid":eid, "did":did, "cast":cast, "duration":duration, "flags":slots[20]}))
}

pub fn registry(value: &PackValue) -> Option<Value> {
    let PackValue::Reference(graph) = value else {
        return None;
    };
    let obj = object(graph, *graph.ids.first()?)?;
    if obj.pkg_id != 0x14f {
        return None;
    }
    let NativeObject::Hash(records) = object(graph, *obj.slots.get(4)?)?.native.as_ref()? else {
        return None;
    };
    let mut order_by_effect = HashMap::new();
    if let Some(NativeObject::Hash(orders)) = obj
        .slots
        .get(11)
        .and_then(|id| object(graph, *id))
        .and_then(|o| o.native.as_ref())
    {
        for (key, eid) in orders.keys.iter().zip(&orders.values) {
            if let HashKey::Value(order) = key {
                order_by_effect.insert(*eid, *order);
            }
        }
    }
    let mut effects = Vec::new();
    for (key, id) in records.keys.iter().zip(&records.values) {
        let HashKey::Value(eid) = key else { continue };
        let (Ok(reference), Ok(effect_id)) = (u32::try_from(*id), u32::try_from(*eid)) else {
            continue;
        };
        let Some(mut row) = record(graph, reference, effect_id) else {
            continue;
        };
        if let Some(order) = order_by_effect.get(eid) {
            row["order"] = json!(order);
        }
        effects.push(row);
    }
    Some(json!({"event":"status_effects","replace":true,"effects":effects}))
}

pub fn update(function: u32, args: &[u8]) -> Option<Value> {
    if !matches!(function, 0x82080016 | 0x82080017) {
        return None;
    }
    let values = unpack_values(args).ok()?;
    match (function, values.as_slice()) {
        (0x82080016, [PackValue::Int(eid)]) => Some(json!({"event":"status_effects","remove":eid})),
        (0x82080017, [PackValue::Int(eid), PackValue::Reference(graph)]) => {
            let row = record(graph, *graph.ids.first()?, *eid)?;
            Some(json!({"event":"status_effects","effects":[row]}))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use legions_server_double::{effect_registry, EffectGrant, EFFECT_TIMED};
    #[test]
    fn registry_uses_instance_id_and_preserves_server_deadline() {
        let registry = effect_registry(&[EffectGrant {
            eid: 91,
            effect: 0x47002345,
            time_cast: 100.5,
            duration: 9.0,
            flags: EFFECT_TIMED,
        }]);
        let event = super::registry(&PackValue::Reference(registry)).unwrap();
        assert_eq!(event["replace"], true);
        let row = &event["effects"][0];
        assert_eq!(row["eid"], 91);
        assert_eq!(row["did"], 0x47002345u32);
        assert_eq!(row["cast"], 100.5);
        assert_eq!(row["duration"], 9.0);
        assert_eq!(row["order"], 1);
        assert_eq!(
            update(0x82080016, &pack_values(&[PackValue::Int(91)])).unwrap()["remove"],
            91
        );
    }
}
