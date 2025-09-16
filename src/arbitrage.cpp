#include "ccapi_cpp/ccapi_session.h"
#include "simple_config.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <thread>

namespace ccapi {

class ArbitrageMonitor {
private:
  struct ExchangeData {
    double bid = 0.0;
    double ask = 0.0;
    double bid_volume = 0.0;
    double ask_volume = 0.0;
    std::chrono::system_clock::time_point last_update;
    bool has_data = false;
  };

  ExchangeData binance_data;
  ExchangeData bybit_data;

  // Configuration (loaded from config.txt if present)
  double min_price_diff = 1.0;   // Minimum $1 difference
  double profit_threshold = 0.5; // Minimum $0.5 profit threshold

public:
  void setConfig(double minDiff, double profitThres) {
    this->min_price_diff = minDiff;
    this->profit_threshold = profitThres;
  }
  void printHeader() {
    std::cout << std::left;
    std::cout << std::setw(12) << "Time" << " | ";
    std::cout << std::setw(11) << "Bin_Bid" << " | ";
    std::cout << std::setw(11) << "Bin_Ask" << " | ";
    std::cout << std::setw(10) << "Bin_BVol" << " | ";
    std::cout << std::setw(10) << "Bin_AVol" << " | ";
    std::cout << std::setw(11) << "Byb_Bid" << " | ";
    std::cout << std::setw(11) << "Byb_Ask" << " | ";
    std::cout << std::setw(10) << "Byb_BVol" << " | ";
    std::cout << std::setw(10) << "Byb_AVol" << " | ";
    std::cout << std::setw(9) << "Bid_Diff" << " | ";
    std::cout << std::setw(9) << "Ask_Diff" << " | ";
    std::cout << std::setw(8) << "Bid_%" << " | ";
    std::cout << std::setw(8) << "Ask_%" << " | ";
    std::cout << std::setw(18) << "Best_Direction" << " | ";
    std::cout << std::setw(12) << "Profit_$" << " | ";
    std::cout << std::setw(8) << "Lat_ms";
    std::cout << std::endl;

    // Print separator line with proper length
    std::cout << std::string(12, '-') << "-+-";
    std::cout << std::string(11, '-') << "-+-";
    std::cout << std::string(11, '-') << "-+-";
    std::cout << std::string(10, '-') << "-+-";
    std::cout << std::string(10, '-') << "-+-";
    std::cout << std::string(11, '-') << "-+-";
    std::cout << std::string(11, '-') << "-+-";
    std::cout << std::string(10, '-') << "-+-";
    std::cout << std::string(10, '-') << "-+-";
    std::cout << std::string(9, '-') << "-+-";
    std::cout << std::string(9, '-') << "-+-";
    std::cout << std::string(8, '-') << "-+-";
    std::cout << std::string(8, '-') << "-+-";
    std::cout << std::string(18, '-') << "-+-";
    std::cout << std::string(12, '-') << "-+-";
    std::cout << std::string(8, '-') << std::endl;
  }

  std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
  }

  void calculateAndPrint() {
    if (!binance_data.has_data || !bybit_data.has_data) {
      return; // Wait for both exchanges to have data
    }

    // Calculate differences
    double bid_diff_dollar = binance_data.bid - bybit_data.bid;
    double ask_diff_dollar = binance_data.ask - bybit_data.ask;

    double bid_diff_percent = (bid_diff_dollar / bybit_data.bid) * 100;
    double ask_diff_percent = (ask_diff_dollar / bybit_data.ask) * 100;

    // Check for arbitrage opportunities
    bool bid_arbitrage = std::abs(bid_diff_dollar) >= min_price_diff;
    bool ask_arbitrage = std::abs(ask_diff_dollar) >= min_price_diff;

    // Determine best direction and potential profit
    std::string best_direction = "None";
    double potential_profit = 0.0;

    // Cross-exchange arbitrage: buy low, sell high
    double arb1 = binance_data.ask - bybit_data.bid; // Buy Bybit, Sell Binance
    double arb2 = bybit_data.ask - binance_data.bid; // Buy Binance, Sell Bybit

    if (arb1 > profit_threshold && arb1 > arb2) {
      best_direction = "Buy_Bybit";
      potential_profit = arb1;
    } else if (arb2 > profit_threshold) {
      best_direction = "Buy_Binance";
      potential_profit = arb2;
    }

    // Calculate latency (time since last update)
    auto now = std::chrono::system_clock::now();
    auto binance_latency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - binance_data.last_update)
            .count();
    auto bybit_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - bybit_data.last_update)
                             .count();
    auto max_latency = std::max(binance_latency, bybit_latency);

    // Print the data row with consistent formatting
    std::cout << std::left << std::fixed;
    std::cout << std::setw(12) << std::setprecision(0) << getCurrentTimestamp()
              << " | ";
    std::cout << std::setw(11) << std::setprecision(2) << binance_data.bid
              << " | ";
    std::cout << std::setw(11) << std::setprecision(2) << binance_data.ask
              << " | ";
    std::cout << std::setw(10) << std::setprecision(3)
              << binance_data.bid_volume << " | ";
    std::cout << std::setw(10) << std::setprecision(3)
              << binance_data.ask_volume << " | ";
    std::cout << std::setw(11) << std::setprecision(2) << bybit_data.bid
              << " | ";
    std::cout << std::setw(11) << std::setprecision(2) << bybit_data.ask
              << " | ";
    std::cout << std::setw(10) << std::setprecision(3) << bybit_data.bid_volume
              << " | ";
    std::cout << std::setw(10) << std::setprecision(3) << bybit_data.ask_volume
              << " | ";
    std::cout << std::setw(9) << std::setprecision(2) << bid_diff_dollar
              << " | ";
    std::cout << std::setw(9) << std::setprecision(2) << ask_diff_dollar
              << " | ";
    std::cout << std::setw(8) << std::setprecision(3) << bid_diff_percent
              << " | ";
    std::cout << std::setw(8) << std::setprecision(3) << ask_diff_percent
              << " | ";
    std::cout << std::setw(18) << best_direction << " | ";
    std::cout << std::setw(12) << std::setprecision(2) << potential_profit
              << " | ";
    std::cout << std::setw(8) << std::setprecision(0) << max_latency;
    std::cout << std::endl;
  }

  void updateBinanceData(const Message &message) {
    bool updated = false;
    for (const auto &element : message.getElementList()) {
      if (element.has("BID_PRICE")) {
        binance_data.bid = std::stod(element.getValue("BID_PRICE"));
        if (element.has("BID_SIZE")) {
          binance_data.bid_volume = std::stod(element.getValue("BID_SIZE"));
        }
        updated = true;
      }
      if (element.has("ASK_PRICE")) {
        binance_data.ask = std::stod(element.getValue("ASK_PRICE"));
        if (element.has("ASK_SIZE")) {
          binance_data.ask_volume = std::stod(element.getValue("ASK_SIZE"));
        }
        updated = true;
      }
    }

    if (updated) {
      binance_data.last_update = std::chrono::system_clock::now();
      binance_data.has_data = true;
      calculateAndPrint();
    }
  }

  void updateBybitData(const Message &message) {
    bool updated = false;
    for (const auto &element : message.getElementList()) {
      if (element.has("BID_PRICE")) {
        bybit_data.bid = std::stod(element.getValue("BID_PRICE"));
        if (element.has("BID_SIZE")) {
          bybit_data.bid_volume = std::stod(element.getValue("BID_SIZE"));
        }
        updated = true;
      }
      if (element.has("ASK_PRICE")) {
        bybit_data.ask = std::stod(element.getValue("ASK_PRICE"));
        if (element.has("ASK_SIZE")) {
          bybit_data.ask_volume = std::stod(element.getValue("ASK_SIZE"));
        }
        updated = true;
      }
    }

    if (updated) {
      bybit_data.last_update = std::chrono::system_clock::now();
      bybit_data.has_data = true;
      calculateAndPrint();
    }
  }
};

class MyEventHandler : public EventHandler {
private:
  ArbitrageMonitor &monitor;

public:
  MyEventHandler(ArbitrageMonitor &mon) : monitor(mon) {}

  void processEvent(const Event &event, Session *session) override {
    if (event.getType() == Event::Type::SUBSCRIPTION_DATA) {
      for (const auto &message : event.getMessageList()) {
        std::string exchange = message.getCorrelationIdList().at(0);

        if (exchange == "binance") {
          monitor.updateBinanceData(message);
        } else if (exchange == "bybit") {
          monitor.updateBybitData(message);
        }
      }
    } else if (event.getType() == Event::Type::SUBSCRIPTION_STATUS) {
      std::cout << "Subscription Status: " << event.toPrettyString()
                << std::endl;
    }
  }
};

Logger *Logger::logger = nullptr; // This line is needed.

} // namespace ccapi

int main(int argc, char** argv) {
  using namespace ccapi;
  std::cout << "BTC/USDT Arbitrage Monitor - Real-time Data Stream"
            << std::endl;
  std::cout << "Monitoring Binance vs Bybit for arbitrage opportunities..."
            << std::endl;
  std::cout << std::endl;

  ArbitrageMonitor monitor;
  monitor.printHeader();

  // Initialize CCAPI Session
  SessionOptions sessionOptions;
  SessionConfigs sessionConfigs;
  MyEventHandler eventHandler(monitor);
  Session session(sessionOptions, sessionConfigs, &eventHandler);

  // Load config (required)
  SimpleConfig cfg;
  std::string cfgPath = RovoConfig::resolveConfigPathFromArgs(argc, argv);
  if (!cfg.loadFromFile(cfgPath)) {
    std::cerr << "Missing config.txt" << std::endl;
    return 1;
  }
  double cfg_min_price_diff = cfg.requireDouble("arb_min_price_diff");
  double cfg_profit_threshold = cfg.requireDouble("arb_profit_threshold");
  std::string symbol = cfg.requireString("arb_symbol");
  monitor.setConfig(cfg_min_price_diff, cfg_profit_threshold);

  // Subscribe to Binance BTC/USDT (or symbol from config)
  Subscription subscriptionBinance("binance", symbol, "MARKET_DEPTH", "",
                                   "binance");
  session.subscribe(subscriptionBinance);

  // Subscribe to Bybit BTC/USDT (or symbol from config)
  Subscription subscriptionBybit("bybit", symbol, "MARKET_DEPTH", "",
                                 "bybit");
  session.subscribe(subscriptionBybit);

  std::cout << "Connecting to exchanges..." << std::endl;
  std::cout << "Press Ctrl+C to stop..." << std::endl;

  // Keep the program running
  std::this_thread::sleep_for(
      std::chrono::seconds(std::numeric_limits<int>::max()));

  return 0;
}
