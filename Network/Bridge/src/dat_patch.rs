// SPDX-License-Identifier: AGPL-3.0-only
//! Publish a new AC2 DAT snapshot; never edit an archive mounted by a client.
//! AC2 DiskController uses 16-byte BTEntry records and linked fixed-size blocks.
use ac2_dat::{DatDataset, DatEntryMeta, DatFileKind, DatOpenOptions, RETAIL_DDD_ITERATION_DID};
use ac2_net::protocol::{interrogation_response, read_int_set, MissingIteration};
use std::{collections::{BTreeMap, BTreeSet}, fs::{self, File, OpenOptions}, io::{Read, Seek, SeekFrom, Write}, path::{Path, PathBuf}, time::{SystemTime, UNIX_EPOCH}};

pub(crate) const MAX_PATCH: usize = 128 * 1024 * 1024;
const MAX_RECORD: usize = 32 * 1024 * 1024;

#[derive(Debug)]
pub(crate) struct Download {
    pub dat: u64,
    pub did: u32,
    pub bytes: Vec<u8>,
}
impl Download {
    /// CCliDatDataEvent::CreateCachePack: DAT id, QualifiedDataID, stored size,
    /// uncompressed size (zero for raw), then zlib/raw bytes. The stored size
    /// includes the four-byte compression marker in both cases.
    pub fn decode(body: &[u8]) -> Result<Self, String> {
        if body.len() < 24 { return Err("Truncated DAT update header.".into()); }
        let word=|p|u32::from_le_bytes(body[p..p+4].try_into().unwrap());
        let stored=word(16) as usize; let expanded=word(20) as usize;
        if stored>MAX_RECORD || expanded>MAX_RECORD {return Err("DAT update record exceeds the size limit.".into());}
        let raw=&body[24..];
        let bytes=if expanded==0 {
            if stored.checked_sub(4)!=Some(raw.len()) {return Err("Invalid raw DAT update size.".into());}
            raw.to_vec()
        } else {
            if stored.checked_sub(4)!=Some(raw.len()) {return Err("Invalid compressed DAT update size.".into());}
            let mut out=vec![0;expanded];
            let mut decoder=flate2::Decompress::new(true);
            let status=decoder.decompress(raw,&mut out,flate2::FlushDecompress::Finish)
                .map_err(|_|"Invalid compressed DAT update.".to_string())?;
            if status!=flate2::Status::StreamEnd || decoder.total_out()!=expanded as u64 || decoder.total_in()!=raw.len() as u64 {
                return Err("Incomplete compressed DAT update.".into());
            }
            out
        };
        if bytes.is_empty() || word(12)==0 || word(12)==RETAIL_DDD_ITERATION_DID {
            return Err("Invalid DAT update record id or payload.".into());
        }
        Ok(Self{dat:u64::from_le_bytes(body[..8].try_into().unwrap()),did:word(12),bytes})
    }
}

pub(crate) fn mounted(directory:&str)->Result<std::sync::Arc<DatDataset>,String>{
    DatDataset::open(DatOpenOptions::for_retail_install(directory)).map_err(|e|format!("Cannot open game data: {e}"))
}
pub(crate) fn roles(data:&DatDataset)->BTreeMap<u64,DatFileKind>{
    DatFileKind::ALL.into_iter().filter_map(|k|data.file_meta(k).map(|m|((u64::from(m.dataset_type)<<32)|u64::from(m.subset),k))).collect()
}
pub(crate) fn publish(source:&str, root:&Path, missing:&[MissingIteration], records:&BTreeMap<(u64,u32),Vec<u8>>)->Result<PathBuf,String>{
    let data=mounted(source)?;let kinds=roles(&data);
    fs::create_dir_all(root).map_err(|e|format!("Cannot create server data folder: {e}"))?;
    let nonce=SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e|e.to_string())?.as_nanos();
    let staging=root.join(format!("pending-{}-{nonce}",std::process::id()));
    let destination=root.join(format!("revision-{nonce}"));
    fs::create_dir(&staging).map_err(|e|e.to_string())?;
    // A failed snapshot remains unselected. Its path is always freshly created
    // underneath our cache root, never a path supplied in a server packet.
    let result=(||->Result<(),String>{
        for kind in DatFileKind::ALL {
            if data.file_meta(kind).is_some(){
                fs::copy(Path::new(source).join(kind.default_file_name()),staging.join(kind.default_file_name()))
                    .map_err(|e|format!("Cannot copy {} for update: {e}",kind.default_file_name()))?;
            }
        }
        // Keep optional additional region files with this installation as well.
        let cell2=Path::new(source).join("cell_2.dat");
        if cell2.is_file(){fs::copy(cell2,staging.join("cell_2.dat")).map_err(|e|e.to_string())?;}
        for (&id,&kind) in &kinds {
            let patches=missing.iter().filter(|m|m.dat_file==id).collect::<Vec<_>>();
            if patches.is_empty(){continue;}
            let mut entries=data.iter_entries(kind).map(|e|(e.did,e)).collect::<BTreeMap<_,_>>();
            let history=data.read_packed(kind,RETAIL_DDD_ITERATION_DID).map_err(|e|e.to_string())?.ok_or("Missing DAT history")?;
            let mut iterations=read_int_set(&history).map_err(|e|e.to_string())?;
            let mut writer=ArchiveWriter::open(&staging.join(kind.default_file_name())).map_err(|e|e.to_string())?;
            let mut replacements=BTreeMap::new();
            for m in patches {
                for did in &m.purge {entries.remove(did);replacements.remove(did);}
                for did in &m.download {
                    replacements.insert(*did,records.get(&(id,*did)).ok_or("Missing downloaded DAT record")?.clone());
                }
                iterations.push(m.iteration as i32);
            }
            // Native iteration history uses the same mostly-consecutive set codec.
            let packed=interrogation_response(0,&[(id,iterations)]);
            replacements.insert(RETAIL_DDD_ITERATION_DID,packed[20..].to_vec());
            for (&did,bytes) in &replacements {
                let offset=writer.append(bytes).map_err(|e|e.to_string())?;
                entries.insert(did,DatEntryMeta{did,file_kind:kind,file_offset:offset as u64,packed_len:bytes.len() as u32,flags:0,version:0,date_stamp:0,iteration:0});
            }
            writer.finish(&entries.values().copied().collect::<Vec<_>>()).map_err(|e|e.to_string())?;
            drop(writer);
            // Reopen using the production reader; validate the entire index plus
            // every replacement and removal before publishing its history.
            let check=mounted(staging.to_str().ok_or("Invalid snapshot path")?)?;
            let actual=check.iter_entries(kind).map(|e|e.did).collect::<BTreeSet<_>>();
            if actual!=entries.keys().copied().collect(){return Err("Updated DAT index verification failed.".into());}
            for (did,bytes) in replacements {
                if check.read_packed(kind,did).map_err(|e|e.to_string())?.as_deref()!=Some(bytes.as_slice()) {
                    return Err(format!("Updated DAT record {did:08X} failed verification."));
                }
            }
        }
        fs::rename(&staging,&destination).map_err(|e|format!("Cannot publish updated game data: {e}"))?;
        Ok(())
    })();
    if result.is_err(){let _=fs::remove_dir_all(&staging);}
    result.map(|_|destination)
}

struct ArchiveWriter {file:File,block:usize,end:u64}
impl ArchiveWriter {
    fn open(path:&Path)->std::io::Result<Self>{
        let mut file=OpenOptions::new().read(true).write(true).open(path)?;
        let mut header=[0;400];file.read_exact(&mut header)?;
        let word=|p|u32::from_le_bytes(header[p..p+4].try_into().unwrap());
        let block=word(304) as usize;let end=file.metadata()?.len();
        if word(300)!=0x5442 || !(512..=65536).contains(&block) || !block.is_power_of_two() || end%block as u64!=0 || end>i32::MAX as u64 {
            return Err(std::io::Error::other("Unsupported AC2 DAT container layout."));
        }
        Ok(Self{file,block,end})
    }
    fn append(&mut self,bytes:&[u8])->std::io::Result<u32>{
        let start=self.end;let chunks=bytes.chunks(self.block-4);let count=chunks.len();
        if start+(count*self.block) as u64>i32::MAX as u64{return Err(std::io::Error::other("Updated DAT exceeds its address range."));}
        self.file.seek(SeekFrom::Start(start))?;
        for (i,chunk) in chunks.enumerate(){
            let mut block=vec![0;self.block];
            let next=if i+1<count{(self.end+self.block as u64) as u32}else{0};
            block[..4].copy_from_slice(&next.to_le_bytes());block[4..4+chunk.len()].copy_from_slice(chunk);
            self.file.write_all(&block)?;self.end+=self.block as u64;
        }
        Ok(start as u32)
    }
    fn node(&mut self,entries:&[DatEntryMeta])->std::io::Result<u32>{
        let mut bytes=vec![0;1228];let mut keys=Vec::new();
        if entries.len()<=61 {keys.extend_from_slice(entries);}else{
            let mut capacity=61usize;
            while entries.len()>capacity*62+61 {capacity=capacity*62+61;}
            let children=(entries.len()+1).div_ceil(capacity+1);
            let remaining=entries.len()-(children-1);let each=remaining/children;let extra=remaining%children;
            let mut at=0;
            for i in 0..children {
                let n=each+usize::from(i<extra);let child=self.node(&entries[at..at+n])?;
                bytes[i*4..i*4+4].copy_from_slice(&child.to_le_bytes());at+=n;
                if i+1<children {keys.push(entries[at]);at+=1;}
            }
        }
        bytes[248..252].copy_from_slice(&(keys.len() as u32).to_le_bytes());
        for (i,e) in keys.iter().enumerate(){
            let p=252+i*16;
            for (j,w) in [e.did,e.file_offset as u32,e.packed_len,e.date_stamp].iter().enumerate(){bytes[p+j*4..p+j*4+4].copy_from_slice(&w.to_le_bytes());}
        }
        self.append(&bytes)
    }
    fn finish(&mut self,entries:&[DatEntryMeta])->std::io::Result<()>{
        let root=self.node(entries)?;
        self.file.seek(SeekFrom::Start(308))?;self.file.write_all(&(self.end as u32).to_le_bytes())?;
        self.file.seek(SeekFrom::Start(332))?;self.file.write_all(&root.to_le_bytes())?;
        self.file.sync_all()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    pub(crate) fn directory()->PathBuf{
        static SERIAL:std::sync::atomic::AtomicUsize=std::sync::atomic::AtomicUsize::new(0);
        let p=std::env::temp_dir().join(format!("ac2-dat-patch-{}-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos(),SERIAL.fetch_add(1,std::sync::atomic::Ordering::Relaxed)));
        fs::create_dir(&p).unwrap();p
    }
    pub(crate) fn fixture(dir:&Path,n:usize){
        for (kind,id) in [(DatFileKind::Portal,1u64<<32),(DatFileKind::Cell,2u64<<32|1),(DatFileKind::Local,3u64<<32|0x80000001),(DatFileKind::HighRes,1u64<<32|1)] {
            let path=dir.join(kind.default_file_name());let mut h=vec![0u8;1024];
            for (p,w) in [(300,0x5442u32),(304,1024),(308,1024),(312,(id>>32) as u32),(316,id as u32)]{h[p..p+4].copy_from_slice(&w.to_le_bytes());}
            fs::write(&path,h).unwrap();let mut w=ArchiveWriter::open(&path).unwrap();let mut es=Vec::new();
            for i in 0..n {
                let did=0x47000000+i as u32;let b=vec![(i%251) as u8;1300];let offset=w.append(&b).unwrap();
                es.push(DatEntryMeta{did,file_kind:kind,file_offset:offset as u64,packed_len:b.len() as u32,flags:0,version:0,date_stamp:42,iteration:0});
            }
            let b=interrogation_response(0,&[(id,vec![1,2,3])]);let offset=w.append(&b[20..]).unwrap();
            es.push(DatEntryMeta{did:RETAIL_DDD_ITERATION_DID,file_kind:kind,file_offset:offset as u64,packed_len:(b.len()-20) as u32,flags:0,version:0,date_stamp:0,iteration:0});
            w.finish(&es).unwrap();
        }
    }
    #[test]
    fn native_raw_and_zlib_records_validate_lengths(){
        let payload=vec![91;4200];
        for compressed in [false,true]{
            let raw=if compressed {let mut e=flate2::write::ZlibEncoder::new(Vec::new(),flate2::Compression::default());e.write_all(&payload).unwrap();e.finish().unwrap()}else{payload.clone()};
            let words=[0,1,0x40000021,0x1f000001,raw.len() as u32+4,if compressed{payload.len() as u32}else{0}];
            let mut body=words.into_iter().flat_map(u32::to_le_bytes).collect::<Vec<_>>();body.extend_from_slice(&raw);
            let record=Download::decode(&body).unwrap();assert_eq!(record.dat,1<<32);assert_eq!(record.did,0x1f000001);assert_eq!(record.bytes,payload);
            body.pop();assert!(Download::decode(&body).is_err());
        }
        assert!(Download::decode(&[0;12]).is_err());
    }
    #[test]
    fn snapshot_updates_adds_purges_and_keeps_source_and_untouched_data(){
        let source=directory();fixture(&source,124);let root=directory();
        let before=fs::read(source.join("portal.dat")).unwrap();let local=fs::read(source.join("local_English.dat")).unwrap();
        let id=1u64<<32;
        let plan=vec![MissingIteration{dat_file:id,iteration:4,download:vec![0x47000002,0x47001000],purge:vec![0x47000003]},MissingIteration{dat_file:id,iteration:5,download:vec![],purge:vec![]}];
        let records=[((id,0x47000002),vec![7;2200]),((id,0x47001000),vec![9;17])].into_iter().collect();
        let updated=publish(source.to_str().unwrap(),&root,&plan,&records).unwrap();
        let data=mounted(updated.to_str().unwrap()).unwrap();assert!(!data.contains(DatFileKind::Portal,0x47000003));
        assert_eq!(data.read_packed(DatFileKind::Portal,0x47000002).unwrap().unwrap(),vec![7;2200]);
        assert_eq!(data.read_packed(DatFileKind::Portal,0x47000005).unwrap().unwrap(),vec![5;1300]);
        assert_eq!(read_int_set(&data.read_packed(DatFileKind::Portal,RETAIL_DDD_ITERATION_DID).unwrap().unwrap()).unwrap(),[1,2,3,4,5]);
        assert_eq!(before,fs::read(source.join("portal.dat")).unwrap());assert_eq!(local,fs::read(updated.join("local_English.dat")).unwrap());
        drop(data);fs::remove_dir_all(source).unwrap();fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn rebuilt_btree_crosses_height_boundary_without_losing_entries(){
        let source=directory();fixture(&source,3844);let data=mounted(source.to_str().unwrap()).unwrap();
        for kind in DatFileKind::ALL {
            assert_eq!(data.iter_entries(kind).count(),3845);
            for i in [0,60,61,62,1919,1920,3842,3843]{assert_eq!(data.read_packed(kind,0x47000000+i).unwrap().unwrap(),vec![(i%251) as u8;1300]);}
        }
        drop(data);fs::remove_dir_all(source).unwrap();
    }
}
