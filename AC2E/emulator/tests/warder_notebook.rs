use pyrealium::{warder_notebook as n, inventory::{Command, Inventory, ITEM_NAMESPACE, BAG_SLOTS}, quests::Book, store::Store};

fn step(q:&mut Book,b:&mut Inventory,a:n::Action,t:u64) -> n::Plan {
    let p=n::plan(q,b,10,a,b.high_water().max(ITEM_NAMESPACE)+1,t).unwrap();
    for c in &p.items {*b=b.apply(c).unwrap();} *q=p.quests.clone();p
}
#[test]
fn stages_and_native_assets() {
    if let Ok(path)=std::env::var("AC2_TEST_GAME_DATA") {
        let mut dat=pyrealium::dat::Dat::open(std::path::Path::new(&path).join("portal.dat")).unwrap();
        let metadata=n::validate(&mut dat).unwrap();assert_eq!(metadata.len(),4);
        for id in [n::NOTEBOOK,n::REWARDS[0],n::REWARDS[1],n::REWARDS[2]] {
            pyrealium::item_services::acquired(1,&n::item(id,ITEM_NAMESPACE+1,0).unwrap(),metadata[&id]).unwrap();
        }
    }
    let (mut q,mut b)=(Book::default(),Inventory::default());
    let p=n::plan(&q,&b,9,n::Action::Talk,ITEM_NAMESPACE+1,100).unwrap();
    assert_eq!(p.quests.notebook.phase,0);assert!(p.text.contains("level 10"));
    step(&mut q,&mut b,n::Action::Talk,100);assert_eq!(q.notebook.phase,1);
    assert!(n::plan(&q,&b,10,n::Action::Take,ITEM_NAMESPACE+1,101).is_err());
    assert_eq!(q.notebook.killed(0x470015bd,101),q.notebook);
    q.notebook=q.notebook.killed(n::BANDIT,101);assert_eq!(q.notebook.phase,2);
    step(&mut q,&mut b,n::Action::Take,102);assert_eq!(q.notebook.phase,3);
    assert!(n::plan(&q,&b,10,n::Action::Take,b.high_water()+1,103).is_err());
    assert!(n::plan(&q,&b,10,n::Action::Return(1),b.high_water()+1,103).is_err());
    let p=step(&mut q,&mut b,n::Action::Talk,103);assert_eq!(q.notebook.phase,4);
    assert!(matches!(p.rewards.as_slice(),[pyrealium::progression::Command::AwardXp(800),pyrealium::progression::Command::AwardQuestPoints(300)]));
    assert_eq!(q.notebook.captain_started,103);
    for id in n::REWARDS {assert_eq!(b.items().filter(|i|i.definition==id).count(),1);}
    assert!(!b.items().any(|i|i.definition==n::NOTEBOOK));
    assert!(step(&mut q,&mut b,n::Action::Talk,104).rewards.is_empty());
    let p=step(&mut q,&mut b,n::Action::Captain,105);
    assert!(matches!(p.rewards.as_slice(),[pyrealium::progression::Command::AwardXp(2500)]));
    assert!(step(&mut q,&mut b,n::Action::Captain,106).rewards.is_empty());
    step(&mut q,&mut b,n::Action::Talk,103+n::REPEAT);assert_eq!(q.notebook.phase,1);
}
#[test]
fn expiry_full_bags_stale_items_and_clock_are_safe() {
    let (mut q,mut b)=(Book::default(),Inventory::default());
    step(&mut q,&mut b,n::Action::Talk,100);
    q.notebook=q.notebook.killed(n::BANDIT,101);step(&mut q,&mut b,n::Action::Take,102);
    let notebook=b.items().next().unwrap().id;
    for slot in 1..BAG_SLOTS {b=b.apply(&Command::Grant(n::item(n::REWARDS[0],b.high_water()+1,slot).unwrap())).unwrap();}
    let before=(q.clone(),b.clone());
    assert!(n::plan(&q,&b,10,n::Action::Return(notebook),b.high_water()+1,103).is_err());
    assert_eq!((q.clone(),b.clone()),before);
    assert!(n::plan(&q,&b,10,n::Action::Return(notebook),b.high_water()+1,100+n::LIMIT).is_err());
    assert_eq!(q.notebook.killed(n::BANDIT,100+n::LIMIT),q.notebook);
    step(&mut q,&mut b,n::Action::Talk,100+n::LIMIT);assert_eq!(q.notebook.phase,1);
    assert!(!b.items().any(|i|i.definition==n::NOTEBOOK));
    assert!(n::plan(&q,&b,10,n::Action::Talk,b.high_water()+1,99).is_err());
    q.notebook.captain_started=100;
    let p=step(&mut q,&mut b,n::Action::Captain,100+n::REFERRAL_LIMIT);
    assert!(p.rewards.is_empty());assert!(p.text.contains("expired"));
    step(&mut q,&mut b,n::Action::Talk,101+n::REFERRAL_LIMIT);
    assert_eq!(q.notebook.captain_started,101+n::REFERRAL_LIMIT);
}
#[test]
fn journal_replay_preserves_kill_loot_referrals_and_once_only_rewards() {
    let dir=std::env::temp_dir().join(format!("ac2-notebook-{}-{}",std::process::id(),pyrealium::recovered_quests::now()));
    let t=pyrealium::recovered_quests::now();let before;
    {
        let mut s=Store::open(&dir).unwrap();s.create("local","Warder Apprentice").unwrap();
        s.progression_batch("local",1,&[pyrealium::progression::Command::AwardXp(100_000)]).unwrap();
        before=s.characters[&1].progression.xp;
        assert!(s.notebook_transaction("other",1,n::Action::Talk,t).is_err());
        s.notebook_transaction("local",1,n::Action::Talk,t).unwrap();
        s.encounter_reward("local",1,n::BANDIT,&[pyrealium::progression::Command::AwardXp(31)]).unwrap();
        assert_eq!(s.characters[&1].quests.notebook.phase,2);s.compact_journal().unwrap();
    }
    {
        let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].quests.notebook.phase,2);
        s.notebook_transaction("local",1,n::Action::Take,t+1).unwrap();
        let id=s.characters[&1].inventory.items().find(|i|i.definition==n::NOTEBOOK).unwrap().id;
        assert!(s.drop_candidate("local",1,id,pyrealium::movement::Placement::HUMAN_START).is_err());
        s.compact_journal().unwrap();
    }
    {
        let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].quests.notebook.phase,3);
        s.notebook_transaction("local",1,n::Action::Talk,t+2).unwrap();
        s.notebook_transaction("local",1,n::Action::Talk,t+3).unwrap();
        s.notebook_transaction("local",1,n::Action::Captain,t+4).unwrap();
        s.notebook_transaction("local",1,n::Action::Captain,t+5).unwrap();s.compact_journal().unwrap();
    }
    {
        let s=Store::open(&dir).unwrap();let c=&s.characters[&1];
        assert_eq!(c.progression.xp,before+31+800+2500);assert_eq!(c.progression.quest_points,300);assert_eq!(c.quests.notebook.completed,t+2);
        assert_eq!(c.quests.notebook.captain_completed,t+4);
        for d in n::REWARDS {assert_eq!(c.inventory.items().filter(|i|i.definition==d).count(),1);}
        assert!(c.quests.list_graph().is_ok());assert_eq!(c.quests.notebook.entries().len(),2);
    }
    std::fs::remove_dir_all(dir).unwrap();
}
