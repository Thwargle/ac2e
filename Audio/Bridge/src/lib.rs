//! Narrow, game-thread-only ABI over the pinned AC2_Rusted SoundManager.
//! The native DAT loader runs on its own thread; Unreal pulls mixed PCM for its audio device.
use ac2_audio::{
    AmbientData, AmbientSound, DatSounds, Emitter, PlaySoundData, SoundManager, SoundPreference,
    MixingSink, Channel,
};
use std::{
    collections::HashMap,
    ffi::{c_char, CStr},
    path::PathBuf,
    sync::Arc,
};

pub struct Audio {
    manager: SoundManager,
    output: MixingSink,
    emitters: HashMap<u64, Emitter>,
    ambient: AmbientSound,
    now: f64,
}

#[repr(C)]
pub struct Source {
    pub id: u64,
    pub x: f32,
    pub y: f32,
    pub z: f32,
}
#[repr(C)]
pub struct AmbientPoint {
    pub kind: u32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub outside: u32,
}

// Only the owning Unreal game thread calls this ABI. Opaque pointers are never shared with
// the render/audio thread. Catch panics before crossing the ABI (including DAT decoder errors).
fn guard<T: Default>(f: impl FnOnce() -> T) -> T {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).unwrap_or_default()
}

#[no_mangle]
pub extern "C" fn ac2_audio_version() -> u32 {
    2
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_create(
    directory: *const c_char,
    gm: *const c_char,
) -> *mut Audio {
    guard(|| {
        if directory.is_null() {
            return std::ptr::null_mut();
        }
        let Ok(directory) = CStr::from_ptr(directory).to_str() else {
            return std::ptr::null_mut();
        };
        let Ok(dataset) = ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(directory)) else {
            return std::ptr::null_mut();
        };
        // Unreal owns the output device (including default-device changes).
        // Keep native DAT mixing/attenuation, and pull stereo PCM through the
        // ABI instead of opening a second, permanently pinned WASAPI stream.
        let output = MixingSink::new(44100);
        let store = Arc::new(ac2_db::ResourceStore::new(dataset));
        // Small region metadata is read once at startup. Wave/music data stays on
        // SoundManager's loader thread; terrain points come from Unreal's block worker.
        let mut ambient = AmbientSound::default();
        ambient.set_data(AmbientData::load(&store, 0x10000000).ok().flatten());
        let mut manager = SoundManager::new(Box::new(output.clone()), Arc::new(DatSounds::new(store)));
        if !gm.is_null() {
            if let Ok(path) = CStr::from_ptr(gm).to_str() {
                let path = PathBuf::from(path);
                if path.is_file() {
                    manager.load_gm(path);
                }
            }
        }
        Box::into_raw(Box::new(Audio {
            manager,
            output,
            emitters: HashMap::new(),
            ambient,
            now: 0.0,
        }))
    })
}

/// Interleaved signed 16-bit stereo at 44100 Hz. Called by the owning game
/// thread, never concurrently with tick/destroy; capped to a quarter second.
#[no_mangle]
pub unsafe extern "C" fn ac2_audio_render(audio: *mut Audio, samples: *mut i16, frames: u32) -> u32 {
    guard(|| {
        let Some(audio)=audio.as_mut() else {return 0};
        if samples.is_null() || frames==0 || frames>11025 {return 0;}
        let pcm=audio.output.render(frames as usize,2);
        let out=std::slice::from_raw_parts_mut(samples,frames as usize*2);
        for (dst,src) in out.iter_mut().zip(pcm) {
            *dst=(src.clamp(-1.0,1.0)*32767.0).round() as i16;
        }
        frames
    })
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_destroy(audio: *mut Audio) {
    guard(|| {
        if !audio.is_null() {
            drop(Box::from_raw(audio));
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_play(
    audio: *mut Audio,
    sound: u32,
    emitter: u64,
    volume: f32,
) -> u32 {
    ac2_audio_play_on_channel(audio, sound, emitter, volume, 0)
}

/// Additive ABI 2 entry point. The caller selects the retail channel: UI sound
/// defaults to Main; FXManager routes MusicInfo by its default-channel bit.
#[no_mangle]
pub unsafe extern "C" fn ac2_audio_play_on_channel(
    audio: *mut Audio,
    sound: u32,
    emitter: u64,
    volume: f32,
    channel: u32,
) -> u32 {
    guard(|| {
        let Some(audio) = audio.as_mut() else {
            return 0;
        };
        if !volume.is_finite() || !matches!(sound >> 24, 0x2a | 0x2c) {
            return 0;
        }
        let mut data = PlaySoundData::new(sound);
        data.channel = match channel {
            0 => Channel::Main,
            1 => Channel::Music,
            2 => Channel::Player,
            3 => Channel::Ambient,
            _ => return 0,
        };
        data.volume = volume.clamp(0.0, 1.0);
        data.emitter = (emitter != 0).then_some(emitter);
        audio.manager.play(&data).unwrap_or(0)
    })
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_context_volume(audio: *mut Audio, context: u32, volume: f32) {
    guard(|| {
        if let Some(audio) = audio.as_mut() {
            if context != 0 && volume.is_finite() {
                audio.manager.set_context_volume(context, volume.clamp(0.0, 1.0));
            }
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_stop(audio: *mut Audio, context: u32, fade: f32) {
    guard(|| {
        if let Some(audio) = audio.as_mut() {
            audio.manager.stop_context(
                context,
                if fade.is_finite() {
                    fade.clamp(0.0, 5.0)
                } else {
                    0.0
                },
            );
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_finished(audio: *mut Audio, context: u32) -> u32 {
    guard(|| {
        audio
            .as_ref()
            .is_none_or(|a| a.manager.has_sound_finished(context)) as u32
    })
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_tick(
    audio: *mut Audio,
    now: f64,
    sources: *const Source,
    count: u32,
    axes: *const f32,
) {
    guard(|| {
        let Some(audio) = audio.as_mut() else {
            return;
        };
        // Streaming includes authored scenery as well as actor components. A
        // large loaded neighborhood must not freeze the entire mixer at 4097.
        if !now.is_finite() || count > 65536 || (count != 0 && sources.is_null()) || axes.is_null() {
            return;
        }
        let axes = std::slice::from_raw_parts(axes, 9);
        if !axes.iter().all(|v| v.is_finite()) {
            return;
        }
        audio.now = now;
        audio.ambient.use_time(now, &mut audio.manager);
        audio.emitters.clear();
        if count != 0 {
            for s in std::slice::from_raw_parts(sources, count as usize) {
                if [s.x, s.y, s.z].iter().all(|v| v.is_finite()) {
                    audio.emitters.insert(
                        s.id,
                        Emitter {
                            offset: [s.x, s.y, s.z],
                            velocity: [0.0; 3],
                            muted: false,
                        },
                    );
                }
            }
        }
        for (id, offset) in audio.ambient.emitters() {
            let mut local = [0.0; 3];
            for i in 0..3 {
                local[i] = offset[0] * axes[i * 3]
                    + offset[1] * axes[i * 3 + 1]
                    + offset[2] * axes[i * 3 + 2];
            }
            audio.emitters.insert(
                id,
                Emitter {
                    offset: local,
                    velocity: [0.0; 3],
                    muted: false,
                },
            );
        }
        let emitters = &audio.emitters;
        audio.manager.use_time_with(now, &|id| {
            Some(emitters.get(&id).copied().unwrap_or(Emitter {
                offset: [0.0; 3],
                velocity: [0.0; 3],
                muted: true,
            }))
        });
    });
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_environment(
    audio: *mut Audio,
    points: *const AmbientPoint,
    count: u32,
    outside: u32,
) {
    guard(|| {
        let Some(audio) = audio.as_mut() else {
            return;
        };
        if count > 4096 || (count != 0 && points.is_null()) {
            return;
        }
        audio.ambient.change_cell_start();
        // An empty environment still uses the native fade-out/retirement path.
        // A null pointer is legal for an empty C array, but not for a Rust slice.
        let points = if count == 0 { &[] } else { std::slice::from_raw_parts(points, count as usize) };
        for p in points {
            if [p.x, p.y, p.z].iter().all(|v| v.is_finite()) {
                audio
                    .ambient
                    .add_cell_sound(p.kind, [p.x, p.y, p.z], p.outside != 0, outside != 0);
            }
        }
        audio.ambient.change_cell_end(audio.now, &mut audio.manager);
    });
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_groove(audio: *mut Audio, level: i32) {
    guard(|| {
        if let Some(audio) = audio.as_mut() {
            audio.manager.set_groove_level(level);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn ac2_audio_volume(audio: *mut Audio, channel: u32, value: f32) {
    guard(|| {
        if let Some(audio) = audio.as_mut() {
            if value.is_finite() && channel < 4 {
                let v = value.clamp(0.0, 1.0);
                audio.manager.set_preference(match channel {
                    0 => SoundPreference::SoundVolume(v),
                    1 => SoundPreference::MusicVolume(v),
                    2 => SoundPreference::PlayerMusic(v),
                    _ => SoundPreference::AmbientVolume(v),
                });
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use ac2_audio::{LoadedSound, MixingSink, SoundSource};
    use std::time::Duration;
    #[test]
    fn pcm_abi_renders_native_sound_and_checks_buffer_limits() {
        let Ok(dir)=std::env::var("AC2_DAT_DIR") else {return};
        let dir=std::ffi::CString::new(dir).unwrap();
        unsafe {
            let engine=ac2_audio_create(dir.as_ptr(),std::ptr::null());
            assert!(!engine.is_null());
            assert_eq!(ac2_audio_version(),2);
            let context=ac2_audio_play(engine,0x2a0000ee,0,1.0);
            assert_ne!(context,0);
            assert!((*engine).manager.wait_for_loads(Duration::from_secs(30)));
            let mut pcm=vec![0i16;882+2];
            pcm[882]=1234;pcm[883]=-1234;
            let axes=[1.,0.,0.,0.,1.,0.,0.,0.,1.];
            let scenery: Vec<_>=(0..8192).map(|id| Source {id:(1<<62)|id,x:0.,y:0.,z:1.}).collect();
            ac2_audio_tick(engine,0.125,scenery.as_ptr(),scenery.len() as u32,axes.as_ptr());
            assert_eq!((*engine).now,0.125,"A loaded scenery neighborhood still advances the mixer");
            assert!(scenery.iter().all(|s|(*engine).emitters.contains_key(&s.id)));
            let mut energy=0u64;
            for n in 0..200 {
                ac2_audio_tick(engine,0.125+n as f64/100.,std::ptr::null(),0,axes.as_ptr());
                assert_eq!(ac2_audio_render(engine,pcm.as_mut_ptr(),441),441);
                energy+=pcm[..882].iter().map(|x|i64::from(*x).pow(2) as u64).sum::<u64>();
            }
            assert!(energy>0,"Native pickup reaches the Unreal PCM boundary");
            assert_eq!(&pcm[882..],&[1234,-1234],"No writes past the requested frames");
            assert_eq!(ac2_audio_render(engine,pcm.as_mut_ptr(),11026),0);
            assert_eq!(ac2_audio_render(engine,std::ptr::null_mut(),441),0);
            assert_eq!(ac2_audio_render(std::ptr::null_mut(),pcm.as_mut_ptr(),441),0);
            ac2_audio_destroy(engine);
        }
    }
    #[test]
    fn music_and_player_music_obey_independent_volume_controls() {
        let Ok(dir) = std::env::var("AC2_DAT_DIR") else { return };
        let directory=std::ffi::CString::new(dir).unwrap();
        let axes=[1.,0.,0.,0.,1.,0.,0.,0.,1.];
        // A looping native EncodedWave MusicInfo avoids synthesizer/instrument
        // ambiguity. The same audio routed onto each channel must mute/unmute
        // independently of the main Sound volume and the other music channel.
        unsafe { for channel in [1,2] {
            let engine=ac2_audio_create(directory.as_ptr(),std::ptr::null());
            assert!(!engine.is_null());
            for c in 0..4 {ac2_audio_volume(engine,c,0.0);}
            assert_eq!(ac2_audio_play_on_channel(engine,0x2c00044a,0,1.,4),0);
            let handle=ac2_audio_play_on_channel(engine,0x2c00044a,0,1.,channel);
            assert_ne!(handle,0);
            assert!((*engine).manager.wait_for_loads(Duration::from_secs(30)));
            let mut pcm=[0i16;882];
            let mut energy=[0u64;3];
            for phase in 0..3 {
                // Enabling the other music slider must not unmute this cue.
                if phase==1 {ac2_audio_volume(engine,3-channel,1.0);}
                if phase==2 {ac2_audio_volume(engine,channel,1.0);}
                for frame in 0..200 {
                    ac2_audio_tick(engine,(phase*200+frame) as f64/100.,std::ptr::null(),0,axes.as_ptr());
                    assert_eq!(ac2_audio_render(engine,pcm.as_mut_ptr(),441),441);
                    energy[phase]+=pcm.iter().map(|x|i64::from(*x).pow(2) as u64).sum::<u64>();
                }
            }
            assert_eq!(energy[0],0,"initially muted channel {channel}");
            assert_eq!(energy[1],0,"the other slider does not control channel {channel}");
            assert!(energy[2]>0,"the correct slider restores channel {channel}");
            ac2_audio_destroy(engine);
        }}
    }
    #[test]
    fn context_volume_changes_a_playing_loop_without_restarting_it() {
        let Ok(dir) = std::env::var("AC2_DAT_DIR") else { return };
        let dir=std::ffi::CString::new(dir).unwrap();
        unsafe {
            let engine=ac2_audio_create(dir.as_ptr(),std::ptr::null());
            assert!(!engine.is_null());
            let handle=ac2_audio_play(engine,0x2a000318,0,0.);
            assert_ne!(handle,0);
            assert!((*engine).manager.wait_for_loads(Duration::from_secs(30)));
            let mut pcm=[0i16;882];let mut energy=[0u64;3];
            let axes=[1.,0.,0.,0.,1.,0.,0.,0.,1.];
            for phase in 0..3 {
                if phase>0 {ac2_audio_context_volume(engine,handle,if phase==1 {1.} else {0.});}
                // Non-finite requests must not corrupt the active voice.
                ac2_audio_context_volume(engine,handle,f32::NAN);
                for frame in 0..100 {
                    ac2_audio_tick(engine,(phase*100+frame) as f64*0.01,std::ptr::null(),0,axes.as_ptr());
                    ac2_audio_render(engine,pcm.as_mut_ptr(),441);
                    energy[phase]+=pcm.iter().map(|x|i64::from(*x).pow(2) as u64).sum::<u64>();
                }
                assert!(!(*engine).manager.has_sound_finished(handle));
            }
            assert_eq!(energy[0],0);assert!(energy[1]>0);assert_eq!(energy[2],0);
            ac2_audio_stop(engine,handle,0.);ac2_audio_destroy(engine);
        }
    }
    #[test]
    fn native_portal_loops_stop_and_exit_is_a_one_shot() {
        let Ok(dir) = std::env::var("AC2_DAT_DIR") else { return };
        let dataset = ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(dir)).unwrap();
        let source = Arc::new(DatSounds::new(Arc::new(ac2_db::ResourceStore::new(dataset))));
        let sink = MixingSink::new(44100);
        let mut manager = SoundManager::new(Box::new(sink.clone()), source.clone());
        // FileArray 40000002 roles are independently asserted by the UE DAT-map test.
        for (id, looping) in [(0x2a000316,false),(0x2a000317,false),(0x2a000318,true),(0x2a000319,true)] {
            let LoadedSound::Sound { info, .. } = source.load(id).unwrap() else { panic!("expected DBWave") };
            assert_eq!(info.test(1),looping,"portal sound role {id:08x}");
        }
        let mut clock = 0.0;
        for _ in 0..3 {
            let enter = manager.play(&PlaySoundData::new(0x2a000316)).unwrap();
            let loops: Vec<_> = [0x2a000318,0x2a000319].into_iter()
                .map(|id| manager.play(&PlaySoundData::new(id)).unwrap()).collect();
            assert!(manager.wait_for_loads(Duration::from_secs(30)));
            let mut energy = 0.0f64;
            for _ in 0..200 {
                manager.use_time(clock);clock+=0.01;
                energy+=sink.render(441,2).iter().map(|x|(*x as f64).powi(2)).sum::<f64>();
            }
            assert!(energy>0.001,"tunnel loops decode and play");
            for handle in loops.iter().chain(std::iter::once(&enter)){manager.stop_context(*handle,0.0);}
            let exit=manager.play(&PlaySoundData::new(0x2a000317)).unwrap();
            assert!(manager.wait_for_loads(Duration::from_secs(30)));
            for _ in 0..1000 {manager.use_time(clock);clock+=0.01;sink.render(441,2);}
            assert!(loops.iter().all(|h|manager.has_sound_finished(*h)));
            assert!(manager.has_sound_finished(exit),"exit must finish naturally");
            assert!(sink.render(4410,2).iter().all(|v|v.abs()<1e-6),"no portal noise after arrival");
        }
    }
    #[test]
    fn native_environment_starts_music_and_clears_on_exit() {
        let Ok(dir) = std::env::var("AC2_DAT_DIR") else {
            return;
        };
        let dataset = ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(dir)).unwrap();
        let store = Arc::new(ac2_db::ResourceStore::new(dataset));
        let data = AmbientData::load(&store, 0x10000000)
            .unwrap()
            .expect("region sound data");
        let info = data
            .desc
            .sounds
            .iter()
            // Human environment: an audible scripted cue at danger groove 50.
            // Many outdoor/interior profiles
            // deliberately reference Cue_Blank_C_Dorian, which is silent at groove 0.
            .find(|s| s.sound_type == 0x8100000f)
            .unwrap();
        let kind = info.sound_type;
        let source = Arc::new(DatSounds::new(store));
        let sink = MixingSink::new(44100);
        let mut manager = SoundManager::new(Box::new(sink.clone()), source);
        manager.set_groove_level(50);
        if let Ok(gm) = std::env::var("AC2_GM_DLS") {
            manager.load_gm(gm.into());
            assert!(manager.wait_for_gm(Duration::from_secs(30)));
        }
        let mut ambient = AmbientSound::default();
        ambient.set_data(Some(data));
        ambient.change_cell_start();
        assert!(ambient.add_cell_sound(kind, [0.0; 3], true, true));
        ambient.change_cell_end(0.0, &mut manager);
        assert!(manager.wait_for_loads(Duration::from_secs(30)));
        let mut energy = 0.0f64;
        for frame in 0..6000 {
            let now = frame as f64 / 100.0;
            ambient.use_time(now, &mut manager);
            let emitters: HashMap<_, _> = ambient.emitters().into_iter().collect();
            manager.use_time_with(now, &|id| {
                emitters.get(&id).map(|offset| Emitter {
                    offset: [offset[0], offset[2], offset[1]],
                    ..Emitter::default()
                })
            });
            energy += sink
                .render(441, 2)
                .iter()
                .map(|v| (*v as f64).powi(2))
                .sum::<f64>();
            if energy > 0.001 {
                break;
            }
        }
        assert!(energy > 0.001, "silent ambient type {kind}");
        ambient.clear(&mut manager);
        assert!(ambient.sound_types().is_empty());
        println!("DAT ambient type {kind}: PCM energy {energy}; exit cleared environments");
    }
    #[test]
    fn dat_sound_and_music_produce_pcm() {
        let Ok(dir) = std::env::var("AC2_DAT_DIR") else {
            return;
        };
        let dataset = ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(&dir)).unwrap();
        let source = Arc::new(DatSounds::new(Arc::new(ac2_db::ResourceStore::new(
            dataset,
        ))));
        // IDs supplied from the actual UI media/attributes, never substitute samples.
        for id in std::env::var("AC2_AUDIO_TEST_IDS")
            .expect("supply DAT UI sound and music IDs")
            .split(',')
        {
            let id = u32::from_str_radix(id, 16).unwrap();
            let loaded = source.load(id).expect("native sound exists");
            assert!(matches!(
                loaded,
                LoadedSound::Sound { .. }
                    | LoadedSound::Music {
                        segment: Some(_),
                        ..
                    }
                    | LoadedSound::MusicWave { .. }
            ));
            let sink = MixingSink::new(44100);
            let mut manager = SoundManager::new(Box::new(sink.clone()), source.clone());
            if let Ok(gm) = std::env::var("AC2_GM_DLS") {
                manager.load_gm(gm.into());
                manager.wait_for_gm(Duration::from_secs(30));
            }
            assert!(manager.play(&PlaySoundData::new(id)).is_some());
            assert!(manager.wait_for_loads(Duration::from_secs(30)));
            let mut energy = 0.0f64;
            for frame in 0..300 {
                manager.use_time(frame as f64 / 100.0);
                energy += sink
                    .render(441, 2)
                    .iter()
                    .map(|x| (*x as f64).powi(2))
                    .sum::<f64>();
            }
            assert!(energy > 0.001, "silent DAT sound {id:08X}");
            println!("DAT {id:08X}: nonzero PCM energy {energy}");
        }
    }
}
