# Exchange Gateways

Multi-exchange gateway for streaming market data and routing orders via SBE (Simple Binary Encoding) queues. Supports **Deribit** (FIX protocol) and **Hyperliquid** (WebSocket API).

## Prerequisites

- CMake 3.10+
- C++23 compiler
- Java (for SBE code generation, runs automatically during build)
- vcpkg (dependencies: openssl, boost-iostreams, boost-filesystem, boost-system, boost-asio, boost-beast, boost-multiprecision, simdjson, nlohmann-json, spdlog)

## Build

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

This produces a single `gateways` binary. The `--app` flag selects which service to run.

## Services

### Market Data (`md-*`)

Streams live market data from an exchange and writes to an SBE queue.

```bash
# Deribit
./gateways --app md-testnet
./gateways --app md-prod

# Hyperliquid
./gateways --app md-testnet-hyperliquid
./gateways --app md-prod-hyperliquid
```

### Gateway (`gw-*`)

Reads orders from an inbound SBE queue, sends them to the exchange, and writes execution reports to an outbound SBE queue.

```bash
# Deribit (FIX)
./gateways --app gw-testnet
./gateways --app gw-prod

# Hyperliquid (WebSocket)
./gateways --app gw-testnet-hyperliquid
```

### Market Data Historical (`md-hist-*`)

Persists raw market data messages to disk for later replay.

```bash
./gateways --app md-hist
./gateways --app md-hist-hyperliquid
```

### Market Data Processor (`md-process`)

Converts raw persisted market data into binary SBE format.

```bash
./gateways --app md-process
```

## Configuration

Each app reads from `config/settings.<app-name>.txt`. Use `--config-override <name>` to load a different config file.

**Deribit** requires:
```
exchange_name=deribit
fix_settings_file_path=config/deribit-testnet-orders.cfg
deribit_client_id=<your_client_id>
deribit_client_secret=<your_client_secret>
md_file_path=<path_to_md_queue>
gw_inbound_file_path=<path_to_inbound_queue>
gw_outbound_file_path=<path_to_outbound_queue>
```

**Hyperliquid** requires:
```
exchange_name=hyperliquid
environment=testnet
hl_account_address=<your_wallet_address>
hl_private_key=<your_private_key>
md_file_path=<path_to_md_queue>
gw_inbound_file_path=<path_to_inbound_queue>
gw_outbound_file_path=<path_to_outbound_queue>
```

Hyperliquid market data configs also take a `coins` field (comma-separated list of symbols to subscribe to).

## Notes

- SBE code generation runs automatically during build from `schema/messages.xml`. To regenerate manually:
  ```bash
  java -Dsbe.target.language=CPP -Dsbe.output.dir=generated -jar env/sbe-all-1.30.0.jar schema/messages.xml
  ```
- QuickFIX is included as a local third-party dependency under `third_party/quickfix`
- The Hyperliquid SDK is included under `third_party/hyperliquid-sdk-cpp`
