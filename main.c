#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define WIDTH  64
#define HEIGHT 32
#define SCALE  10

#define SAMPLE_RATE 44100
#define BUZZ_FREQ 440
#define BUZZ_DURATION_MS 100
#define BUZZ_SAMPLES (SAMPLE_RATE * BUZZ_DURATION_MS / 1000)

const int MEM_CODE_START = 0x0200;
const int DISP_ROWS = 32;
const int DISP_COLS = 64;
const int STACK_SIZE = 48;
uint32_t TIMESTAMP_1 = 0;
uint32_t TIMESTAMP_2 = 0;
const int INSTRUCTIONS_PER_FRAME = 10;

//functions
//---------------------------------------------------------------------
int getSdlScancode(uint8_t key);
uint8_t getKeyCode(const uint8_t *keyboardState);
uint8_t getReleaseCode(const uint8_t *keyboardState, uint8_t scancode);
void reRender(uint32_t pixels[], SDL_Renderer *renderer, SDL_Texture *texture);
void clearScreen(uint32_t pixels[], SDL_Renderer *renderer, SDL_Texture *texture);
void updateDisplay(uint8_t display[][DISP_COLS], uint8_t buffer[], uint16_t regI, int row, int column, int height, uint8_t *VF);
void drawDisplay(uint8_t display[][DISP_COLS], uint32_t pixels[], SDL_Renderer *renderer, SDL_Texture *texture);

//main
//---------------------------------------------------------------------
int main(int argc, char* argv[])
{
    srand(time(NULL));
    //initializing buffer
    //---------------------------------------------------------------------
    int stack[STACK_SIZE] = {};
    uint8_t display[DISP_ROWS][DISP_COLS]  = {};
    uint8_t buffer[4096] = {0xF0, 0x90, 0x90, 0x90, 0xF0,   // 0
                            0x20, 0x60, 0x20, 0x20, 0x70,   // 1
                            0xF0, 0x10, 0xF0, 0x80, 0xF0,   // 2
                            0xF0, 0x10, 0xF0, 0x10, 0xF0,   // 3
                            0x90, 0x90, 0xF0, 0x10, 0x10,   // 4
                            0xF0, 0x80, 0xF0, 0x10, 0xF0,   // 5
                            0xF0, 0x80, 0xF0, 0x90, 0xF0,   // 6
                            0xF0, 0x10, 0x20, 0x40, 0x40,   // 7
                            0xF0, 0x90, 0xF0, 0x90, 0xF0,   // 8
                            0xF0, 0x90, 0xF0, 0x10, 0xF0,   // 9
                            0xF0, 0x90, 0xF0, 0x90, 0x90,   // A
                            0xE0, 0x90, 0xE0, 0x90, 0xE0,   // B
                            0xF0, 0x80, 0x80, 0x80, 0xF0,   // C
                            0xE0, 0x90, 0x90, 0x90, 0xE0,   // D
                            0xF0, 0x80, 0xF0, 0x80, 0xF0,   // E
                            0xF0, 0x80, 0xF0, 0x80, 0x80 }; // F
    uint8_t *bufferPtr = buffer + MEM_CODE_START;

    if(argc != 2){printf("%s\n", "Invalid argument count, shutting down\n"); return -1;}

    //reading rom data
    //---------------------------------------------------------------------
    FILE *fp;
    //fp = fopen("brix.ch8", "rb");
    fp = fopen(argv[1], "rb");
    if(fp == NULL){printf("Invalid ROM filename, shutting down\n"); return -1;};

    fseek(fp, 0, SEEK_END);
    long int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fread(bufferPtr, sizeof(uint8_t), size, fp);

    for(int i = 0; i < size; i++){
        printf("Byte %d: %x\n", i, buffer[i + MEM_CODE_START]);
    }

    //creating hardware variables
    //---------------------------------------------------------------------
    uint8_t regV[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint16_t regI = 0;
    int sp = 0;
    int pc = MEM_CODE_START;
    uint8_t delayTimer = 0;
    uint8_t soundTimer = 0;

    //creating emulator variables
    //---------------------------------------------------------------------
    uint16_t opcode = 0;
    uint8_t nibbleTwoX = 0;
    uint8_t nibbleThreeY = 0;
    uint8_t nibbleFourN = 0;
    uint8_t byteTwoNN = 0;
    uint16_t TwoThreeFourNNN = 0;
    uint8_t tempMisc8B = 0;
    uint8_t tempMisc8B2 = 0;
    uint32_t tempMisc32B = 0;
    bool takeInput = false;
    bool checkRelease = false;
    const uint8_t *keyboardState = NULL;

    //initializing the SDL display loop
    //---------------------------------------------------------------------
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Window *window = SDL_CreateWindow("Chip-8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH * SCALE, HEIGHT * SCALE, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    uint32_t pixels[WIDTH * HEIGHT];

    //Audio Stuff
    //---------------------------------------------------------------------
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 2048;
    want.callback = NULL;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    uint8_t buzzer[BUZZ_SAMPLES];
    int period = SAMPLE_RATE / BUZZ_FREQ;
    for (int i = 0; i < BUZZ_SAMPLES; i++) {
        buzzer[i] = (i % period < period / 2) ? 255 : 0;
    }

    SDL_PauseAudioDevice(dev, 0);

    //main loop
    //---------------------------------------------------------------------
    for(;;){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT) {goto END_PROGRAM;}
        }

        //timer updates
        //---------------------------------------------------------------------
        for(;;){
            TIMESTAMP_2 = SDL_GetTicks();
            if(TIMESTAMP_2 - TIMESTAMP_1 >= 16)
                break;
        }
        TIMESTAMP_1 = TIMESTAMP_2;

        //beeping it up
        //---------------------------------------------------------------------
        if(soundTimer){
                SDL_QueueAudio(dev, buzzer, sizeof(buzzer));
                soundTimer -= 1;
        }
        else{SDL_ClearQueuedAudio(dev);}

        //if(soundTimer) {soundTimer -= 1;}
        if(delayTimer) {delayTimer -= 1;}

        //keyboard input for
        //---------------------------------------------------------------------
        if(takeInput){
            keyboardState = SDL_GetKeyboardState(NULL);
            if(checkRelease){
                tempMisc8B2 = getReleaseCode(keyboardState, tempMisc8B);
                if(tempMisc8B2 != 0xFF){
                    regV[nibbleTwoX] = tempMisc8B2;
                    checkRelease = false;
                    takeInput = false;
                    goto INSTRUCTION_CYCLE;
                }
                else {goto SKIP_INSTRUCTION_CYCLE;}
            }
            tempMisc8B = getKeyCode(keyboardState);
            if(tempMisc8B){checkRelease = true;}
            goto SKIP_INSTRUCTION_CYCLE;
        }

        //batch instruction execution
        //---------------------------------------------------------------------
        INSTRUCTION_CYCLE:
        for(int i = 0; i < INSTRUCTIONS_PER_FRAME; i++){
            //fetch
            //---------------------------------------------------------------------
            opcode = buffer[pc];
            opcode <<= 8;
            opcode |= buffer[pc+1];
            pc+=2;

            //decode
            //---------------------------------------------------------------------
            nibbleTwoX = (opcode & 0x0F00) >> 8;
            nibbleThreeY = (opcode & 0x00F0) >> 4;
            nibbleFourN = (opcode & 0x000F);
            byteTwoNN = (opcode & 0x00FF);
            TwoThreeFourNNN = (opcode & 0x0FFF);

            //execute
            //---------------------------------------------------------------------
            switch((opcode & 0xF000) >> 12){
            case 0x00:
                switch(opcode & 0x00FF){
                case 0xE0:
                    for (int i = 0; i < WIDTH * HEIGHT; ++i) {pixels[i] = 0xFF000000;}
                    for(int r = 0; r < DISP_ROWS; r++){
                        for(int c = 0; c < DISP_COLS; c++){
                            display[r][c] = 0;
                        }
                    }
                    reRender(pixels, renderer, texture);
                    break;
                case 0xEE:
                    sp -= 1;
                    pc = stack[sp];
                    break;
                }
                break;
            case 0x01:
                pc = TwoThreeFourNNN;
                break;
            case 0x02:
                stack[sp] = pc;
                sp += 1;
                pc = TwoThreeFourNNN;
                break;
            case 0x03:
                if(regV[nibbleTwoX] == byteTwoNN) {pc += 2;}
                break;
            case 0x04:
                if(regV[nibbleTwoX] != byteTwoNN) {pc += 2;}
                break;
            case 0x05:
                if(regV[nibbleTwoX] == regV[nibbleThreeY]) {pc += 2;}
                break;
            case 0x06:
                regV[nibbleTwoX] = (opcode & 0x00FF);
                break;
            case 0x07:
                regV[nibbleTwoX] += (opcode & 0x00FF);
                break;
            case 0x08:
                switch(opcode & 0x000F){
                case 0x00:
                    regV[nibbleTwoX] = regV[nibbleThreeY];
                    break;
                case 0x01:
                    regV[nibbleTwoX] |= regV[nibbleThreeY];
                    break;
                case 0x02:
                    regV[nibbleTwoX] &= regV[nibbleThreeY];
                    break;
                case 0x03:
                    regV[nibbleTwoX] ^= regV[nibbleThreeY];
                    break;
                case 0x04:
                    tempMisc8B = regV[nibbleTwoX];
                    regV[nibbleTwoX] += regV[nibbleThreeY];
                    regV[0x0F] = (regV[nibbleTwoX] < tempMisc8B) ? 1 : 0;
                    break;
                case 0x05:
                    tempMisc8B = (regV[nibbleTwoX] >= regV[nibbleThreeY]) ? 1 : 0;
                    regV[nibbleTwoX] -= regV[nibbleThreeY];
                    regV[0x0F] = tempMisc8B;
                    break;
                case 0x06:
                    regV[nibbleTwoX] = regV[nibbleThreeY];
                    tempMisc8B = (regV[nibbleTwoX] & 0x01);
                    regV[nibbleTwoX] >>= 1;
                    regV[0x0F] = tempMisc8B;
                    break;
                case 0x07:
                    tempMisc8B = (regV[nibbleThreeY] >= regV[nibbleTwoX]) ? 1 : 0;
                    regV[nibbleTwoX] = regV[nibbleThreeY] - regV[nibbleTwoX];
                    regV[0x0F] = tempMisc8B;
                    break;
                case 0x0E:
                    regV[nibbleTwoX] = regV[nibbleThreeY];
                    tempMisc8B = ((regV[nibbleTwoX] & 0x80) >> 7);
                    regV[nibbleTwoX] <<= 1;
                    regV[0x0F] = tempMisc8B;
                    break;
                }
                break;
            case 0x09:
                if(regV[nibbleTwoX] != regV[nibbleThreeY]) {pc += 2;}
                break;
            case 0x0A:
                regI = TwoThreeFourNNN;
                break;
            case 0x0B:
                pc = regV[0] + TwoThreeFourNNN;
                break;
            case 0x0C:
                regV[nibbleTwoX] = (rand() & byteTwoNN);
                break;
            case 0x0D:
                updateDisplay(display, buffer, regI, regV[nibbleThreeY], regV[nibbleTwoX], nibbleFourN, &regV[0x0F]);
                drawDisplay(display, pixels, renderer, texture);
                break;
            case 0x0E:
                keyboardState = SDL_GetKeyboardState(NULL);
                switch(opcode & 0x00FF){
                case 0x9E:
                    if(keyboardState[getSdlScancode(regV[nibbleTwoX] & 0x0F)])
                        pc += 2;
                    break;
                case 0xA1:
                    if(!keyboardState[getSdlScancode(regV[nibbleTwoX] & 0x0F)])
                        pc += 2;
                    break;
                }
                break;
            case 0x0F:
                switch(opcode & 0x00FF){
                case 0x07:
                    regV[nibbleTwoX] = delayTimer;
                    break;
                case 0x0A:
                    takeInput = true;
                    goto SKIP_INSTRUCTION_CYCLE;
                    break;
                case 0x15:
                    delayTimer = regV[nibbleTwoX];
                    break;
                case 0x18:
                    soundTimer = regV[nibbleTwoX];
                    if(soundTimer == 1)
                        soundTimer += 2;
                    break;
                case 0x1E:
                    regI += regV[nibbleTwoX];
                    break;
                case 0x29:
                    regI = ((regV[nibbleTwoX] & 0x0F) * 5);
                    break;
                case 0x33:
                    tempMisc8B = regV[nibbleTwoX];
                    buffer[regI + 2] = tempMisc8B % 10; tempMisc8B /= 10;
                    buffer[regI + 1] = tempMisc8B % 10; tempMisc8B /= 10;
                    buffer[regI + 0] = tempMisc8B % 10; tempMisc8B /= 10;
                    break;
                case 0x055:
                    for(int i = 0; i <= nibbleTwoX; i++, regI++) {buffer[regI] = regV[i];}
                    break;
                case 0x65:
                    for(int i = 0; i <= nibbleTwoX; i++, regI++) {regV[i] = buffer[regI];}
                    break;
                }
                break;
            }
        }
        SKIP_INSTRUCTION_CYCLE:
    }
    END_PROGRAM:
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    fclose(fp);
    return 0;
}

void reRender(uint32_t pixels[], SDL_Renderer *renderer, SDL_Texture *texture){
    SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_Rect dest_rect = {0, 0, WIDTH * SCALE, HEIGHT * SCALE};
    SDL_RenderCopy(renderer, texture, NULL, &dest_rect);
    SDL_RenderPresent(renderer);
}

void updateDisplay(uint8_t display[][DISP_COLS], uint8_t buffer[], uint16_t regI, int row, int column, int height, uint8_t *VF){
    row %= DISP_ROWS;
    column %= DISP_COLS;
    *VF = 0;
    int extractedBit = 0;
    int originalCol = column;
    for(int h = 0; h < height; h++){
        for(int i = 0; i < 8; i++){
            if(row > DISP_ROWS || column > DISP_COLS)
                {continue;}
            extractedBit = ((buffer[regI + h] << i) & 0x80) >> 7;
            if(!(*VF) && extractedBit && display[row][column])
                *VF = 1;
            display[row][column] ^= extractedBit;
            column += 1;
        }
        row += 1;
        column = originalCol;
    }
}

void drawDisplay(uint8_t display[][DISP_COLS], uint32_t pixels[], SDL_Renderer *renderer, SDL_Texture *texture){
    for(int r = 0; r < DISP_ROWS; r++){
        for(int c = 0; c < DISP_COLS; c++){
            if(display[r][c])
                {pixels[DISP_COLS*r+c] = 0xFFFFFFFF;}
            else
                {pixels[DISP_COLS*r+c] = 0xFF000000;}
        }
    }
    reRender(pixels, renderer, texture);
}

uint8_t getKeyCode(const uint8_t *keyboardState){
    if(keyboardState[SDL_SCANCODE_1]) return SDL_SCANCODE_1;
    if(keyboardState[SDL_SCANCODE_2]) return SDL_SCANCODE_2;
    if(keyboardState[SDL_SCANCODE_3]) return SDL_SCANCODE_3;
    if(keyboardState[SDL_SCANCODE_4]) return SDL_SCANCODE_4;
    if(keyboardState[SDL_SCANCODE_Q]) return SDL_SCANCODE_Q;
    if(keyboardState[SDL_SCANCODE_W]) return SDL_SCANCODE_W;
    if(keyboardState[SDL_SCANCODE_E]) return SDL_SCANCODE_E;
    if(keyboardState[SDL_SCANCODE_R]) return SDL_SCANCODE_R;
    if(keyboardState[SDL_SCANCODE_A]) return SDL_SCANCODE_A;
    if(keyboardState[SDL_SCANCODE_S]) return SDL_SCANCODE_S;
    if(keyboardState[SDL_SCANCODE_D]) return SDL_SCANCODE_D;
    if(keyboardState[SDL_SCANCODE_F]) return SDL_SCANCODE_F;
    if(keyboardState[SDL_SCANCODE_Z]) return SDL_SCANCODE_Z;
    if(keyboardState[SDL_SCANCODE_X]) return SDL_SCANCODE_X;
    if(keyboardState[SDL_SCANCODE_C]) return SDL_SCANCODE_C;
    if(keyboardState[SDL_SCANCODE_V]) return SDL_SCANCODE_V;
    return 0x00;
}

uint8_t getReleaseCode(const uint8_t *keyboardState, uint8_t scancode){
    switch(scancode){
    case SDL_SCANCODE_1:
        if(!keyboardState[SDL_SCANCODE_1]) {return 0x01;}
        break;
    case SDL_SCANCODE_2:
        if(!keyboardState[SDL_SCANCODE_2]) {return 0x02;}
        break;
    case SDL_SCANCODE_3:
        if(!keyboardState[SDL_SCANCODE_3]) {return 0x03;}
        break;
    case SDL_SCANCODE_4:
        if(!keyboardState[SDL_SCANCODE_4]) {return 0x0C;}
        break;
    case SDL_SCANCODE_Q:
        if(!keyboardState[SDL_SCANCODE_Q]) {return 0x04;}
        break;
    case SDL_SCANCODE_W:
        if(!keyboardState[SDL_SCANCODE_W]) {return 0x05;}
        break;
    case SDL_SCANCODE_E:
        if(!keyboardState[SDL_SCANCODE_E]) {return 0x06;}
        break;
    case SDL_SCANCODE_R:
        if(!keyboardState[SDL_SCANCODE_R]) {return 0x0D;}
        break;
    case SDL_SCANCODE_A:
        if(!keyboardState[SDL_SCANCODE_A]) {return 0x07;}
        break;
    case SDL_SCANCODE_S:
        if(!keyboardState[SDL_SCANCODE_S]) {return 0x08;}
        break;
    case SDL_SCANCODE_D:
        if(!keyboardState[SDL_SCANCODE_D]) {return 0x09;}
        break;
    case SDL_SCANCODE_F:
        if(!keyboardState[SDL_SCANCODE_F]) {return 0x0E;}
        break;
    case SDL_SCANCODE_Z:
        if(!keyboardState[SDL_SCANCODE_Z]) {return 0x0A;}
        break;
    case SDL_SCANCODE_X:
        if(!keyboardState[SDL_SCANCODE_X]) {return 0x00;}
        break;
    case SDL_SCANCODE_C:
        if(!keyboardState[SDL_SCANCODE_C]) {return 0x0B;}
        break;
    case SDL_SCANCODE_V:
        if(!keyboardState[SDL_SCANCODE_V]) {return 0x0F;}
        break;
    default:
        break;
    }
    return 0xFF;
}

int getSdlScancode(uint8_t key){
    switch(key){
    case 0x00: return SDL_SCANCODE_X;
    case 0x01: return SDL_SCANCODE_1;
    case 0x02: return SDL_SCANCODE_2;
    case 0x03: return SDL_SCANCODE_3;
    case 0x04: return SDL_SCANCODE_Q;
    case 0x05: return SDL_SCANCODE_W;
    case 0x06: return SDL_SCANCODE_E;
    case 0x07: return SDL_SCANCODE_A;
    case 0x08: return SDL_SCANCODE_S;
    case 0x09: return SDL_SCANCODE_D;
    case 0x0A: return SDL_SCANCODE_Z;
    case 0x0B: return SDL_SCANCODE_C;
    case 0x0C: return SDL_SCANCODE_4;
    case 0x0D: return SDL_SCANCODE_R;
    case 0x0E: return SDL_SCANCODE_F;
    case 0x0F: return SDL_SCANCODE_V;
    }
    return 0;
}
