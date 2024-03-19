#include <StreamDeckSDK/ESDBasePlugin.h>

#include <map>
#include <mutex>
#include <set>
#include <string>

using json = nlohmann::json;

// Name of the status file we are monitoring.
constexpr char StatusFileName[] = "talon-status";

// Information on a visible key handled by this plugin.
struct KeyInfo {
  std::string action;
  std::string deviceId;
  std::string monitorValue;
  std::string pressAction;
  int row = -1;
  int column = -1;
};

// Stream Deck plugin. Public methods must be thread-safe.
class MyStreamDeckPlugin : public ESDBasePlugin {
 public:
  MyStreamDeckPlugin();
  virtual ~MyStreamDeckPlugin();

  // Update the stored status from the status file on disk.
  void UpdateStatus();

  // Stream Deck event handlers.
  void KeyDownForAction(const std::string &inAction, const std::string &inContext, const json &inPayload,
                        const std::string &inDeviceID) override;
  void KeyUpForAction(const std::string &inAction, const std::string &inContext, const json &inPayload,
                      const std::string &inDeviceID) override;

  void WillAppearForAction(const std::string &inAction, const std::string &inContext, const json &inPayload,
                           const std::string &inDeviceID) override;
  void WillDisappearForAction(const std::string &inAction, const std::string &inContext, const json &inPayload,
                              const std::string &inDeviceID) override;

  void DeviceDidConnect(const std::string &inDeviceID, const json &inDeviceInfo) override;
  void DeviceDidDisconnect(const std::string &inDeviceID) override;

  void SendToPlugin(const std::string &inAction, const std::string &inContext, const json &inPayload,
                    const std::string &inDeviceID) override;

 private:
  void UpdateTimer();

  // Clear the current status. Must NOT be called with mutex acquired.
  void ClearStatus();

  // Update key states. Must be called with mutex acquired.
  void UpdateKeys();

  // Helper functions to update different action types. Must be called with mutex acquired.
  void UpdateSpeechStatusKey(const std::string &context, const KeyInfo &keyInfo);
  void UpdateTagStatusKey(const std::string &context, const KeyInfo &keyInfo);
  void UpdateModeStatusKey(const std::string &context, const KeyInfo &keyInfo);
  void UpdateAppStatusKey(const std::string &context, const KeyInfo &keyInfo);

  // Mutex for access to all other data members.
  std::mutex _mutex;

  // Current status read from file.
  std::set<std::string> _modes;
  std::set<std::string> _tags;
  std::set<std::string> _apps;

  // Info on visible keys keyed by their context.
  std::map<std::string, KeyInfo> _keysByContext;
};
