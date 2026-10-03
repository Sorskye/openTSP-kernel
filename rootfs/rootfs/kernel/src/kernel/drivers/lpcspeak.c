#include "types.h"
#include "io.h"
#include "sleep.h"

uint32_t err_panic_freq = 800;
uint32_t err_freq_1 = 400;
uint32_t err_freq_2 = 600;

uint32_t err_speed_0= 10;
uint32_t err_speed_1 = 40;
uint32_t err_speed_2 = 70;

void interrupt_safe_wait(uint32_t i){
    for(int x=0; x<=i; x++){
        uint32_t count = 0;
        for(count = 0; count < 99999; count++){
            __asm__ __volatile__("pause");
        }
    }
}

void speaker_beep(uint32_t freq) {
    uint32_t divisor = 1193180 / freq;

    // Tell PIT we want to set channel 2, mode 3 (square wave)
    outb(0x43, 0xB6);

    // Send divisor low byte then high byte
    outb(0x42, divisor & 0xFF);
    outb(0x42, (divisor >> 8) & 0xFF);

    // Enable speaker
    uint8_t tmp = inb(0x61);
    if (!(tmp & 3)) {
        outb(0x61, tmp | 3);
    }
}

void speaker_stop() {
    uint8_t tmp = inb(0x61) & 0xFC; // clear bits 0 and 1
    outb(0x61, tmp);
}

void speaker_sound_ok(){
    speaker_beep(600);
    sleep_ms(100);
    speaker_stop();
}

void speaker_sound_panic(){
    speaker_beep(400);
    interrupt_safe_wait(7);

    speaker_beep(200);
    interrupt_safe_wait(7);
    speaker_stop();
}

void speaker_warning(){
    speaker_beep(err_freq_1);
    sleep_ms(err_speed_1);
    speaker_stop();
    
    sleep_ms(err_speed_1);
    speaker_beep(err_freq_1);
    sleep_ms(err_speed_1);
    speaker_stop();

    sleep_ms(err_speed_1);
    speaker_beep(err_freq_2);
    sleep_ms(err_speed_2);
    speaker_stop();

    sleep_ms(err_speed_2*2);
    speaker_beep(err_freq_2);
    sleep_ms(err_speed_2*2);
    speaker_stop();
}

void speaker_error(){
    
    speaker_beep(50);
    sleep_ms(200);
    speaker_stop();
    sleep_ms(200);

    speaker_beep(50);
    sleep_ms(200);
    speaker_stop();

    sleep_ms(200);
    speaker_beep(50);
    sleep_ms(200);
    speaker_stop();
    
}

void SpeakerBlip(){
    speaker_beep(50);
    sleep_ms(120);
    speaker_stop();
}