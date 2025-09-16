#include "ccapi_cpp/ccapi_session.h"
#include "simple_config.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Load environment variables
std::map<std::string, std::string> loadEnv() {
  std::map<std::string, std::string> env;
  std::ifstream file(".env");
  if (!file.is_open()) {
    std::cerr << "Warning: .env file not found" << std::endl;
    return env;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    size_t pos = line.find('=');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1);
      // Trim whitespace
      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);
      env[key] = value;
    }
  }
  return env;
}

// Trade data structure
struct Trade {
  double price;
  double amount;
  int64_t timestamp;
  std::string side; // "buy" or "sell"
  double cost;
  std::string id;

  Trade(double p, double a, int64_t ts, const std::string &s,
        const std::string &trade_id = "")
      : price(p), amount(a), timestamp(ts), side(s), cost(p * a), id(trade_id) {
    // Validate inputs
    if (price <= 0.0 || amount <= 0.0) {
      throw std::invalid_argument(
          "Invalid trade data: price and amount must be positive");
    }
  }
};

// Order book level
struct OrderBookLevel {
  double price;
  double size;

  OrderBookLevel(double p = 0.0, double s = 0.0) : price(p), size(s) {
    if (price < 0.0 || size < 0.0) {
      throw std::invalid_argument(
          "Invalid order book level: price and size must be non-negative");
    }
  }
};

// Comprehensive liquidity metrics structure
struct LiquidityMetrics {
  // Order book metrics
  double spread = 0.0;
  double relative_spread = 0.0;
  double bid_depth = 0.0;
  double ask_depth = 0.0;
  std::optional<double> order_book_imbalance = std::nullopt;

  // VWAP and slippage metrics
  std::optional<double> bid_vwap = std::nullopt;
  std::optional<double> ask_vwap = std::nullopt;
  std::optional<double> bid_slippage = std::nullopt;
  std::optional<double> ask_slippage = std::nullopt;

  // Order book slopes
  double bid_slope = 0.0;
  double ask_slope = 0.0;

  // Risk metrics
  double realized_volatility = 0.0;
  double var_95 = 0.0;
  double expected_shortfall_95 = 0.0;
  std::optional<double> historical_volatility = std::nullopt;

  // Kyle's lambda (nested structure)
  struct KylesLambda {
    double daily = 0.0;
    double hourly = 0.0;
  } kyles_lambda;

  // Amihud measures (nested structure)
  struct AmihudMeasures {
    double one_day = 0.0;
    double thirty_days = 0.0;
    double ninety_days = 0.0;
  } amihud_measures;

  // Helper method to convert to JSON-like string
  std::string toJsonString() const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(8);

    ss << "{\n";
    ss << "  \"spread\": " << spread << ",\n";
    ss << "  \"relative_spread\": " << relative_spread << ",\n";
    ss << "  \"bid_depth\": " << bid_depth << ",\n";
    ss << "  \"ask_depth\": " << ask_depth << ",\n";
    ss << "  \"order_book_imbalance\": ";
    if (order_book_imbalance.has_value()) {
      ss << order_book_imbalance.value();
    } else {
      ss << "null";
    }
    ss << ",\n";

    ss << "  \"bid_vwap\": ";
    if (bid_vwap.has_value()) {
      ss << bid_vwap.value();
    } else {
      ss << "null";
    }
    ss << ",\n";

    ss << "  \"ask_vwap\": ";
    if (ask_vwap.has_value()) {
      ss << ask_vwap.value();
    } else {
      ss << "null";
    }
    ss << ",\n";

    ss << "  \"bid_slippage\": ";
    if (bid_slippage.has_value()) {
      ss << bid_slippage.value();
    } else {
      ss << "null";
    }
    ss << ",\n";

    ss << "  \"ask_slippage\": ";
    if (ask_slippage.has_value()) {
      ss << ask_slippage.value();
    } else {
      ss << "null";
    }
    ss << ",\n";

    ss << "  \"bid_slope\": " << bid_slope << ",\n";
    ss << "  \"ask_slope\": " << ask_slope << ",\n";
    ss << "  \"realized_volatility\": " << realized_volatility << ",\n";
    ss << "  \"var_95\": " << var_95 << ",\n";
    ss << "  \"expected_shortfall_95\": " << expected_shortfall_95 << ",\n";

    ss << "  \"historical_volatility\": ";
    if (historical_volatility.has_value()) {
      ss << historical_volatility.value();
    } else {
      ss << "null";
    }
    ss << ",\n";

    ss << "  \"kyles_lambda\": {\n";
    ss << "    \"daily\": " << kyles_lambda.daily << ",\n";
    ss << "    \"hourly\": " << kyles_lambda.hourly << "\n";
    ss << "  },\n";
    ss << "  \"amihud_measures\": {\n";
    ss << "    \"1_day\": " << amihud_measures.one_day << ",\n";
    ss << "    \"30_days\": " << amihud_measures.thirty_days << ",\n";
    ss << "    \"90_days\": " << amihud_measures.ninety_days << "\n";
    ss << "  }\n";
    ss << "}";

    return ss.str();
  }
};

class LiquidityAnalyzer {
private:
  std::deque<Trade> trade_history;
  std::vector<OrderBookLevel> current_bids;
  std::vector<OrderBookLevel> current_asks;
  std::vector<double> price_history;
  std::map<std::string, std::string> env_vars;
  mutable std::mutex data_mutex; // Thread safety

  static constexpr int MAX_TRADE_HISTORY = 10000;
  static constexpr int64_t HOUR_IN_MS = 3600000LL;
  static constexpr int64_t DAY_IN_MS = 86400000LL;

public:
  LiquidityAnalyzer() : env_vars(loadEnv()) {}

  void addTrade(const Trade &trade) {
    std::lock_guard<std::mutex> lock(data_mutex);

    trade_history.push_back(trade);
    if (trade_history.size() > MAX_TRADE_HISTORY) {
      trade_history.pop_front();
    }

    price_history.push_back(trade.price);
    if (price_history.size() > MAX_TRADE_HISTORY) {
      price_history.erase(price_history.begin());
    }
  }

  void updateOrderBook(const std::vector<OrderBookLevel> &bids,
                       const std::vector<OrderBookLevel> &asks) {
    std::lock_guard<std::mutex> lock(data_mutex);

    current_bids = bids;
    current_asks = asks;

    // Remove invalid levels and sort
    current_bids.erase(std::remove_if(current_bids.begin(), current_bids.end(),
                                      [](const OrderBookLevel &level) {
                                        return level.price <= 0.0 ||
                                               level.size <= 0.0;
                                      }),
                       current_bids.end());

    current_asks.erase(std::remove_if(current_asks.begin(), current_asks.end(),
                                      [](const OrderBookLevel &level) {
                                        return level.price <= 0.0 ||
                                               level.size <= 0.0;
                                      }),
                       current_asks.end());

    // Sort to ensure best prices first
    std::sort(current_bids.begin(), current_bids.end(),
              [](const OrderBookLevel &a, const OrderBookLevel &b) {
                return a.price > b.price; // Highest bid first
              });

    std::sort(current_asks.begin(), current_asks.end(),
              [](const OrderBookLevel &a, const OrderBookLevel &b) {
                return a.price < b.price; // Lowest ask first
              });
  }

  // Calculate Kyle's Lambda measure of market impact
  double calculateKylesLambda(int64_t time_window_ms = DAY_IN_MS) const {
    std::lock_guard<std::mutex> lock(data_mutex);

    if (trade_history.size() < 2) {
      return 0.0;
    }

    std::vector<double> log_returns;
    std::vector<double> signed_volumes;

    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    for (size_t i = 1; i < trade_history.size(); ++i) {
      const auto &prev_trade = trade_history[i - 1];
      const auto &curr_trade = trade_history[i];

      // Filter by time window
      if (current_time - curr_trade.timestamp > time_window_ms) {
        continue;
      }

      // Calculate log return with safety checks
      if (prev_trade.price > 0 && curr_trade.price > 0) {
        double log_return = std::log(curr_trade.price / prev_trade.price);

        // Filter out extreme values that might be errors
        if (std::isfinite(log_return) && std::abs(log_return) < 1.0) {
          log_returns.push_back(log_return);

          // Calculate signed volume
          double multiplier = (curr_trade.side == "buy")    ? 1.0
                              : (curr_trade.side == "sell") ? -1.0
                                                            : 0.0;
          signed_volumes.push_back(curr_trade.amount * multiplier);
        }
      }
    }

    if (log_returns.size() < 2) {
      return 0.0;
    }

    // Linear regression to find lambda
    return calculateLinearRegression(signed_volumes, log_returns);
  }

  // Calculate Amihud's illiquidity measure
  double calculateAmihudMeasure(int period_days = 30) const {
    std::lock_guard<std::mutex> lock(data_mutex);

    if (trade_history.size() < 2) {
      return 0.0;
    }

    std::map<int64_t, std::pair<double, double>>
        daily_data; // day -> (total_return, total_volume)

    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    int64_t period_ms = static_cast<int64_t>(period_days) * DAY_IN_MS;

    for (size_t i = 1; i < trade_history.size(); ++i) {
      const auto &prev_trade = trade_history[i - 1];
      const auto &curr_trade = trade_history[i];

      if (current_time - curr_trade.timestamp > period_ms) {
        continue;
      }

      int64_t day = curr_trade.timestamp / DAY_IN_MS;
      int64_t prev_day = prev_trade.timestamp / DAY_IN_MS;

      if (day == prev_day && prev_trade.price > 0) {
        double return_i =
            std::abs(curr_trade.price - prev_trade.price) / prev_trade.price;
        double volume = curr_trade.amount * curr_trade.price;

        if (std::isfinite(return_i) && std::isfinite(volume) && volume > 0) {
          daily_data[day].first += return_i;
          daily_data[day].second += volume;
        }
      }
    }

    if (daily_data.empty()) {
      return 0.0;
    }

    double total_amihud = 0.0;
    int valid_days = 0;

    for (const auto &[day, data] : daily_data) {
      if (data.second > 0) { // Volume > 0
        double amihud_day = data.first / data.second;
        if (std::isfinite(amihud_day)) {
          total_amihud += amihud_day;
          valid_days++;
        }
      }
    }

    return valid_days > 0 ? total_amihud / valid_days : 0.0;
  }

  // Calculate risk metrics from price data
  void calculateRiskMetrics(LiquidityMetrics &metrics) const {
    std::lock_guard<std::mutex> lock(data_mutex);

    if (price_history.size() < 2) {
      return;
    }

    std::vector<double> returns;
    for (size_t i = 1; i < price_history.size(); ++i) {
      if (price_history[i - 1] > 0 && price_history[i] > 0) {
        double return_val = std::log(price_history[i] / price_history[i - 1]);
        if (std::isfinite(return_val)) {
          returns.push_back(return_val);
        }
      }
    }

    if (returns.empty()) {
      return;
    }

    // Calculate mean and standard deviation
    double mean_return =
        std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();

    double variance = 0.0;
    for (double ret : returns) {
      variance += (ret - mean_return) * (ret - mean_return);
    }
    variance /= std::max(
        1.0, static_cast<double>(returns.size() - 1)); // Sample variance

    // Annualized volatility (assuming 24h trading)
    if (variance >= 0 && std::isfinite(variance)) {
      metrics.realized_volatility = std::sqrt(variance * 365 * 24) * 100;
    }

    // Value at Risk (5th percentile)
    std::vector<double> sorted_returns = returns;
    std::sort(sorted_returns.begin(), sorted_returns.end());
    size_t var_index =
        static_cast<size_t>(std::ceil(sorted_returns.size() * 0.05));
    var_index = std::min(var_index, sorted_returns.size() - 1);

    if (var_index < sorted_returns.size()) {
      metrics.var_95 = sorted_returns[var_index] * 100;
    }

    // Expected Shortfall (mean of worst 5%)
    if (var_index > 0) {
      double es_sum = 0.0;
      for (size_t i = 0; i < var_index; ++i) {
        es_sum += sorted_returns[i];
      }
      metrics.expected_shortfall_95 = (es_sum / var_index) * 100;
    }

    // Rolling historical volatility (last 30 periods)
    size_t window_size = std::min(static_cast<size_t>(30), returns.size());
    if (window_size > 1) {
      auto start_it = returns.end() - static_cast<std::ptrdiff_t>(window_size);
      double window_mean =
          std::accumulate(start_it, returns.end(), 0.0) / window_size;

      double window_variance = 0.0;
      for (auto it = start_it; it != returns.end(); ++it) {
        window_variance += (*it - window_mean) * (*it - window_mean);
      }
      window_variance /= std::max(1.0, static_cast<double>(window_size - 1));

      if (window_variance >= 0 && std::isfinite(window_variance)) {
        metrics.historical_volatility =
            std::sqrt(window_variance * 365 * 24) * 100;
      }
    }
  }

  // Analyze order book liquidity
  void analyzeOrderBookLiquidity(LiquidityMetrics &metrics, int depth = 10,
                                 double sample_volume = 1.0) const {
    std::lock_guard<std::mutex> lock(data_mutex);

    if (current_bids.empty() || current_asks.empty()) {
      return;
    }

    double best_bid = current_bids[0].price;
    double best_ask = current_asks[0].price;

    // Basic spread metrics
    metrics.spread = best_ask - best_bid;
    double mid_price = (best_ask + best_bid) / 2.0;
    if (mid_price > 0) {
      metrics.relative_spread = metrics.spread / mid_price;
    }

    // Depth metrics
    size_t bid_depth_count =
        std::min(static_cast<size_t>(depth), current_bids.size());
    size_t ask_depth_count =
        std::min(static_cast<size_t>(depth), current_asks.size());

    metrics.bid_depth = 0.0;
    metrics.ask_depth = 0.0;

    for (size_t i = 0; i < bid_depth_count; ++i) {
      metrics.bid_depth += current_bids[i].size;
    }

    for (size_t i = 0; i < ask_depth_count; ++i) {
      metrics.ask_depth += current_asks[i].size;
    }

    // Order book imbalance
    if (metrics.bid_depth + metrics.ask_depth > 0) {
      metrics.order_book_imbalance = (metrics.bid_depth - metrics.ask_depth) /
                                     (metrics.bid_depth + metrics.ask_depth);
    }

    // VWAP calculation
    double bid_vwap = calculateVWAP(current_bids, sample_volume);
    double ask_vwap = calculateVWAP(current_asks, sample_volume);

    if (bid_vwap > 0) {
      metrics.bid_vwap = bid_vwap;
      metrics.bid_slippage = (best_bid - bid_vwap) / best_bid;
    }

    if (ask_vwap > 0) {
      metrics.ask_vwap = ask_vwap;
      metrics.ask_slippage = (ask_vwap - best_ask) / best_ask;
    }

    // Order book slopes
    metrics.bid_slope = calculateOrderBookSlope(current_bids, depth);
    metrics.ask_slope = calculateOrderBookSlope(current_asks, depth);
  }

  // Comprehensive analysis
  LiquidityMetrics performComprehensiveAnalysis() const {
    LiquidityMetrics metrics;

    // Calculate risk metrics
    calculateRiskMetrics(metrics);

    // Calculate order book metrics
    analyzeOrderBookLiquidity(metrics);

    // Calculate Kyle's lambda for both timeframes
    metrics.kyles_lambda.daily = calculateKylesLambda(DAY_IN_MS);
    metrics.kyles_lambda.hourly = calculateKylesLambda(HOUR_IN_MS);

    // Calculate Amihud measures for all periods
    metrics.amihud_measures.one_day = calculateAmihudMeasure(1);
    metrics.amihud_measures.thirty_days = calculateAmihudMeasure(30);
    metrics.amihud_measures.ninety_days = calculateAmihudMeasure(90);

    return metrics;
  }

  // Print comprehensive analysis results
  void printAnalysis(const std::string &symbol,
                     const LiquidityMetrics &metrics) const {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "COMPREHENSIVE LIQUIDITY ANALYSIS FOR: " << symbol
              << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "\nORDER BOOK METRICS:" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    std::cout << "  Spread:                $" << std::setprecision(2)
              << metrics.spread << std::endl;
    std::cout << "  Relative Spread:       " << std::setprecision(4)
              << (metrics.relative_spread * 100) << "%" << std::endl;
    std::cout << "  Bid Depth:             " << std::setprecision(2)
              << metrics.bid_depth << std::endl;
    std::cout << "  Ask Depth:             " << std::setprecision(2)
              << metrics.ask_depth << std::endl;
    std::cout << "  Order Book Imbalance:  " << std::setprecision(4);
    if (metrics.order_book_imbalance.has_value()) {
      std::cout << metrics.order_book_imbalance.value();
    } else {
      std::cout << "N/A";
    }
    std::cout << std::endl;

    std::cout << "\nVWAP & SLIPPAGE ANALYSIS:" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    std::cout << "  Bid VWAP:              $" << std::setprecision(2);
    if (metrics.bid_vwap.has_value()) {
      std::cout << metrics.bid_vwap.value();
    } else {
      std::cout << "N/A";
    }
    std::cout << std::endl;

    std::cout << "  Ask VWAP:              $" << std::setprecision(2);
    if (metrics.ask_vwap.has_value()) {
      std::cout << metrics.ask_vwap.value();
    } else {
      std::cout << "N/A";
    }
    std::cout << std::endl;

    std::cout << "  Bid Slippage:          " << std::setprecision(4);
    if (metrics.bid_slippage.has_value()) {
      std::cout << (metrics.bid_slippage.value() * 100) << "%";
    } else {
      std::cout << "N/A";
    }
    std::cout << std::endl;

    std::cout << "  Ask Slippage:          " << std::setprecision(4);
    if (metrics.ask_slippage.has_value()) {
      std::cout << (metrics.ask_slippage.value() * 100) << "%";
    } else {
      std::cout << "N/A";
    }
    std::cout << std::endl;

    std::cout << "  Bid Slope:             " << std::setprecision(6)
              << metrics.bid_slope << std::endl;
    std::cout << "  Ask Slope:             " << std::setprecision(6)
              << metrics.ask_slope << std::endl;

    std::cout << "\nMARKET MICROSTRUCTURE:" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    std::cout << "  Kyle's Lambda:" << std::endl;
    std::cout << "    Daily:               " << std::setprecision(8)
              << metrics.kyles_lambda.daily << std::endl;
    std::cout << "    Hourly:              " << std::setprecision(8)
              << metrics.kyles_lambda.hourly << std::endl;
    std::cout << "  Amihud Measures:" << std::endl;
    std::cout << "    1 Day:               " << std::setprecision(8)
              << metrics.amihud_measures.one_day << std::endl;
    std::cout << "    30 Days:             " << std::setprecision(8)
              << metrics.amihud_measures.thirty_days << std::endl;
    std::cout << "    90 Days:             " << std::setprecision(8)
              << metrics.amihud_measures.ninety_days << std::endl;

    std::cout << "\nRISK METRICS:" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    std::cout << "  Realized Volatility:   " << std::setprecision(2)
              << metrics.realized_volatility << "%" << std::endl;
    std::cout << "  Historical Volatility: ";
    if (metrics.historical_volatility.has_value()) {
      std::cout << std::setprecision(2) << metrics.historical_volatility.value()
                << "%";
    } else {
      std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "  VaR (95%):             " << std::setprecision(4)
              << metrics.var_95 << "%" << std::endl;
    std::cout << "  Expected Shortfall:    " << std::setprecision(4)
              << metrics.expected_shortfall_95 << "%" << std::endl;

    std::cout << std::string(80, '=') << std::endl;
  }

  size_t getTradeHistorySize() const {
    std::lock_guard<std::mutex> lock(data_mutex);
    return trade_history.size();
  }

private:
  // Helper function for linear regression
  double calculateLinearRegression(const std::vector<double> &x,
                                   const std::vector<double> &y) const {
    if (x.size() != y.size() || x.size() < 2) {
      return 0.0;
    }

    double x_mean = std::accumulate(x.begin(), x.end(), 0.0) / x.size();
    double y_mean = std::accumulate(y.begin(), y.end(), 0.0) / y.size();

    double numerator = 0.0;
    double denominator = 0.0;

    for (size_t i = 0; i < x.size(); ++i) {
      numerator += (x[i] - x_mean) * (y[i] - y_mean);
      denominator += (x[i] - x_mean) * (x[i] - x_mean);
    }

    return (denominator != 0.0 && std::isfinite(numerator) &&
            std::isfinite(denominator))
               ? numerator / denominator
               : 0.0;
  }

  // Calculate VWAP for given volume
  double calculateVWAP(const std::vector<OrderBookLevel> &orders,
                       double target_volume) const {
    if (orders.empty() || target_volume <= 0.0) {
      return 0.0;
    }

    double cumulative_volume = 0.0;
    double weighted_sum = 0.0;

    for (const auto &order : orders) {
      if (cumulative_volume >= target_volume) {
        break;
      }

      double volume = std::min(order.size, target_volume - cumulative_volume);
      if (volume > 0) {
        weighted_sum += order.price * volume;
        cumulative_volume += volume;
      }
    }

    return cumulative_volume > 0 ? weighted_sum / cumulative_volume : 0.0;
  }

  // Calculate order book slope
  double calculateOrderBookSlope(const std::vector<OrderBookLevel> &orders,
                                 int depth) const {
    size_t actual_depth = std::min(static_cast<size_t>(depth), orders.size());
    if (actual_depth < 2) {
      return 0.0;
    }

    std::vector<double> prices;
    std::vector<double> cumulative_volumes;
    double cumulative = 0.0;

    for (size_t i = 0; i < actual_depth; ++i) {
      cumulative += orders[i].size;
      prices.push_back(orders[i].price);
      cumulative_volumes.push_back(cumulative);
    }

    return calculateLinearRegression(cumulative_volumes, prices);
  }
};

namespace ccapi {

class LiquidityEventHandler : public EventHandler {
private:
  LiquidityAnalyzer analyzer;
  std::string current_symbol;
  std::atomic<int> trade_count{0};

public:
  void processEvent(const Event &event, Session *session) override {
    try {
      std::cout << "Received event type: " << static_cast<int>(event.getType())
                << std::endl;
      if (event.getType() == Event::Type::SUBSCRIPTION_DATA) {
        std::cout << "Processing " << event.getMessageList().size()
                  << " messages" << std::endl;
        for (const auto &message : event.getMessageList()) {
          processMarketData(message);
        }
      } else if (event.getType() == Event::Type::SUBSCRIPTION_STATUS) {
        std::cout << "Subscription status event received" << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "Error processing event: " << e.what() << std::endl;
    }
  }

private:
  void processMarketData(const Message &message) {
    current_symbol = "BTCUSDT"; // Fixed symbol

    static int message_count = 0;
    message_count++;

    // Only print detailed info every 50 messages to avoid spam
    if (message_count % 50 == 1) {
      std::cout << "\n=== Processing Message #" << message_count
                << " ===" << std::endl;
    }

    // Determine message type based on content structure
    bool hasTrade = false;
    bool hasOrderBook = false;

    for (const auto &element : message.getElementList()) {
      const std::map<std::string_view, std::string> &elementNameValueMap =
          element.getNameValueMap();
      for (const auto &pair : elementNameValueMap) {
        if (message_count % 50 == 1) {
          std::cout << "  " << pair.first << " = " << pair.second << std::endl;
        }
        if (pair.first == "LAST_PRICE" || pair.first == "LAST_SIZE") {
          hasTrade = true;
        } else if (pair.first.find("BID_PRICE") != std::string_view::npos ||
                   pair.first.find("ASK_PRICE") != std::string_view::npos) {
          hasOrderBook = true;
        }
      }
    }

    if (hasTrade) {
      if (message_count % 50 == 1)
        std::cout << "Processing as TRADE data" << std::endl;
      processTrade(message);
    } else if (hasOrderBook) {
      if (message_count % 50 == 1)
        std::cout << "Processing as ORDER BOOK data" << std::endl;
      processOrderBook(message);
    } else {
      if (message_count % 50 == 1)
        std::cout << "Unknown message type" << std::endl;
    }
  }

  void processTrade(const Message &message) {
    try {
      double price = 0.0;
      double amount = 0.0;
      std::string side = "unknown";
      int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                              message.getTime().time_since_epoch())
                              .count();

      for (const auto &element : message.getElementList()) {
        const std::map<std::string_view, std::string> &elementNameValueMap =
            element.getNameValueMap();
        for (const auto &pair : elementNameValueMap) {
          try {
            if (pair.first == "LAST_PRICE") {
              price = std::stod(pair.second);
            } else if (pair.first == "LAST_SIZE") {
              amount = std::stod(pair.second);
            } else if (pair.first == "IS_BUYER_MAKER") {
              side = (pair.second == "1") ? "sell" : "buy";
            }
          } catch (const std::exception &e) {
            std::cerr << "Error parsing trade field " << pair.first << ": "
                      << e.what() << std::endl;
            continue;
          }
        }
      }

      if (price > 0 && amount > 0) {
        Trade trade(price, amount, timestamp, side);
        analyzer.addTrade(trade);

        // Perform analysis every 100 trades
        int current_count = trade_count.fetch_add(1) + 1;
        if (current_count % 100 == 0) {
          performAndPrintAnalysis();
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error processing trade: " << e.what() << std::endl;
    }
  }

  void processOrderBook(const Message &message) {
    try {
      std::vector<OrderBookLevel> bids, asks;

      for (const auto &element : message.getElementList()) {
        const std::map<std::string_view, std::string> &elementNameValueMap =
            element.getNameValueMap();
        for (const auto &pair : elementNameValueMap) {
          try {
            std::string name(pair.first);

            if (name.find("BID_PRICE_") == 0) {
              std::string level_str = name.substr(10);
              size_t level = std::stoul(level_str);
              if (bids.size() <= level) {
                bids.resize(level + 1, OrderBookLevel(0, 0));
              }
              bids[level].price = std::stod(pair.second);
            } else if (name.find("BID_SIZE_") == 0) {
              std::string level_str = name.substr(9);
              size_t level = std::stoul(level_str);
              if (bids.size() <= level) {
                bids.resize(level + 1, OrderBookLevel(0, 0));
              }
              bids[level].size = std::stod(pair.second);
            } else if (name.find("ASK_PRICE_") == 0) {
              std::string level_str = name.substr(10);
              size_t level = std::stoul(level_str);
              if (asks.size() <= level) {
                asks.resize(level + 1, OrderBookLevel(0, 0));
              }
              asks[level].price = std::stod(pair.second);
            } else if (name.find("ASK_SIZE_") == 0) {
              std::string level_str = name.substr(9);
              size_t level = std::stoul(level_str);
              if (asks.size() <= level) {
                asks.resize(level + 1, OrderBookLevel(0, 0));
              }
              asks[level].size = std::stod(pair.second);
            }
          } catch (const std::exception &e) {
            std::cerr << "Error parsing order book field " << pair.first << ": "
                      << e.what() << std::endl;
            continue;
          }
        }
      }

      analyzer.updateOrderBook(bids, asks);
    } catch (const std::exception &e) {
      std::cerr << "Error processing order book: " << e.what() << std::endl;
    }
  }

  void performAndPrintAnalysis() {
    try {
      auto metrics = analyzer.performComprehensiveAnalysis();
      analyzer.printAnalysis(current_symbol, metrics);

      // Print JSON for easy integration
      std::cout << "\nJSON OUTPUT:" << std::endl;
      std::cout << std::string(40, '-') << std::endl;
      std::cout << metrics.toJsonString() << std::endl;

    } catch (const std::exception &e) {
      std::cerr << "Error performing analysis: " << e.what() << std::endl;
    }
  }
};

Logger *Logger::logger = nullptr; // This line is needed.

} // namespace ccapi

int main(int argc, char** argv) {
  using namespace ccapi;

  std::cout << "Starting Comprehensive Liquidity Analyzer..." << std::endl;
  std::cout << "All Python metrics are ensured to be included:" << std::endl;
  std::cout << "✓ Order book metrics (spread, depth, imbalance, slopes)"
            << std::endl;
  std::cout << "✓ VWAP and slippage analysis (with null handling)" << std::endl;
  std::cout << "✓ Risk metrics (volatility, VaR, expected shortfall)"
            << std::endl;
  std::cout << "✓ Kyle's lambda (daily and hourly)" << std::endl;
  std::cout << "✓ Amihud measures (1, 30, 90 days)" << std::endl;

  try {
    // Load environment variables
    auto env = loadEnv();

    SessionOptions sessionOptions;
    SessionConfigs sessionConfigs;

    // Set API credentials if available
    if (env.find("BINANCE_API_KEY") != env.end() &&
        env.find("BINANCE_API_SECRET") != env.end()) {
      sessionConfigs.setCredential(
          {{"BINANCE_API_KEY", env["BINANCE_API_KEY"]},
           {"BINANCE_API_SECRET", env["BINANCE_API_SECRET"]}});
      std::cout << "API credentials loaded successfully." << std::endl;
    } else {
      std::cout << "No API credentials found, using public data only."
                << std::endl;
    }

    LiquidityEventHandler eventHandler;
    Session session(sessionOptions, sessionConfigs, &eventHandler);

    // Resolve config and build subscriptions
    std::string cfgPath = RovoConfig::resolveConfigPathFromArgs(argc, argv);
    SimpleConfig cfg;
    if (!cfg.loadFromFile(cfgPath)) {
      std::cerr << "Missing config file: " << cfgPath << std::endl;
      return 1;
    }
    std::string exchange = cfg.getString("liq_exchange", "binance");
    std::string symbol = cfg.getString("liq_symbol", "BTCUSDT");
    bool subscribe_trade = cfg.getInt("liq_sub_trade", 1) != 0;
    bool subscribe_orderbook = cfg.getInt("liq_sub_orderbook", 1) != 0;

    std::vector<Subscription> subscriptions;
    if (subscribe_trade) subscriptions.emplace_back(exchange, symbol, "TRADE");
    if (subscribe_orderbook) subscriptions.emplace_back(exchange, symbol, "MARKET_DEPTH");

    std::cout << "\nSubscribing to " << symbol << " on " << exchange << " (trade=" << subscribe_trade << ", orderbook=" << subscribe_orderbook << ")..."
              << std::endl;
    std::cout << "This will analyze:" << std::endl;
    std::cout << "  • Order book liquidity metrics" << std::endl;
    std::cout << "  • Kyle's lambda (market impact)" << std::endl;
    std::cout << "  • Amihud illiquidity measure" << std::endl;
    std::cout << "  • Risk and volatility metrics" << std::endl;
    std::cout << "  • VWAP and slippage analysis" << std::endl;

    for (auto &subscription : subscriptions) {
      session.subscribe(subscription);
    }

    std::cout << "\nListening for market data... (Press Ctrl+C to exit)"
              << std::endl;
    std::cout << "Analysis will be printed every 100 trades." << std::endl;
    std::cout << "JSON output included for easy integration with other systems."
              << std::endl;

    // Keep running for extended analysis
    std::this_thread::sleep_for(std::chrono::minutes(10));

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "\nLiquidity analysis completed." << std::endl;
  return 0;
}
