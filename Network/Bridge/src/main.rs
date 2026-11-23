// SPDX-License-Identifier: AGPL-3.0-only
// Separate process adapter for AC2_Rusted's native protocol. Stdio is private IPC, never a listener.
#![forbid(unsafe_code)]
mod events;
mod status_effects;
mod story_scenes;
mod output;
mod examination;
mod shop;
mod journal;
mod crafting;
mod craft_catalog;
mod groups;
mod allegiance;
mod preferences;
mod login;
mod dat_login;
mod dat_patch;
mod room_chat;
mod friends;
mod selection;
mod look;
mod inventory_bulk;
mod link_status;
mod localization;
#[cfg(test)]
mod native_usage_tests;
use ac2_net::{protocol::*, NetClient, NetClientConfig, NetEvent};
use serde_json::{json, Value};
use std::{
    io::{self, BufRead},
    net::ToSocketAddrs,
    sync::mpsc,
    time::{Duration, Instant},
};

const MAX_LINE: usize = 65536;
#[cfg(test)]
mod use_tests {
    use super::*;
    #[test]
    fn movement_stamps_reject_duplicates_stale_values_and_honor_wrap() {
        assert!(!newer_stamp(1,1));assert!(!newer_stamp(10,9));
        assert!(newer_stamp(0,1));assert!(newer_stamp(65535,0));
        assert!(!newer_stamp(0,65535));
    }
    #[test]
    fn targeted_stop_is_native_and_preserves_the_full_unsigned_target() {
        let target=0x8140000000030003;
        let ClientEvent::Interp{function,arguments,encrypted}=stop_target(target) else {panic!("targeted stop")};
        assert_eq!(function,0x82040025);assert!(!encrypted);
        let values=unpack_values(&arguments).unwrap();
        assert_eq!(values.len(),1);
        assert!(matches!(values[0],PackValue::LongInt(id) if id==target));
    }
    #[test]
    fn combat_stance_packs_the_native_null_attack_target() {
        let ClientEvent::Interp{function,arguments,..}=combat_stance(true,0) else {panic!("combat")};
        assert_eq!(function,0x82040008);
        assert_eq!(arguments,vec![2,0,0,0,0,0,0,0,0,0,0,0]);
        let ClientEvent::Interp{function,arguments,..}=combat_stance(false,42) else {panic!("peace")};
        assert_eq!(function,0x82040010);assert!(arguments.is_empty());
    }
    #[test]
    fn bag_sort_uses_native_reorganize_and_unsigned_heap_fields() {
        let ClientEvent::Interp{function,arguments,..}=reorganize_item(InvMoveDesc::new(0x813000000000002a,7,3,7,8,12)) else {panic!("reorganize event")};
        assert_eq!(function,0x820e0049);assert_eq!(arguments.len(),284);
        assert_eq!(arguments[232+2],0x84);assert_eq!(arguments[232+44],0x84);
        assert_eq!(arguments[232+25],4);assert_eq!(arguments[232+26],4);
        let ScriptRequest::ReorganizeContents(desc)=ScriptRequest::decode(function,&arguments).unwrap().unwrap() else {panic!("sort descriptor")};
        assert_eq!(desc.item(),0x813000000000002a);assert_eq!(desc.from_slot(),3);assert_eq!(desc.target_slot(),8);assert_eq!(desc.quantity(),12);
    }
    #[test]
    fn equip_and_unequip_keep_native_unsigned_ids_and_slot_types() {
        for equip in [true,false] {
            let d=if equip {InvEquipDesc::equip(0x8140000000000001,7,0x40,7,3)}
                else {InvEquipDesc::unequip(0x8140000000000001,7,0x40,3)};
            let ClientEvent::Interp {function,arguments,..}=equip_item(d,equip) else {panic!("equip event")};
            assert_eq!(arguments.len(),180);
            assert_eq!(arguments[111],0x84);assert_eq!(arguments[115],4);
            let decoded=ScriptRequest::decode(function,&arguments).unwrap().unwrap();
            let d=match decoded {ScriptRequest::EquipItem(d)|ScriptRequest::UnequipItem(d)=>d,_=>panic!("equip descriptor")};
            assert_eq!(d.item(),0x8140000000000001);assert_eq!(d.container(),7);assert_eq!(d.location(),0x40);assert_eq!(d.slot(),3);
        }
    }
    #[test]
    fn native_use_keeps_unsigned_object_ids_and_default_target() {
        let owner=0x5000000000000001;let item=0x8140000000030003;
        let ClientEvent::Interp {function,arguments,..}=use_item(owner,item,0) else {panic!("use event")};
        assert_eq!(function,0x82160016);assert_eq!(arguments.len(),160);
        assert_eq!(&arguments[112..132],&[1,1,0x84,0xcd,1,2,0x84,0xcd,3,0x82,0xcd,2,0x82,0xcd,2,1,2,2,2,2]);
        let ScriptRequest::Use(desc)=ScriptRequest::decode(function,&arguments).unwrap().unwrap() else {panic!("use desc")};
        assert_eq!(desc.user,owner);assert_eq!(desc.item,item);assert_eq!(desc.target,0);
    }
}
fn emit(value: Value) {
    use std::sync::OnceLock;
    static OUTPUT: OnceLock<output::Output> = OnceLock::new();
    let output = OUTPUT.get_or_init(|| {
        let (output, pending) = output::Output::channel(8 * 1024 * 1024);
        std::thread::spawn(move || {
            if pending.write_to(io::stdout().lock()).is_err() { std::process::exit(1); }
        });
        output
    });
    if output.send(format!("{value}\n")).is_err() { std::process::exit(1); }
}
// CPhysicsObj::is_newer (16-bit native movement stamps).
fn newer_stamp(old: u16, new: u16) -> bool {
    if new > old { new-old <= 0x7fff } else { old-new > 0x7fff }
}
fn text<'a>(v: &'a Value, key: &str) -> Result<&'a str, String> {
    v[key].as_str().ok_or_else(|| format!("Missing {key}"))
}
fn id(v: &Value, key: &str) -> Result<u64, String> {
    text(v, key)?.parse().map_err(|_| format!("Invalid {key}"))
}
// Legions gmCWInterface::StopAttacking: IFun 00b40104 -> 9aac0013 ->
// 82040026 sends 82040025 with the target IID. This is not peace mode.
fn stop_target(target: u64) -> ClientEvent {
    ClientEvent::Interp {function:0x82040025,
        arguments:pack_values(&[PackValue::LongInt(target)]),encrypted:false}
}
// Legions WM_Combat::16 packs a long IID for 82040008, even when merely
// drawing a weapon without a target. An empty payload throws on retail servers.
// WM_Combat::14 (peace) has no arguments.
fn combat_stance(enabled: bool,target:u64) -> ClientEvent {
    ClientEvent::Interp {function:if enabled {0x82040008} else {0x82040010},
        arguments:if enabled {pack_values(&[PackValue::LongInt(target)])} else {vec![]},encrypted:false}
}
fn word(v: &Value, key: &str) -> Result<u32, String> {
    v[key]
        .as_u64()
        .and_then(|n| u32::try_from(n).ok())
        .ok_or_else(|| format!("Invalid {key}"))
}
fn vector(v: &Value, key: &str) -> Result<[f32; 3], String> {
    let a = v[key]
        .as_array()
        .filter(|a| a.len() == 3)
        .ok_or("Invalid vector")?;
    let mut result = [0.; 3];
    for i in 0..3 {
        result[i] = a[i]
            .as_f64()
            .filter(|n| n.is_finite() && n.abs() < 1e8)
            .ok_or("Invalid coordinate")? as f32;
    }
    Ok(result)
}
fn send(client: &mut NetClient, event: ClientEvent) -> Result<(), String> {
    let (queue, data) = event.encode();
    // Pyrealium accepts clear character creation on its control queue (4),
    // whereas the original client uses encrypted queue 8 with a session key.
    let queue = if queue == 8 { 4 } else { queue };
    client.send(queue, data).ok_or("Not connected")?;
    Ok(())
}
fn use_item(user: u64, item: u64, target: u64) -> ClientEvent {
    let mut reference = UsageDesc::new(user, item, target).to_reference();
    // Native UsageDesc::37 stores the item/user as unsigned object IDs (0x84).
    // The reference crate uses generic signed long tags (0x82) for both;
    // preserve the captured native profile accepted by Pyrealium/AC2E.
    if let PackedEntry::Heap { object, .. } = &mut reference.entries[0] {
        object.tags[2] = 0x84;
        object.tags[6] = 0x84;
        if target!=0 {object.tags[9]=0x84;}
    }
    ClientEvent::Interp { function: 0x82160016,
        arguments: pack_values(&[PackValue::Reference(reference)]), encrypted: false }
}
fn equip_item(desc: InvEquipDesc, equip: bool) -> ClientEvent {
    // gmCWInterface::EquipItem / UnEquipItem: typed unsigned IDs and slot enums.
    // Generic reference setters keep signed Int/Long tags; the native script
    // constructors preserve these narrower tags in their packed heap graph.
    let mut reference=desc.to_reference();
    if let PackedEntry::Heap {object,..}=&mut reference.entries[0] {
        object.tags=vec![2,1,2,0x84,0xcd,2,if equip {2}else{4},4,0x84,0xcd,0x84,0xcd,2,2,2,4,0x82,0xcd,4];
    }
    ClientEvent::Interp {function:if equip {0x820e004f}else{0x820e0015},
        arguments:pack_values(&[PackValue::Reference(reference)]),encrypted:false}
}
fn transfer_item(desc: InvMoveDesc) -> ClientEvent {
    transfer_item_with_type(desc,0)
}
fn transfer_item_with_type(desc: InvMoveDesc, move_type: u32) -> ClientEvent {
    let mut reference=desc.to_reference();
    // gmCWInterface::MoveItem -> InvMoveDesc::3F retains the unsigned
    // object IDs and slot/quantity arguments from CallIFun_MoveItem.
    if let PackedEntry::Heap{object,..}=&mut reference.entries[0] {
        object.slots[12]=move_type;
        for i in [44,16,2,36,8,32,28] {object.tags[i]=0x84;object.tags[i+1]=0xcd;}
        for i in [39,11,26,1,24,50,25] {object.tags[i]=4;}
    }
    ClientEvent::Interp{function:0x820e0036,arguments:pack_values(&[PackValue::Reference(reference)]),encrypted:false}
}
fn world_item_request(v: &Value, player: u64, native: bool) -> Result<ClientEvent,String> {
    let giving=match v["command"].as_str(){Some("give_item")=>true,Some("drop_item")=>false,
        _=>return Err("Invalid world item action".into())};
    if giving && !native {return Err("This server profile does not support giving items yet".into());}
    let item=id(v,"id")?;
    let from=word(v,"from")?;
    let quantity=word(v,"quantity")?;
    if item==0 || item==u64::MAX || player==0 || item==player {return Err("Invalid inventory item".into());}
    if from>i32::MAX as u32 || quantity==0 || quantity>i32::MAX as u32 {return Err("Invalid item slot or quantity".into());}
    let target=if giving {id(v,"target")?}else if native {0}else{u64::from(word(v,"landblock")?)};
    if giving && (target==0 || target==player || target==item || target==u64::MAX) {
        return Err("Invalid item recipient".into());
    }
    let desc=InvMoveDesc::new(item,player,from as i32,target,
        if native || giving {0}else{-1},quantity as i32);
    if native {
        // CInventorySystem::DirectiveMoveItem classifies creature -> world
        // (33) or creature -> creature (36) before sending WM_Inventory::36.
        Ok(transfer_item_with_type(desc,if giving {36}else{33}))
    } else {
        // AC2E's bounded inventory profile predates the native client path.
        let mut event=reorganize_item(desc);
        if let ClientEvent::Interp{function,..}=&mut event {*function=0x820e0036;}
        Ok(event)
    }
}
fn reorganize_item(desc: InvMoveDesc) -> ClientEvent {
    let mut reference=desc.to_reference();
    // Native same-container drag (820e0049), including typed DID/unsigned IID
    // fields retained in the sorting captures. Generic MoveItem is a different
    // operation: ownership transfer, splitting, or dropping into the world.
    if let PackedEntry::Heap{object,..}=&mut reference.entries[0] {
        object.tags=vec![2,2,0x84,0xcd,2,2,4,2,0x84,0xcd,2,4,2,2,2,2,0x84,0xcd,0x82,0xcd,2,2,4,2,4,2,4,2,0x84,0xcd,1,2,0x84,0xcd,2,2,0x84,0xcd,4,4,2,2,2,2,0x84,0xcd,2,2,2,2,4];
        if desc.from_slot()!=0 {object.tags[1]=4;}
        if desc.quantity()>1 {object.tags[25]=4;}
    }
    ClientEvent::Interp{function:0x820e0049,arguments:pack_values(&[PackValue::Reference(reference)]),encrypted:false}
}
struct Session {
    client: NetClient,
    link_status: link_status::LinkStatus,
    build: Build,
    dat_login: dat_login::DatLogin,
    account: String,
    player: u64,
    instance: u16,
    position_stamp: u16,
    teleport: u16,
    force: u16,
    moveto: u16,
    ready: bool,
    native_inventory: bool,
    characters_received: bool,
    since: Instant,
    entered: bool,
    selection_return: events::SelectionReturn,
    selection: selection::Selection,
    skill_request: u32,
    preferences_dirty: bool,
    logout_fence: preferences::LogoutFence,
    logout_started: Option<Instant>,
    logout_probe_sent: bool,
    last_examination_reply: Option<Instant>,
    last_examination_request: Option<Instant>,
    room_chat: room_chat::RoomChatState,
    story_scenes: story_scenes::StoryScenes,
    friends: Option<friends::Friends>,
    bulk_inventory: Option<inventory_bulk::Profile>,
    dat_directory: String,
    localization: localization::Localization,
    craft_catalog:craft_catalog::Catalog,
    local_tell_echo: bool,
}
impl Session {
    fn connect(v: &Value) -> Result<Self, String> {
        let host = text(v, "host")?;
        let port = word(v, "port")?;
        if host.len() > 253 || port == 0 || port >= 65535 {
            return Err("Invalid server address/port".into());
        }
        let server = (host, port as u16)
            .to_socket_addrs()
            .map_err(|_| "Cannot resolve server")?
            .find(|a| a.is_ipv4())
            .ok_or("Server has no IPv4 address")?;
        let logon = login::logon(v, server.ip().is_loopback())?;
        let account = logon.account.clone();
        let build = Build::from_version(&logon.version);
        let socket = ac2_net::UdpTransport::bind(server).map_err(|e| e.to_string())?;
        let (transport, link_status) = link_status::measure(socket);
        let client = NetClient::with_transport(NetClientConfig {server, logon}, Box::new(transport));
        let mut dat_login=dat_login::DatLogin::new(v["dat_directory"].as_str().unwrap_or(""));
        if v["auth"].as_str()==Some("gls_direct"){dat_login.allow_updates(v["dat_cache_root"].as_str().unwrap_or(""));}
        Ok(Self {
            client,
            link_status,
            build,
            dat_login,
            account,
            player: 0,
            instance: 0,
            position_stamp: 0,
            teleport: 0,
            force: 0,
            moveto: 0,
            ready: false,
            native_inventory: v["auth"].as_str()==Some("gls_direct"),
            characters_received: false,
            since: Instant::now(),
            skill_request: 0,
            entered: false,
            selection_return: events::SelectionReturn::new(v["auth"].as_str()==Some("gls_direct")),
            selection: selection::Selection::new(v["auth"].as_str()==Some("gls_direct")),
            preferences_dirty: false,
            logout_fence: preferences::LogoutFence::default(),
            logout_started: None,
            logout_probe_sent: false,
            last_examination_reply: None,
            last_examination_request: None,
            room_chat: room_chat::RoomChatState::default(),
            story_scenes: story_scenes::StoryScenes::default(),
            friends: (v["auth"].as_str()!=Some("gls_direct")).then(friends::Friends::default),
            bulk_inventory: if v["auth"].as_str()==Some("gls_direct"){None}else{Some(inventory_bulk::Profile::stock())},
            dat_directory: v["dat_directory"].as_str().unwrap_or("").to_owned(),
            craft_catalog:craft_catalog::Catalog::load(v["dat_directory"].as_str().unwrap_or("")),
            localization: localization::Localization::load(v["dat_directory"].as_str().unwrap_or("")),
            local_tell_echo: v["auth"].as_str()==Some("gls_direct"),
        })
    }
    fn command(&mut self, v: &Value) -> Result<(), String> {
        if self.selection_return.waiting() {
            return Err("Wait for the server character list before selecting a character".into());
        }
        match text(v, "command")? {
            "delete" if !self.ready && self.player == 0 => {
                send(&mut self.client, ClientEvent::DeleteCharacter {
                    account: self.account.clone(), character: id(v, "id")?,
                })?;
            }
            "enter" if self.logout_started.is_none() => {
                self.story_scenes.clear();
                if let Some(friends)=&mut self.friends { friends.reset(); }
                self.ready = false;
                self.player = 0;
                self.selection.reset();
                self.preferences_dirty=false;self.logout_fence=preferences::LogoutFence::default();self.logout_probe_sent=false;
                // Pyrealium retains its incoming ordered counter across character logout.
                if !self.entered {
                    self.client.net_mut().enter_world();
                    self.entered = true;
                }
                send(
                    &mut self.client,
                    ClientEvent::EnterWorld {
                        character: id(v, "id")?,
                        account: self.account.clone(),
                    },
                )?;
                emit(
                    json!({"event":"status","phase":"entering","message":"Entering server world..."}),
                );
            }
            "create" => {
                let name = text(v, "name")?;
                if name.len() > 64 || name.is_empty() {
                    return Err("Invalid character name".into());
                }
                let mut physical = Vec::new();
                for pair in v["physical"].as_array().ok_or("Missing physical values")? {
                    let key = pair[0]
                        .as_u64()
                        .and_then(|n| u32::try_from(n).ok())
                        .ok_or("Invalid physique")?;
                    let value = pair[1]
                        .as_f64()
                        .filter(|x| x.is_finite() && x.abs() < 100.)
                        .ok_or("Invalid physique")?;
                    physical.push((key, value as f32));
                    if physical.len() > 32 {
                        return Err("Too many physique values".into());
                    }
                }
                send(
                    &mut self.client,
                    ClientEvent::CharGenResult {
                        account: self.account.clone(),
                        result: CharGenResult {
                            name: name.into(),
                            entity_did: 0x4700110f,
                            start_area: word(v, "start_area")?,
                            race: word(v, "race")?,
                            sex: word(v, "sex")?,
                            physical,
                            ..Default::default()
                        },
                    },
                )?;
            }
            "ready" if self.player != 0 => {
                send(&mut self.client, ClientEvent::SceneRenderingComplete)?;
                self.ready = true;
                emit(
                    json!({"event":"status","phase":"world","message":"Connected to server world"}),
                );
                if let Some(friends)=&mut self.friends {
                    if !friends.subscribed {
                        friends.request("list","",None,|packet| send(&mut self.client,packet))?;
                        friends.request("mode","",None,|packet| send(&mut self.client,packet))?;
                    }
                }
            }
            "friends" if self.ready => {
                let friends=self.friends.as_mut().ok_or("This server does not support the AC2E friends service.")?;
                let mode=v.get("mode").map(|value| value.as_u64().and_then(|n| u32::try_from(n).ok()).ok_or("Invalid friend availability.")).transpose()?;
                friends.request(text(v,"action")?,v["name"].as_str().unwrap_or(""),mode,|packet| send(&mut self.client,packet))?;
            }
            "move" if self.ready => {
                let heading = v["heading"]
                    .as_f64()
                    .filter(|v| v.is_finite())
                    .ok_or("Invalid heading")? as f32;
                let p = ClientPosition {
                    time: self.client.net().timer().cur_time(),
                    position: CellPosition {
                        cell: word(v, "cell")?,
                        origin: vector(v, "position")?,
                    },
                    heading_degrees: heading.rem_euclid(360.),
                    motion: vector(v, "motion")?,
                    // PlayerMovementModule::m_is_actively_moving, not a heartbeat.
                    // Cancelling on every idle report interrupts NPC use/equipment.
                    cancel_moveto: v["cancel_moveto"].as_bool().unwrap_or(false),
                    instance_stamp: self.instance,
                    teleport_stamp: self.teleport,
                    force_position_stamp: self.force,
                    moveto_stamp: self.moveto,
                    jump_velocity: None,
                };
                send(&mut self.client, ClientEvent::Position(p))?;
            }
            "game_options" if self.ready => {
                send(&mut self.client, preferences::game_command(v)?)?;self.preferences_dirty=true;
            }
            "shortcut" if self.ready => {
                send(&mut self.client, preferences::command(v)?)?;self.preferences_dirty=true;
            }
            "chat" if self.ready => {
                let message = text(v, "text")?;
                if matches!(v["channel"].as_str(), Some("general" | "fellowship" | "allegiance")) {
                    let packet = self.room_chat.request_channel(v["channel"].as_str().unwrap(), message)?;
                    self.client.net_mut().send_chat(&packet).ok_or("Could not send chat")?;
                    return Ok(());
                }
                if message.trim().is_empty() || message.len() > 2048 || message.chars().any(char::is_control) {
                    return Err("Enter a chat message of up to 2048 bytes on one line.".into());
                }
                if v["channel"].as_str()==Some("tell") && text(v,"recipient")?.trim().is_empty(){return Err("Enter a tell recipient.".into())}
                let literal = StringInfo {
                    literal: Some(message.into()),
                    ..Default::default()
                };
                let request = match v["channel"].as_str().unwrap_or("say") {
                    "emote" => ScriptRequest::EmoteText(message.into()),
                    "tell" => ScriptRequest::Tell {
                        mode: 0,
                        text: literal,
                        recipient: StringInfo {
                            literal: Some(text(v, "recipient")?.into()),
                            ..Default::default()
                        },
                    },
                    "say" => ScriptRequest::Say {
                        mode: 0,
                        text: literal,
                    },
                    _ => return Err("Unsupported server chat channel".into()),
                };
                send(&mut self.client, request.to_event())?;
                // Native CCommunicationSystem echoes a submitted tell locally.
                // AC2E already echoes it from the server; do not duplicate that.
                // Emit only after the transport accepts the request (not delivery).
                if self.local_tell_echo && v["channel"].as_str()==Some("tell") {
                    emit(json!({"event":"chat","kind":0x20,"format":"tell_sent",
                        "speaker":{"literal":text(v,"recipient")?},"text":{"literal":message}}));
                }
            }
            "target" if self.ready => {
                for event in self.selection.requests(id(v,"id")?) {
                    send(&mut self.client,event)?;
                }
            }
            "look_target" | "look_direction" if self.ready => {
                send(&mut self.client,look::command(v)?)?;
            }
            "fellowship" if self.ready => {send(&mut self.client,groups::command(v)?)?;}
            "allegiance" if self.ready => {send(&mut self.client,allegiance::command(v)?)?;}
            "examine" if self.ready => {
                let target=id(v,"id")?;
                send(&mut self.client, examination::request(target,self.build))?;
                self.last_examination_request=Some(Instant::now());
                if target==self.player {self.logout_fence.request(false);}
            }
            "attack" if self.ready => {
                let target = id(v, "id")?;
                send(
                    &mut self.client,
                    ClientEvent::Interp {
                        function: 0x82040008,
                        arguments: pack_values(&[PackValue::LongInt(target)]),
                        encrypted: false,
                    },
                )?;
            }
            "stop_attack" if self.ready => {
                send(
                    &mut self.client,
                    ClientEvent::Interp {
                        function: 0x82040010,
                        arguments: vec![],
                        encrypted: false,
                    },
                )?;
            }
            "stop_target" if self.ready => {
                send(&mut self.client,stop_target(id(v,"id")?))?;
            }
            "combat" if self.ready => {
                let enabled = v["enabled"].as_bool().ok_or("Expected combat enabled flag")?;
                send(&mut self.client, combat_stance(enabled,self.selection.combat_target()))?;
            }
            "skill_cancel_untrain" if self.ready => {
                send(&mut self.client,ClientEvent::Interp{function:0x82140001,arguments:vec![],encrypted:false})?;
            }
            "skill_train" | "skill_raise" | "skill_untrain" if self.ready => {
                let skill=word(v,"skill")?;
                if skill==0 || skill>65535{return Err("Invalid skill ID".into());}
                let function=match v["command"].as_str().unwrap_or("") {"skill_train"=>0x82140002,"skill_raise"=>0x8214000c,_=>0x82140007};
                send(&mut self.client,ClientEvent::Interp{function,arguments:pack_values(&[PackValue::Int(skill)]),encrypted:false})?;
            }
            "skill" if self.ready => {
                let skill = word(v, "skill")?;
                if skill == 0 || skill > 65535 { return Err("Invalid skill ID".into()); }
                self.skill_request = self.skill_request.wrapping_add(1);
                send(&mut self.client, ClientEvent::Interp {
                    function: 0x8204000b,
                    arguments: pack_values(&[PackValue::Int(self.skill_request), PackValue::Int(skill), PackValue::LongInt(id(v, "target")?)]),
                    encrypted: false,
                })?;
                // Native RequestSpecialAttack is a local notice paired with the
                // actual wire counter, not a speculative cooldown or acceptance.
                emit(json!({"event":"skill_requested","request":self.skill_request,"skill":skill}));
            }
            "resurrect" if self.ready => {
                send(
                    &mut self.client,
                    ClientEvent::Interp {
                        function: 0x8207000e,
                        arguments: vec![],
                        encrypted: false,
                    },
                )?;
            }
            "recall" if self.ready => {
                self.skill_request=self.skill_request.wrapping_add(1);
                send(&mut self.client, ClientEvent::Interp {
                    function: 0x8204000b,
                    arguments: pack_values(&[PackValue::Int(self.skill_request), PackValue::Int(0x343), PackValue::LongInt(self.player)]),
                    encrypted: false,
                })?;
                emit(json!({"event":"skill_requested","request":self.skill_request,"skill":0x343}));
            }
            "loot" | "transmute_loot" if self.ready => {
                if self.bulk_inventory.is_none(){self.bulk_inventory=Some(inventory_bulk::Profile::load(&self.dat_directory)?);}
                let request=self.bulk_inventory.unwrap().request(id(v,"id")?,self.player,v["command"]=="transmute_loot");
                send(&mut self.client,request)?;
            }
            "open_container" if self.ready => {
                send(
                    &mut self.client,
                    ScriptRequest::OpenContainer(id(v, "id")?).to_event(),
                )?;
            }
            "close_container" if self.ready => {
                send(&mut self.client, ScriptRequest::CloseContainer(id(v,"id")?).to_event())?;
            }
            "take_item" if self.ready => {
                send(&mut self.client, transfer_item(InvMoveDesc::new(
                    id(v,"id")?,id(v,"container")?,word(v,"from")? as i32,self.player,-1,1)))?;
            }
            "move_item" if self.ready => {
                send(
                    &mut self.client,
                    reorganize_item(InvMoveDesc::new(
                        id(v, "id")?,
                        self.player,
                        word(v, "from")? as i32,
                        self.player,
                        word(v, "to")? as i32,
                        word(v, "quantity")? as i32,
                    )),
                )?;
            }
            "drop_item" | "give_item" if self.ready => {
                let event=world_item_request(v,self.player,self.native_inventory)?;
                send(&mut self.client,event)?;
            }
            "equip" if self.ready => {
                send(
                    &mut self.client,
                    equip_item(InvEquipDesc::equip(
                        id(v, "id")?,
                        self.player,
                        word(v, "location")?,
                        self.player,
                        word(v, "slot")? as i32,
                    ),true),
                )?;
            }
            "unequip" if self.ready => {
                send(
                    &mut self.client,
                    equip_item(InvEquipDesc::unequip(
                        id(v, "id")?,
                        self.player,
                        word(v, "location")?,
                        word(v, "slot")? as i32,
                    ),false),
                )?;
            }
            "craft_validate" | "craft_execute" | "craft_raise" if self.ready => send(&mut self.client,crafting::command(v)?)?,
            "shop_buy" if self.ready => send(&mut self.client,shop::buy(self.player,id(v,"vendor")?,id(v,"offer")?))?,
            "shop_sell" if self.ready => send(&mut self.client,shop::sell(self.player,id(v,"item")?,word(v,"slot")?,word(v,"quantity")?))?,
            "shop_close" if self.ready => send(&mut self.client,shop::close(id(v,"vendor")?))?,
            "shop_refresh" if self.ready => send(&mut self.client,ClientEvent::Interp{function:0xb83a0066,arguments:pack_values(&[PackValue::LongInt(id(v,"vendor")?)]),encrypted:false})?,
            "story_scene_complete" if self.ready => {
                if let Some(reply) = self.story_scenes.finished(word(v,"id")?) {
                    send(&mut self.client,reply)?;
                    self.story_scenes.clear();
                }
            }
            "use" if self.ready => {
                let event = use_item(self.player, id(v, "item")?, id(v, "target")?);
                send(&mut self.client, event)?;
            }
            "logout" if self.ready && self.logout_started.is_none() => {
                let character=id(v,"id")?;
                if character!=self.player {return Err("Logout character does not match this session.".into());}
                if self.preferences_dirty {
                    // Logoff is unordered queue 4; preference writes are ordered
                    // queue 3. A native read-only self-examination is a barrier
                    // on queue 3 before we let logoff overtake those writes.
                    self.logout_probe_sent=false;self.logout_started=Some(Instant::now());
                    self.ready=false;return Ok(());
                }
                self.ready = false;
                send(
                    &mut self.client,
                    ClientEvent::LogOffCharacter {
                        character: id(v, "id")?,
                    },
                )?;
            }
            _ => return Err("Command is not available in this session state".into()),
        }
        Ok(())
    }
    fn poll(&mut self) -> bool {
        match self.dat_login.poll(){
            Ok(Some(reply))=>{
                if self.client.net_mut().send_dat_event(reply).is_none(){emit(json!({"event":"disconnected","message":"Could not acknowledge installed game data."}));return false;}
                self.since=Instant::now();
                if let Some(path)=self.dat_login.updated.take(){
                    emit(json!({"event":"data_updated","directory":path}));
                    // Let the transport flush the acknowledgement before the UI
                    // reloads its DAT readers and opens a new login session.
                    self.client.poll();
                    return false;
                }
            },
            Ok(None)=>{},
            Err(message)=>{emit(json!({"event":"disconnected","message":message}));return false;}
        }

        if self.logout_started.is_some_and(|t|t.elapsed()>Duration::from_secs(15)) {
            self.logout_started=None;self.logout_fence.cancel();self.ready=true;
            emit(json!({"event":"logout_cancelled","message":"Logout cancelled: the server did not confirm processing the hotbar changes. You are still connected."}));
        }
        // Do not collide with an outstanding UI examination or the server's
        // 100 ms cooldown. Time is a pacing guard; only the reply releases logout.
        if self.logout_started.is_some() && !self.logout_probe_sent && self.logout_fence.idle()
            && self.last_examination_reply.is_none_or(|t|t.elapsed()>=Duration::from_millis(350))
            && self.last_examination_request.is_none_or(|t|t.elapsed()>=Duration::from_millis(600)) {
            match send(&mut self.client,examination::request(self.player,self.build)) {
                Ok(())=>{self.logout_fence.request(true);self.logout_probe_sent=true;self.last_examination_request=Some(Instant::now());}
                Err(message)=>{self.logout_started=None;self.ready=true;emit(json!({"event":"logout_cancelled","message":message}));}
            }
        }
        if !self.characters_received && self.since.elapsed() > Duration::from_secs(if self.dat_login.progress().is_some(){180}else{45}) {
            emit(json!({"event":"disconnected","message":"Server login timed out before receiving the character list. Check the server's supported client version and game data."}));
            return false;
        }
        for event in self.client.poll() {
            match event {
                NetEvent::Connected { server_time, .. } => {
                    emit(json!({"event":"clock","server_time":server_time}));
                }
                NetEvent::TimeSync(_) => {
                    // HandleTimeSynch has already applied native monotonic clock adoption.
                    // Forward that clock, not the process's local elapsed time.
                    emit(json!({"event":"clock","server_time":self.client.net().timer().cur_time()}));
                }
                NetEvent::LoggedOn => {
                    emit(
                        json!({"event":"status","phase":"selection","message":"Loading server characters..."}),
                    );
                }
                NetEvent::Disconnected(_) => {
                    emit(
                        json!({"event":"disconnected","message":"Server connection closed or timed out"}),
                    );
                    return false;
                }
                NetEvent::ConnectionError { .. } => {
                    emit(
                        json!({"event":"disconnected","message":"Server rejected the connection"}),
                    );
                    return false;
                }
                NetEvent::Blob(blob) => {
                    match login::decode(&blob.data, self.build) {
                        Ok(event) => {
                            match &event {
                                ServerEvent::Dat(dat) if blob.queue==QUEUE_DAT => {
                                    self.since=Instant::now();
                                    match self.dat_login.receive(dat) {
                                        Ok(Some(reply)) => {
                                            if self.client.net_mut().send_dat_event(reply).is_none() {
                                                emit(json!({"event":"disconnected","message":"Could not send the game-data version response."}));
                                                return false;
                                            }
                                            emit(json!({"event":"status","phase":"checking_data","message":"Checking installed game data..."}));
                                        }
                                        Ok(None) => {if let Some(message)=self.dat_login.progress(){emit(json!({"event":"status","phase":"updating_data","message":message}));}},
                                        Err(message) => {emit(json!({"event":"disconnected","message":message}));return false;}
                                    }
                                }
                                ServerEvent::CharacterSet(set) => {
                                    self.characters_received=true;
                                    self.room_chat.reset();
                                    self.room_chat.turbine=set.turbine_chat;
                                }
                                ServerEvent::CreatePlayer { id, .. } => self.player = *id,
                                ServerEvent::CreateObject(o) if o.id == self.player => {
                                    self.instance = o.physics.instance_stamp;
                                    self.position_stamp = o.physics.timestamps[0];
                                    // PhysicsDesc stamps: position, teleport, force-position, move-to.
                                    self.teleport = o.physics.timestamps[1];
                                    self.force = o.physics.timestamps[2];
                                    self.moveto = o.physics.timestamps[3];
                                }
                                ServerEvent::UpdatePosition(p) | ServerEvent::PositionCell(p)
                                    if p.sender.id == self.player =>
                                {
                                    if p.sender.instance!=self.instance || p.position.cell==0 || !newer_stamp(self.position_stamp,p.position_stamp) {continue;}
                                    self.position_stamp=p.position_stamp;
                                    if newer_stamp(self.teleport,p.teleport_stamp) {self.teleport=p.teleport_stamp;}
                                    if newer_stamp(self.force,p.force_position_stamp) {self.force=p.force_position_stamp;}
                                }
                                ServerEvent::Object(o) if o.sender.id == self.player && o.sender.instance == self.instance => {
                                    if let ObjectAction::MoveTo {stamp,..} = &o.action {
                                        if newer_stamp(self.moveto,*stamp) {self.moveto=*stamp;}
                                    }
                                }
                                ServerEvent::CharExitGame => {
                                    self.story_scenes.clear();
                                    if let Some(friends)=&mut self.friends { friends.reset(); }
                                    self.ready = false;
                                    self.player = 0;
                                    self.selection.reset();
                                    self.room_chat.reset();
                                }
                                _ => {}
                            }
                            if let Some(value) = self.story_scenes.receive(&event) {
                                emit(self.localization.event(value));
                                continue;
                            }
                            if let Some(values) = self.room_chat.receive(&event) {
                                for value in values { emit(value); }
                                continue;
                            }
                            if matches!(&event, ServerEvent::Interp { function: ac2_gun_friends::EVENT_ID, .. }) {
                                if self.ready {
                                    if let Some(friends)=&mut self.friends {
                                        if let Some(value)=friends.receive(&event,self.player) { emit(value); }
                                    }
                                }
                                continue;
                            }
                            for value in self.selection_return.convert(event) {
                                if value["event"]=="examination" {
                                    self.last_examination_reply=Some(Instant::now());
                                    if value["id"].as_str()==Some(self.player.to_string().as_str()) && self.logout_fence.response() {
                                        if self.logout_started.take().is_some() {
                                            self.preferences_dirty=false;
                                            if let Err(message)=send(&mut self.client,ClientEvent::LogOffCharacter{character:self.player}) {
                                                self.preferences_dirty=true;self.ready=true;emit(json!({"event":"logout_cancelled","message":message}));
                                            }
                                        }
                                        continue;
                                    }
                                }
                                emit(self.localization.event(self.craft_catalog.event(value)));
                            }
                        }
                        Err(_) => {
                            let opcode = blob
                                .data
                                .get(..4)
                                .map(|a| u32::from_le_bytes(a.try_into().unwrap()))
                                .unwrap_or(0);
                            emit(
                                json!({"event":"unsupported","opcode":opcode,"bytes":blob.data.len()}),
                            );
                        }
                    }
                }
                _ => {}
            }
        }
        if let Some(sample) = self.link_status.sample(Instant::now()) {
            emit(sample);
        }
        true
    }
}
fn main() {
    let (tx, rx) = mpsc::sync_channel::<Value>(64);
    std::thread::spawn(move || {
        let mut input = io::stdin().lock();
        loop {
            let mut line = Vec::new();
            // Bound each command before allocating; closing stdin closes the native session.
            let mut limited = std::io::Read::take(&mut input, (MAX_LINE + 1) as u64);
            let n = match limited.read_until(b'\n', &mut line) {
                Ok(n) => n,
                Err(_) => break,
            };
            if n == 0 || n > MAX_LINE {
                break;
            }
            if let Ok(v) = serde_json::from_slice(&line) {
                if tx.send(v).is_err() {
                    break;
                }
            }
        }
    });
    let mut session: Option<Session> = None;
    emit(json!({"event":"bridge","version":1}));
    loop {
        for _ in 0..64 {
            match rx.try_recv() {
                Ok(v) => {
                    let command = v["command"].as_str().unwrap_or("");
                    if command == "disconnect" {
                        session = None;
                        emit(json!({"event":"disconnected","message":"Disconnected"}));
                        continue;
                    }
                    if command == "quit" {
                        return;
                    }
                    let result = if command == "connect" {
                        session = None;
                        Session::connect(&v).map(|s|{session=Some(s);emit(json!({"event":"status","phase":"connecting","message":"Connecting to server..."}));})
                    } else if let Some(s) = session.as_mut() {
                        s.command(&v)
                    } else {
                        Err("Not connected".into())
                    };
                    if let Err(message) = result {
                        emit(
                            json!({"event":if command=="connect" {"disconnected"} else {"error"},"message":message}),
                        );
                    }
                }
                Err(mpsc::TryRecvError::Empty) => break,
                Err(mpsc::TryRecvError::Disconnected) => return,
            }
        }
        if let Some(s) = session.as_mut() {
            if !s.poll() {
                session = None;
            }
        }
        std::thread::sleep(Duration::from_millis(5));
    }
}
