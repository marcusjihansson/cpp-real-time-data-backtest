#include "ccapi_cpp/ccapi_session.h"
#include "simple_config.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace ccapi { Logger* Logger::logger = nullptr; }

// Load environment variables
std::map<std::string, std::string> loadEnv() {
  std::map<std::string, std::string> env;
  std::ifstream file(".env");
  std::string line;

  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    size_t pos = line.find('=');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1);
      env[key] = value;
    }
  }
  return env;
}

// Mathematical functions for Black-Scholes
class BlackScholesCalculator {
private:
  // Cumulative standard normal distribution
  static double normalCDF(double x) {
    return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
  }

  // Standard normal probability density function
  static double normalPDF(double x) {
    return (1.0 / std::sqrt(2.0 * M_PI)) * std::exp(-0.5 * x * x);
  }

  // Calculate d1 parameter
  static double calculateD1(double S, double K, double T, double r,
                            double sigma) {
    return (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) /
           (sigma * std::sqrt(T));
  }

  // Calculate d2 parameter
  static double calculateD2(double d1, double sigma, double T) {
    return d1 - sigma * std::sqrt(T);
  }

public:
  struct OptionData {
    double spot_price;     // Current price of underlying
    double strike_price;   // Strike price
    double time_to_expiry; // Time to expiration in years
    double risk_free_rate; // Risk-free interest rate
    double volatility;     // Implied volatility
    bool is_call;          // true for call, false for put

    // Market data
    double option_price;
    double volume;
    double open_interest;
  };

  struct Greeks {
    double delta;
    double gamma;
    double theta;
    double vega;
    double rho;
    double intrinsic_value;
    double extrinsic_value;
    double implied_volatility;
  };

  static Greeks calculateGreeks(const OptionData &data) {
    Greeks greeks;

    double S = data.spot_price;
    double K = data.strike_price;
    double T = data.time_to_expiry;
    double r = data.risk_free_rate;
    double sigma = data.volatility;

    double d1 = calculateD1(S, K, T, r, sigma);
    double d2 = calculateD2(d1, sigma, T);

    double Nd1 = normalCDF(d1);
    double Nd2 = normalCDF(d2);
    double nd1 = normalPDF(d1);

    if (data.is_call) {
      // Call option Greeks
      greeks.delta = Nd1;
      greeks.gamma = nd1 / (S * sigma * std::sqrt(T));
      greeks.theta = -(S * nd1 * sigma) / (2 * std::sqrt(T)) -
                     r * K * std::exp(-r * T) * Nd2;
      greeks.vega = S * nd1 * std::sqrt(T);
      greeks.rho = K * T * std::exp(-r * T) * Nd2;

      greeks.intrinsic_value = std::max(0.0, S - K);
    } else {
      // Put option Greeks
      greeks.delta = Nd1 - 1.0;
      greeks.gamma = nd1 / (S * sigma * std::sqrt(T));
      greeks.theta = -(S * nd1 * sigma) / (2 * std::sqrt(T)) +
                     r * K * std::exp(-r * T) * normalCDF(-d2);
      greeks.vega = S * nd1 * std::sqrt(T);
      greeks.rho = -K * T * std::exp(-r * T) * normalCDF(-d2);

      greeks.intrinsic_value = std::max(0.0, K - S);
    }

    // Convert theta to per day (divide by 365)
    greeks.theta /= 365.0;

    // Convert vega to per 1% change in volatility (divide by 100)
    greeks.vega /= 100.0;

    // Convert rho to per 1% change in interest rate (divide by 100)
    greeks.rho /= 100.0;

    greeks.extrinsic_value = data.option_price - greeks.intrinsic_value;
    greeks.implied_volatility = data.volatility;

    return greeks;
  }

  static void printGreeks(const std::string &symbol, const OptionData &data,
                          const Greeks &greeks) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "OPTIONS ANALYSIS FOR: " << symbol << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    std::cout << std::fixed << std::setprecision(4);

    std::cout << "\nMARKET DATA:" << std::endl;
    std::cout << "  Spot Price:        $" << data.spot_price << std::endl;
    std::cout << "  Strike Price:      $" << data.strike_price << std::endl;
    std::cout << "  Option Price:      $" << data.option_price << std::endl;
    std::cout << "  Time to Expiry:    " << data.time_to_expiry << " years"
              << std::endl;
    std::cout << "  Volume:            " << data.volume << std::endl;
    std::cout << "  Open Interest:     " << data.open_interest << std::endl;

    std::cout << "\nOPTION VALUES:" << std::endl;
    std::cout << "  Intrinsic Value:   $" << greeks.intrinsic_value
              << std::endl;
    std::cout << "  Extrinsic Value:   $" << greeks.extrinsic_value
              << std::endl;
    std::cout << "  Implied Volatility: " << (greeks.implied_volatility * 100)
              << "%" << std::endl;

    std::cout << "\nTHE GREEKS:" << std::endl;
    std::cout << "  Delta (Δ):         " << greeks.delta << std::endl;
    std::cout << "  Gamma (Γ):         " << greeks.gamma << std::endl;
    std::cout << "  Theta (Θ):         $" << greeks.theta << " per day"
              << std::endl;
    std::cout << "  Vega (ν):          $" << greeks.vega << " per 1% IV"
              << std::endl;
    std::cout << "  Rho (ρ):           $" << greeks.rho << " per 1% rate"
              << std::endl;

    std::cout << "\nGREEKS INTERPRETATION:" << std::endl;
    std::cout << "  Delta: Option price changes by $" << std::abs(greeks.delta)
              << " for each $1 move in underlying" << std::endl;
    std::cout << "  Gamma: Delta changes by " << greeks.gamma
              << " for each $1 move in underlying" << std::endl;
    std::cout << "  Theta: Option loses $" << std::abs(greeks.theta)
              << " in value each day (time decay)" << std::endl;
    std::cout << "  Vega: Option price changes by $" << std::abs(greeks.vega)
              << " for each 1% change in volatility" << std::endl;
  }
};

namespace ccapi {

class OptionsEventHandler : public EventHandler {
private:
  std::map<std::string, std::string> env_vars;
  double risk_free_rate_ = 0.0;
  double days_to_expiry_ = 0.0;

public:
  explicit OptionsEventHandler(double risk_free_rate, double default_days_to_expiry)
      : risk_free_rate_(risk_free_rate), days_to_expiry_(default_days_to_expiry) {
    env_vars = loadEnv();
  }

  void processEvent(const Event &event, Session *session) override {
    if (event.getType() == Event::Type::SUBSCRIPTION_DATA) {
      for (const auto &message : event.getMessageList()) {
        processOptionData(message);
      }
    }
  }

private:
  void processOptionData(const Message &message) {
    std::cout << "\n" << std::string(50, '-') << std::endl;
    std::cout << "Received market data for: BTCUSDT" << std::endl;

    // Extract price data
    double current_price = 0.0;
    for (const auto &element : message.getElementList()) {
      const std::map<std::string_view, std::string> &elementNameValueMap =
          element.getNameValueMap();
      for (const auto &pair : elementNameValueMap) {
        if (pair.first == "LAST_PRICE" || pair.first == "BID_PRICE_0") {
          current_price = std::stod(pair.second);
          break;
        }
      }
      if (current_price > 0)
        break;
    }

    if (current_price > 0) {
      // Example: Calculate Greeks for a BTC option
      // In practice, you'd get this data from Binance options API
      calculateExampleGreeks("BTCUSDT", current_price);
    }
  }

  void calculateExampleGreeks(const std::string &instrument,
                              double spot_price) {
    // Example option data - in practice, get this from Binance options API
    BlackScholesCalculator::OptionData option_data;
    option_data.spot_price = spot_price;
    option_data.strike_price = spot_price * 1.05; // 5% OTM call
    option_data.time_to_expiry = days_to_expiry_ / 365.0;    // days from config
    option_data.risk_free_rate = risk_free_rate_;               // risk-free from config
    option_data.volatility =
        0.80;                   // 80% implied volatility (typical for crypto)
    option_data.is_call = true; // Call option
    option_data.option_price = spot_price * 0.02; // Example option price
    option_data.volume = 1500;                    // Example volume
    option_data.open_interest = 5000;             // Example open interest

    BlackScholesCalculator::Greeks greeks =
        BlackScholesCalculator::calculateGreeks(option_data);
    BlackScholesCalculator::printGreeks(instrument + " Call Option",
                                        option_data, greeks);

    // Also calculate for a put option
    option_data.is_call = false;
    option_data.strike_price = spot_price * 0.95;  // 5% OTM put
    option_data.option_price = spot_price * 0.015; // Example put price
    option_data.time_to_expiry = days_to_expiry_ / 365.0; // days from config
    option_data.risk_free_rate = risk_free_rate_;            // risk-free from config

    BlackScholesCalculator::Greeks put_greeks =
        BlackScholesCalculator::calculateGreeks(option_data);
    BlackScholesCalculator::printGreeks(instrument + " Put Option", option_data,
                                        put_greeks);
  }
};

} // namespace ccapi

int main(int argc, char** argv) {
  using namespace ccapi;

  std::cout << "Starting Options Greeks Calculator..." << std::endl;

  // Load environment variables
  auto env = loadEnv();

  // Load optional config
  std::string cfgPath = RovoConfig::resolveConfigPathFromArgs(argc, argv);
  SimpleConfig cfg;
  if (!cfg.loadFromFile(cfgPath)) {
    std::cerr << "Missing config.txt" << std::endl;
    return 1;
  }
  double cfg_r = cfg.requireDouble("risk_free_rate");
  double cfg_days = cfg.requireDouble("default_days_to_expiry");

  // Create session configuration with API credentials
  SessionOptions sessionOptions;
  SessionConfigs sessionConfigs;

  // Set API credentials if available
  if (env.find("BINANCE_API_KEY") != env.end()) {
    sessionConfigs.setCredential(
        {{"BINANCE_API_KEY", env["BINANCE_API_KEY"]},
         {"BINANCE_API_SECRET", env["BINANCE_API_SECRET"]}});
    std::cout << "API credentials loaded successfully." << std::endl;
  } else {
    std::cout << "Warning: No API credentials found in .env file." << std::endl;
    std::cout << "Add BINANCE_API_KEY and BINANCE_API_SECRET to .env for full "
                 "functionality."
              << std::endl;
  }

  OptionsEventHandler eventHandler(cfg_r, cfg_days);

  Session session(sessionOptions, sessionConfigs, &eventHandler);

  // Subscribe to BTC spot price for options calculations
  Subscription subscription("binance", "BTCUSDT", "TRADE");

  std::cout << "\nSubscribing to BTCUSDT for options Greeks calculations..."
            << std::endl;
  std::cout
      << "This will calculate theoretical Greeks based on current BTC price."
      << std::endl;
  std::cout << "\nNote: This example uses theoretical option data."
            << std::endl;
  std::cout << "For real options data, you would need to integrate with "
               "Binance Options API."
            << std::endl;

  try {
    session.subscribe(subscription);

    std::cout << "\nListening for price updates... (Press Ctrl+C to exit)"
              << std::endl;

    // Keep running for 60 seconds to see multiple calculations
    std::this_thread::sleep_for(std::chrono::seconds(60));

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "\nProgram completed." << std::endl;
  return 0;
}
