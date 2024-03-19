#include <CoreServices/CoreServices.h>
#include <StreamDeckSDK/ESDLogger.h>
#include <StreamDeckSDK/ESDMain.h>
#include <stdio.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "MyStreamDeckPlugin.h"

// Plugin instance.
MyStreamDeckPlugin _plugin;

// Checks if `haystack` ends with `needle`.
bool EndsWith(const std::string &haystack, const std::string &needle) {
  if (haystack.length() < needle.length()) {
    return false;
  }
  return haystack.compare(haystack.length() - needle.length(), needle.length(), needle) == 0;
}

// File system events callback.
// flags are unsigned long, IDs are uint64_t.
void FileSystemEventsCallback(ConstFSEventStreamRef streamRef, void *clientCallBackInfo, size_t numEvents,
                              void *eventPaths, const FSEventStreamEventFlags eventFlags[],
                              const FSEventStreamEventId eventIds[]) {
  char **paths = (char **)eventPaths;
  std::string fileSuffix = std::string("/") + StatusFileName;

  // Check if the status file was modified.
  bool found = false;
  for (int i = 0; i < numEvents; i++) {
    // Get path.
    std::string path(paths[i]);

    // Check if this is our status file.
    // TODO: We can also check if the file was modified using (eventFlags[i] & kFSEventStreamEventFlagItemModified).
    // Might not be necessary, since we probably care about most event types (modified, created, deleted, renamed,
    // etc.).
    if (EndsWith(path, fileSuffix)) {
      std::cout << "Talon status file modified. Path: " << path << "\n";
      found = true;
      break;
    }
  }
  if (!found) {
    return;
  }

  // Update the plugin's status.
  _plugin.UpdateStatus();
}

// Initialization and run loop for file system monitor thread.
void FileSystemMonitorRunLoop() {
  CFStringRef mypath = CFStringCreateWithCString(NULL, getenv("TMPDIR"), kCFStringEncodingUTF8);
  CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);

  FSEventStreamContext *callbackInfo = NULL;
  CFAbsoluteTime latencySeconds = 0.1;

  FSEventStreamRef stream =
      FSEventStreamCreate(NULL, &FileSystemEventsCallback, callbackInfo, pathsToWatch, kFSEventStreamEventIdSinceNow,
                          latencySeconds, kFSEventStreamCreateFlagFileEvents);

  FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  FSEventStreamStart(stream);
  CFRunLoopRun();
}

int main(int argc, const char **argv) {
  ESDLogger::Get()->SetWin32DebugPrefix("[esdtalon] ");
  std::cout << "Temp Dir: " << getenv("TMPDIR") << "\n";

  // Initialize the plugin's status from file (if it exists).
  _plugin.UpdateStatus();

  // Spawn a thread to monitor changes to the status file.
  std::thread thr(FileSystemMonitorRunLoop);

  // Run Stream Deck plugin.
  return esd_main(argc, argv, &_plugin);
}
