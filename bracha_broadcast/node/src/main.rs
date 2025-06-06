use std::borrow::BorrowMut;
use std::collections::HashMap;
use std::collections::HashSet;
use std::env;
use std::fs;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::net::SocketAddrV4;
use std::os::fd::AsFd;
use std::os::fd::RawFd;
use std::str::FromStr;
use std::thread;
use std::time::Duration;
use std::vec;

use serde::{Deserialize, Serialize};
use serde_json;

use nix::sys::select::{FdSet, select};
use std::os::unix::io::AsRawFd;

type MsgValueType = i32;

#[derive(Serialize, Deserialize, Debug)]
enum Msg {
    Initial { v: MsgValueType },
    Echo { v: MsgValueType },
    Ready { v: MsgValueType },
}

#[derive(PartialEq)]
enum MsgState {
    SentEcho,
    SentReady,
}

fn broadcast(msg: &Msg, tcp_streams: &mut HashMap<String, TcpStream>) -> std::io::Result<()> {
    let serialized_send_msg = serde_json::to_string(&msg).unwrap();

    for (_, stream) in tcp_streams {
        stream.write(&serialized_send_msg.as_bytes())?;
    }

    Ok(())
}

fn main() -> std::io::Result<()> {

    let mut addresses: Vec<SocketAddrV4> = vec![];

    // read config files names
    let args: Vec<String> = env::args().collect();
    let id: usize = args[1].parse::<usize>().unwrap();

    // read config file content
    let contents = fs::read_to_string(&args[2]).expect("Should have been able to read the file");

    let mut lines_it = contents.lines();

    let number_of_msg_per_process = lines_it.next().unwrap().parse::<usize>().unwrap();
    let mut current_msg_id_to_send = 1; // goes from 1 to number_of_msg_per_process

    for line in lines_it {
        let addr = SocketAddrV4::from_str(line).unwrap();
        addresses.push(addr);
    }

    let process_number = addresses.len();
    let max_byzantin_process_number = process_number / 3;

    let mut counter_initial_msgs: HashMap<MsgValueType, usize> = HashMap::new();
    let mut counter_echo_msgs: HashMap<MsgValueType, usize> = HashMap::new();
    let mut counter_ready_msgs: HashMap<MsgValueType, i32> = HashMap::new();
    let mut msgs_states: HashMap<MsgValueType, MsgState> = HashMap::new();

    let mut msgs_delivered: HashSet<MsgValueType> = HashSet::new();

    let mut tcp_streams: HashMap<String, TcpStream> = HashMap::new(); // addr -> stream tcp
    let mut tcp_stream_recv_buffers: HashMap<RawFd, Vec<u8>> = HashMap::new();

    // TODO do not hardcode the port
    let listener = TcpListener::bind("0.0.0.0:42512").unwrap();

    for i in (id+1)..process_number {

        loop {

            match TcpStream::connect(&addresses[i]) {
                Ok(stream) => {
                    stream.set_nonblocking(true);
                    tcp_stream_recv_buffers.insert(stream.as_raw_fd(), vec![]);
                    tcp_streams.insert(addresses[i].ip().to_string(), stream);
                    println!("connection ok");
                    break;
                }
                Err(_) => {
                    println!("cannot connect, sleeping for a bit");
                    thread::sleep(Duration::from_millis(500)) // sleep to wait for peer program to start
                }
            }
        }
    }

    // accept connections and process them

    if tcp_streams.len() < process_number -1 as usize {
        println!("Waiting for connections...");
        for stream_res in listener.incoming() {
            println!("Got connection");
            let stream = stream_res.unwrap();

            let addr = stream.peer_addr().unwrap().ip().to_string();
            stream.set_nonblocking(true);
            tcp_stream_recv_buffers.insert(stream.as_raw_fd(), vec![]);
            tcp_streams.insert(addr, stream);

            if tcp_streams.len() == process_number -1 as usize {
                println!("Got all connection");
                break;
            }
        }
    }

    loop {

        if current_msg_id_to_send == 1 {
            let value = (current_msg_id_to_send * process_number + id) as i32;
            let send_msg = Msg::Initial {
                v: value,
            };
            current_msg_id_to_send += 1;
            *counter_initial_msgs.entry(value).or_insert(0) += 1;
            broadcast(&send_msg, &mut tcp_streams).unwrap();
        }

        let mut raw_fd_set_ready : Vec<i32> = vec![];
        {
            let mut fdset = FdSet::new();
            for (_,stream) in &tcp_streams {
                fdset.insert(stream.as_fd());
            }

            let _ = select(None, &mut fdset, None, None, None).unwrap();

            for fd in fdset.fds(None) {
                raw_fd_set_ready.push(fd.as_raw_fd());
            }
        }

        for addr in &addresses {
            if *addr == addresses[id]{ // if it's our addr we won't have a stream to it and crash when looking for the stream
                continue;
            }

            let mut temp_recv_buffer = [0; 200];
            let mut stream = tcp_streams.get(&addr.ip().to_string()).borrow_mut().unwrap();
            if raw_fd_set_ready.contains(&stream.as_raw_fd()) {
                let n = stream.read(&mut temp_recv_buffer[..]).unwrap();
                let recv_buffer = tcp_stream_recv_buffers
                    .get_mut(&stream.as_raw_fd())
                    .unwrap();
                recv_buffer.append(&mut temp_recv_buffer[..n].to_vec());

                let de = serde_json::Deserializer::from_slice(&recv_buffer[..]);
                let mut msg_result_stream = de.into_iter::<Msg>();

                let mut number_bytes_correctly_processed = 0;
                loop {
                    match msg_result_stream.next() {
                        Some(msg_result) => {
                            match msg_result {
                                Ok(msg) => {
                                    number_bytes_correctly_processed =
                                        msg_result_stream.byte_offset();
                                    match msg {
                                        Msg::Initial { v: value } => {
                                            *counter_initial_msgs.entry(value).or_insert(0) += 1;

                                            if *counter_initial_msgs.get(&value).unwrap() >= 0
                                                && !msgs_states.contains_key(&value)
                                            {
                                                msgs_states.insert(value, MsgState::SentEcho);
                                                let echo_msg = Msg::Echo { v: value };
                                                *counter_echo_msgs.entry(value).or_insert(0) += 1;
                                                broadcast(&echo_msg, &mut tcp_streams).unwrap();
                                            }
                                        }
                                        Msg::Echo { v: value } => {
                                            *counter_echo_msgs.entry(value).or_insert(0) += 1;

                                            if *counter_echo_msgs.get(&value).unwrap()
                                                >= ((process_number + max_byzantin_process_number)
                                                    / 2)
                                                && !msgs_states.contains_key(&value)
                                            {
                                                msgs_states.insert(value, MsgState::SentEcho);
                                                let echo_msg = Msg::Echo { v: value };
                                                *counter_echo_msgs.entry(value).or_insert(0) += 1;
                                                broadcast(&echo_msg, &mut tcp_streams).unwrap();
                                            }

                                            if *counter_echo_msgs.get(&value).unwrap()
                                                >= ((process_number + max_byzantin_process_number)
                                                    / 2)
                                                && *msgs_states.get(&value).unwrap()
                                                    == MsgState::SentEcho
                                            {
                                                msgs_states.insert(value, MsgState::SentReady);
                                                let ready_msg = Msg::Ready { v: value };
                                                *counter_ready_msgs.entry(value).or_insert(0) += 1;
                                                broadcast(&ready_msg, &mut tcp_streams).unwrap();
                                            }
                                        }
                                        Msg::Ready { v: value } => {
                                            *counter_ready_msgs.entry(value).or_insert(0) += 1;

                                            if *counter_ready_msgs.get(&value).unwrap()
                                                >= (max_byzantin_process_number as i32 + 1)
                                                && !msgs_states.contains_key(&value)
                                            {
                                                msgs_states.insert(value, MsgState::SentEcho);
                                                let echo_msg = Msg::Echo { v: value };
                                                *counter_echo_msgs.entry(value).or_insert(0) += 1;
                                                broadcast(&echo_msg, &mut tcp_streams).unwrap();
                                            }

                                            if *counter_ready_msgs.get(&value).unwrap()
                                                >= (max_byzantin_process_number as i32 + 1)
                                                && *msgs_states.get(&value).unwrap()
                                                    == MsgState::SentEcho
                                            {
                                                msgs_states.insert(value, MsgState::SentReady);
                                                let ready_msg = Msg::Ready { v: value };
                                                *counter_ready_msgs.entry(value).or_insert(0) += 1;
                                                broadcast(&ready_msg, &mut tcp_streams).unwrap();
                                            }

                                            if *counter_ready_msgs.get(&value).unwrap()
                                                >= (2 * max_byzantin_process_number as i32 + 1)
                                                && *msgs_states.get(&value).unwrap()
                                                    == MsgState::SentReady
                                            {
                                                msgs_delivered.insert(value);

                                                // print last 50 msg to check arrival order
                                                if value > ((number_of_msg_per_process * process_number) - 50) as i32 {
                                                    println!("Delivered msg {value}")
                                                }   

                                                // check if need to send new msg
                                                if value == ( (current_msg_id_to_send-1) * process_number + id) as i32 {
                                                    if current_msg_id_to_send <= number_of_msg_per_process{
                                                        let value = (current_msg_id_to_send * process_number + id) as i32;
                                                        let send_msg = Msg::Initial {
                                                            v: value,
                                                        };
                                                        current_msg_id_to_send += 1;
                                                        *counter_initial_msgs.entry(value).or_insert(0) += 1;
                                                        broadcast(&send_msg, &mut tcp_streams).unwrap();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                Err(e) => {
                                    println!("Got an err {e:#?}")
                                }
                            }
                        }
                        None => break,
                    }
                }

                recv_buffer.drain(0..number_bytes_correctly_processed);
            }
        }

        if msgs_delivered.len() == number_of_msg_per_process * process_number {
            break;
        }
    }

    println!("Delivered all msgs");

    Ok(())
}
