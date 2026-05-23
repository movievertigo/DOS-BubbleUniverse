#include <stdint.h>
#include <string.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/nearptr.h>
#include "sintable.h"

#define CURVECOUNT 256
#define CURVESTEP 4
#define ITERATIONS 256

#define SCREENWIDTH 320
#define SCREENHEIGHT 200
#define SCREENBUFFER 0xA0000
#define SCREENBUFFERSIZE (SCREENWIDTH*SCREENHEIGHT)

#define INITANIMSPEED 8
#define SCALEINSPEED 257
#define SCALEOUTSPEED 255
#define ASPECTMUL 307
#define MOVESPEED (64*(SCREENHEIGHT<<8))
#define ANIMSPEEDCHANGE 1

#define PI 3.1415926535897932384626433832795
#define SINTABLEPOWER 14
#define SINTABLEENTRIES (1<<SINTABLEPOWER)
#define ANG1INC ((int32_t)((CURVESTEP * SINTABLEENTRIES) / 235))
#define ANG2INC ((int32_t)((CURVESTEP * SINTABLEENTRIES) / (2*PI)))
#define SCALEMUL ((int32_t)(SCREENHEIGHT*PI/2))

#define KEY_ESCAPE      0x01
#define KEY_MINUS       0x0C
#define KEY_EQUAL       0x0D
#define KEY_BACKSPACE   0x0E
#define KEY_RETURN      0x1C
#define KEY_SLASH       0x35
#define KEY_SPACE       0x39
#define KEY_HOME        0x47
#define KEY_LEFT        0x4B
#define KEY_RIGHT       0x4D
#define KEY_UP          0x48
#define KEY_DOWN        0x50

#define STRINGIZE_INTERNAL(s) #s
#define STRINGIZE(s) STRINGIZE_INTERNAL(s)

int32_t sinTable[SINTABLEENTRIES*3];

// Reduce exe size by defining these functions as empty
// See https://www.delorie.com/djgpp/v2faq/faq8_14.html
void __crt0_load_environment_file(char *progname) { }
char **__crt0_glob_function(char *arg) { return 0; }
void __crt0_setup_arguments(void) { }

void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void vsync(void) { while (inb(0x3DA) & 0x08) {} while (!(inb(0x3DA) & 0x08)) {} }

void clearBuffer(uint8_t* buffer, uint32_t value, uint32_t dwordCount) { __asm__ volatile ("rep stosl" : "+D"(buffer), "+c"(dwordCount) : "a"(value) : "memory"); }
void copyBuffer(uint8_t* src, uint8_t* dst, uint32_t dwordCount) { __asm__ volatile ("rep movsl" : "+D"(dst), "+S"(src),"+c"(dwordCount) : : "memory"); }
void setPaletteEntry(uint8_t i, uint8_t r, uint8_t g, uint8_t b) { outb(0x3C8, i); outb(0x3C9, r); outb(0x3C9, g); outb(0x3C9, b); }

void expandSinTable()
{
    for (int i = 0; i < SINTABLEENTRIES/4; ++i)
    {
        sinTable[i] = sinTable[SINTABLEENTRIES/2 - i - 1] = compactsintable[i];
        sinTable[SINTABLEENTRIES/2 + i] = sinTable[SINTABLEENTRIES - i - 1] = -compactsintable[i];
    }
    for (int i = 0; i < SINTABLEENTRIES; ++i)
    {
        *((int16_t*)(sinTable+i)+1) = *(int16_t*)(sinTable+((i+SINTABLEENTRIES/4)%SINTABLEENTRIES));
    }
    for (int i = 0; i < SINTABLEENTRIES; ++i)
    {
        sinTable[SINTABLEENTRIES*1 + i] = sinTable[i];
        sinTable[SINTABLEENTRIES*2 + i] = sinTable[i];
    }
}

void setPalette()
{
    for (int i = 0; i < 16; ++i)
    {
        const int red = (i*63)/15;
        for (int j = 0; j < 16; ++j)
        {
            const int green = (j*63)/15;
            const int blue = (126-(red+green))>>1;
            setPaletteEntry(i*16+j, red, green, blue);
        }
    }
    setPaletteEntry(0, 0, 0, 0);
}

uint8_t keyState[128] = {0};
_go32_dpmi_seginfo oldKeyHandler, newKeyHandler;
uint8_t oldSpaceState;
uint8_t oldSlashState;

void keyHandler()
{
    uint8_t scanCode = inb(0x60);
    keyState[scanCode & 0x7F] = !(scanCode & 0x80);
    outb(0x20, 0x20); // send EOI to PIC
}

void installKeyHandler()
{
    _go32_dpmi_get_protected_mode_interrupt_vector(9, &oldKeyHandler);
    newKeyHandler.pm_offset = (uint32_t)keyHandler;
    newKeyHandler.pm_selector = _go32_my_cs();
    _go32_dpmi_allocate_iret_wrapper(&newKeyHandler);
    _go32_dpmi_set_protected_mode_interrupt_vector(9, &newKeyHandler);
}

void removeKeyHandler()
{
    _go32_dpmi_set_protected_mode_interrupt_vector(9, &oldKeyHandler);
    _go32_dpmi_free_iret_wrapper(&newKeyHandler);
}

void setMode(uint16_t mode)
{
    __asm__ volatile ("int $0x10" : "+a"(mode));
}

void printChar(char c)
{
    uint32_t ax = 0x0200;
    __asm__ volatile ("int $0x21" : "+a"(ax), "+d"(c));
}

void credits()
{
    const char* lines[] = {
        "",
        "Bubble Universe",
        "by Movie Vertigo",
        "youtube.com/movievertigo",
        "twitter.com/movievertigo",
        ""
    };

    for (int i = 0; i < sizeof(lines)/sizeof(lines[0]); ++i)
    {
        int padding = (80 - strlen(lines[i])) / 2;
        for (int j = 0; j < padding; ++j) { printChar(' '); }
        for (int j = 0; j < strlen(lines[i]); ++j) { printChar(lines[i][j]); }
        printChar('\r'); printChar('\n');
    }
}

uint8_t backBuffer[SCREENBUFFERSIZE];
uint8_t* lineAddresses[SCREENHEIGHT];
uint8_t* screenBuffer;

void calcLineAddresses()
{
    for (int i = 0; i < SCREENHEIGHT; ++i)
    {
        lineAddresses[i] = backBuffer + i * SCREENWIDTH;
    }
}

void bubbleUniverse()
{
    int32_t scaleMul = SCALEMUL<<8;
    int32_t scaleMulX = (scaleMul*ASPECTMUL)>>8;
    int32_t animSpeed = INITANIMSPEED;
    int32_t oldAnimSpeed = 0;
    int32_t animTime = 0;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
    uint8_t debug = 0;

    while (1)
    {
        clearBuffer(backBuffer, 0, SCREENBUFFERSIZE/4);
        if (debug) setPaletteEntry(0, 0, 0, 63);

        int32_t ang1Start = animTime & (SINTABLEENTRIES - 1);
        int32_t ang2Start = animTime & (SINTABLEENTRIES - 1);

        int32_t smX = scaleMulX >> 8;
        int32_t sm = scaleMul >> 8;
        int32_t xOff = (xOffset * smX) + ((SCREENWIDTH >> 1) << SINTABLEPOWER);
        int32_t yOff = (yOffset * sm) + ((SCREENHEIGHT >> 1) << SINTABLEPOWER);

        for (int i = 0; i < CURVECOUNT; i += CURVESTEP)
        {
            uint32_t iCol = i & 0xF0;
            int32_t x = 0, y = 0;
            int32_t* sinAng1Start = sinTable + SINTABLEENTRIES + ang1Start;
            int32_t* sinAng2Start = sinTable + SINTABLEENTRIES + ang2Start; 
            for (int jBase = 0; jBase < ITERATIONS; jBase += (1<<4))
            {
                uint32_t col = iCol | (jBase >> 4);

#define JLOOP \
                    "movl (%[sinAng1Start], %[x], 4), %%ebx \n" \
                    "addl (%[sinAng2Start], %[y], 4), %%ebx \n" \
                    "movswl %%bx, %%eax \n" \
                    "sarl $16, %%ebx \n" \
                    "movl %%eax, %[x] \n" \
                    "imull %[smX], %%eax \n" \
                    "movl %%ebx, %[y] \n" \
                    "imull %[sm], %%ebx \n" \
                    "addl %[xOff], %%eax \n" \
                    "addl %[yOff], %%ebx \n" \
                    "sarl $" STRINGIZE(SINTABLEPOWER) ", %%eax \n" \
                    "sarl $" STRINGIZE(SINTABLEPOWER) ", %%ebx \n" \
 \
                    "cmp $" STRINGIZE(SCREENHEIGHT) ", %%ebx \n" \
                    "jnb 1f \n" \
                    "movl %c[lineAddresses](, %%ebx, 4), %%ebx \n" \
                    "cmp $" STRINGIZE(SCREENWIDTH) ", %%eax \n" \
                    "jnb 1f \n" \
\
                    "movb %b[col], (%%eax, %%ebx) \n" \
                    "1: \n" \

                asm volatile (
                    JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP JLOOP
 
                    : [x]"+r"(x), [y]"+r"(y)
                    : [sinAng1Start]"r"(sinAng1Start), [sinAng2Start]"r"(sinAng2Start),
                      [smX]"rm"(smX), [sm]"rm"(sm), [xOff]"rm"(xOff), [yOff]"rm"(yOff),
                      [col]"q"(col), [lineAddresses]"i"(lineAddresses)
                    : "memory", "cc", "eax", "ebx"
                );
            }

            ang1Start = (ang1Start + ANG1INC) & (SINTABLEENTRIES - 1);
            ang2Start = (ang2Start + ANG2INC) & (SINTABLEENTRIES - 1);
        }

        animTime += animSpeed;

        if (keyState[KEY_ESCAPE]) break;
        if (keyState[KEY_HOME]) { scaleMul = SCALEMUL<<8; scaleMulX = (scaleMul*ASPECTMUL)>>8; animSpeed = INITANIMSPEED; xOffset = 0; yOffset = 0; }
        if (keyState[KEY_MINUS]) animSpeed -= ANIMSPEEDCHANGE;
        if (keyState[KEY_EQUAL]) animSpeed += ANIMSPEEDCHANGE;
        if (keyState[KEY_RETURN]) { scaleMul = (scaleMul * SCALEINSPEED)>>8; scaleMulX = (scaleMul*ASPECTMUL)>>8; }
        if (keyState[KEY_BACKSPACE]) { scaleMul = (scaleMul * SCALEOUTSPEED)>>8; scaleMulX = (scaleMul*ASPECTMUL)>>8; }
        if (keyState[KEY_SPACE] && !oldSpaceState) { if (animSpeed == 0) { animSpeed = oldAnimSpeed; } else { oldAnimSpeed = animSpeed; animSpeed = 0; } }
        if (keyState[KEY_SLASH] && !oldSlashState) { debug = !debug; setPaletteEntry(0, 0, 0, 0); }
        if (keyState[KEY_LEFT]) xOffset += MOVESPEED/scaleMul;
        if (keyState[KEY_RIGHT]) xOffset -= MOVESPEED/scaleMul;
        if (keyState[KEY_UP]) yOffset += MOVESPEED/scaleMul;
        if (keyState[KEY_DOWN]) yOffset -= MOVESPEED/scaleMul;

        oldSpaceState = keyState[KEY_SPACE];
        oldSlashState = keyState[KEY_SLASH];

        if (debug) setPaletteEntry(0, 0, 0, 0);
        vsync();
        if (debug) setPaletteEntry(0, 63, 0, 0);
        copyBuffer(backBuffer, screenBuffer, SCREENBUFFERSIZE/4);
        if (debug) setPaletteEntry(0, 0, 63, 0);
    }
}

int main()
{
    __djgpp_nearptr_enable();
    installKeyHandler();

    // Set VGA Mode 13h (320x200 x 256)
    setMode(0x13);

    screenBuffer = (uint8_t*)(SCREENBUFFER + __djgpp_conventional_base);

    setPalette();
    expandSinTable();
    calcLineAddresses();

    bubbleUniverse(screenBuffer, backBuffer);
    // Restore text mode
    setMode(0x03);

    credits();

    removeKeyHandler();
    __djgpp_nearptr_disable();

    return 0;
}
