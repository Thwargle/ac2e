// SPDX-License-Identifier: AGPL-3.0-only
use ac2_net::{flags, NetError, PacketHeader, Transport, HEADER_LEN};
use serde_json::{json, Value};
use std::{
    cell::RefCell,
    collections::{HashSet, VecDeque},
    net::{IpAddr, SocketAddr},
    rc::Rc,
    time::{Duration, Instant},
};

// ClientNet::ProcessConnection notifies gmNetStatusUI every two seconds.
const HEARTBEAT: Duration = Duration::from_secs(2);
const HISTORY: usize = 40;

#[derive(Default)]
struct Traffic {
    packets: u64,
    retransmitted: u64,
    sent: u64,
    received: u64,
    last_received: Option<Instant>,
}

pub struct LinkStatus {
    traffic: Rc<RefCell<Traffic>>,
    started: Instant,
    last_sample: Instant,
    history: VecDeque<(u64, u64)>,
}

struct Measured<T> {
    inner: T,
    traffic: Rc<RefCell<Traffic>>,
    peers: HashSet<IpAddr>,
}

pub fn measure(inner: impl Transport) -> (impl Transport, LinkStatus) {
    let now = Instant::now();
    let traffic = Rc::new(RefCell::new(Traffic::default()));
    (
        Measured { inner, traffic: traffic.clone(), peers: HashSet::new() },
        LinkStatus { traffic, started: now, last_sample: now, history: VecDeque::new() },
    )
}

impl Traffic {
    fn count(&mut self, bytes: &[u8]) -> bool {
        let Some(header) = PacketHeader::decode(bytes) else { return false };
        if header.recipient > 255 || bytes.len() != HEADER_LEN + usize::from(header.size) {
            return false;
        }
        self.packets += 1;
        if header.flags & flags::RETRANSMISSION != 0 {
            self.retransmitted += 1;
        }
        true
    }
}

impl<T: Transport> Transport for Measured<T> {
    fn receive(&mut self) -> Result<Vec<(SocketAddr, Vec<u8>)>, NetError> {
        let batch = self.inner.receive()?;
        if !batch.is_empty() {
            let now = Instant::now();
            let mut traffic = self.traffic.borrow_mut();
            for (from, bytes) in &batch {
                if self.peers.contains(&from.ip()) && traffic.count(bytes) {
                    traffic.received += bytes.len() as u64;
                    traffic.last_received = Some(now);
                }
            }
        }
        Ok(batch)
    }

    fn send(&mut self, to: SocketAddr, bytes: &[u8]) -> Result<(), NetError> {
        self.inner.send(to, bytes)?;
        // Logon and referred world servers can have different addresses. The
        // native transport may receive the reply on the adjacent port.
        self.peers.insert(to.ip());
        let mut traffic = self.traffic.borrow_mut();
        if traffic.count(bytes) {
            traffic.sent += bytes.len() as u64;
        }
        Ok(())
    }

    fn clock(&mut self) -> (f64, i64) { self.inner.clock() }
    fn sleep(&mut self, duration: Duration) { self.inner.sleep(duration) }
}

impl LinkStatus {
    pub fn sample(&mut self, now: Instant) -> Option<Value> {
        let elapsed = now.duration_since(self.last_sample);
        if elapsed < HEARTBEAT { return None }
        self.last_sample = now;
        let mut traffic = self.traffic.borrow_mut();
        let last_received = traffic.last_received;
        let current = std::mem::take(&mut *traffic);
        traffic.last_received = last_received;
        if self.history.len() == HISTORY { self.history.pop_front(); }
        self.history.push_back((current.packets, current.retransmitted));
        let (packets, retransmitted) = self.history.iter().copied()
            .fold((0, 0), |(p, r), (n, k)| (p + n, r + k));
        // CLinkStatusAverages::GetAveragePacketLoss uses twice the sum of
        // retransmitted traffic divided by received plus transmitted packets.
        let percent = if packets == 0 { 0.0 } else { 200.0 * retransmitted as f64 / packets as f64 };
        Some(json!({
            "event": "link_status",
            "sent_bps": current.sent as f64 / elapsed.as_secs_f64(),
            "received_bps": current.received as f64 / elapsed.as_secs_f64(),
            "retransmit_percent": percent,
            "silence_seconds": now.saturating_duration_since(last_received.unwrap_or(self.started)).as_secs_f64(),
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::Ipv4Addr;

    #[derive(Default)]
    struct Socket {
        incoming: Vec<(SocketAddr, Vec<u8>)>,
        fail_send: bool,
    }
    impl Transport for Socket {
        fn receive(&mut self) -> Result<Vec<(SocketAddr, Vec<u8>)>, NetError> {
            Ok(std::mem::take(&mut self.incoming))
        }
        fn send(&mut self, _: SocketAddr, _: &[u8]) -> Result<(), NetError> {
            if self.fail_send { Err(NetError::Truncated) } else { Ok(()) }
        }
        fn clock(&mut self) -> (f64, i64) { (0.0, 0) }
        fn sleep(&mut self, _: Duration) {}
    }
    fn addr(ip: u8, port: u16) -> SocketAddr {
        SocketAddr::from((Ipv4Addr::new(127, 0, 0, ip), port))
    }
    fn packet(flags: u32, size: u16) -> Vec<u8> {
        let mut data = PacketHeader { flags, size, ..Default::default() }.encode().to_vec();
        data.resize(HEADER_LEN + usize::from(size), 0);
        data
    }
    #[test]
    fn heartbeat_counts_wire_bytes_and_native_retransmissions_between_both_peers() {
        let recv = packet(flags::RETRANSMISSION, 12);
        let send = packet(flags::ACK, 4);
        let arrivals = vec![(addr(1, 1101), recv.clone()), (addr(2, 3110), recv.clone()),
            (addr(3, 1000), recv), (addr(1, 1100), vec![0; 4])];
        let (mut t, mut s) = measure(Socket { incoming: arrivals.clone(), ..Default::default() });
        let start = s.started;
        t.send(addr(1, 1100), &send).unwrap();
        t.send(addr(2, 3110), &send).unwrap();
        assert_eq!(t.receive().unwrap(), arrivals);
        assert!(s.sample(start + Duration::from_secs(1)).is_none());
        let update = s.sample(start + HEARTBEAT).unwrap();
        assert_eq!(update["sent_bps"], 24.0);
        assert_eq!(update["received_bps"], 32.0);
        assert_eq!(update["retransmit_percent"], 100.0);
        let idle = s.sample(start + Duration::from_secs(10)).unwrap();
        assert_eq!(idle["sent_bps"], 0.0);
        assert_eq!(idle["received_bps"], 0.0);
        assert!((9.0..=10.0).contains(&idle["silence_seconds"].as_f64().unwrap()));
        assert_eq!(idle["retransmit_percent"], 100.0);
        for tick in 1..=HISTORY {
            let update = s.sample(start + Duration::from_secs(10 + 2 * tick as u64)).unwrap();
            if tick == HISTORY { assert_eq!(update["retransmit_percent"], 0.0); }
        }
    }
    #[test]
    fn failed_sends_do_not_make_the_link_look_active() {
        let (mut t, mut s) = measure(Socket { fail_send: true, ..Default::default() });
        assert!(t.send(addr(1, 1100), &packet(flags::RETRANSMISSION, 12)).is_err());
        let update = s.sample(s.started + HEARTBEAT).unwrap();
        assert_eq!(update["sent_bps"], 0.0);
        assert_eq!(update["retransmit_percent"], 0.0);
        assert_eq!(update["silence_seconds"], 2.0);
    }
}
