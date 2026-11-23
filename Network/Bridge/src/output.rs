// SPDX-License-Identifier: AGPL-3.0-only
//! Ordered, byte-bounded IPC output. A busy renderer must not stall the UDP pump.
use std::{io::{self, Write}, sync::{mpsc, Arc, atomic::{AtomicUsize, Ordering}}};

pub struct Output {
    tx: mpsc::Sender<String>,
    queued: Arc<AtomicUsize>,
    limit: usize,
}
pub struct PendingOutput {
    rx: mpsc::Receiver<String>,
    queued: Arc<AtomicUsize>,
}
impl Output {
    pub fn channel(limit: usize) -> (Self, PendingOutput) {
        let (tx, rx) = mpsc::channel();
        let queued = Arc::new(AtomicUsize::new(0));
        (Self { tx, queued: queued.clone(), limit }, PendingOutput { rx, queued })
    }
    pub fn send(&self, line: String) -> io::Result<()> {
        let size = line.len();
        self.queued.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |n|
            n.checked_add(size).filter(|&total| total <= self.limit))
            .map_err(|_| io::Error::other("Client output backlog exceeded its limit"))?;
        if self.tx.send(line).is_err() {
            self.queued.fetch_sub(size, Ordering::Relaxed);
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        Ok(())
    }
}
impl PendingOutput {
    pub fn write_to(self, mut out: impl Write) -> io::Result<()> {
        for line in self.rx {
            out.write_all(line.as_bytes())?;
            out.flush()?;
            self.queued.fetch_sub(line.len(), Ordering::Relaxed);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{sync::Mutex, thread, time::Duration};
    struct StalledWriter {
        entered: mpsc::Sender<()>,
        release: mpsc::Receiver<()>,
        bytes: Arc<Mutex<Vec<u8>>>,
        stalled: bool,
    }
    impl Write for StalledWriter {
        fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
            if !self.stalled {
                self.stalled = true;
                self.entered.send(()).unwrap();
                self.release.recv_timeout(Duration::from_secs(5)).unwrap();
            }
            self.bytes.lock().unwrap().extend_from_slice(bytes);
            Ok(bytes.len())
        }
        fn flush(&mut self) -> io::Result<()> { Ok(()) }
    }
    #[test]
    fn stalled_client_does_not_block_producer_and_backlog_is_bounded_and_ordered() {
        let (output, pending) = Output::channel(8);
        let (entered_tx, entered_rx) = mpsc::channel();
        let (release_tx, release_rx) = mpsc::channel();
        let bytes = Arc::new(Mutex::new(Vec::new()));
        let writer = StalledWriter { entered: entered_tx, release: release_rx, bytes: bytes.clone(), stalled: false };
        let worker = thread::spawn(move || pending.write_to(writer));
        output.send("one\n".into()).unwrap();
        entered_rx.recv_timeout(Duration::from_secs(5)).unwrap();
        output.send("two\n".into()).unwrap();
        assert!(output.send("overflow\n".into()).is_err());
        release_tx.send(()).unwrap();
        drop(output);
        worker.join().unwrap().unwrap();
        assert_eq!(&*bytes.lock().unwrap(), b"one\ntwo\n");
    }
    #[test]
    fn closed_reader_is_reported() {
        let (output, pending) = Output::channel(8);
        drop(pending);
        assert_eq!(output.send("one\n".into()).unwrap_err().kind(), io::ErrorKind::BrokenPipe);
        assert_eq!(output.queued.load(Ordering::Relaxed), 0);
    }
}
