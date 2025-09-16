# C++ Crypto Trading Showcase

This repository collects several focused C++ programs that demonstrate real-time market data handling and quantitative calculations
using the excellent [ccapi](https://github.com/crypto-chassis/ccapi) library.

Included examples (src/)

- liquidity_analyzer.cpp: Thread-safe market microstructure and risk analytics
  (spreads, depth, VWAP/slippage, order book slope, Kyle's lambda, Amihud, realized/historical volatility, VaR/ES) with JSON-like output.
- options_calculator.cpp: Black–Scholes Greeks for calls and puts with clear console output.
- arbitrage.cpp: Real-time, config-driven cross-exchange arbitrage monitor (Binance vs Bybit) with formatted table output.
- trades.cpp: Streaming analysis with adaptive thresholds, EWMA volatility, and anomaly detection for trades.

Support utilities

- include/simple_config.h: Minimal config loader with convenient require/get helpers and a CLI config path resolver (supports --config and --config=...)
- config/config.example.txt: Example configuration you can copy to config.txt and modify.
- .env.example: Example environment variables for API keys.

Build
Requirements

- C++17 compiler
- CMake 3.15+
- ccapi (headers; provide include path). You can add ccapi as a submodule or point to your local install.

Configure and build

```bash
# from repo root
cmake -S . -B build \
  -DCCAPI_INCLUDE_DIR=/path/to/ccapi/include \
  -DCCAPI_LIBS=""
cmake --build build -j
```

Notes

- CCAPI_INCLUDE_DIR should contain the folder `ccapi_cpp`.
- If ccapi requires linking libraries in your setup, set CCAPI_LIBS accordingly, e.g. `-DCCAPI_LIBS="pthread websockets"`.
- If you include private endpoints or REST requests, you may need additional libraries (curl/json) — the targets here avoid those by default.

Run
Copy the example config and optionally create a .env:

```bash
cp config/config.example.txt config.txt
cp .env.example .env   # optional
```

Examples

```bash
# Liquidity Analyzer (Binance BTCUSDT trade + order book)
./build/liquidity_analyzer --config config.txt

# Options Calculator (listens to BTCUSDT spot price for input)
./build/options_calculator --config config.txt

# Arbitrage monitor (Binance vs Bybit, symbol from config)
./build/arbitrage --config config.txt

# Trades stream analyzer
./build/trades --config config.txt
```

License

- This showcase depends on external libraries under their own licenses.
- The original code is provided under your preferred license; add a LICENSE file if desired.
