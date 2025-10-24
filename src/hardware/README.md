# Hardware Relay Service Specification

This document describes the companion daemon that runs on the Raspberry Pi and feeds telemetry to the FTXUI dashboard. The service isolates all USB/serial interaction (Teensy 4.1, multimeters, logic analyzers, etc.) and streams normalized data to every UI instance over a single Unix domain socket using JSON‑RPC 2.0.

## High‑Level Responsibilities

1. **Own the hardware** – Open and manage all serial ports, GPIO expanders, and other sensors. Translate the vendor protocols into normalized measurements.
2. **Expose a JSON‑RPC API** – Listen on `/var/run/workbench/hardware-relay.sock` (configurable) and accept bi-directional JSON‑RPC 2.0 messages from any number of UI clients.
3. **Broadcast measurements** – Convert every reading into a `workbench.dataFrame` notification (see schema below) so all connected dashboards stay in sync.
4. **Maintain metadata** – Emit `workbench.metadata` when sources appear/disappear or when descriptive fields change (name, unit, description, capabilities).
5. **Accept control commands** – Execute GPIO toggles, range changes, or min/max resets requested via JSON‑RPC.

## Socket Transport

- **Address**: Unix domain socket (`AF_UNIX`, `SOCK_STREAM`) at `/var/run/workbench/hardware-relay.sock`.
- **Framing**: Each JSON message is UTF‑8 encoded and terminated with a single `\n` character. The client accumulates bytes until it sees `\n`, then parses the JSON payload.
- **Permissions**: create the socket with group `workbench`, mode `0660`, and place the service in `systemd` so it automatically restarts on failure.

## JSON‑RPC Methods & Notifications

| Name                         | Direction      | Description |
|------------------------------|----------------|-------------|
| `workbench.registerClient`   | UI → Relay     | Initial handshake. Params: `{ "protocol": 1 }`. The relay responds with `{ "result": { "relayVersion": "…", "protocol": 1 } }`.
| `workbench.subscribe`        | UI → Relay     | Start streaming a particular `sourceId`. Params: `{ "sourceId": "demo.metrics" }`.
| `workbench.unsubscribe`      | UI → Relay     | Stop streaming a particular source. |
| `workbench.resetMetric`      | UI → Relay     | Reset stored statistics (e.g., min/max). Params: `{ "sourceId": "…", "channelId": "…", "metric": "min" }`.
| `workbench.gpioSet`          | UI → Relay     | Optional control channel for toggling GPIO lines. |
| `workbench.dataFrame`        | Relay → UI     | Notification delivering a single `DataFrame` (see schema below).
| `workbench.metadata`         | Relay → UI     | Notification delivering either a single source description or an array of sources. |
| `workbench.error`            | Relay → UI     | Optional diagnostic notification. |

### Data Frame Schema (`workbench.dataFrame`)

```json
{
  "jsonrpc": "2.0",
  "method": "workbench.dataFrame",
  "params": {
    "source": {
      "id": "demo.metrics",
      "name": "Demo Metrics",
      "kind": "numeric",
      "description": "Synthetic data published by DemoModule",
      "unit": "V"
    },
    "frame": {
      "timestamp": 1700000000.125,                // seconds since UNIX epoch
      "sourceId": "demo.metrics",
      "sourceName": "Demo Metrics",
      "points": [
        {
          "channelId": "voltage",
          "numeric": {
            "value": 5.021,
            "unit": "V"
          }
        },
        {
          "channelId": "current",
          "numeric": {
            "value": 0.402,
            "unit": "A"
          }
        },
        {
          "channelId": "logic",
          "logic": {
            "channels": [true, false, true],
            "periodNs": 20
          }
        },
        {
          "channelId": "serial",
          "serial": {
            "text": "GPIB:MEAS?\n"
          }
        }
      ]
    }
  }
}
```

### Metadata Notification (`workbench.metadata`)

The relay may either send a single object or an array:

```json
{
  "jsonrpc": "2.0",
  "method": "workbench.metadata",
  "params": [
    {
      "id": "demo.metrics",
      "name": "Demo Metrics",
      "kind": "numeric",
      "description": "Synthetic voltage/current feed",
      "unit": "V"
    },
    {
      "id": "lab.logic",
      "name": "Logic Analyzer",
      "kind": "logic"
    }
  ]
}
```

The UI automatically calls `DataRegistry::registerSource` with the provided fields.

## Message Flow

1. **Client connects** → sends `workbench.registerClient`.
2. **Relay replies** with version info and optionally a full metadata dump.
3. **Client subscribes** to the default sources it cares about (e.g., saved layout).
4. **Relay streams** `workbench.dataFrame` notifications. Each notification contains all the information required to call `DataRegistry::update()`.
5. **Client issues controls** (`workbench.resetMetric`, `workbench.gpioSet`, …) when the user presses buttons.

## Service Implementation Outline

1. **Main loop**
   - Create the Unix domain socket, bind to `/var/run/workbench/hardware-relay.sock`, listen, and accept incoming clients.
   - Track each client connection; broadcast `workbench.dataFrame` notifications to every subscribed client.
2. **Hardware layer**
   - Encapsulate the Teensy serial protocol in a dedicated thread. Convert raw packets into strongly typed measurements (voltage, current, GPIO, etc.).
   - Maintain per-source min/max statistics server-side so all UI instances see consistent numbers.
3. **JSON-RPC handling**
   - Parse requests using your library of choice (e.g., `nlohmann::json`).
   - For `workbench.subscribe`, add the client to that source’s subscriber list.
   - For `workbench.resetMetric`, clear the relevant statistics and emit an updated `workbench.dataFrame` notification immediately.
4. **Resilience**
   - If the Teensy disconnects, broadcast an error notification and attempt to reconnect.
   - Detect broken client sockets and clean them up promptly.

## Testing Tips

- Start the relay in verbose mode and pipe mock data:
  ```bash
  python tools/mock_publisher.py | ./hardware-relay --socket /tmp/workbench.sock --verbose
  ```
- Run the dashboard with `HARDWARE_SOCKET=/tmp/workbench.sock ./WorkshopScreens` so two UI processes can connect simultaneously.
- Use `socat - UNIX-CONNECT:/tmp/workbench.sock` to observe raw JSON traffic.

## Future Enhancements

- Authentication/authorization if the relay is ever exposed remotely.
- Binary bulk channels for high-bandwidth waveform data (still announced through JSON metadata).
- Telemetry logging to disk for replay / analysis.

With this contract in place the UI can remain a pure data consumer: any platform-specific or hardware-specific work lives inside the relay, and the dashboard receives a consistent, testable data stream.
