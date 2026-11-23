//! Offline UI-call oracle; never opens a socket or changes game data.
use std::sync::Arc;
use ac2_core::DataId;
use ac2_db::ResourceStore;
use ac2_dat::DatDataset;
use ac2_script::{bytestream::ByteStream, data::DatScriptData,
    host::{IFunArg, IFunResult, ScriptHost}, vm::{InterpStatus, Program}};
use serde_json::{json,Value};

fn calls(host: &mut ScriptHost) -> Vec<Value> {
    host.take_server_calls().into_iter().map(|c|json!({
        "function":c.function,"encrypted":c.encrypt,
        "arguments":ac2_net::protocol::pack_values(&c.args)
    })).collect()
}
fn completed(result: IFunResult) -> Result<(),Box<dyn std::error::Error>> {
    if result.outcome.status!=InterpStatus::Terminated || result.outcome.fault.is_some() {
        return Err(format!("Native UI call did not complete: {:?}",result.outcome).into());
    }
    Ok(())
}

pub fn probe(dataset: Arc<DatDataset>, store: ResourceStore, stream: &ByteStream)
    -> Result<(), Box<dyn std::error::Error>> {
    let arg=std::env::args().nth(3).ok_or("Expected entity DID")?;
    let did=u32::from_str_radix(arg.trim_start_matches("0x"),16)?;
    let state=ac2_script::wstate::load_wstate(&store,DataId::new(0x71000000 | (did&0xffffff)))?
        .ok_or("Missing entity WState")?;
    let pid=state.pid.ok_or("Entity WState has no package")?;
    let program=Arc::new(Program::from_stream(stream));
    let package=program.package_names.iter().find(|(_,id)|*id==pid)
        .map(|(n,_)|String::from_utf8_lossy(n).into_owned());
    let mut host=ScriptHost::new(program,Box::new(DatScriptData::new(dataset,store)));
    let user=0x2000000000000456u64;
    let item=0x8000000000000123u64;
    if !host.create_weenie(user,0x1aa4,0) || !host.create_weenie(item,pid,did) {
        return Err("Cannot construct offline player/item".into());
    }
    host.set_player(user);
    if std::env::args().any(|a|a=="--combat") {
        // gCWPhysicsObject::CallEFun_StopBehavior returns whether it stopped a
        // behavior. No animated physics object exists in this offline harness.
        host.vm_mut().efuns.register(0x800027,|_,ctx|{
            let i=ctx.fp as usize-3;ctx.data[i]=0;ctx.tags[i]=2;
            InterpStatus::Success
        });
        if std::env::args().any(|a|a=="--selected") {
            completed(host.call_ifun(user,0x1aa4006a,&[IFunArg::Long(item)])
                .map_err(|e|format!("Native selection: {e:?}"))?)?;
            calls(&mut host);
        }
        let mut results=Vec::new();
        for enabled in [1,0] {
            completed(host.call_ifun(user,0x00b40008,&[IFunArg::Int(enabled)])
                .map_err(|e|format!("Native combat interface: {e:?}"))?)?;
            results.push(json!({"enabled":enabled,"calls":calls(&mut host),
                "notices":format!("{:?}",host.take_notices()),"dropped":host.take_dropped()}));
        }
        println!("{}",serde_json::to_string_pretty(&results)?);
        return Ok(());
    }
    // Native CPlayer::Select, then the ggmCWInterface::CallIFun_UseSelection
    // interface. This exercises item checks and CUsageSystem, not just UsageDesc.
    let call=|h:&mut ScriptHost, f, args:&[IFunArg]| -> Result<(),Box<dyn std::error::Error>> {
        completed(h.call_ifun(user,f,args).map_err(|e|format!("Native UI call: {e:?}"))?)
    };
    call(&mut host,0x1aa4006a,&[IFunArg::Long(item)])?;
    let select_calls=calls(&mut host);
    call(&mut host,0x1aa4006a,&[IFunArg::Long(item)])?;
    let repeat_calls=calls(&mut host);
    call(&mut host,0x00b40006,&[])?;
    let use_calls=calls(&mut host);
    call(&mut host,0x1aa4006a,&[IFunArg::Long(0)])?;
    let clear_calls=calls(&mut host);
    let dropped=host.take_dropped();
    if !dropped.is_empty() {return Err(format!("Native host dropped calls: {dropped:?}").into());}
    println!("{}",serde_json::to_string_pretty(&json!({
        "source":"Installed DAT CPlayer::Select and CallIFun_UseSelection; no server connection",
        "entity":did,"package":package,"pid":pid,"user":user.to_string(),"item":item.to_string(),
        "completed":true,"select_calls":select_calls,"repeat_calls":repeat_calls,
        "use_calls":use_calls,"clear_calls":clear_calls,
        "notices":format!("{:?}",host.take_notices()),
        "script_notices":format!("{:?}",host.take_script_notices()),
        "dropped":dropped
    }))?);
    Ok(())
}
