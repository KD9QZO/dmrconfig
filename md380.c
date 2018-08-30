/*
 * Interface to TYT MD-380.
 *
 * Copyright (C) 2018 Serge Vakulenko, KK6ABQ
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. The name of the author may not be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include "radio.h"
#include "util.h"

#define NCHAN           1000
#define NCONTACTS       1000
#define NZONES          250
#define NGLISTS         250
#define NSCANL          250
#define NMESSAGES       50

#define MEMSZ           0x40000
#define OFFSET_TIMESTMP 0x02001
#define OFFSET_SETTINGS 0x02040
#define OFFSET_MSG      0x02180
#define OFFSET_CONTACTS 0x05f80
#define OFFSET_GLISTS   0x0ec20
#define OFFSET_ZONES    0x149e0
#define OFFSET_SCANL    0x18860
#define OFFSET_CHANNELS 0x1ee00

//
// Channel data.
//
typedef struct {
    // Byte 0
    uint8_t channel_mode        : 2,    // Mode: Analog or Digital
#define MODE_ANALOG     1
#define MODE_DIGITAL    2

            bandwidth           : 2,    // Bandwidth: 12.5 or 20 or 25 kHz
#define BW_12_5_KHZ     0
#define BW_20_KHZ       1
#define BW_25_KHZ       2

            autoscan            : 1,    // Autoscan Enable
            squelch             : 1,    // Squelch
#define SQ_TIGHT        0
#define SQ_NORMAL       1

            _unused1            : 1,    // 1
            lone_worker         : 1;    // Lone Worker

    // Byte 1
    uint8_t talkaround          : 1,    // Allow Talkaround
            rx_only             : 1,    // RX Only Enable
            repeater_slot       : 2,    // Repeater Slot: 1 or 2
            colorcode           : 4;    // Color Code: 1...15

    // Byte 2
    uint8_t privacy_no          : 4,    // Privacy No. (+1): 1...16
            privacy             : 2,    // Privacy: None, Basic or Enhanced
#define PRIV_NONE       0
#define PRIV_BASIC      1
#define PRIV_ENHANCED   2

            private_call_conf   : 1,    // Private Call Confirmed
            data_call_conf      : 1;    // Data Call Confirmed

    // Byte 3
    uint8_t rx_ref_frequency    : 2,    // RX Ref Frequency: Low, Medium or High
#define REF_LOW         0
#define REF_MEDIUM      1
#define REF_HIGH        2

            _unused2            : 1,    // 0
            emergency_alarm_ack : 1,    // Emergency Alarm Ack
            _unused3            : 2,    // 0b10
            uncompressed_udp    : 1,    // Compressed UDP Data header (0) Enable, (1) Disable
            display_pttid_dis   : 1;    // Display PTT ID (inverted)

    // Byte 4
    uint8_t tx_ref_frequency    : 2,    // RX Ref Frequency: Low, Medium or High
            _unused4            : 2,    // 0b01
            vox                 : 1,    // VOX Enable
            power               : 1,    // Power: Low, High
#define POWER_HIGH      1
#define POWER_LOW       0

            admit_criteria      : 2;    // Admit Criteria: Always, Channel Free or Correct CTS/DCS
#define ADMIT_ALWAYS    0
#define ADMIT_CH_FREE   1
#define ADMIT_TONE      2
#define ADMIT_COLOR     3

    // Byte 5
    uint8_t _unused5            : 4,    // 0
            in_call_criteria    : 2,    // In Call Criteria: Always, Follow Admit Criteria or TX Interrupt
#define INCALL_ALWAYS   0
#define INCALL_ADMIT    1

            _unused6            : 2;    // 0b11

    // Bytes 6-7
    uint16_t contact_name_index;        // Contact Name: Contact1...

    // Bytes 8-9
    uint8_t tot;                        // TOT x 15sec: 0-Infinite, 1=15s... 37=555s
    uint8_t tot_rekey_delay;            // TOT Rekey Delay: 0s...255s

    // Bytes 10-11
    uint8_t emergency_system_index;     // Emergency System: None, System1...32
    uint8_t scan_list_index;            // Scan List: None, ScanList1...250

    // Bytes 12-13
    uint8_t group_list_index;           // Group List: None, GroupList1...250
    uint8_t _unused7;                   // 0

    // Bytes 14-15
    uint8_t _unused8;                   // 0
    uint8_t _unused9;                   // 0xff

    // Bytes 16-23
    uint32_t rx_frequency;              // RX Frequency: 8 digits BCD
    uint32_t tx_frequency;              // TX Frequency: 8 digits BCD

    // Bytes 24-27
    uint16_t ctcss_dcs_receive;         // CTCSS/DCS Dec: 4 digits BCD
    uint16_t ctcss_dcs_transmit;        // CTCSS/DCS Enc: 4 digits BCD

    // Bytes 28-29
    uint8_t rx_signaling_syst;          // Rx Signaling System: Off, DTMF-1...4
    uint8_t tx_signaling_syst;          // Tx Signaling System: Off, DTMF-1...4

    // Bytes 30-31
    uint8_t _unused10;                  // 0xff
    uint8_t _unused11;                  // 0xff

    // Bytes 32-63
    uint16_t name[16];                  // Channel Name (Unicode)
} channel_t;

//
// Contact data.
//
typedef struct {
    // Bytes 0-2
    uint32_t id                 : 24;   // Call ID: 1...16777215

    // Byte 3
    uint8_t type                : 2,    // Call Type: Group Call, Private Call or All Call
#define CALL_GROUP      1
#define CALL_PRIVATE    2
#define CALL_ALL        3

            _unused1            : 3,    // 0
            receive_tone        : 1,    // Call Receive Tone: No or yes
            _unused2            : 2;    // 0b11

    // Bytes 4-19
    uint16_t name[16];                  // Contact Name (Unicode)
} contact_t;

//
// Zone data.
//
typedef struct {
    // Bytes 0-31
    uint16_t name[16];                  // Zone Name (Unicode)

    // Bytes 32-63
    uint16_t member[16];                // Member: channels 1...16
} zone_t;

//
// Group list data.
//
typedef struct {
    // Bytes 0-31
    uint16_t name[16];                  // Group List Name (Unicode)

    // Bytes 32-95
    uint16_t member[32];                // Contacts
} grouplist_t;

//
// Scan list data.
//
typedef struct {
    // Bytes 0-31
    uint16_t name[16];                  // Scan List Name (Unicode)

    // Bytes 32-37
    uint16_t priority_ch1;              // Priority Channel 1 or ffff
    uint16_t priority_ch2;              // Priority Channel 2 or ffff
    uint16_t tx_designated_ch;          // Tx Designated Channel or ffff

    // Bytes 38-41
    uint8_t _unused1;                   // 0xf1
    uint8_t sign_hold_time;             // Signaling Hold Time (x25 = msec)
    uint8_t prio_sample_time;           // Priority Sample Time (x250 = msec)
    uint8_t _unused2;                   // 0xff

    // Bytes 42-103
    uint16_t member[31];                // Channels
} scanlist_t;

//
// General settings.
// TODO: verify the general settings with official CPS
//
typedef struct {

    // Bytes 0-19
    uint16_t intro_line1[10];

    // Bytes 20-39
    uint16_t intro_line2[10];

    // Bytes 40-63
    uint8_t  _unused40[24];

    // Byte 64
    uint8_t  _unused64_0                : 3,
             monitor_type               : 1,
             _unused64_4                : 1,
             disable_all_leds           : 1,
             _unused64_6                : 2;

    // Byte 65
    uint8_t  talk_permit_tone           : 2,
             pw_and_lock_enable         : 1,
             ch_free_indication_tone    : 1,
             _unused65_4                : 1,
             disable_all_tones          : 1,
             save_mode_receive          : 1,
             save_preamble              : 1;

    // Byte 66
    uint8_t  _unused66_0                : 2,
             keypad_tones               : 1,
             intro_picture              : 1,
             _unused66_4                : 4;

    // Byte 67
    uint8_t  _unused67;

    // Bytes 68-71
    uint8_t  radio_id[3];
    uint8_t  _unused71;

    // Bytes 72-84
    uint8_t  tx_preamble_duration;
    uint8_t  group_call_hang_time;
    uint8_t  private_call_hang_time;
    uint8_t  vox_sensitivity;
    uint8_t  _unused76[2];
    uint8_t  rx_low_battery_interval;
    uint8_t  call_alert_tone_duration;
    uint8_t  lone_worker_response_time;
    uint8_t  lone_worker_reminder_time;
    uint8_t  _unused82;
    uint8_t  scan_digital_hang_time;
    uint8_t  scan_analog_hang_time;

    // Byte 85
    uint8_t  _unused85_0                : 6,
             backlight_time             : 2;

    // Bytes 86-87
    uint8_t  set_keypad_lock_time;
    uint8_t  mode;

    // Bytes 88-95
    uint32_t power_on_password;
    uint32_t radio_prog_password;

    // Bytes 96-103
    uint8_t  pc_prog_password[8];

    // Bytes 104-111
    uint8_t  _unused104[8];

    // Bytes 112-143
    uint16_t radio_name[16];
} general_settings_t;

static const char *POWER_NAME[] = { "Low", "High" };
static const char *SQUELCH_NAME[] = { "Tight", "Normal" };
static const char *BANDWIDTH[] = { "12.5", "20", "25" };
static const char *CONTACT_TYPE[] = { "-", "Group", "Private", "All" };
static const char *ADMIT_NAME[] = { "-", "Free", "Tone", "Color" };
static const char *INCALL_NAME[] = { "-", "Admit", "???", "???" };

#ifdef PRINT_RARE_PARAMS
static const char *REF_FREQUENCY[] = { "Low", "Med", "High" };
static const char *PRIVACY_NAME[] = { "-", "Basic", "Enhanced" };
static const char *SIGNALING_SYSTEM[] = { "-", "DTMF-1", "DTMF-2", "DTMF-3", "DTMF-4" };
#endif

//
// Print a generic information about the device.
//
static void md380_print_version(radio_device_t *radio, FILE *out)
{
    unsigned char *timestamp = &radio_mem[OFFSET_TIMESTMP];
    static const char charmap[16] = "0123456789:;<=>?";

    if (*timestamp != 0xff) {
        fprintf(out, "Last Programmed Date: %d%d%d%d-%d%d-%d%d",
            timestamp[0] >> 4, timestamp[0] & 15, timestamp[1] >> 4, timestamp[1] & 15,
            timestamp[2] >> 4, timestamp[2] & 15, timestamp[3] >> 4, timestamp[3] & 15);
        fprintf(out, " %d%d:%d%d:%d%d\n",
            timestamp[4] >> 4, timestamp[4] & 15, timestamp[5] >> 4, timestamp[5] & 15,
            timestamp[6] >> 4, timestamp[6] & 15);
        fprintf(out, "CPS Software Version: V%c%c.%c%c\n",
            charmap[timestamp[7] & 15], charmap[timestamp[8] & 15],
            charmap[timestamp[9] & 15], charmap[timestamp[10] & 15]);
    }
}

//
// Read memory image from the device.
//
static void md380_download(radio_device_t *radio)
{
    int bno;

    for (bno=0; bno<MEMSZ/1024; bno++) {
        dfu_read_block(bno, &radio_mem[bno*1024], 1024);

        ++radio_progress;
        if (radio_progress % 32 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }
}

//
// Write memory image to the device.
//
static void md380_upload(radio_device_t *radio, int cont_flag)
{
    int bno;

    dfu_erase(MEMSZ);

    fprintf(stderr, "Sending data... ");
    fflush(stderr);
    for (bno=0; bno<MEMSZ/1024; bno++) {
        dfu_write_block(bno, &radio_mem[bno*1024], 1024);

        ++radio_progress;
        if (radio_progress % 32 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }
}

//
// Check whether the memory image is compatible with this device.
//
static int md380_is_compatible(radio_device_t *radio)
{
    return 1;
}

//
// Set the bitmask of zones for a given channel.
// Return 0 on failure.
//
static void setup_zone(int zone_index, int chan_index)
{
    uint8_t *data = &radio_mem[OFFSET_ZONES + zone_index*0x80 + chan_index/8];

    *data |= 1 << (chan_index & 7);
}

static void erase_zone(int zone_index)
{
    uint8_t *data = &radio_mem[OFFSET_ZONES + zone_index*0x80];

    memset(data, 0, 0x80);
}

//
// Check that the radio does support this frequency.
//
static int is_valid_frequency(int mhz)
{
    if (mhz >= 136 && mhz <= 174)
        return 1;
    if (mhz >= 400 && mhz <= 480)
        return 1;
    return 0;
}

//
// Set the parameters for a given memory channel.
//
static void setup_channel(int i, int mode, char *name, double rx_mhz, double tx_mhz,
    int power, int scanlist, int autoscan, int squelch, int tot, int rxonly,
    int admit, int colorcode, int timeslot, int incall, int grouplist, int contact,
    int rxtone, int txtone, int width)
{
    //TODO: always set Data Call Confirmed=1 (wait for SMS acknowledge)
    //TODO: always set talkaround=0
#if 0
    memory_channel_t *ch = i + (memory_channel_t*) &radio_mem[OFFSET_CHANNELS];

    hz_to_freq((int) (rx_mhz * 1000000.0), ch->rxfreq);

    double offset_mhz = tx_mhz - rx_mhz;
    ch->offset = 0;
    ch->txfreq[0] = ch->txfreq[1] = ch->txfreq[2] = 0;
    if (offset_mhz == 0) {
        ch->duplex = D_SIMPLEX;
    } else if (offset_mhz > 0 && offset_mhz < 256 * 0.05) {
        ch->duplex = D_POS_OFFSET;
        ch->offset = (int) (offset_mhz / 0.05 + 0.5);
    } else if (offset_mhz < 0 && offset_mhz > -256 * 0.05) {
        ch->duplex = D_NEG_OFFSET;
        ch->offset = (int) (-offset_mhz / 0.05 + 0.5);
    } else {
        ch->duplex = D_CROSS_BAND;
        hz_to_freq((int) (tx_mhz * 1000000.0), ch->txfreq);
    }
    ch->used = (rx_mhz > 0);
    ch->tmode = tmode;
    ch->power = power;
    ch->isnarrow = ! wide;
    ch->isam = isam;
    ch->step = (rx_mhz >= 400) ? STEP_12_5 : STEP_5;
    ch->_u1 = 0;
    ch->_u2 = (rx_mhz >= 400);
    ch->_u3 = 0;
    ch->_u4[0] = 15;
    ch->_u4[1] = 0;
    ch->_u5[0] = ch->_u5[1] = ch->_u5[2] = 0;

    // Scan mode.
    unsigned char *scan_data = &radio_mem[OFFSET_SCAN + i/4];
    int scan_shift = (i & 3) * 2;
    *scan_data &= ~(3 << scan_shift);
    *scan_data |= scan << scan_shift;

    encode_name(i, name);
#endif
}

//
// Erase the channel record.
//
static void erase_channel(int i)
{
    //TODO: erase channel
}

static void print_chanlist(FILE *out, uint16_t *unsorted, int nchan)
{
    int last  = -1;
    int range = 0;
    int n;
    uint16_t data[nchan];

    // Sort the list before printing.
    memcpy(data, unsorted, nchan * sizeof(uint16_t));
    qsort(data, nchan, sizeof(uint16_t), compare_index);
    for (n=0; n<=nchan; n++) {
        int cnum = data[n];

        if (cnum == 0)
            break;

        if (cnum == last+1) {
            range = 1;
        } else {
            if (range) {
                fprintf(out, "-%d", last);
                range = 0;
            }
            if (n > 0)
                fprintf(out, ",");
            fprintf(out, "%d", cnum);
        }
        last = cnum;
    }
    if (range)
        fprintf(out, "-%d", last);
}

static void print_id(FILE *out)
{
    general_settings_t *gs = (general_settings_t*) &radio_mem[OFFSET_SETTINGS];
    unsigned id = gs->radio_id[0] | (gs->radio_id[1] << 8) | (gs->radio_id[2] << 16);

    fprintf(out, "Name: ");
    if (gs->radio_name[0] != 0 && gs->radio_name[0] != 0xffff) {
        print_unicode(out, gs->radio_name, 16, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\nID: %u\n", id);
}

static void print_intro(FILE *out)
{
    general_settings_t *gs = (general_settings_t*) &radio_mem[OFFSET_SETTINGS];

    fprintf(out, "\n# Text displayed when the radio powers up.\n");
    fprintf(out, "Welcome Line 1: ");
    if (gs->intro_line1[0] != 0 && gs->intro_line1[0] != 0xffff) {
        print_unicode(out, gs->intro_line1, 10, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\nWelcome Line 2: ");
    if (gs->intro_line2[0] != 0 && gs->intro_line2[0] != 0xffff) {
        print_unicode(out, gs->intro_line2, 10, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\n");
}

//
// Do we have any channels of given mode?
//
static int have_channels(int mode)
{
    int i;

    for (i=0; i<NCHAN; i++) {
        channel_t *ch = (channel_t*) &radio_mem[OFFSET_CHANNELS + i*64];

        if (ch->name[0] != 0 && ch->channel_mode == mode)
            return 1;
    }
    return 0;
}

//
// Print base parameters of the channel:
//      Name
//      RX Frequency
//      TX Frequency
//      Power
//      Scan List
//      Squelch
//      Admit Criteria
//
static void print_chan_base(FILE *out, channel_t *ch, int cnum)
{
    fprintf(out, "%5d   ", cnum);
    print_unicode(out, ch->name, 16, 1);
    fprintf(out, " ");
    print_freq(out, ch->rx_frequency);
    fprintf(out, " ");
    print_offset(out, ch->rx_frequency, ch->tx_frequency);

    fprintf(out, "%-4s  ", POWER_NAME[ch->power]);

    if (ch->scan_list_index == 0)
        fprintf(out, "-    ");
    else
        fprintf(out, "%-4d ", ch->scan_list_index);

    fprintf(out, "%c  ", "-+"[ch->autoscan]);

    fprintf(out, "%-7s ", SQUELCH_NAME[ch->squelch]);

    if (ch->tot == 0)
        fprintf(out, "-   ");
    else
        fprintf(out, "%-3d ", ch->tot * 15);

    fprintf(out, "%c  ", "-+"[ch->rx_only]);

    fprintf(out, "%-6s ", ADMIT_NAME[ch->admit_criteria]);
}

#ifdef PRINT_RARE_PARAMS
//
// Print extended parameters of the channel:
//      TOT
//      TOT Rekey Delay
//      RX Ref Frequency
//      RX Ref Frequency
//      Autoscan
//      RX Only
//      Lone Worker
//      VOX
//
static void print_chan_ext(FILE *out, channel_t *ch)
{
    fprintf(out, "%-3d ", ch->tot_rekey_delay);
    fprintf(out, "%-5s ", REF_FREQUENCY[ch->rx_ref_frequency]);
    fprintf(out, "%-5s ", REF_FREQUENCY[ch->tx_ref_frequency]);
    fprintf(out, "%c  ", "-+"[ch->lone_worker]);
    fprintf(out, "%c   ", "-+"[ch->vox]);
    fprintf(out, "%c  ", "-+"[ch->talkaround]);
}
#endif

static void print_digital_channels(FILE *out, int verbose)
{
    int i;

    if (verbose) {
        fprintf(out, "# Table of digital channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Low\n");
        fprintf(out, "# 6) Scan list: - or index in Scanlist table\n");
        fprintf(out, "# 7) Autoscan flag: -, +\n");
        fprintf(out, "# 8) Squelch level: Normal, Tight\n");
        fprintf(out, "# 9) Transmit timeout timer in seconds: 0, 15, 30, 45... 555\n");
        fprintf(out, "# 10) Receive only: -, +\n");
        fprintf(out, "# 11) Admit criteria: -, Free, Color\n");
        fprintf(out, "# 12) Color code: 1, 2, 3... 15\n");
        fprintf(out, "# 13) Time slot: 1 or 2\n");
        fprintf(out, "# 14) In call criteria: -, Admit, TXInt\n");
        fprintf(out, "# 15) Receive group list: - or index in Grouplist table\n");
        fprintf(out, "# 16) Contact for transmit: - or index in Contacts table\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Digital Name             Receive   Transmit Power Scan AS Squelch TOT RO Admit  Color Slot InCall RxGL TxContact");
#ifdef PRINT_RARE_PARAMS
    fprintf(out, " Dly RxRef TxRef LW VOX TA EmSys Privacy  PN PCC EAA DCC CU");
#endif
    fprintf(out, "\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = (channel_t*) &radio_mem[OFFSET_CHANNELS + i*64];

        if (ch->name[0] == 0 || ch->channel_mode != MODE_DIGITAL) {
            // Select digital channels
            continue;
        }
        print_chan_base(out, ch, i+1);

        // Print digital parameters of the channel:
        //      Color Code
        //      Repeater Slot
        //      In Call Criteria
        //      Group List
        //      Contact Name
        fprintf(out, "%-5d %-3d  ", ch->colorcode, ch->repeater_slot);
        fprintf(out, "%-6s ", INCALL_NAME[ch->in_call_criteria]);

        if (ch->group_list_index == 0)
            fprintf(out, "-    ");
        else
            fprintf(out, "%-4d ", ch->group_list_index);

        if (ch->contact_name_index == 0)
            fprintf(out, "-");
        else
            fprintf(out, "%d", ch->contact_name_index);

#ifdef PRINT_RARE_PARAMS
        print_chan_ext(out, ch);

        // Extended digital parameters of the channel:
        //      Emergency System
        //      Privacy
        //      Privacy No. (+1)
        //      Private Call Confirmed
        //      Emergency Alarm Ack
        //      Data Call Confirmed
        //      DCDM switch (inverted)
        //      Leader/MS

        if (ch->emergency_system_index == 0)
            fprintf(out, "-     ");
        else
            fprintf(out, "%-5d ", ch->emergency_system_index);

        fprintf(out, "%-8s ", PRIVACY_NAME[ch->privacy]);

        if (ch->privacy == PRIV_NONE)
            fprintf(out, "-  ");
        else
            fprintf(out, "%-2d ", ch->privacy_no + 1);

        fprintf(out, "%c   ", "-+"[ch->private_call_conf]);
        fprintf(out, "%c   ", "-+"[ch->emergency_alarm_ack]);
        fprintf(out, "%c   ", "-+"[ch->data_call_conf]);
        fprintf(out, "%c   ", "+-"[ch->uncompressed_udp]);
#endif
        fprintf(out, "\n");
    }
}

static void print_analog_channels(FILE *out, int verbose)
{
    int i;

    if (verbose) {
        fprintf(out, "# Table of analog channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Low\n");
        fprintf(out, "# 6) Scan list: - or index\n");
        fprintf(out, "# 7) Autoscan flag: -, +\n");
        fprintf(out, "# 8) Squelch level: Normal, Tight\n");
        fprintf(out, "# 9) Transmit timeout timer in seconds: 0, 15, 30, 45... 555\n");
        fprintf(out, "# 10) Receive only: -, +\n");
        fprintf(out, "# 11) Admit criteria: -, Free, Tone\n");
        fprintf(out, "# 12) Guard tone for receive, or '-' to disable\n");
        fprintf(out, "# 13) Guard tone for transmit, or '-' to disable\n");
        fprintf(out, "# 14) Bandwidth in kHz: 12.5, 20, 25\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Analog  Name             Receive   Transmit Power Scan AS Squelch TOT RO Admit  RxTone TxTone Width");
#ifdef PRINT_RARE_PARAMS
    fprintf(out, " Dly RxRef TxRef LW VOX TA RxSign TxSign ID");
#endif
    fprintf(out, "\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = (channel_t*) &radio_mem[OFFSET_CHANNELS + i*64];

        if (ch->name[0] == 0 || ch->channel_mode != MODE_ANALOG) {
            // Select analog channels
            continue;
        }
        print_chan_base(out, ch, i+1);

        // Print analog parameters of the channel:
        //      CTCSS/DCS Dec
        //      CTCSS/DCS Enc
        //      Bandwidth
        print_tone(out, ch->ctcss_dcs_receive);
        fprintf(out, "  ");
        print_tone(out, ch->ctcss_dcs_transmit);
        fprintf(out, "  %s", BANDWIDTH[ch->bandwidth]);

#ifdef PRINT_RARE_PARAMS
        print_chan_ext(out, ch);

        // Extended analog parameters of the channel:
        //      Rx Signaling System
        //      Tx Signaling System
        //      Display PTT ID (inverted)
        //      Non-QT/DQT Turn-off Freq.

        fprintf(out, "%-6s ", SIGNALING_SYSTEM[ch->rx_signaling_syst]);
        fprintf(out, "%-6s ", SIGNALING_SYSTEM[ch->tx_signaling_syst]);
        fprintf(out, "%c  ", "+-"[ch->display_pttid_dis]);
#endif
        fprintf(out, "\n");
    }
}

static int have_zones()
{
    zone_t *z = (zone_t*) &radio_mem[OFFSET_ZONES];

    return z->name[0] != 0 && z->name[0] != 0xffff;
}

static int have_scanlists()
{
    scanlist_t *sl = (scanlist_t*) &radio_mem[OFFSET_SCANL];

    return sl->name[0] != 0 && sl->name[0] != 0xffff;
}

static int have_contacts()
{
    contact_t *ct = (contact_t*) &radio_mem[OFFSET_CONTACTS];

    return ct->name[0] != 0 && ct->name[0] != 0xffff;
}

static int have_grouplists()
{
    grouplist_t *gl = (grouplist_t*) &radio_mem[OFFSET_GLISTS];

    return gl->name[0] != 0 && gl->name[0] != 0xffff;
}

static int have_messages()
{
    uint16_t *msg = (uint16_t*) &radio_mem[OFFSET_MSG];

    return msg[0] != 0 && msg[0] != 0xffff;
}

//
// Print full information about the device configuration.
//
static void md380_print_config(radio_device_t *radio, FILE *out, int verbose)
{
    int i;

    fprintf(out, "Radio: %s\n", radio->name);
    print_id(out);
    if (verbose)
        md380_print_version(radio, out);
    print_intro(out);

    //
    // Channels.
    //
    if (have_channels(MODE_DIGITAL)) {
        fprintf(out, "\n");
        print_digital_channels(out, verbose);
    }
    if (have_channels(MODE_ANALOG)) {
        fprintf(out, "\n");
        print_analog_channels(out, verbose);
    }

    //
    // Zones.
    //
    if (have_zones()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of channel zones.\n");
            fprintf(out, "# 1) Zone number: 1-%d\n", NZONES);
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) List of channels: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Zone    Name             Channels\n");
        for (i=0; i<NZONES; i++) {
            zone_t *z = (zone_t*) &radio_mem[OFFSET_ZONES + i*64];

            if (z->name[0] == 0 || z->name[0] == 0xffff) {
                // Zone is disabled.
                continue;
            }

            fprintf(out, "%4d    ", i + 1);
            print_unicode(out, z->name, 16, 1);
            fprintf(out, " ");
            if (z->member[0]) {
                print_chanlist(out, z->member, 16);
            } else {
                fprintf(out, "-");
            }
            fprintf(out, "\n");
        }
    }

    //
    // Scan lists.
    //
    if (have_scanlists()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of scan lists.\n");
            fprintf(out, "# 1) Zone number: 1-%d\n", NSCANL);
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) Priority channel 1 (50%% of scans): -, Sel or index\n");
            fprintf(out, "# 4) Priority channel 2 (25%% of scans): -, Sel or index\n");
            fprintf(out, "# 5) Designated transmit channel: -, Last or index\n");
            fprintf(out, "# 6) List of channels: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Scanlist Name             PCh1 PCh2 TxCh ");
#ifdef PRINT_RARE_PARAMS
        fprintf(out, "Hold Smpl ");
#endif
        fprintf(out, "Channels\n");
        for (i=0; i<NSCANL; i++) {
            scanlist_t *sl = (scanlist_t*) &radio_mem[OFFSET_SCANL + i*104];

            if (sl->name[0] == 0 || sl->name[0] == 0xffff) {
                // Scan list is disabled.
                continue;
            }

            fprintf(out, "%5d    ", i + 1);
            print_unicode(out, sl->name, 16, 1);
            if (sl->priority_ch1 == 0xffff) {
                fprintf(out, " -    ");
            } else if (sl->priority_ch1 == 0) {
                fprintf(out, " Sel  ");
            } else {
                fprintf(out, " %-4d ", sl->priority_ch1);
            }
            if (sl->priority_ch2 == 0xffff) {
                fprintf(out, "-    ");
            } else if (sl->priority_ch2 == 0) {
                fprintf(out, "Sel  ");
            } else {
                fprintf(out, "%-4d ", sl->priority_ch2);
            }
            if (sl->tx_designated_ch == 0xffff) {
                fprintf(out, "-    ");
            } else if (sl->tx_designated_ch == 0) {
                fprintf(out, "Last ");
            } else {
                fprintf(out, "%-4d ", sl->tx_designated_ch);
            }
#ifdef PRINT_RARE_PARAMS
            fprintf(out, "%-4d %-4d ",
                sl->sign_hold_time * 25, sl->prio_sample_time * 250);
#endif
            if (sl->member[0]) {
                print_chanlist(out, sl->member, 31);
            } else {
                fprintf(out, "-");
            }
            fprintf(out, "\n");
        }
    }

    //
    // Contacts.
    //
    if (have_contacts()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of contacts.\n");
            fprintf(out, "# 1) Contact number: 1-%d\n", NCONTACTS);
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) Call type: Group, Private, All\n");
            fprintf(out, "# 4) Call ID: 1...16777215\n");
            fprintf(out, "# 5) Call receive tone: -, Yes\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Contact Name             Type    ID       RxTone\n");
        for (i=0; i<NCONTACTS; i++) {
            contact_t *ct = (contact_t*) &radio_mem[OFFSET_CONTACTS + i*36];

            if (ct->name[0] == 0 || ct->name[0] == 0xffff) {
                // Contact is disabled
                continue;
            }

            fprintf(out, "%5d   ", i+1);
            print_unicode(out, ct->name, 16, 1);
            fprintf(out, " %-7s %-8d %s\n",
                CONTACT_TYPE[ct->type], ct->id, ct->receive_tone ? "Yes" : "-");
        }
    }

    //
    // Group lists.
    //
    if (have_grouplists()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of group lists.\n");
            fprintf(out, "# 1) Group list number: 1-%d\n", NGLISTS);
            fprintf(out, "# 2) List of contacts: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Grouplist Contacts\n");
        for (i=0; i<NGLISTS; i++) {
            grouplist_t *gl = (grouplist_t*) &radio_mem[OFFSET_GLISTS + i*96];

            if (gl->name[0] == 0 || gl->name[0] == 0xffff) {
                // Group list is disabled.
                continue;
            }

            fprintf(out, "%5d     ", i + 1);
            if (gl->member[0]) {
                print_chanlist(out, gl->member, 32);
            } else {
                fprintf(out, "-");
            }
            fprintf(out, "\n");
        }
    }

    //
    // Text messages.
    //
    if (have_messages()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of text messages.\n");
            fprintf(out, "# 1) Message number: 1-%d\n", NMESSAGES);
            fprintf(out, "# 2) Text: up to 144 characters\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Message Text\n");
        for (i=0; i<NMESSAGES; i++) {
            uint16_t *msg = (uint16_t*) &radio_mem[OFFSET_MSG + i*288];

            if (msg[0] == 0 || msg[0] == 0xffff) {
                // Message is disabled
                continue;
            }

            fprintf(out, "%5d   ", i+1);
            print_unicode(out, msg, 144, 0);
            fprintf(out, "\n");
        }
    }
}

//
// Read memory image from the binary file.
//
static void md380_read_image(radio_device_t *radio, FILE *img)
{
    struct stat st;

    // Guess device type by file size.
    if (fstat(fileno(img), &st) < 0) {
        fprintf(stderr, "Cannot get file size.\n");
        exit(-1);
    }
    switch (st.st_size) {
    case MEMSZ:
        // IMG file.
        if (fread(&radio_mem[0], 1, MEMSZ, img) != MEMSZ) {
            fprintf(stderr, "Error reading image data.\n");
            exit(-1);
        }
        break;
    case MEMSZ + 0x225 + 0x10:
        // RTD file.
        // Header 0x225 bytes and footer 0x10 bytes at 0x40225.
        fseek(img, 0x225, SEEK_SET);
        if (fread(&radio_mem[0], 1, MEMSZ, img) != MEMSZ) {
            fprintf(stderr, "Error reading image data.\n");
            exit(-1);
        }
        break;
    default:
        fprintf(stderr, "Unrecognized file size %u bytes.\n", (int) st.st_size);
        exit(-1);
    }
}

//
// Save memory image to the binary file.
//
static void md380_save_image(radio_device_t *radio, FILE *img)
{
    fwrite(&radio_mem[0], 1, MEMSZ, img);
}

//
// Erase all channels, zones and scanlists.
//
static void erase_channels()
{
    int i;

    for (i=0; i<NCHAN; i++) {
        erase_channel(i);
    }
    for (i=0; i<NZONES; i++) {
        erase_zone(i);
    }
    //TODO: erase scanlists
}

//
// Parse the scalar parameter.
//
static void md380_parse_parameter(radio_device_t *radio, char *param, char *value)
{
    general_settings_t *gs = (general_settings_t*) &radio_mem[OFFSET_SETTINGS];

    if (strcasecmp("Radio", param) == 0) {
        if (strcasecmp(radio->name, value) != 0) {
            fprintf(stderr, "Bad value for %s: %s\n", param, value);
            exit(-1);
        }
        return;
    }
    if (strcasecmp ("Name", param) == 0) {
        utf8_decode(gs->radio_name, value, 16);
        return;
    }
    if (strcasecmp ("ID", param) == 0) {
        uint32_t id = strtoul(value, 0, 0);
        gs->radio_id[0] = id;
        gs->radio_id[1] = id >> 8;
        gs->radio_id[2] = id >> 16;
        return;
    }
    if (strcasecmp ("Last Programmed Date", param) == 0) {
        // Ignore.
        return;
    }
    if (strcasecmp ("CPS Software Version", param) == 0) {
        // Ignore.
        return;
    }
    if (strcasecmp ("Welcome Line 1", param) == 0) {
        utf8_decode(gs->intro_line1, value, 10);
        return;
    }
    if (strcasecmp ("Welcome Line 2", param) == 0) {
        utf8_decode(gs->intro_line2, value, 10);
        return;
    }
    fprintf(stderr, "Unknown parameter: %s = %s\n", param, value);
    exit(-1);
}

//
// Parse one line of Digital channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_digital_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char power_str[256], scanlist_str[256], autoscan_str[256], squelch_str[256];
    char tot_str[256], rxonly_str[256], admit_str[256], colorcode_str[256];
    char slot_str[256], incall_str[256], grouplist_str[256], contact_str[256];
    int num, power, scanlist, autoscan, squelch, tot, rxonly, admit;
    int colorcode, timeslot, incall, grouplist, contact;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str,
        power_str, scanlist_str, autoscan_str, squelch_str,
        tot_str, rxonly_str, admit_str, colorcode_str,
        slot_str, incall_str, grouplist_str, contact_str) != 16)
        return 0;

    num = atoi(num_str);
    if (num < 1 || num > NCHAN) {
        fprintf(stderr, "Bad channel number.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        !is_valid_frequency(rx_mhz)) {
        fprintf(stderr, "Bad receive frequency.\n");
        return 0;
    }
    if (sscanf(offset_str, "%lf", &tx_mhz) != 1) {
badtx:  fprintf(stderr, "Bad transmit frequency.\n");
        return 0;
    }
    if (offset_str[0] == '-' || offset_str[0] == '+')
        tx_mhz += rx_mhz;
    if (! is_valid_frequency(tx_mhz))
        goto badtx;

    if (strcasecmp("High", power_str) == 0) {
        power = POWER_HIGH;
    } else if (strcasecmp("Low", power_str) == 0) {
        power = POWER_LOW;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (*scanlist_str == '-') {
        scanlist = 0;
    } else {
        scanlist = atoi(scanlist_str);
        if (scanlist == 0 || scanlist > NSCANL) {
            fprintf(stderr, "Bad scanlist.\n");
            return 0;
        }
    }

    if (*autoscan_str == '-') {
        autoscan = 0;
    } else if (*autoscan_str == '+') {
        autoscan = 1;
    } else {
        fprintf(stderr, "Bad autoscan flag.\n");
        return 0;
    }

    squelch = atoi(squelch_str);
    if (squelch > 9) {
        fprintf(stderr, "Bad squelch level.\n");
        return 0;
    }

    tot = atoi(tot_str);
    if (tot > 37) {
        fprintf(stderr, "Bad timeout timer.\n");
        return 0;
    }
    tot *= 15;

    if (*rxonly_str == '-') {
        rxonly = 0;
    } else if (*rxonly_str == '+') {
        rxonly = 1;
    } else {
        fprintf(stderr, "Bad receive only flag.\n");
        return 0;
    }

    if (*admit_str == '-' || strcasecmp("Always", admit_str) == 0) {
        admit = ADMIT_ALWAYS;
    } else if (strcasecmp("Free", admit_str) == 0) {
        admit = ADMIT_CH_FREE;
    } else if (strcasecmp("Color", admit_str) == 0) {
        admit = ADMIT_COLOR;
    } else {
        fprintf(stderr, "Bad admit criteria.\n");
        return 0;
    }

    colorcode = atoi(colorcode_str);
    if (colorcode < 1 || colorcode > 15) {
        fprintf(stderr, "Bad color code.\n");
        return 0;
    }

    timeslot = atoi(slot_str);
    if (timeslot < 1 || timeslot > 2) {
        fprintf(stderr, "Bad timeslot.\n");
        return 0;
    }

    if (*incall_str == '-' || strcasecmp("Always", incall_str) == 0) {
        incall = INCALL_ALWAYS;
    } else if (strcasecmp("Admit", incall_str) == 0) {
        incall = INCALL_ADMIT;
    } else {
        fprintf(stderr, "Bad incall criteria.\n");
        return 0;
    }

    if (*grouplist_str == '-') {
        grouplist = 0;
    } else {
        grouplist = atoi(grouplist_str);
        if (grouplist == 0 || grouplist > NGLISTS) {
            fprintf(stderr, "Bad receive grouplist.\n");
            return 0;
        }
    }

    if (*contact_str == '-') {
        contact = 0;
    } else {
        contact = atoi(contact_str);
        if (contact == 0 || contact > NCONTACTS) {
            fprintf(stderr, "Bad transmit contact.\n");
            return 0;
        }
    }

    if (first_row && radio->channel_count == 0) {
        // On first entry, erase all channels, zones and scanlists.
        erase_channels();
    }

    setup_channel(num-1, MODE_DIGITAL, name_str, rx_mhz, tx_mhz,
        power, scanlist, autoscan, squelch, tot, rxonly, admit,
        colorcode, timeslot, incall, grouplist, contact, 0, 0, 0);

    radio->channel_count++;
    return 1;
}

//
// Parse one line of Analog channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_analog_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char power_str[256], scanlist_str[256], autoscan_str[256], squelch_str[256];
    char tot_str[256], rxonly_str[256], admit_str[256];
    char rxtone_str[256], txtone_str[256], width_str[256];
    int num, power, scanlist, autoscan, squelch, tot, rxonly, admit;
    int rxtone, txtone, width;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str,
        power_str, scanlist_str, autoscan_str, squelch_str,
        tot_str, rxonly_str, admit_str,
        rxtone_str, txtone_str, width_str) != 14)
        return 0;

    num = atoi(num_str);
    if (num < 1 || num > NCHAN) {
        fprintf(stderr, "Bad channel number.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        !is_valid_frequency(rx_mhz)) {
        fprintf(stderr, "Bad receive frequency.\n");
        return 0;
    }
    if (sscanf(offset_str, "%lf", &tx_mhz) != 1) {
badtx:  fprintf(stderr, "Bad transmit frequency.\n");
        return 0;
    }
    if (offset_str[0] == '-' || offset_str[0] == '+')
        tx_mhz += rx_mhz;
    if (! is_valid_frequency(tx_mhz))
        goto badtx;

    if (strcasecmp("High", power_str) == 0) {
        power = POWER_HIGH;
    } else if (strcasecmp("Low", power_str) == 0) {
        power = POWER_LOW;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (*scanlist_str == '-') {
        scanlist = 0;
    } else {
        scanlist = atoi(scanlist_str);
        if (scanlist == 0 || scanlist > NSCANL) {
            fprintf(stderr, "Bad scanlist.\n");
            return 0;
        }
    }

    if (*autoscan_str == '-') {
        autoscan = 0;
    } else if (*autoscan_str == '+') {
        autoscan = 1;
    } else {
        fprintf(stderr, "Bad autoscan flag.\n");
        return 0;
    }

    squelch = atoi(squelch_str);
    if (squelch > 9) {
        fprintf(stderr, "Bad squelch level.\n");
        return 0;
    }

    tot = atoi(tot_str);
    if (tot > 37) {
        fprintf(stderr, "Bad timeout timer.\n");
        return 0;
    }
    tot *= 15;

    if (*rxonly_str == '-') {
        rxonly = 0;
    } else if (*rxonly_str == '+') {
        rxonly = 1;
    } else {
        fprintf(stderr, "Bad receive only flag.\n");
        return 0;
    }

    if (*admit_str == '-' || strcasecmp("Always", admit_str) == 0) {
        admit = ADMIT_ALWAYS;
    } else if (strcasecmp("Free", admit_str) == 0) {
        admit = ADMIT_CH_FREE;
    } else if (strcasecmp("Tone", admit_str) == 0) {
        admit = ADMIT_TONE;
    } else {
        fprintf(stderr, "Bad admit criteria.\n");
        return 0;
    }

    rxtone = encode_tone(rxtone_str);
    if (rxtone < 0) {
        fprintf(stderr, "Bad receive tone.\n");
        return 0;
    }
    txtone = encode_tone(txtone_str);
    if (txtone < 0) {
        fprintf(stderr, "Bad transmit tone.\n");
        return 0;
    }

    if (strcasecmp ("12.5", width_str) == 0) {
        width = BW_12_5_KHZ;
    } else if (strcasecmp ("20", width_str) == 0) {
        width = BW_20_KHZ;
    } else if (strcasecmp ("25", width_str) == 0) {
        width = BW_25_KHZ;
    } else {
        fprintf (stderr, "Bad width.\n");
        return 0;
    }

    if (first_row && radio->channel_count == 0) {
        // On first entry, erase all channels, zones and scanlists.
        erase_channels();
    }

    setup_channel(num-1, MODE_ANALOG, name_str, rx_mhz, tx_mhz,
        power, scanlist, autoscan, squelch, tot, rxonly, admit,
        1, 1, 0, 0, 0, rxtone, txtone, width);

    radio->channel_count++;
    return 1;
}

//
// Parse one line of Zones table.
// Return 0 on failure.
//
static int parse_zones(int first_row, char *line)
{
    char num_str[256], chan_str[256];
    int bnum;

    if (sscanf(line, "%s %s", num_str, chan_str) != 2)
        return 0;

    bnum = atoi(num_str);
    if (bnum < 1 || bnum > NZONES) {
        fprintf(stderr, "Bad zone number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Zones table.
        memset(&radio_mem[OFFSET_ZONES], 0, NZONES * 0x80);
    }

    if (*chan_str == '-')
        return 1;

    char *str   = chan_str;
    int   nchan = 0;
    int   range = 0;
    int   last  = 0;

    // Parse channel list.
    for (;;) {
        char *eptr;
        int cnum = strtoul(str, &eptr, 10);

        if (eptr == str) {
            fprintf(stderr, "Zone %d: wrong channel list '%s'.\n", bnum, str);
            return 0;
        }
        if (cnum < 1 || cnum > NCHAN) {
            fprintf(stderr, "Zone %d: wrong channel number %d.\n", bnum, cnum);
            return 0;
        }

        if (range) {
            // Add range.
            int c;
            for (c=last; c<cnum; c++) {
                setup_zone(bnum-1, c);
                nchan++;
            }
        } else {
            // Add single channel.
            setup_zone(bnum-1, cnum-1);
            nchan++;
        }

        if (*eptr == 0)
            break;

        if (*eptr != ',' && *eptr != '-') {
            fprintf(stderr, "Zone %d: wrong channel list '%s'.\n", bnum, eptr);
            return 0;
        }
        range = (*eptr == '-');
        last = cnum;
        str = eptr + 1;
    }
    return 1;
}

//
// Parse one line of Scanlist table.
// Return 0 on failure.
//
static int parse_scanlist(int first_row, char *line)
{
    //TODO: parse scanlist Name
    //TODO: parse scanlist PCh1
    //TODO: parse scanlist PCh2
    //TODO: parse scanlist TxCh
    //TODO: parse scanlist Channels
    return 0;
}

//
// Parse one line of Contacts table.
// Return 0 on failure.
//
static int parse_contact(int first_row, char *line)
{
    //TODO: parse contact Name
    //TODO: parse contact Type
    //TODO: parse contact ID
    //TODO: parse contact RxTone
    return 0;
}

//
// Parse one line of Grouplist table.
// Return 0 on failure.
//
static int parse_grouplist(int first_row, char *line)
{
    //TODO: parse grouplist Contacts
    return 0;
}

//
// Parse table header.
// Return table id, or 0 in case of error.
//
static int md380_parse_header(radio_device_t *radio, char *line)
{
    if (strncasecmp(line, "Digital", 7) == 0)
        return 'D';
    if (strncasecmp(line, "Analog", 6) == 0)
        return 'A';
    if (strncasecmp(line, "Zone", 4) == 0)
        return 'Z';
    if (strncasecmp(line, "Scanlist", 8) == 0)
        return 'S';
    if (strncasecmp(line, "Contact", 7) == 0)
        return 'C';
    if (strncasecmp(line, "Grouplist", 9) == 0)
        return 'G';
    return 0;
}

//
// Parse one line of table data.
// Return 0 on failure.
//
static int md380_parse_row(radio_device_t *radio, int table_id, int first_row, char *line)
{
    switch (table_id) {
    case 'D': return parse_digital_channel(radio, first_row, line);
    case 'A': return parse_analog_channel(radio, first_row, line);
    case 'Z': return parse_zones(first_row, line);
    case 'S': return parse_scanlist(first_row, line);
    case 'C': return parse_contact(first_row, line);
    case 'G': return parse_grouplist(first_row, line);
    }
    return 0;
}

//
// Update timestamp.
//
static void md380_update_timestamp(radio_device_t *radio)
{
    unsigned char *timestamp = &radio_mem[OFFSET_TIMESTMP];
    char p[16];

    // Last Programmed Date
    get_timestamp(p);
    timestamp[0] = ((p[0]  & 0xf) << 4) | (p[1]  & 0xf); // year upper
    timestamp[1] = ((p[2]  & 0xf) << 4) | (p[3]  & 0xf); // year lower
    timestamp[2] = ((p[4]  & 0xf) << 4) | (p[5]  & 0xf); // month
    timestamp[3] = ((p[6]  & 0xf) << 4) | (p[7]  & 0xf); // day
    timestamp[4] = ((p[8]  & 0xf) << 4) | (p[9]  & 0xf); // hour
    timestamp[5] = ((p[10] & 0xf) << 4) | (p[11] & 0xf); // minute
    timestamp[6] = ((p[12] & 0xf) << 4) | (p[13] & 0xf); // second

    // CPS Software Version: Vdx.xx
    const char *dot = strchr(VERSION, '.');
    if (dot) {
        timestamp[7] = 0x0d; // Prints as '='
        timestamp[8] = dot[-1] & 0x0f;
        if (dot[2] == '.') {
            timestamp[9] = 0;
            timestamp[10] = dot[1] & 0x0f;
        } else {
            timestamp[9] = dot[1] & 0x0f;
            timestamp[10] = dot[2] & 0x0f;
        }
    }
}

//
// TYT MD-380
//
radio_device_t radio_md380 = {
    "TYT MD-380",
    md380_download,
    md380_upload,
    md380_is_compatible,
    md380_read_image,
    md380_save_image,
    md380_print_version,
    md380_print_config,
    md380_parse_parameter,
    md380_parse_header,
    md380_parse_row,
    md380_update_timestamp,
};