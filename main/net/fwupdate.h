#pragma once
#include <stddef.h>
#include <stdint.h>

#include "esp_ota_ops.h"

// Firmware updates: the app image writer shared by the web upload and the
// self-update, and the self-update itself. The miner asks GitHub which release is
// the latest (every 6 h, and on request), and installs its board's file from it
// when the user presses Update now, or by itself when auto-update is on.
namespace fwupdate {

// Streams an app image into the next OTA partition. Checks the ESP image magic and
// the AxeForge board tag on the way: an image for another board, or one without a
// tag, is refused before it is ever set to boot.
class Writer {
  public:
    ~Writer();
    bool begin();                        // false: no OTA partition, or begin failed
    bool feed(const char *data, int n);  // false: stop; error() says why
    bool finish();                       // validate, then set it to boot
    void abort();
    const char *error() const { return m_err; }
    bool wrong_board() const { return m_wrong; }
    const char *found_board() const { return m_other; }
    const esp_partition_t *partition() const { return m_part; }

  private:
    static const int KEEP = 48;  // a tag split across two chunks is still found
    const esp_partition_t *m_part = nullptr;
    esp_ota_handle_t m_h = 0;
    bool m_open = false, m_first = true, m_wrong = false;
    char *m_scan = nullptr;
    int m_carry = 0;
    char m_other[24] = "";
    const char *m_err = "";
};

enum class State : uint8_t { Idle, Checking, Downloading, Failed };

struct Info {
    char latest[24];   // newest release tag on GitHub ("v1.4.8"), empty until checked
    bool newer;        // newer than this firmware
    State state;
    uint8_t pct;       // download progress
    char msg[96];      // last result, for the web UI
};

void start();             // background task; after Wi-Fi is up
const Info &info();
const char *state_name(State s);
void check_now();         // ask GitHub again, soon
bool install_now();       // false: busy, or nothing newer to install

} // namespace fwupdate
