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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "prodikeys-core.h"

bool prodikeys_send_hid_data(libusb_device_handle* handle, uint8_t byte){
    if (handle == NULL) return false;

    int res;
    int numBytes                 = 0;  /* Actual bytes transferred. */
    uint8_t buffer[3];                /* 3 byte transfer buffer */

    memset(buffer, 0, 3);
    buffer[0] = 6; //report ID
    buffer[1] = 0x01;
    buffer[2] = byte;

    res = libusb_interrupt_transfer(handle, 3, buffer, 3, &numBytes, 1000);
    if (res == 0)
    {
        //printf("OK.\n", numBytes);
        return true;
    }
    else
    {
        //fprintf(stderr, "Error sending message to device.\n");
        return false;
    }
}

bool prodikeys_disable_midi(struct pcmidi_snd *pm){
    if (pm->handle == NULL || prodikeys_send_hid_data(pm->handle, 0xC2)) {
        pm->midi_mode = false;
        if (pm->port){
            virtualMIDIClosePort( pm->port );
        }
        pm->port = NULL;
        return true;
    }
    return false;
}

bool prodikeys_claim_interface(libusb_device_handle** handle){
    int res = libusb_init(0);
    if (res != 0)
    {
        //fprintf(stderr, "Error initialising libusb.\n");
        return false;
    }

    /* Get the first device with the matching Vendor ID and Product ID. If
     * intending to allow multiple demo boards to be connected at once, you
     * will need to use libusb_get_device_list() instead. Refer to the libusb
     * documentation for details. */
    *handle = libusb_open_device_with_vid_pid(0, 0x041e, 0x2801);
    if (!*handle)
    {
        //fprintf(stderr, "Unable to open device.\n");
        libusb_exit(0);
        return false;
    }
    /* Claim interface #1. */
    res = libusb_claim_interface(*handle, 1);
    if (res != 0)
    {
        //fprintf(stderr, "Error claiming interface. \nMake sure WinUSB driver is installed for Prodikeys interface 1\n");
        libusb_exit(0);
        return false;
    }

    return true;
}

void pcmidi_send_note(struct pcmidi_snd *pm,
                             unsigned char status, unsigned char note, unsigned char velocity)
{
    unsigned char buffer[3];

    buffer[0] = status;
    buffer[1] = note;
    buffer[2] = velocity;

    virtualMIDISendData(pm->port, buffer, 3);

    return;
}

void pcmidi_send_control(struct pcmidi_snd *pm, unsigned char number, unsigned char value){
    unsigned char buffer[3];
    buffer[0] = 128+32+16+pm->midi_channel;
    buffer[1] = number;
    buffer[2] = value;
    virtualMIDISendData(pm->port, buffer, 3);
}

void pcmidi_send_pitch(struct pcmidi_snd *pm){
    unsigned char buffer[3];
    buffer[0] = 128+64+32+pm->midi_channel;
    buffer[1] = pm->midi_pitch & 0x1F;
    buffer[2] = pm->midi_pitch >> 7;
    virtualMIDISendData(pm->port, buffer, 3);
}

void pcmidi_next_instrument(struct pcmidi_snd *pm){
    unsigned char buffer[2];
    if (pm->midi_inst < PCMIDI_INST_MAX) pm->midi_inst++;
    buffer[0] = 128+64+pm->midi_channel;
    buffer[1] = pm->midi_inst;
    virtualMIDISendData(pm->port, buffer, 2);
}

void pcmidi_prev_instrument(struct pcmidi_snd *pm){
    unsigned char buffer[2];
    if (pm->midi_inst > PCMIDI_INST_MIN) pm->midi_inst--;
    buffer[0] = 128+64+pm->midi_channel;
    buffer[1] = pm->midi_inst;
    virtualMIDISendData(pm->port, buffer, 2);
}

bool prodikeys_sustain_switch(struct pcmidi_snd *pm){
    if (!pm->midi_mode) return false;

    if (pm->fn_state){ //sostenuto in FN mode (momentary)
        pcmidi_send_control(pm, 66, 0);
        pcmidi_send_control(pm, 66, 127);
        return true;
    }
    if (pm->midi_sustain_mode){
        pm->midi_sustain_mode = false;
        pcmidi_send_control(pm, 64, 0);
    } else {
        pm->midi_sustain_mode = true;
        pcmidi_send_control(pm, 64, 127);
    }
    return true;
}

bool prodikeys_fn_switch(struct pcmidi_snd *pm){
    bool ret;
    //in case send_hid_data didn't work, force fn_state to false as keyboard is probably unplugged anyway
    if (pm->fn_state){
        ret = prodikeys_send_hid_data(pm->handle, 0xC6);
        pm->fn_state = false;
    } else {
        ret = prodikeys_send_hid_data(pm->handle, 0xC5);
        if (ret)
            pm->fn_state = true;
    }
    return ret;
}

void pm_init_values(struct pcmidi_snd *pm){
    pm->midi_channel = 0;
    pm->midi_inst = 0;
    pm->midi_octave = 0;
    pm->midi_pitch = PCMIDI_PITCH_BASE;
    pm->fn_state = true;
    prodikeys_fn_switch(pm);
    pm->port = NULL;
    pm->midi_sustain_mode = false;
    pm->midi_mode = true;
    prodikeys_disable_midi(pm);
}

bool prodikeys_enable_midi(struct pcmidi_snd *pm){
    pm_init_values(pm);
    //printf("Activating MIDI keys.\n");
    bool ret = prodikeys_send_hid_data(pm->handle, 0xC1);
    if (ret){
        pm->port = virtualMIDICreatePortEx2( L"Prodikeys MIDI Interface", NULL, 0, MAX_SYSEX_BUFFER, TE_VM_FLAGS_PARSE_RX );
        if ( !pm->port ) {
            //printf( "could not create port: %d\n", GetLastError() );
            return false;
        }
        pm->midi_mode = true;
    }
    return ret;
}

void pcmidi_handle_note_report(struct pcmidi_snd *pm, uint8_t *data, int size)
{
    unsigned i, j;
    unsigned char status, note, velocity;

    unsigned num_notes = (size-1)/2;

    for (j = 0; j < num_notes; j++)	{
        note = data[j*2+1];
        velocity = data[j*2+2];

        if (note < 0x81) { /* note on */
            status = 128 + 16 + pm->midi_channel; /* 1001nnnn */
            note = note - 0x54 + PCMIDI_MIDDLE_C +
                   (pm->midi_octave * 12);
            if (velocity == 0){
                //printf("VELOCITY 0!!\n");
                velocity = 0x20; /* force note on */
            }
            pcmidi_send_note(pm, status, note, velocity);
        } else { /* note off */
            status = 128 + pm->midi_channel; /* 1000nnnn */
            note = note - 0x94 + PCMIDI_MIDDLE_C +
                   (pm->midi_octave*12);
            pcmidi_send_note(pm, status, note, velocity);
        }

    }
}

//TODO: write an easier to read code using a 4 state thing ( neutral / fn / midi / midi+fn )
void pcmidi_handle_report_extra(struct pcmidi_snd *pm, uint8_t *data, int size)
{
    static uint32_t prev_data1 = 0;
    static uint8_t prev_data2 = 0;
    static uint32_t prev_data4 = 0;

INPUT in[20] = {0}; // up to 20 state changes at once (buttons)
uint8_t keys[20];
int key_index = 0;
bool keyState[20] = {false};

if (data[0] == 0x02) {
    if (data[1] != prev_data2) {
        //printf("SLEEP\n");
        if (data[1] == 0x02) keyState[key_index] = true;
        keys[key_index++] = VK_SLEEP;
        prev_data2 = data[1];
    }
}
else if (data[0] == 0x01)
{
    uint32_t *report1 = (uint32_t *) &(data[1]);
    if (*report1 != prev_data1){
        if ((*report1 & 0x040000) != (prev_data1 & 0x040000)){
            if (*report1 & 0x040000){
                if (pm->midi_mode) prodikeys_sustain_switch(pm);
                else keyState[key_index] = true;
            }
            if (!pm->midi_mode)
                keys[key_index++] = VK_BROWSER_HOME;
        }
        if ((*report1 & 0x0100) != (prev_data1 & 0x0100)){
            if (*report1 & 0x0100) {
                if (pm->midi_mode && pm->fn_state){
                    if (pm->midi_pitch>PCMIDI_PITCH_MIN+1000) {
                        pm->midi_pitch -= 1000;
                        pcmidi_send_pitch(pm);
                    }
                } else {
                    keyState[key_index] = true;
                }
            }
            if (!((pm->midi_mode && pm->fn_state)))
                keys[key_index++] = VK_VOLUME_DOWN;
        }
        if ((*report1 & 0x2000) != (prev_data1 & 0x2000)){
                if (*report1 & 0x2000) {
                    if (pm->midi_mode && pm->fn_state){
                        pm->midi_channel = 9; //switch to drum channel. TODO: remember previous channel to restore?
                    } else {
                        keyState[key_index] = true;
                    }
                }
                if (!((pm->midi_mode && pm->fn_state)))
                    keys[key_index++] = VK_LAUNCH_MEDIA_SELECT; // EJECT CD, TODO: implement CD drive eject?
            }

        if ((*report1 & 0x4000) != (prev_data1 & 0x4000)){
            if (*report1 & 0x4000){
                if(pm->midi_mode){
                    if (pm->fn_state){
                        pcmidi_prev_instrument(pm);
                    } else if (pm->midi_octave > PCMIDI_OCTAVE_MIN) pm->midi_octave--;
                }
                else keyState[key_index] = true;
            }
            if (!pm->midi_mode) keys[key_index++] = VK_LAUNCH_MAIL;
        }
        if ((*report1 & 0x8000) != (prev_data1 & 0x8000)){
            if (*report1 & 0x8000) ShellExecute(NULL, "open", "calc.exe", NULL, NULL, SW_SHOWDEFAULT); //system("calc.exe");
        }

        //next track (becomes next channel in midi mode)
        if ((*report1 & 0x01) != (prev_data1 & 0x01)){
            if (*report1 & 0x01) {
                if (pm->midi_mode && pm->fn_state){
                    if (pm->midi_channel<PCMIDI_CHANNEL_MAX) pm->midi_channel++;
                } else {
                    keyState[key_index] = true;
                }
            }
            if (!((pm->midi_mode && pm->fn_state)))
                keys[key_index++] = VK_MEDIA_PREV_TRACK;
        }
        if ((*report1 & 0x02) != (prev_data1 & 0x02)){
            if (*report1 & 0x02) {
                if (pm->midi_mode && pm->fn_state){
                    if (pm->midi_channel>PCMIDI_CHANNEL_MIN) pm->midi_channel--;
                } else {
                    keyState[key_index] = true;
                }
            }
            if (!((pm->midi_mode && pm->fn_state)))
                keys[key_index++] = VK_MEDIA_PREV_TRACK;
        }
        if ((*report1 & 0x04) != (prev_data1 & 0x04)){
            if (*report1 & 0x04) {
                if (pm->midi_mode && pm->fn_state) pm->midi_channel = 0;
                else keyState[key_index] = true;
            }
            if (!((pm->midi_mode && pm->fn_state)))
                keys[key_index++] = VK_MEDIA_STOP;
        }
        if ((*report1 & 0x08) != (prev_data1 & 0x08)){
            if (*report1 & 0x08) keyState[key_index] = true;
            keys[key_index++] = VK_MEDIA_PLAY_PAUSE;
        }
        if ((*report1 & 0x10) != (prev_data1 & 0x10)){
            if (*report1 & 0x10) {
                if (pm->midi_mode && pm->fn_state){
                    pm->midi_pitch = PCMIDI_PITCH_BASE;
                    pcmidi_send_pitch(pm);
                } else {
                    keyState[key_index] = true;
                }
            }
            if (!((pm->midi_mode && pm->fn_state)))
                keys[key_index++] = VK_VOLUME_MUTE;
        }
        if ((*report1 & 0x80) != (prev_data1 & 0x80)){
            if (*report1 & 0x80) {
                if (pm->midi_mode && pm->fn_state){
                    if (pm->midi_pitch<PCMIDI_PITCH_MAX-1000) {
                        pm->midi_pitch += 1000;
                        pcmidi_send_pitch(pm);
                    }
                } else {
                    keyState[key_index] = true;
                }
            }
            if (!((pm->midi_mode && pm->fn_state)))
                keys[key_index++] = VK_VOLUME_UP;
        }
        prev_data1 = *report1;
    }
}
else if (data[0] == 0x04)
{
    uint32_t *report4 = (uint32_t *) &(data[1]);
    if (*report4 != prev_data4){
        if ((*report4 & 0x100000) != (prev_data4 & 0x100000)){
            if (*report4 & 0x100000) {
                prodikeys_fn_switch(pm);
            }
        }
        if ((*report4 & 0x01) != (prev_data4 & 0x01)){
            if (*report4 & 0x01) keyState[key_index] = true; //TODO: implement session lock?
            //printf("LOCK\n");
            //lock keys[key_index++] = VK_L;
        }
        if ((*report4 & 0x02) != (prev_data4 & 0x02)){
            if (*report4 & 0x02) {
                pm->midi_mode? prodikeys_disable_midi(pm): prodikeys_enable_midi(pm);
            }
        }
        if ((*report4 & 0x04) != (prev_data4 & 0x04)){
            if (*report4 & 0x04) system("explorer.exe \"%userprofile%\\Documents\"");
        }
        if ((*report4 & 0x08) != (prev_data4 & 0x08)){
            if (*report4 & 0x08) keyState[key_index] = true; //TODO: implement address book
            //printf("adress book\n");
            //key_index++;
            //keys[key_index++] = ;
        }
        if ((*report4 & 0x10) != (prev_data4 & 0x10)){
            if (*report4 & 0x10) {
                if(pm->midi_mode){
                    if (pm->fn_state){
                        pcmidi_next_instrument(pm);
                    } else if (pm->midi_octave < PCMIDI_OCTAVE_MAX) pm->midi_octave++;
                }
            }
            //instant messaging is octave++ when in midi mode, and instrument++ when in midi+fn
            //key_index++;
            //keys[key_index++] = VK_MEDIA_NEXT_TRACK;
        }
        if ((*report4 & 0x20) != (prev_data4 & 0x20)){
            //My Music
            if (*report4 & 0x20) system("explorer.exe \"%userprofile%\\Music\"");
        }
        if ((*report4 & 0x40) != (prev_data4 & 0x40)){
            if (*report4 & 0x40) keyState[key_index] = true; //TODO: implement calendar
            //printf("calendar\n");
            //key_index++;
            //keys[key_index++] = VK_MEDIA_STOP;
        }
        if ((*report4 & 0x80) != (prev_data4 & 0x80)){
            if (*report4 & 0x80) system("explorer.exe \"%userprofile%\\Pictures\"");
            //keys[key_index++] = VK_MEDIA_PLAY_PAUSE;
        }
        prev_data4 = *report4;
    }
}

//finished collecting data, sending key updates
for (int i = 0; i < key_index; i++){
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.time = 0;
        in[i].ki.dwExtraInfo = 0;
        in[i].ki.wVk = keys[i];
        in[i].ki.dwFlags = 0x0000; // 0x0008 is for unicode, disables wVk and uses wScan instead
        if (!keyState[i]) in[i].ki.dwFlags |= 0x0002;
}
        SendInput(key_index, in, sizeof(INPUT));
}