#include "ccapi_cpp/ccapi_session.h"
#include "simple_config.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace ccapi {

Logger *Logger::logger = nullptr; // This line is needed.

class MyEventHandler : public EventHandler {
private:
  // Store recent trade data for analysis
  struct TradeData {
    double price;
    double volume;
    std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>
        timestamp;
  };

  std::deque<TradeData> recent_trades;
  size_t trade_count = 0; // Counter for total trades processed

  // Adaptive thresholds based on market conditions
  double large_trade_threshold = 1.0; // Will be updated based on market data
  const double VOLATILITY_THRESHOLD = 0.02; // 2% volatility
  double price_movement_threshold =
      100.0; // Will be updated based on market data

  // EWMA Volatility parameters
  const double LAMBDA =
      0.92; // Decay factor (0.92 good for high-freq crypto data)
  bool ewma_initialized = false;
  double ewma_variance = 0.0;
  double previous_price = 0.0;

  // Moving average based thresholds (multipliers of average)
  const double TRADE_SIZE_MULTIPLIER = 3.0;      // 3x average trade size
  const double PRICE_DEVIATION_MULTIPLIER = 2.5; // 2.5x average price deviation

  // Window sizes for calculations
  const size_t VOLATILITY_WINDOW_SIZE = 20;
  const size_t AVERAGE_WINDOW_SIZE = 50;
  const size_t MIN_TRADES_FOR_ANALYSIS = 10;

public:
  void processEvent(const Event &event, Session *session) override {
    if (event.getType() == Event::Type::SUBSCRIPTION_STATUS) {
      std::cout << "Received an event of type SUBSCRIPTION_STATUS:\n" +
                       event.toPrettyString(2, 2)
                << std::endl;
    } else if (event.getType() == Event::Type::SUBSCRIPTION_DATA) {
      for (const auto &message : event.getMessageList()) {
        // Process trade data
        if (message.getType() == Message::Type::MARKET_DATA_EVENTS_TRADE) {
          for (const auto &element : message.getElementList()) {
            const std::map<std::string_view, std::string> &elementNameValueMap =
                element.getNameValueMap();

            // Extract price and quantity from trade
            auto priceIt = elementNameValueMap.find("LAST_PRICE");
            auto quantityIt = elementNameValueMap.find("LAST_SIZE");

            if (priceIt == elementNameValueMap.end() ||
                quantityIt == elementNameValueMap.end()) {
              std::cout << "Error: Missing required fields. Available fields: ";
              for (const auto &pair : elementNameValueMap) {
                std::cout << pair.first << " ";
              }
              std::cout << std::endl;
              continue;
            }

            double price = std::stod(priceIt->second);
            double quantity = std::stod(quantityIt->second);

            // Create trade data entry
            TradeData trade_data;
            trade_data.price = price;
            trade_data.volume = quantity;
            trade_data.timestamp = message.getTime();

            // Add to our recent trades store
            recent_trades.push_back(trade_data);
            trade_count++;

            // Update EWMA volatility with new trade
            updateEWMAVolatility(price);

            // Maintain window size for analysis
            if (recent_trades.size() > AVERAGE_WINDOW_SIZE) {
              recent_trades.pop_front();
            }

            // Update adaptive thresholds based on recent data
            updateAdaptiveThresholds();

            // Perform anomaly detection
            bool price_anomaly = detectPriceAnomaly(price);
            bool size_anomaly = detectSizeAnomaly(quantity);
            bool volatility_anomaly = detectVolatilityAnomaly();

            // Print current trade information with anomaly flags
            std::cout << "Trade #" << trade_count << " | Price: $" << std::fixed
                      << std::setprecision(2) << price
                      << " | Size: " << std::fixed << std::setprecision(4)
                      << quantity << " BTC"
                      << " | Price Anomaly: "
                      << (price_anomaly ? "true" : "false")
                      << " | Size Anomaly: "
                      << (size_anomaly ? "true" : "false")
                      << " | Volatility Anomaly: "
                      << (volatility_anomaly ? "true" : "false")
                      << " | Time: " << getFormattedTimestamp(message.getTime())
                      << std::endl;

            // Print statistics every 20 and 50 trades
            if (trade_count == 20 || trade_count == 50 ||
                (trade_count > 50 && trade_count % 50 == 0)) {
              printStatistics();
            }
          }
        }
      }
    }
  }

private:
  // Helper function to format timestamp
  template <typename Duration>
  std::string
  getFormattedTimestamp(const std::chrono::time_point<std::chrono::system_clock,
                                                      Duration> &time_point) {
    auto converted_time_point =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            time_point);
    auto time_t = std::chrono::system_clock::to_time_t(converted_time_point);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  time_point.time_since_epoch()) %
              1000;

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return ss.str();
  }

  // Detect price movement anomalies
  bool detectPriceAnomaly(double current_price) {
    if (recent_trades.size() < 2) {
      return false; // Need at least 2 trades to detect price movements
    }

    // Calculate average price deviation
    double avg_price_deviation = calculateAveragePriceDeviation();
    if (avg_price_deviation <= 0) {
      return false;
    }

    // Get previous price
    double previous_price = recent_trades[recent_trades.size() - 2].price;
    double price_change = std::abs(current_price - previous_price);

    // Check both absolute and relative thresholds
    bool absolute_anomaly = price_change > price_movement_threshold;
    bool relative_anomaly =
        price_change > (avg_price_deviation * PRICE_DEVIATION_MULTIPLIER);

    return absolute_anomaly || relative_anomaly;
  }

  // Detect trade size anomalies
  bool detectSizeAnomaly(double current_volume) {
    if (recent_trades.size() < MIN_TRADES_FOR_ANALYSIS) {
      // For early trades, use absolute threshold only
      return current_volume > large_trade_threshold;
    }

    // Calculate average trade size
    double avg_trade_size = calculateAverageTradeSize();
    if (avg_trade_size <= 0) {
      return current_volume > large_trade_threshold;
    }

    // Check both absolute and relative thresholds
    bool absolute_anomaly = current_volume > large_trade_threshold;
    bool relative_anomaly =
        current_volume > (avg_trade_size * TRADE_SIZE_MULTIPLIER);

    return absolute_anomaly || relative_anomaly;
  }

  // Detect volatility anomalies using EWMA
  bool detectVolatilityAnomaly() {
    if (!ewma_initialized) {
      return false; // Need EWMA to be initialized
    }

    double current_ewma_volatility = std::sqrt(ewma_variance);
    return current_ewma_volatility > VOLATILITY_THRESHOLD;
  }

  // Calculate average trade size
  double calculateAverageTradeSize() {
    if (recent_trades.empty()) {
      return 0.0;
    }

    double sum_volumes = 0.0;
    for (const auto &trade : recent_trades) {
      sum_volumes += trade.volume;
    }
    return sum_volumes / recent_trades.size();
  }

  // Calculate average price deviation
  double calculateAveragePriceDeviation() {
    if (recent_trades.size() < 2) {
      return 0.0;
    }

    double sum_deviations = 0.0;
    for (size_t i = 1; i < recent_trades.size(); ++i) {
      sum_deviations +=
          std::abs(recent_trades[i].price - recent_trades[i - 1].price);
    }
    return sum_deviations / (recent_trades.size() - 1);
  }

  // Calculate average price
  double calculateAveragePrice() {
    if (recent_trades.empty()) {
      return 0.0;
    }

    double sum_prices = 0.0;
    for (const auto &trade : recent_trades) {
      sum_prices += trade.price;
    }
    return sum_prices / recent_trades.size();
  }

  // Update EWMA volatility with new price
  void updateEWMAVolatility(double current_price) {
    if (!ewma_initialized) {
      // Initialize with first price
      previous_price = current_price;
      ewma_variance = 0.0001; // Small initial variance (1% daily vol squared)
      ewma_initialized = true;
      return;
    }

    // Calculate return (log return)
    double return_value = std::log(current_price / previous_price);

    // Update EWMA variance: σ²(t) = λ × σ²(t-1) + (1-λ) × r²(t-1)
    ewma_variance =
        LAMBDA * ewma_variance + (1.0 - LAMBDA) * (return_value * return_value);

    // Update previous price for next iteration
    previous_price = current_price;
  }

  // Get current EWMA volatility (standard deviation)
  double getEWMAVolatility() {
    if (!ewma_initialized) {
      return 0.0;
    }
    return std::sqrt(ewma_variance);
  }
  void updateAdaptiveThresholds() {
    if (recent_trades.size() < MIN_TRADES_FOR_ANALYSIS) {
      return;
    }

    // Calculate percentiles for adaptive thresholds
    std::vector<double> volumes, price_changes;
    for (const auto &trade : recent_trades) {
      volumes.push_back(trade.volume);
    }

    for (size_t i = 1; i < recent_trades.size(); ++i) {
      price_changes.push_back(
          std::abs(recent_trades[i].price - recent_trades[i - 1].price));
    }

    if (!volumes.empty()) {
      std::sort(volumes.begin(), volumes.end());
      // Set threshold at 90th percentile
      size_t idx = static_cast<size_t>(volumes.size() * 0.9);
      large_trade_threshold =
          std::max(1.0, volumes[std::min(idx, volumes.size() - 1)]);
    }

    if (!price_changes.empty()) {
      std::sort(price_changes.begin(), price_changes.end());
      // Set threshold at 95th percentile
      size_t idx = static_cast<size_t>(price_changes.size() * 0.95);
      price_movement_threshold = std::max(
          10.0, price_changes[std::min(idx, price_changes.size() - 1)]);
    }
  }

  // Calculate volatility using EWMA method
  double calculateVolatility() { return getEWMAVolatility(); }

  // Print statistics summary
  void printStatistics() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "STATISTICS AFTER " << trade_count << " TRADES:" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    double avg_price = calculateAveragePrice();
    double avg_trade_size = calculateAverageTradeSize();
    double current_volatility = calculateVolatility();

    std::cout << "Average Price: $" << std::fixed << std::setprecision(2)
              << avg_price << std::endl;
    std::cout << "Average Trade Size: " << std::fixed << std::setprecision(4)
              << avg_trade_size << " BTC" << std::endl;
    std::cout << "EWMA Volatility: " << std::fixed << std::setprecision(4)
              << (current_volatility * 100) << "%" << std::endl;
    std::cout << "EWMA Variance: " << std::fixed << std::setprecision(8)
              << ewma_variance << std::endl;

    // Additional statistics
    std::cout << "Current Thresholds:" << std::endl;
    std::cout << "  - Large Trade Threshold: " << std::fixed
              << std::setprecision(4) << large_trade_threshold << " BTC"
              << std::endl;
    std::cout << "  - Price Movement Threshold: $" << std::fixed
              << std::setprecision(2) << price_movement_threshold << std::endl;
    std::cout << "  - Volatility Threshold: " << std::fixed
              << std::setprecision(2) << (VOLATILITY_THRESHOLD * 100) << "%"
              << std::endl;

    std::cout << "Data Window Size: " << recent_trades.size() << " trades"
              << std::endl;
    std::cout << std::string(80, '=') << std::endl << std::endl;
  }
};

} /* namespace ccapi */

using ::ccapi::MyEventHandler;
using ::ccapi::Session;
using ::ccapi::SessionConfigs;
using ::ccapi::SessionOptions;
using ::ccapi::Subscription;

int main(int argc, char **argv) {
  std::string cfgPath = RovoConfig::resolveConfigPathFromArgs(argc, argv);
  SimpleConfig cfg;
  if (!cfg.loadFromFile(cfgPath)) {
    std::cerr << "Missing config file: " << cfgPath << std::endl;
    return 1;
  }
  std::string exchange = cfg.getString("trades_exchange", "binance");
  std::string symbol = cfg.getString("trades_symbol", "BTCUSDT");
  std::string channel = cfg.getString("trades_channel", "TRADE");

  std::cout << "Starting Binance Large Trade/Volatility Monitor..."
            << std::endl;

  // Create session configuration
  SessionOptions sessionOptions;
  SessionConfigs sessionConfigs;

  // Create our custom event handler
  MyEventHandler eventHandler;

  // Create the session
  Session session(sessionOptions, sessionConfigs, &eventHandler);

  // Create subscription from config
  Subscription subscription(exchange, symbol, channel);

  std::cout << "Subscribing to " << exchange << " " << symbol << " " << channel << "..." << std::endl;

  try {
    // Start the subscription
    session.subscribe(subscription);

    // Keep the program running to receive data
    std::cout << "Monitoring for large trades, volatility spikes, and price "
                 "movements... (Press Ctrl+C to exit)"
              << std::endl;

    // Run indefinitely
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Stop the session (this part won't be reached in normal operation)
    session.stop();

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Bye" << std::endl;
  return EXIT_SUCCESS;
}
