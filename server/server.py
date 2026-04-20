import socket
import ssl
import json
import threading
import time

HOST = "0.0.0.0"
PORT = 5005

context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(certfile="cert.pem", keyfile="key.pem")

context.set_ciphers("ECDHE-RSA-AES128-GCM-SHA256")

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind((HOST, PORT))
server.listen(5)

node_data = {}
clients = {}
current_green = None

def send(conn, msg):
    conn.send((json.dumps(msg) + "\n").encode())

def traffic_control():
    global current_green

    while True:
        if len(node_data) < 2:
            time.sleep(1)
            continue

        nodes = list(node_data.keys())
        n1, n2 = nodes[0], nodes[1]

        v1 = node_data[n1]
        v2 = node_data[n2]

        new_green = n1 if v1 > v2 else n2
        other = n2 if new_green == n1 else n1

        if current_green != new_green:
            print(f"\nSwitching → {new_green} gets GREEN")

            if current_green:
                send(clients[current_green], {"signal": "YELLOW"})
                time.sleep(2)
                send(clients[current_green], {"signal": "RED"})

            send(clients[new_green], {"signal": "YELLOW"})
            time.sleep(2)
            send(clients[new_green], {"signal": "GREEN"})
            send(clients[other], {"signal": "RED"})

            current_green = new_green

        time.sleep(2)

def handle_client(conn, addr):
    buffer = ""

    try:
        print("🔒 Client connected:", addr)
        print("Cipher:", conn.cipher())

        while True:
            data = conn.recv(1024)
            if not data:
                break

            buffer += data.decode()

            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)

                msg = json.loads(line)
                node = msg["node_id"]
                vehicles = msg["vehicle_count"]

                node_data[node] = vehicles
                clients[node] = conn

                print(f"{node}: {vehicles} vehicles")

    except Exception as e:
        print("Client error:", e)

    finally:
        conn.close()

def accept_clients():
    while True:
        client, addr = server.accept()
        secure_client = context.wrap_socket(client, server_side=True)

        threading.Thread(target=handle_client, args=(secure_client, addr), daemon=True).start()

threading.Thread(target=accept_clients, daemon=True).start()
threading.Thread(target=traffic_control, daemon=True).start()

print("🚦 Secure Traffic Server Running...")
while True:
    time.sleep(1)