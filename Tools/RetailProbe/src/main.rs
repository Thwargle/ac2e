//! Read-only reference oracle. Run the installed DAT's character-generation
//! bytecode through the pinned reference interpreter, independently of UE's port.
//! Content modes decode retained records and bytecode for evidence-backed authoring.
use std::{path::PathBuf, sync::Arc};

use ac2_core::DataId;
use ac2_dat::{DatDataset, DatOpenOptions};
use ac2_db::ResourceStore;
use ac2_script::{
    data::DatScriptData,
    host::{chargen, ScriptHost},
    native::CharGenResult,
    vm::Program,
};
use serde_json::json;
mod content;
mod bytecode;
mod usage;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let directory = PathBuf::from(std::env::args_os().nth(1).ok_or("Expected DAT directory")?);
    let dataset = DatDataset::open(DatOpenOptions::for_retail_install(directory))?;
    let store = ResourceStore::new(dataset.clone());
    if std::env::args().nth(2).as_deref() == Some("--startup-sounds") {
        use ac2_assets::Ac2VisualDesc;
        use ac2_dat::DatFileKind;
        use ac2_db::TypedResourceRef;
        ac2_db::install_ac2_property_types(&store)?;
        for id in dataset.ids_in_range(DatFileKind::Portal, 0x1f000000, 0x1fffffff) {
            let mut current = Some(DataId::new(id));
            let mut keys = std::collections::BTreeMap::new();
            let mut seen = std::collections::BTreeSet::new();
            while let Some(did) = current {
                if !seen.insert(did.raw()) { break; }
                let Some(desc) = store.load_cached_packed::<Ac2VisualDesc>(TypedResourceRef::new(DatFileKind::Portal, did, 0))? else { break; };
                for (key, strength) in desc.fx.startup.iter() { keys.entry(*key).or_insert(*strength); }
                current = desc.parent;
            }
            if keys.is_empty() { continue; }
            if let Some(fx) = ac2_assets::resolve_ac2_visual_fx_sounds(&store, DataId::new(id))? {
                for (key, strength) in keys {
                    if let Some(nodes) = fx.get(key) {
                        let sounds: Vec<_> = nodes.iter().flat_map(|n| n.sounds.iter()).map(|s| format!("{s:08x}")).collect();
                        if !sounds.is_empty() { println!("{}", json!({"visual":format!("{id:08x}"),"effect":format!("{key:08x}"),"strength":strength,"sounds":sounds})); }
                    }
                }
            }
        }
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--material") {
        for arg in std::env::args().skip(3) {
            let did=u32::from_str_radix(arg.trim_start_matches("0x"),16)?;
            println!("{did:08x}: {:#?}",ac2_assets::load_ac2_material(&store,DataId::new(did))?);
        }
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--fx-sounds") {
        ac2_db::install_ac2_property_types(&store)?;
        for arg in std::env::args().skip(3) {
            let did=u32::from_str_radix(arg.trim_start_matches("0x"),16)?;
            if let Some(fx)=ac2_assets::resolve_ac2_visual_fx_sounds(&store,DataId::new(did))? {
                for key in 0x41000000..0x41001000 {
                    if let Some(nodes)=fx.get(key) {
                        let sounds:Vec<_>=nodes.iter().flat_map(|n|n.sounds.iter()).map(|s|format!("{s:08x}")).collect();
                        if !sounds.is_empty(){println!("{}",json!({"visual":arg,"effect":format!("{key:08x}"),"sounds":sounds}));}
                    }
                }
            }
        }
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--loose-strings") {
        use ac2_db::ResourcePackedDecode;
        let root = PathBuf::from(std::env::args_os().nth(3).ok_or("Expected loose string-table directory")?);
        for arg in std::env::args().skip(4) {
            let did = u32::from_str_radix(arg.trim_start_matches("0x"), 16)?;
            if !(ac2_db::STRING_TABLE_FIRST..=ac2_db::STRING_TABLE_LAST).contains(&did) {
                return Err("Expected StringTable DID".into());
            }
            let path = root.join(format!("0x{did:08X}"));
            if !path.exists() {
                println!("{}", json!({"table":format!("{did:08x}"),"error":"missing loose record"}));
                continue;
            }
            if path.metadata()?.len() > 16 * 1024 * 1024 {
                return Err("Loose StringTable exceeds size bound".into());
            }
            let bytes = std::fs::read(path)?;
            let table = ac2_db::StringTable::decode_packed(
                ac2_core::QualifiedDataId::new(DataId::new(did), 0), 0, &bytes)?;
            let entries: std::collections::BTreeMap<_,_> = table.strings.iter().map(|(key,value)|
                (format!("{key:08x}"), json!({"text":value.text(),"private_table":value.table,
                    "variables":value.variables}))).collect();
            println!("{}", json!({"table":format!("{did:08x}"),"entries":entries}));
        }
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--strings") {
        for arg in std::env::args().skip(3) {
            let did=u32::from_str_radix(arg.trim_start_matches("0x"),16)?;
            if let Some(table)=ac2_db::load_string_table(&store,DataId::new(did))? {
                let entries:std::collections::BTreeMap<_,_>=table.strings.iter().map(|(key,value)|
                    (format!("{key:08x}"),json!({"text":value.text(),"variables":value.variables}))).collect();
                println!("{}",json!({"table":format!("{did:08x}"),"entries":entries}));
            }
        }
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--state") {
        for arg in std::env::args().skip(3) {
            let did = u32::from_str_radix(arg.trim_start_matches("0x"), 16)?;
            let state = ac2_script::wstate::load_wstate(&store, DataId::new(did))?;
            if let Some(state)=state {
                println!("{did:08x} pid {:?}",state.pid);
                match &state.references {
                    Ok(refs)=>for (id,entry) in refs.ids.iter().zip(&refs.entries) {println!("{id}: {entry:?}");},
                    Err(e)=>println!("{e:?}"),
                }
            } else {println!("{did:08x}: missing");}
        }
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--clock") {
        let source = ac2_world::Ac2WorldSource::open(Arc::new(store))?.ok_or("No world source")?;
        let region = source.region().ok_or("No game calendar")?;
        let mut cases = Vec::new();
        for seconds in [0., 1., 1234.5, 2453.999, 2454., 9000., 54321.,
            2640026.999, 2640027., 1.0e6, 1.0e9] {
            let mut clock = region.game_clock();
            clock.use_time(seconds);
            let (year, day, _, _) = clock.calendar();
            cases.push(json!({"seconds":seconds,"year":year,"day":day,
                "fraction":clock.present_time_of_day()}));
        }
        println!("{}", serde_json::to_string_pretty(&json!({
            "source":"Legions DAT calendar through pinned GameTime::UseTime reference",
            "cases":cases}))?);
        return Ok(());
    }
    let library = ac2_script::load_wlib(&store, DataId::new(ac2_script::LEGIONS_WLIB_DID))?
        .ok_or("Missing script library")?;
    if std::env::args().nth(2).as_deref() == Some("--code") {
        println!("{}",serde_json::to_string_pretty(&bytecode::listing(&library.stream,&std::env::args().skip(3).collect::<Vec<_>>()))?);
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--content") {
        println!("{}", serde_json::to_string_pretty(&content::recover(&store, &library.stream))?);
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--use-selection") {
        return usage::probe(dataset, store, &library.stream);
    }
    let mut host = ScriptHost::new(
        Arc::new(Program::from_stream(&library.stream)),
        Box::new(DatScriptData::new(dataset, store)),
    );
    if std::env::args().nth(2).as_deref() == Some("--radar") {
        for arg in std::env::args().skip(3) {
            let pid: u32 = arg.parse()?;
            host.create_weenie(1, pid, 0);
            println!("{pid}: {:?}",host.inq_radar_look(1));
            host.remove_weenie(1);
        }
        return Ok(());
    }
    let vm = host.vm_mut();
    if std::env::args().nth(2).as_deref() == Some("--bulk-desc") {
        use ac2_script::vm::{Context,FunctionId,InitState,InterpStatus,tag};
        let container=0x8000000000000123u64;let player=0x2000000000000456u64;
        let mut cases=Vec::new();
        for name in ["InvTakeAllDesc","InvTransmuteAllDesc"] {
            let pid=vm.program.package_names.iter().find(|(n,_)|n.as_slice()==name.as_bytes()).ok_or("Missing bulk package")?.1;
            let selector=match pid {0x20ef=>0x17,0x234b=>0x19,0x1b58=>0x1e,0x1b59=>0x1d,_=>return Err("Unreviewed bulk constructor".into())};
            let instance=vm.allocate_package(pid,InitState::UseWState,false).ok_or("Bulk defaults")?;
            let mut ctx=Context::new(0,None);
            ctx.push(0,tag::INT).map_err(|_|"quiet")?;
            ctx.push64(container,0x84).map_err(|_|"container")?;
            ctx.push64(player,0x84).map_err(|_|"player")?;
            ctx.push(instance,tag::REF).map_err(|_|"instance")?;
            let status=vm.call_ifun(&mut ctx,pid,FunctionId((pid<<16)|selector));
            if status!=InterpStatus::Terminated || vm.last_fault.is_some(){return Err(format!("{name}: {status:?} {:?}",vm.last_fault).into());}
            let reference=ac2_script::wire::to_net(&vm.refs.pack(instance)).map_err(|e|format!("Bulk reference: {e:?}"))?;
            cases.push(json!({"name":name,"pid":pid,"reference":reference.to_bytes()}));
        }
        println!("{}",json!({"container":container.to_string(),"player":player.to_string(),"cases":cases}));
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--move-desc") {
        use ac2_script::vm::{Context,FunctionId,InitState,InterpStatus,tag};
        let instance=vm.allocate_package(0x1f8,InitState::UseWState,false).ok_or("InvMoveDesc")?;
        let mode=std::env::args().nth(3).unwrap_or_default();
        let world=mode=="drop" || mode=="give";
        let item=0x8000000000000123u64;
        let from=if world {0x2000000000000456u64}else{0x8000000000000789u64};
        let target=match mode.as_str(){"drop"=>0,"give"=>0x8000000000000789u64,_=>0x2000000000000456u64};
        let from_slot=if world{7}else{0};let target_slot=if world{0}else{u32::MAX};let quantity=if world{3}else{1};
        let mut ctx=Context::new(0,None);
        ctx.push(0,tag::INT).map_err(|_|"move type")?;
        ctx.push(quantity,4).map_err(|_|"quantity")?;
        ctx.push(target_slot,4).map_err(|_|"target slot")?;
        ctx.push64(target,0x84).map_err(|_|"target")?;
        ctx.push(from_slot,4).map_err(|_|"source slot")?;
        ctx.push64(from,0x84).map_err(|_|"source")?;
        ctx.push64(item,0x84).map_err(|_|"item")?;
        ctx.push(instance,tag::REF).map_err(|_|"instance")?;
        let status=vm.call_ifun(&mut ctx,0x1f8,FunctionId(0x01f8003f));
        if status!=InterpStatus::Terminated || vm.last_fault.is_some(){return Err(format!("InvMoveDesc: {status:?} {:?}",vm.last_fault).into());}
        // CInventorySystem determines the direction after construction:
        // creature -> ground = 33, creature -> creature = 36. Its setter
        // receives a signed enum, unlike the UI's unsigned slots/counts.
        if world {
            let mut ctx=Context::new(0,None);
            ctx.push(if mode=="drop"{33}else{36},tag::INT).map_err(|_|"move type")?;
            ctx.push(instance,tag::REF).map_err(|_|"instance")?;
            let status=vm.call_ifun(&mut ctx,0x1f8,FunctionId(0x01f8001f));
            if status!=InterpStatus::Terminated || vm.last_fault.is_some(){return Err(format!("SetMoveType: {status:?} {:?}",vm.last_fault).into());}
        }
        let reference=ac2_script::wire::to_net(&vm.refs.pack(instance)).map_err(|e|format!("Move reference: {e:?}"))?;
        println!("{}",json!({"item":item.to_string(),"from":from.to_string(),"target":target.to_string(),
            "from_slot":from_slot,"target_slot":target_slot,"quantity":quantity,"reference":reference.to_bytes()}));
        return Ok(());
    }
    if std::env::args().nth(2).as_deref() == Some("--usage") {
        use ac2_script::vm::{Context, FunctionId, InitState, InterpStatus, tag};
        // CUsageSystem::HandleItemUsage -> UsageDesc::37. Execute the active
        // DAT's class defaults/constructor instead of assuming their layout.
        let instance=vm.allocate_package(0x245, InitState::UseWState, false)
            .ok_or("Cannot allocate native UsageDesc")?;
        let user=0x2000000000000456u64;
        let item=0x8000000000000123u64;
        let mut ctx=Context::new(0,None);
        let target = if std::env::args().nth(3).as_deref() == Some("targeted") { 0x8000000000000789u64 } else { 0 };
        let targets = if target == 0 { u32::MAX } else {
            use ac2_script::native::{ntype, NativeObject, ScriptList};
            vm.refs.create_native(ntype::L_LIST, NativeObject::List(ScriptList { wide: true, items: vec![target] }))
        };
        ctx.push(targets,tag::REF).map_err(|_|"Usage target list")?;
        ctx.push64(item,0x84).map_err(|_|"Usage item")?;
        ctx.push64(user,0x84).map_err(|_|"Usage user")?;
        ctx.push(instance,tag::REF).map_err(|_|"Usage instance")?;
        let status=vm.call_ifun(&mut ctx,0x245,FunctionId(0x02450037));
        if status != InterpStatus::Terminated || vm.last_fault.is_some() {
            return Err(format!("UsageDesc constructor failed: {status:?}, {:?}",vm.last_fault).into());
        }
        let reference=ac2_script::wire::to_net(&vm.refs.pack(instance))
            .map_err(|e|format!("Cannot pack native UsageDesc: {e:?}"))?;
        println!("{}",serde_json::to_string_pretty(&json!({
            "source":"Installed DAT UsageDesc::37 with class-default WState; no server connection",
            "user":user.to_string(),"item":item.to_string(),"target":target.to_string(),
            "reference":reference.to_bytes()
        }))?);
        return Ok(());
    }
    let (_, races) = chargen::race_sex_data(vm).ok_or("Race list script failed")?;
    let (_, race_info) = chargen::race_sex_info_data(vm).ok_or("Race/sex info script failed")?;
    let mut avatars = Vec::new();
    for (race, sexes) in races {
        for sex in sexes {
            let mut result = CharGenResult {
                race,
                sex,
                ..Default::default()
            };
            if chargen::set_defaults(vm, &mut result) != Some(1) {
                return Err(format!("SetCGRDefaults failed: {:?}", vm.last_fault).into());
            }
            let mut defaults = result.phys_type_values();
            defaults.sort_by_key(|(kind, _)| *kind);
            let mut choices = Vec::new();
            for (kind, value) in defaults {
                // The pinned wrapper pushes arguments in declaration order.
                // Retail CallIFun_InqValidPhysTypeValues pushes kind, sex, race
                // (admin 7148 and Legions 7160), so compensate at this boundary.
                let (_, values) = chargen::valid_phys_type_values(vm, kind, sex, race)
                    .ok_or("Valid-physique-values script failed")?;
                let index = values.iter().position(|v| v.to_bits() == value.to_bits());
                choices.push(json!({"kind": kind, "value": value,
                    "index": index, "count": values.len(), "values": values}));
            }
            let info = race_info.iter().find(|(key, _)| *key == race | sex)
                .map(|(_, info)| info).ok_or("Race/sex info missing")?;
            avatars.push(json!({"race": race, "sex": sex, "choices": choices,
                "physics": info.phys_obj_did, "held_objects": info.obj_in_loc}));
        }
    }
    println!(
        "{}",
        serde_json::to_string_pretty(&json!({
            "source": "DAT SetCGRDefaults / InqValidPhysTypeValues via pinned AC2_Rusted VM",
            "avatars": avatars
        }))?
    );
    Ok(())
}
