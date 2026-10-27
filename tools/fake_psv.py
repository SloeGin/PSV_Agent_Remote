"""A terminal stand-in for the PS Vita app, for testing the hooks end to end.

    hooks/.venv/bin/python tools/fake_psv.py [--host H] [--port P] [--mode sofa|desk] [--auto allow|deny|none]

Answers requests with a/A/d/D (allow / allow_always / deny / deny_stop), or
automatically with --auto. Ctrl-C to quit.
"""

import argparse
import json
import queue
import signal
import sys
import threading
import uuid

import paho.mqtt.client as mqtt

ONLINE = "claude/psv/online"
MODE = "claude/psv/mode"
KEYS = {"a": "allow", "A": "allow_always", "d": "deny", "D": "deny_stop"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--mode", choices=("sofa", "desk"), default="sofa")
    parser.add_argument("--auto", choices=("allow", "deny", "none"))
    args = parser.parse_args()
    # Background jobs ignore SIGINT, so scripts stop us with SIGTERM.
    signal.signal(signal.SIGTERM, signal.default_int_handler)

    requests: queue.Queue = queue.Queue()
    connected = threading.Event()

    def on_connect(client, userdata, flags, reason_code, properties):
        client.publish(ONLINE, "1", qos=1, retain=True)
        client.publish(MODE, args.mode, qos=1, retain=True)
        client.subscribe("claude/session/#")
        print(f"connected to {args.host}:{args.port} (mode={args.mode})", flush=True)
        connected.set()

    def on_message(client, userdata, msg):
        parts = msg.topic.split("/")
        if len(parts) < 4:
            return
        sid = parts[2][:8]
        if parts[3] == "status":
            if not msg.payload:
                print(f"[{sid}] session ended", flush=True)
                return
            st = json.loads(msg.payload)
            print(f"[{sid}] {st.get('project')}: {st['state']} {st.get('summary', '')[:80]}", flush=True)
        elif parts[3] == "request" and len(parts) == 5 and msg.payload:
            requests.put((parts[2], json.loads(msg.payload)))

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"fake-psv-{uuid.uuid4().hex[:6]}")
    client.will_set(ONLINE, "0", qos=1, retain=True)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()
    try:
        connected.wait(5) or sys.exit("could not connect")
        while True:
            try:
                sid, req = requests.get(timeout=0.5)  # a bare get() would swallow Ctrl-C
            except queue.Empty:
                continue
            topic = f"claude/session/{sid}/response/{req['rid']}"
            if req.get("kind") == "question":
                for q in req["questions"]:
                    print(f"\n=== [{sid[:8]}] QUESTION {q['header']}: {q['question']}", flush=True)
                    for i, o in enumerate(q["options"]):
                        print(f"  {i}. {o['label']} — {o['description']}", flush=True)
                if args.auto == "allow":
                    answers = [{"selected": [0]} for _ in req["questions"]]
                    client.publish(topic, json.dumps({"rid": req["rid"], "answers": answers}), qos=1)
                    print("sent first option for each question", flush=True)
                elif args.auto == "deny":
                    client.publish(topic, json.dumps({"rid": req["rid"], "choice": "mac"}), qos=1)
                    print("sent: answer on Mac", flush=True)
                continue
            options = [o["id"] for o in req["options"]]
            print(f"\n=== [{sid[:8]}] {req['tool']}: {req['title']}\n{req['body']}\n--- options: {options}", flush=True)
            if args.auto == "none":
                continue
            if args.auto:
                choice = args.auto
            else:
                key = input("a=allow A=allow_always d=deny D=deny_stop > ").strip()
                choice = KEYS.get(key)
                if choice not in options:
                    print("skipped", flush=True)
                    continue
            client.publish(topic, json.dumps({"rid": req["rid"], "choice": choice}), qos=1)
            print(f"sent {choice}", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        client.publish(ONLINE, "0", qos=1, retain=True).wait_for_publish(2)
        client.disconnect()
        client.loop_stop()


if __name__ == "__main__":
    main()
