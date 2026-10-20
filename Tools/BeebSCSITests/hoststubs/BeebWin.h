#pragma once
struct LEDType { bool HDisc[4]; };
extern LEDType LEDs;
class BeebWin { public: const char *GetUserDataPath() const { return "/tmp/hatest-card"; } const char *GetAppPath() const { return "/nonexistent"; } };
extern BeebWin *mainWin;
