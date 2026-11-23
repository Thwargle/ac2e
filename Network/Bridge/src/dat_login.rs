// SPDX-License-Identifier: AGPL-3.0-only
//! Retail CLCache login exchange. Never acknowledge uninstalled iterations.
use ac2_dat::{DatDataset, DatFileKind, DatOpenOptions, RETAIL_DDD_ITERATION_DID};
use ac2_net::protocol::{end_ddd, interrogation_response, read_int_set, DatEvent};

use crate::dat_patch::{self, Download, MAX_PATCH};
use std::{collections::{BTreeMap,BTreeSet}, path::PathBuf, sync::mpsc::{self,Receiver}};

pub(crate) struct DatLogin {
    directory: String,
    response: Option<Vec<u8>>,
    cache_root: Option<PathBuf>,
    missing: Option<Vec<ac2_net::protocol::MissingIteration>>,
    expected: BTreeSet<(u64,u32)>,
    records: BTreeMap<(u64,u32),Vec<u8>>,
    received_bytes: usize,
    install: Option<Receiver<Result<PathBuf,String>>>,
    pub updated: Option<PathBuf>,
}
impl DatLogin {
    pub(crate) fn new(directory: &str) -> Self {
        Self { directory: directory.into(), response: None, cache_root:None,missing:None,expected:BTreeSet::new(),records:BTreeMap::new(),received_bytes:0,install:None,updated:None }
    }
    pub(crate) fn allow_updates(&mut self,root:&str){if !root.is_empty(){self.cache_root=Some(PathBuf::from(root));}}
    pub(crate) fn progress(&self)->Option<String>{
        if self.install.is_some(){Some("Installing and verifying server game data...".into())}
        else if self.missing.is_some(){Some(format!("Downloading server game data: {} / {} records",self.records.len(),self.expected.len()))}
        else{None}
    }
    pub(crate) fn poll(&mut self)->Result<Option<Vec<u8>>,String>{
        let Some(rx)=&self.install else{return Ok(None)};
        match rx.try_recv(){
            Ok(result)=>{self.install=None;self.updated=Some(result?);self.response=None;Ok(Some(end_ddd()))},
            Err(mpsc::TryRecvError::Empty)=>Ok(None),
            Err(_)=>Err("Game-data installation stopped unexpectedly.".into()),
        }
    }
    fn maybe_install(&mut self)->Result<(),String>{
        if self.records.len()!=self.expected.len(){return Ok(())}
        let missing=self.missing.take().ok_or("Unsolicited DAT completion")?;
        let records=std::mem::take(&mut self.records);let source=self.directory.clone();
        let root=self.cache_root.clone().ok_or("No server data folder")?;
        let (tx,rx)=mpsc::channel();self.install=Some(rx);
        std::thread::spawn(move||{let _=tx.send(dat_patch::publish(&source,&root,&missing,&records));});
        Ok(())
    }
    pub(crate) fn receive(&mut self, event: &DatEvent) -> Result<Option<Vec<u8>>, String> {
        match event {
            DatEvent::Interrogation(_) => {
                if self.response.is_none() { self.response = Some(installed_response(&self.directory)?); }
                Ok(self.response.clone())
            }
            DatEvent::BeginDdd { bytes: 0, missing } if missing.is_empty() => {
                if !self.records.is_empty() || self.missing.is_some() || self.install.is_some(){return Err("Empty DAT plan conflicts with pending updates.".into());}
                Ok(Some(end_ddd()))
            },
            DatEvent::BeginDdd { bytes, missing } => {
                if self.cache_root.is_none(){return Err(format!(
                    "This server requests {bytes} bytes of game-data updates ({}). Install its current DAT files or connect using an Icefire server profile to download them.",
                    missing.iter().take(8).map(|m|format!("{}, iteration {}: {} records, {} removals",dat_name(m.dat_file),m.iteration,m.download.len(),m.purge.len())).collect::<Vec<_>>().join("; ")))}
                if self.missing.is_some() || self.install.is_some() || self.updated.is_some() || *bytes as usize>MAX_PATCH || missing.is_empty() || missing.len()>4096 {
                    return Err("Invalid or overlapping game-data update plan.".into());
                }
                let data=dat_patch::mounted(&self.directory)?;let roles=dat_patch::roles(&data);
                let mut revisions=BTreeSet::new();
                if missing.iter().map(|m|m.download.len()+m.purge.len()).sum::<usize>()>100000{return Err("Too many DAT update changes.".into());}
                for m in missing {
                    if !roles.contains_key(&m.dat_file) || m.iteration==0 || m.iteration>i32::MAX as u32 || !revisions.insert((m.dat_file,m.iteration)) || m.download.len()+m.purge.len()>100000 {
                        return Err("Server requested an unsupported DAT revision.".into());
                    }
                    if m.download.iter().chain(&m.purge).any(|id|*id==0||*id==RETAIL_DDD_ITERATION_DID){return Err("Invalid DAT update plan record.".into());}
                    self.expected.extend(m.download.iter().map(|did|(m.dat_file,*did)));
                }
                if self.expected.len()>100000{return Err("Too many DAT update records.".into());}
                // Iterations are applied in order. Shared IDs need downloading once.
                let mut plan=missing.clone();plan.sort_by_key(|m|(m.dat_file,m.iteration));
                if self.records.keys().any(|key|!self.expected.contains(key)){return Err("Received DAT data outside the update plan.".into());}
                self.missing=Some(plan);self.maybe_install()?;Ok(None)
            }
            DatEvent::Data(body) => {
                if self.cache_root.is_none() || self.install.is_some() || self.updated.is_some(){return Err("Unsolicited game-data record.".into());}
                let record=Download::decode(body)?;let key=(record.dat,record.did);
                // Queue 5 is reliable but unordered. Native CLCache keeps records
                // that arrive ahead of BeginDDD and reconciles them with its plan.
                if self.missing.is_some() && !self.expected.contains(&key){return Err("Server sent a record outside its DAT update plan.".into());}
                if let Some(previous)=self.records.get(&key){
                    if previous!=&record.bytes{return Err("Conflicting duplicate DAT update record.".into());}
                    return Ok(None)
                }
                if self.records.len()>=100000{return Err("Too many DAT update records.".into());}
                self.received_bytes+=record.bytes.len();
                if self.received_bytes>MAX_PATCH{return Err("Game-data update exceeds the size limit.".into());}
                self.records.insert(key,record.bytes);if self.missing.is_some(){self.maybe_install()?;}Ok(None)
            }
            DatEvent::RequestFailed { .. } => Err("The server could not provide the required game data.".into()),
            DatEvent::PatchtimePending(_) | DatEvent::EndDdd => Ok(None),
        }
    }
}

fn dat_name(id: u64) -> String {
    match id {
        0x0000000100000000 => "portal.dat".into(),
        0x0000000380000001 => "local_English.dat".into(),
        _ => format!("DAT {id:016X}"),
    }
}

fn installed_response(directory: &str) -> Result<Vec<u8>, String> {
    if directory.is_empty() { return Err("The server requested game-data versions, but no DAT directory was supplied.".into()); }
    let dataset = DatDataset::open(DatOpenOptions::for_retail_install(directory))
        .map_err(|_| "Cannot open the installed game data for the server's version check.".to_owned())?;
    let mut lists = Vec::new();
    // Native CLCache slot order: portal, localized, current-region cell, high-res.
    for kind in [DatFileKind::Portal, DatFileKind::Local, DatFileKind::Cell, DatFileKind::HighRes] {
        let Some(meta) = dataset.file_meta(kind) else { continue; };
        let history = dataset.read_packed(kind, RETAIL_DDD_ITERATION_DID)
            .map_err(|_| format!("Cannot read {kind:?} DAT version history."))?
            .ok_or_else(|| format!("The {kind:?} DAT is missing its version history."))?;
        let iterations = read_int_set(&history).map_err(|_| format!("Invalid {kind:?} DAT version history."))?;
        lists.push((u64::from(meta.dataset_type) << 32 | u64::from(meta.subset), iterations));
    }
    // The client currently mounts local_English.dat; retail Language English = 0.
    Ok(interrogation_response(0, &lists))
}

#[cfg(test)]
mod tests {
    use super::*;
    use ac2_net::protocol::MissingIteration;

    #[test]
    fn only_an_empty_patch_is_acknowledged() {
        let mut state=DatLogin::new("");
        assert_eq!(state.receive(&DatEvent::BeginDdd{bytes:0,missing:vec![]}).unwrap(),Some(end_ddd()));
        assert!(state.receive(&DatEvent::BeginDdd{bytes:1,missing:vec![]}).is_err());
        assert!(state.receive(&DatEvent::BeginDdd{bytes:0,missing:vec![MissingIteration{
            dat_file:1,iteration:1,download:vec![],purge:vec![]}]}).is_err());
        assert!(state.receive(&DatEvent::Interrogation(vec![])).is_err());
        assert_eq!(state.receive(&DatEvent::EndDdd).unwrap(),None);
    }

    #[test]
    fn missing_versions_are_reported_without_acknowledging_them() {
        let mut state=DatLogin::new("");
        let error=state.receive(&DatEvent::BeginDdd{bytes:760070,missing:vec![MissingIteration{
            dat_file:0x0000000100000000,iteration:138,download:vec![],purge:vec![]},MissingIteration{
            dat_file:0x0000000380000001,iteration:11,download:vec![],purge:vec![]}]}).unwrap_err();
        assert!(error.contains("760070 bytes"));
        assert!(error.contains("portal.dat, iteration 138"));
        assert!(error.contains("local_English.dat, iteration 11"));
        assert!(error.contains("Install its current DAT files"));
    }

    fn raw_record(did:u32)->DatEvent{
        let mut bytes=[0u32,1,0x40000021,did,8,0].into_iter().flat_map(u32::to_le_bytes).collect::<Vec<_>>();
        bytes.extend_from_slice(&[1,2,3,4]);DatEvent::Data(bytes)
    }
    #[test]
    fn unordered_records_are_installed_before_any_completion_ack(){
        let source=crate::dat_patch::tests::directory();crate::dat_patch::tests::fixture(&source,4);
        let root=crate::dat_patch::tests::directory();let mut state=DatLogin::new(source.to_str().unwrap());state.allow_updates(root.to_str().unwrap());
        assert_eq!(state.receive(&raw_record(0x47000001)).unwrap(),None);
        assert_eq!(state.receive(&DatEvent::BeginDdd{bytes:8,missing:vec![MissingIteration{dat_file:1<<32,iteration:4,download:vec![0x47000001,0x47000002],purge:vec![0x47000003]}]}).unwrap(),None);
        assert_eq!(state.poll().unwrap(),None);assert!(state.updated.is_none());
        assert_eq!(state.receive(&raw_record(0x47000002)).unwrap(),None);
        let start=std::time::Instant::now();
        loop {if let Some(ack)=state.poll().unwrap(){assert_eq!(ack,end_ddd());break}assert!(start.elapsed().as_secs()<5);std::thread::sleep(std::time::Duration::from_millis(5));}
        let output=state.updated.as_ref().unwrap();let data=crate::dat_patch::mounted(output.to_str().unwrap()).unwrap();
        assert_eq!(data.read_packed(DatFileKind::Portal,0x47000001).unwrap().unwrap(),[1,2,3,4]);assert!(!data.contains(DatFileKind::Portal,0x47000003));
        drop(data);std::fs::remove_dir_all(source).unwrap();std::fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn failure_and_unannounced_records_never_advance_history(){
        let source=crate::dat_patch::tests::directory();crate::dat_patch::tests::fixture(&source,4);
        let before=std::fs::read(source.join("portal.dat")).unwrap();let root=crate::dat_patch::tests::directory();
        let blocked=root.join("file-not-directory");std::fs::write(&blocked,b"occupied").unwrap();
        let mut state=DatLogin::new(source.to_str().unwrap());state.allow_updates(blocked.to_str().unwrap());
        assert_eq!(state.receive(&DatEvent::BeginDdd{bytes:0,missing:vec![MissingIteration{dat_file:1<<32,iteration:4,download:vec![],purge:vec![]}]}).unwrap(),None);
        let start=std::time::Instant::now();loop {
            match state.poll(){Err(_)=>break,Ok(None)=>{},Ok(Some(_))=>panic!("acknowledged failed disk write")}
            assert!(start.elapsed().as_secs()<5);std::thread::sleep(std::time::Duration::from_millis(5));
        }
        assert!(state.updated.is_none());assert_eq!(before,std::fs::read(source.join("portal.dat")).unwrap());
        let mut state=DatLogin::new(source.to_str().unwrap());state.allow_updates(root.to_str().unwrap());
        state.receive(&raw_record(0x47009999)).unwrap();
        assert!(state.receive(&DatEvent::BeginDdd{bytes:8,missing:vec![MissingIteration{dat_file:1<<32,iteration:4,download:vec![0x47000001],purge:vec![]}]}).is_err());
        assert!(state.updated.is_none());std::fs::remove_dir_all(source).unwrap();std::fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn mounted_histories_are_repeatable_and_leave_files_unchanged() {
        let Ok(directory)=std::env::var("AC2_DAT_DIR") else {return;};
        let mut state=DatLogin::new(&directory);
        let options=DatOpenOptions::for_retail_install(&directory);
        let files=[DatFileKind::Portal,DatFileKind::Local,DatFileKind::Cell,DatFileKind::HighRes]
            .into_iter().map(|kind| options.path_for(kind)).filter(|p|p.is_file()).collect::<Vec<_>>();
        let before=files.iter().map(|p|std::fs::metadata(p).unwrap().modified().unwrap()).collect::<Vec<_>>();
        let first=state.receive(&DatEvent::Interrogation(vec![])).unwrap().unwrap();
        assert_eq!(&first[..8], &[0x38,0,1,0,0,0,0,0]);
        assert_eq!(u32::from_le_bytes(first[8..12].try_into().unwrap()) as usize,files.len());
        assert_eq!(state.receive(&DatEvent::Interrogation(vec![])).unwrap(),Some(first));
        assert_eq!(before,files.iter().map(|p|std::fs::metadata(p).unwrap().modified().unwrap()).collect::<Vec<_>>());
    }
}
