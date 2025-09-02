#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <WiFi.h>
#include <vector>
#include <functional>

class WiFiConfig {
public:
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        ERROR
    };

    using ConnectionCallback = std::function<void(ConnectionState)>;

    static WiFiConfig& getInstance();

    // Configuration methods
    void setCredentials(const char* ssid, const char* password);
    void addNetwork(const char* ssid, const char* password);
    void setConnectionTimeout(uint32_t timeoutMs = 30000);
    void setRetryInterval(uint32_t intervalMs = 10000);
    
    // Connection management
    bool begin();
    void update();
    void disconnect();
    bool reconnect();
    
    // Status methods
    bool isConnected() const;
    ConnectionState getState() const;
    String getIP() const;
    int8_t getRSSI() const;
    
    // Callback registration
    void onConnectionChange(ConnectionCallback callback);
    
    // Advanced configuration
    void setTxPower(wifi_power_t power = WIFI_POWER_19_5dBm);
    void enableAutoReconnect(bool enable = true);
    void setHostname(const char* hostname);

private:
    WiFiConfig();
    WiFiConfig(const WiFiConfig&) = delete;
    WiFiConfig& operator=(const WiFiConfig&) = delete;

    struct Network {
        String ssid;
        String password;
    };

    std::vector<Network> networks_;
    std::vector<ConnectionCallback> callbacks_;
    
    ConnectionState state_ = ConnectionState::DISCONNECTED;
    uint32_t lastAttemptMillis_ = 0;
    uint32_t connectionTimeout_ = 30000;
    uint32_t retryInterval_ = 10000;
    bool autoReconnect_ = true;
    wifi_power_t txPower_ = WIFI_POWER_19_5dBm;
    String hostname_;

    void changeState(ConnectionState newState);
    void notifyCallbacks(ConnectionState state);
    bool attemptConnection();
};

#endif