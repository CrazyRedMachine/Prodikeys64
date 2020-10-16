/* Prodikeys MIDI Interface
 * Copyright 2020, CrazyRedMachine
 *
 * Based on Virtual MIDI SDK
 * Copyright 2009-2019, Tobias Erichsen
 *
 * Based on prodikeys_hid.c by Don Prince
 * Copyright 2009 Don Prince
 *
 */
#include "teVirtualMIDI.h"
#include "libusb-1.0/libusb.h"

//Prodikeys device global struct
struct pcmidi_snd {
    bool			    fn_state;           // fn lock key is active
    bool		    	midi_mode;          // piano keys are active
    bool                midi_sustain_mode;  // sustain mode is active
    unsigned short		midi_channel;       // midi channel in use
    unsigned short		midi_inst;          // instrument in use
    short				midi_octave;        // current octave
    unsigned short		midi_pitch;         // current pitch
    LPVM_MIDI_PORT port;                    // teVirtualMIDI handle
    libusb_device_handle *handle;           // libusb handle
};

#define PCMIDI_PITCH_MAX 0x3FFF
#define PCMIDI_PITCH_BASE 0x2000
#define PCMIDI_PITCH_MIN 0x0
#define PCMIDI_MIDDLE_C 60
#define PCMIDI_CHANNEL_MIN 0
#define PCMIDI_CHANNEL_MAX 15
#define PCMIDI_OCTAVE_MIN (-2)
#define PCMIDI_OCTAVE_MAX 2
#define PCMIDI_INST_MIN 0
#define PCMIDI_INST_MAX 127

#define MAX_SYSEX_BUFFER	65535

/**
 * Init prodikeys default values :
 * channel, instrument, octave set to 0
 * fn_state, sustain_mode, midi_mode set to false
 * pitch set to 0x2000
 * VirtualMIDI port handle set to NULL
 * @param pm the Prodikeys device
 */
void pm_init_values(struct pcmidi_snd *pm);

/**
 * Send a 3-byte HID message to report id 6. The message will be 06 01 nn with nn = byte.
 * C1 enable piano keys
 * C2 disable piano keys
 * C3 unknown, gets a reply message on report id 5 ( 05 c3 0a 00 00 02 )
 * C4 unknown, gets a reply message on report id 5 ( 05 c4 00 00 00 3f )
 * C5 turn the FN led ON
 * C6 turn the FN led OFF
 *
 * @param handle libusb handle to the device
 * @param byte the command byte
 * @return true iff the message was sent successfully.
 */
bool prodikeys_send_hid_data(libusb_device_handle* handle, uint8_t byte);

/**
 * Enable midi keys
 * @param pm the prodikeys device
 * @return true iff the message was sent successfully
 */
bool prodikeys_enable_midi(struct pcmidi_snd *pm);

/**
 * Disable midi keys
 * @param pm the prodikeys device
 * @return true iff the message was sent successfully
 */
bool prodikeys_disable_midi(struct pcmidi_snd *pm);

/**
 * Attach to the Prodikeys device (VID_041E&PID_2801) interface 1
 * @param handle resulting handle if successful, or NULL
 * @return true iff the interface was claimed successfully
 */
bool prodikeys_claim_interface(libusb_device_handle** handle);

/**
 * Send a midi NOTE ON or NOTE OFF message to the VirtualMIDI driver
 * (could theoretically be used to send any other 3 byte midi message to the current channel)
 * @param pm the Prodikeys device
 * @param status status byte (0x9n with n = channel number)
 * @param note note byte
 * @param velocity velocity byte
 */
void pcmidi_send_note(struct pcmidi_snd *pm, unsigned char status, unsigned char note, unsigned char velocity);

/**
 * Send a MIDI Control message to the VirtualMIDI driver
 * Status byte : 1011 CCCC
 * Data byte 1 : 0NNN NNNN //control number
 * Data byte 2 : 0VVV VVVV //control value
 * @param pm the Prodikeys device
 * @param number control number
 * @param value control value
 */
void pcmidi_send_control(struct pcmidi_snd *pm, unsigned char number, unsigned char value);

/**
 * Send a MIDI pitch message to the VirtualMIDI driver, taking value from the struct midi_pitch field
 * Status byte : 1110 CCCC
 * Data byte 1 : 0LLL LLLL // pitch value LSB
 * Data byte 2 : 0MMM MMMM // pitch value MSB
 * @param pm the Prodikeys device
 */
void pcmidi_send_pitch(struct pcmidi_snd *pm);

/**
 * Send a MIDI instrument change message to the VirtualMIDI driver, taking value from the struct midi_inst field
 * Status byte : 1100 CCCC
 * Data byte 1 : 0XXX XXXX //instrument number
 * @param pm the Prodikeys device
 */
void pcmidi_next_instrument(struct pcmidi_snd *pm);
/**
 * Send a MIDI instrument change message to the VirtualMIDI driver, taking value from the struct midi_inst field
 * Status byte : 1100 CCCC
 * Data byte 1 : 0XXX XXXX //instrument number
 * @param pm the Prodikeys device
 */
void pcmidi_prev_instrument(struct pcmidi_snd *pm);

/**
 * handle keypress on Prodikeys FN key
 * (updates the fn_state field and light the FN led accordingly)
 * @param pm the Prodikeys device
 * @return true iff message was successfully sent.
 */
bool prodikeys_fn_switch(struct pcmidi_snd *pm);

/**
 * handle keypress on Prodikeys sustain key
 * (latching sustain switch (midi control 64) when fn_state is off,
 * momentary sostenuto (midi control 66) otherwise
 * @param pm the Prodikeys device
 * @return true iff message was successfully sent.
 */
bool prodikeys_sustain_switch(struct pcmidi_snd *pm);

/**
 * Handle prodikeys report id 3 hid messages (piano keys : note on/off forwarding to VirtualMIDI driver)
 * @param pm the Prodikeys device
 * @param data hid report data
 * @param size hid report size
 */
void pcmidi_handle_note_report(struct pcmidi_snd *pm, uint8_t *data, int size);

/**
 * Handle prodikeys report id 1/2/4 hid messages (special function keys and volume wheel..)
 * cf. appendix for details
 * @param pm the Prodikeys device
 * @param data hid report data
 * @param size hid report size
 */
void pcmidi_handle_report_extra(struct pcmidi_snd *pm, uint8_t *data, int size);

/*
 * Appendix: Prodikeys HID messages reference
 *
report id 1, size 3 : media keys
--------------------------------
00 00 04 : VK_BROWSER_HOME
            (When midi_mode active: latching sustain mode)
            (When midi_mode active and fn_state active : momentary sostenuto)
00 01 00 : VK_VOLUME_DOWN
            (When midi_mode active and fn_state active : pitch wheel down)
00 20 00 : (top right, CD eject key)VK_LAUNCH_MEDIA_SELECT
            (When midi_mode active and fn_state active : midi channel 9 (drums))
00 40 00 : VK_LAUNCH_MAIL
            (When midi_mode active: previous octave)
            (When midi_mode active and fn_state active : previous instrument)
00 80 00 : CALCULATOR (ShellExecute calc.exe)
01 00 00 : MEDIA NEXT
            (When midi_mode active and fn_state active : next midi channel)
02 00 00 : MEDIA PREVIOUS
            (When midi_mode active and fn_state active : previous midi channel)
04 00 00 : MEDIA STOP
            (When midi_mode active and fn_state active : channel 0)
08 00 00 : MEDIA PLAY/PAUSE
10 00 00 : VK_VOLUME_MUTE
            (When midi_mode active and fn_state active : pitch wheel reset to 0x2000)
80 00 00 : VK_VOLUME_UP
            (When midi_mode active and fn_state active : pitch wheel up)

report id 2, size 1 : system keys
---------------------------------
02 : VK_SLEEP

report id 3, size variable : midi piano keys
--------------------------------------------

report 4, size 3 : extra keys
-----------------------------
00 00 10 : FN key (latching fn_state value)
01 00 00 : SESSION LOCK (win+L, UNIMPLEMENTED)
02 00 00 : PIANO key (enable/disable midi keys on report id 3)
04 00 00 : My Documents folder (system("explorer.exe \"%userprofile%\\Documents\"");)
08 00 00 : ADDRESS BOOK (UNIMPLEMENTED)
10 00 00 : Instant Messaging (UNIMPLEMENTED)
            (When midi_mode active: next octave)
            (When midi_mode active and fn_state active : next instrument)
20 00 00 : My Music folder (system("explorer.exe \"%userprofile%\\Music\"");)
40 00 00 : CALENDAR (UNIMPLEMENTED)
80 00 00 : My Pictures folder (system("explorer.exe \"%userprofile%\\Pictures\"");)

*/