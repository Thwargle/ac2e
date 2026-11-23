// SPDX-License-Identifier: AGPL-3.0-only
//! ECM_Physics head reports are independent of CPlayer/combat selection.
use ac2_net::protocol::ClientEvent;
use serde_json::Value;

pub(crate) fn command(v: &Value) -> Result<ClientEvent, String> {
    match v["command"].as_str() {
        Some("look_target") => Ok(ClientEvent::LookAt { target: super::id(v, "id")? }),
        Some("look_direction") => {
            let component = |name: &str| -> Result<f32, String> {
                v[name].as_f64().map(|value| value as f32).filter(|value| value.is_finite())
                    .ok_or_else(|| format!("Invalid head {name}"))
            };
            Ok(ClientEvent::LookAtDir { x: component("x")?, y: component("y")? })
        }
        _ => Err("Unknown head report".into()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn target_and_clear_preserve_the_native_iid_without_changing_selection() {
        for id in [0x8140000000030003u64, 0, u64::MAX] {
            let event=command(&json!({"command":"look_target","id":id.to_string()})).unwrap();
            assert_eq!(event, ClientEvent::LookAt { target:id });
            let (queue, bytes)=event.encode();
            assert_eq!(queue, 3);
            assert_eq!(&bytes[..4], &0x100c7u32.to_le_bytes());
            assert_eq!(&bytes[4..], &id.to_le_bytes());
        }
    }

    #[test]
    fn direction_uses_x_and_z_sliders_on_the_physics_queue() {
        let event=command(&json!({"command":"look_direction","x":0.25,"y":-0.5})).unwrap();
        assert_eq!(event.encode(), (3, vec![0xc8,0,1,0,0,0,0x80,0x3e,0,0,0,0xbf]));
        let orbit=command(&json!({"command":"look_direction","x":4.5,"y":-0.3})).unwrap();
        assert_eq!(orbit, ClientEvent::LookAtDir { x:4.5, y:-0.3 });
        for v in [json!({"x":1e150,"y":0}),json!({"x":null,"y":0}),json!({"x":0})] {
            let mut v=v;v["command"]=json!("look_direction");assert!(command(&v).is_err());
        }
    }
}
