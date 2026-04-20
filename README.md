# CN-socket-programming

Secure (TLS) socket programming project for Computer Networks: a Python **traffic control server** coordinates two ESP32 **traffic nodes**.  
Each node sends its current `vehicle_count` to the server, and the server decides which node gets **GREEN** (the other gets **RED**) with a brief **YELLOW** transition.

## Repository layout
- `server/server.py` — Python TLS server (listens on `0.0.0.0:5005`)
- `node/node.ino` — ESP32 client node (WiFi + TLS), sends JSON lines and controls LEDs

## How it works (protocol)
The ESP32 sends newline-delimited JSON:
```json
{"node_id":"Node 1","vehicle_count":42}
```

The server responds with newline-delimited JSON:
```json
{"signal":"GREEN"}   // or "YELLOW" / "RED"
```

The Python server continuously compares the two nodes’ `vehicle_count` values and assigns GREEN to the node with the higher count (ties go to the second node in the list).

## Requirements

### Hardware (2 nodes)
For a true “two node” demo you need:
- 2× ESP32 boards (any ESP32 supported by Arduino IDE)
- 4× LEDs (2 per ESP32: one “RED”, one “GREEN”)
- 4× resistors (typically 220Ω–330Ω, one per LED)
- Jumper wires + breadboards
- A Wi‑Fi network both ESP32 boards can join
- 1× computer to run the Python server (or any machine reachable on the LAN)

### Software
- Python 3.x
- Arduino IDE (or PlatformIO) with ESP32 board support
- Python dependencies: none beyond the standard library (`socket`, `ssl`, `threading`, etc.)

---

## Hardware wiring (per ESP32)

The sketch uses these pins (see `node/node.ino`):
- `RED_LED_PIN = 4`
- `GREEN_LED_PIN = 5`

Wire **each LED in series with a resistor**.

Suggested wiring (per LED):
- ESP32 GPIO (4 or 5) → resistor (220–330Ω) → LED anode (+)
- LED cathode (–) → GND

Repeat for both LEDs on each ESP32.

> Note: The sketch’s LED logic is “active-low” style for RED/GREEN, and it shows YELLOW by turning **both LEDs on** (with only two LEDs available).

---

## Server setup (Python TLS)

### 1) Create TLS certificate + key
`server/server.py` expects these files in the **same directory** as `server.py`:
- `cert.pem`
- `key.pem`

From the repo root:
```bash
cd server
openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 365
```

### 2) Run the server
From the repo root:
```bash
python3 server/server.py
```

By default it listens on:
- Host: `0.0.0.0`
- Port: `5005`

Keep this running while both ESP32 nodes connect.

### 3) Find your server IP
On the machine running the server, find its LAN IP (example commands):
- macOS / Linux:
  ```bash
  ip addr
  ```
Use that IP in the ESP32 config as `SERVER_HOST`.

---

## ESP32 node setup (two nodes)

You will flash the same sketch to **two different ESP32 boards**, but with different `NODE_ID` values.

Open: `node/node.ino` and update:

### Wi‑Fi
```cpp
const char *WIFI_SSID = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

### Node identity (make them different)
On ESP32 #1:
```cpp
const char *NODE_ID = "Node 1";
```

On ESP32 #2:
```cpp
const char *NODE_ID = "Node 2";
```

### Server address
Set your Python server’s LAN IP:
```cpp
const char *SERVER_HOST = "192.168.1.50"; // example
const uint16_t SERVER_PORT = 5005;
```

### TLS certificate (required)
In the sketch there is:
```cpp
static const char SERVER_CA[] PROGMEM = R"PEM(
Paste Certificate Here
)PEM";
```

Replace `Paste Certificate Here` with the PEM certificate contents that the server uses.

Typical approach:
- Copy the contents of `server/cert.pem` (the certificate) into that block.

> If you keep using a self-signed cert, the ESP32 must trust that exact certificate (or a CA that signed it).

### Upload
Upload to both ESP32 boards. Then open Serial Monitor (baud `115200`) for each board.

---

## Running the full demo (2 nodes)

1. Start the server:
   ```bash
   cd server
   python3 server.py
   ```

2. Power and reset both ESP32 nodes.
3. Watch the server console: it should print each node’s vehicle counts as they arrive.
4. Watch the ESP32 Serial Monitor(s) for:
   - Wi‑Fi connection
   - TLS connection
   - Received `signal` changes

### Controlling vehicle count from Serial Monitor
Each ESP32 supports text commands (see `printHelp()` in the sketch):
- Send a number (0–200) to set and send immediately:
  ```
  80
  ```
- Force send current value:
  ```
  send
  ```
- Toggle automatic updates:
  ```
  auto
  ```
- Enable/disable auto mode:
  ```
  auto on
  auto off
  ```
- Print status:
  ```
  status
  ```

---

## Notes / Troubleshooting

- If the ESP32 can’t connect:
  - Confirm `SERVER_HOST` is correct and reachable from the Wi‑Fi network.
  - Confirm port `5005` is allowed through your firewall.
  - Confirm the certificate in `SERVER_CA` matches the server certificate.

- If only one node connects:
  - The server’s traffic logic waits until it has **2 nodes** before switching signals.

- Cipher suite:
  - The server sets `ECDHE-RSA-AES128-GCM-SHA256`. If you run into handshake issues, you may need to adjust cipher/cert settings depending on your environment.

---

## License
No license specified yet.
