#include "MyStreamDeckPlugin.h"

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <CoreServices/CoreServices.h>
#include <StreamDeckSDK/ESDConnectionManager.h>
#include <stdio.h>

#include <atomic>
#include <filesystem>
#include <fstream>

// Execute an action in the Talon REPL. Spawns a Talon REPL and pipes `action` into it.
// Does not check if the action was executed successfully.
void ExecuteTalonReplAction(const std::string &action) {
  constexpr char ReplPath[] = "~/.talon/.venv/bin/repl";

  FILE *fp = popen(ReplPath, "w");
  if (fp == NULL) {
    std::cout << "Failed to launch REPL\n";
    return;
  }

  if (fputs(action.c_str(), fp) < 0) {
    std::cout << "Error writing to REPL\n";
    return;
  }

  if (pclose(fp) == -1) {
    std::cout << "Failed to close REPL\n";
  }
}

// Simulate pressing the given key.
void SimulateKeypress(CGKeyCode keycode, bool shift = false, bool command = false, bool control = false,
                      bool alt = false) {
  CGEventFlags flags = 0;
  if (command) flags |= kCGEventFlagMaskCommand;
  if (control) flags |= kCGEventFlagMaskControl;
  if (alt) flags |= kCGEventFlagMaskAlternate;
  if (shift) flags |= kCGEventFlagMaskShift;

  CGEventRef eventKeyDown = CGEventCreateKeyboardEvent(NULL, keycode, true);
  CGEventRef eventKeyUp = CGEventCreateKeyboardEvent(NULL, keycode, false);
  CGEventSetFlags(eventKeyDown, flags);
  CGEventSetFlags(eventKeyUp, flags);

  CGEventPost(kCGHIDEventTap, eventKeyDown);
  CGEventPost(kCGHIDEventTap, eventKeyUp);

  CFRelease(eventKeyDown);
  CFRelease(eventKeyUp);
}

MyStreamDeckPlugin::MyStreamDeckPlugin() {}

MyStreamDeckPlugin::~MyStreamDeckPlugin() {}

void MyStreamDeckPlugin::KeyDownForAction(const std::string &inAction, const std::string &inContext,
                                          const json &inPayload, const std::string &inDeviceID) {
  // Nothing to do
}

void MyStreamDeckPlugin::KeyUpForAction(const std::string &inAction, const std::string &inContext,
                                        const json &inPayload, const std::string &inDeviceID) {
  // Get information for the pressed key.
  auto keyInfoIt = _keysByContext.find(inContext);
  if (keyInfoIt == _keysByContext.end()) {
    // Could not find entry for this key.
    return;
  }

  // Execute REPL action if available.
  const auto &action = keyInfoIt->second.pressAction;
  if (!action.empty()) {
    ExecuteTalonReplAction(action);
  }

  // Previous action for toggling speech status (simulating pressing Shift+F13) is commented below.
  // NOTE: cmd-shift-f17 is reserved for VS Code command server usage.
  // if (inAction == "com.talon.speech.speechstatus") {
  //   SimulateKeypress(kVK_F13, /*shift=*/true);
  //   return;
  // }
}

void MyStreamDeckPlugin::WillAppearForAction(const std::string &inAction, const std::string &inContext,
                                             const json &inPayload, const std::string &inDeviceID) {
  const std::lock_guard<std::mutex> guard(_mutex);

  // Get key information.
  KeyInfo key;
  key.action = inAction;
  key.deviceId = inDeviceID;

  // Get coords if present in payload.
  if (inPayload.contains("coordinates") && inPayload["coordinates"].contains("column") &&
      inPayload["coordinates"].contains("row")) {
    key.column = inPayload["coordinates"]["column"].get<int>();
    key.row = inPayload["coordinates"]["row"].get<int>();
  }

  // Get settings if present in payload.
  if (inPayload.contains("settings")) {
    if (inPayload["settings"].contains("monitorValue")) {
      key.monitorValue = inPayload["settings"]["monitorValue"].get<std::string>();
    }
    if (inPayload["settings"].contains("pressAction")) {
      key.pressAction = inPayload["settings"]["pressAction"].get<std::string>();
    }
  }
  // Remember the key by context.
  _keysByContext[inContext] = key;

  // Update keys.
  UpdateKeys();
}

void MyStreamDeckPlugin::WillDisappearForAction(const std::string &inAction, const std::string &inContext,
                                                const json &inPayload, const std::string &inDeviceID) {
  const std::lock_guard<std::mutex> guard(_mutex);

  // Remove this key by context.
  _keysByContext.erase(inContext);

  // Update keys.
  UpdateKeys();
}

void MyStreamDeckPlugin::DeviceDidConnect(const std::string &inDeviceID, const json &inDeviceInfo) {
  // Nothing to do
}

void MyStreamDeckPlugin::DeviceDidDisconnect(const std::string &inDeviceID) {
  // Nothing to do
}

void MyStreamDeckPlugin::SendToPlugin(const std::string &inAction, const std::string &inContext, const json &inPayload,
                                      const std::string &inDeviceID) {
  // Nothing to do
}

void MyStreamDeckPlugin::ClearStatus() {
  const std::lock_guard<std::mutex> guard(_mutex);
  _modes.clear();
  _tags.clear();
  _apps.clear();
}

void MyStreamDeckPlugin::UpdateStatus() {
  // Get full file path.
  std::string filePath = getenv("TMPDIR");
  if (!filePath.empty() && filePath[filePath.length() - 1] != '/') {
    filePath += "/";
  }
  filePath += StatusFileName;

  // Try to open the file and check if it succeeded.
  std::cout << "Reading status file: " << filePath << "\n";
  std::ifstream file(filePath);
  if (!file.good()) {
    std::cerr << "Unable to open status file.\n";
    ClearStatus();
    return;
  }

  // Read the file.
  std::string readLine;
  std::vector<std::string> lines;
  while (std::getline(file, readLine)) {
    lines.push_back(readLine);
  }

  // Make sure the file is terminated properly.
  if (lines.empty()) {
    std::cerr << "Status file is empty.\n";
    ClearStatus();
    return;
  }
  if (lines.back() != "end") {
    std::cerr << "Status file not properly terminated. Last line: " << lines.back() << "\n";
    ClearStatus();
    return;
  }

  // Remove terminator line.
  lines.erase(lines.end() - 1);

  // Parse status lines.
  std::set<std::string> modes;
  std::set<std::string> tags;
  std::set<std::string> apps;
  for (const auto &line : lines) {
    auto sepIndex = line.find(" ");

    // Make sure a space separator was found and is not the first or last character in the line.
    if (sepIndex <= 0 || sepIndex >= line.length() - 1 || sepIndex == std::string::npos) {
      std::cerr << "Badly formatted line: " << line << "\n";
      ClearStatus();
      return;
    }

    std::string entryType = line.substr(0, sepIndex);
    std::string entryValue = line.substr(sepIndex + 1);

    if (entryType == "mode") {
      modes.insert(entryValue);
    } else if (entryType == "tag") {
      tags.insert(entryValue);
    } else if (entryType == "app") {
      apps.insert(entryValue);
    } else {
      std::cerr << "Unrecognized entry type: " << line << "\n";
      ClearStatus();
      return;
    }
  }

  // Update stored status and keys.
  {
    const std::lock_guard<std::mutex> guard(_mutex);
    _modes = std::move(modes);
    _tags = std::move(tags);
    _apps = std::move(apps);
    UpdateKeys();
  }

  std::cout << "Successfully read status file.\n";
}

// Note: Must be called with mutex acquired.
void MyStreamDeckPlugin::UpdateKeys() {
  // Do nothing if we are not connected or there are no active keys.
  if (mConnectionManager == nullptr || _keysByContext.empty()) {
    return;
  }

  for (const auto &[context, keyInfo] : _keysByContext) {
    if (keyInfo.action == "com.talon.speech.speechstatus") {
      UpdateSpeechStatusKey(context, keyInfo);
    } else if (keyInfo.action == "com.talon.speech.tagstatus") {
      UpdateTagStatusKey(context, keyInfo);
    } else if (keyInfo.action == "com.talon.speech.modestatus") {
      UpdateModeStatusKey(context, keyInfo);
    } else if (keyInfo.action == "com.talon.speech.appstatus") {
      UpdateAppStatusKey(context, keyInfo);
    } else {
      // Unknown action type.
      mConnectionManager->SetTitle("Unk Action", context, kESDSDKTarget_HardwareAndSoftware);
    }
  }
}

void MyStreamDeckPlugin::UpdateSpeechStatusKey(const std::string &context, const KeyInfo &keyInfo) {
  // Check if the speech system is active.
  bool speechSleep = _modes.find("sleep") != _modes.end();
  bool speechCommand = _modes.find("command") != _modes.end();

  if (speechCommand) {
    mConnectionManager->SetState(0, context);
  } else if (speechSleep) {
    mConnectionManager->SetState(1, context);
  } else {
    mConnectionManager->SetState(1, context);
    mConnectionManager->SetTitle("No Status", context, kESDSDKTarget_HardwareAndSoftware);
  }
}

void MyStreamDeckPlugin::UpdateTagStatusKey(const std::string &context, const KeyInfo &keyInfo) {
  // Make sure we have a tag.
  if (keyInfo.monitorValue.empty()) {
    mConnectionManager->SetState(1, context);
    mConnectionManager->SetTitle("No Tag", context, kESDSDKTarget_HardwareAndSoftware);
  }

  bool tagActive = _tags.find(keyInfo.monitorValue) != _tags.end();
  mConnectionManager->SetState(tagActive ? 0 : 1, context);
}

void MyStreamDeckPlugin::UpdateModeStatusKey(const std::string &context, const KeyInfo &keyInfo) {
  // Make sure we have a mode.
  if (keyInfo.monitorValue.empty()) {
    mConnectionManager->SetState(1, context);
    mConnectionManager->SetTitle("No Mode", context, kESDSDKTarget_HardwareAndSoftware);
  }

  bool modeActive = _modes.find(keyInfo.monitorValue) != _modes.end();
  mConnectionManager->SetState(modeActive ? 0 : 1, context);
}

void MyStreamDeckPlugin::UpdateAppStatusKey(const std::string &context, const KeyInfo &keyInfo) {
  // Make sure we have an app.
  if (keyInfo.monitorValue.empty()) {
    mConnectionManager->SetState(1, context);
    mConnectionManager->SetTitle("No App", context, kESDSDKTarget_HardwareAndSoftware);
  }

  bool appActive = _apps.find(keyInfo.monitorValue) != _apps.end();
  mConnectionManager->SetState(appActive ? 0 : 1, context);
}
