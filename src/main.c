
/// INCLUDES und DEFINES ///
#include <stm32f0xx.h>
#include "clock_.h"
#include <stdio.h>
#include <string.h>


#define BAUDRATE 115200
#define APB_FREQ 48000000


/// Globale Spielvariablen ///
typedef enum {  //state machines
    STATE_INIT,
    STATE_GAME,
    STATE_RESULT
} GameState;

GameState state = STATE_INIT;

uint8_t board[10][10] = {
    { 5,5,5,5,5,0,0,0,0,0 },
    { 0,0,0,0,0,0,0,3,3,3 },
    { 0,4,4,4,4,0,0,0,0,0 },
    { 0,0,0,0,0,0,0,3,3,3 },
    { 4,4,4,4,0,0,0,0,0,0 },
    { 0,0,0,0,0,0,0,2,2,0 },
    { 0,0,2,2,0,2,0,0,0,0 },
    { 0,0,0,0,0,2,0,0,3,0 },
    { 0,0,2,2,0,0,0,0,3,0 },
    { 0,0,0,0,0,0,0,0,3,0 }
};
uint8_t original_board[10][10];

int opponent_checksum[10] = {0};
int row_hits[10] = {0};  // für schusslogik
int my_remaining = 30;

int next_x = 0;
int next_y = 0;

int sf_received = 0;
char opponent_sf[10][11];
int game_over_board_sent = 0;



/// Systemfunktionen & UART ///

int _write(int handle, char* data, int size) {
    for (int i = 0; i < size; i++) {
        while (!(USART2->ISR & USART_ISR_TXE));
        USART2->TDR = data[i];
    }
    return size;
}


/// Spielstart & Reset ///

void reset_game_state() {
    memcpy(board, original_board, sizeof(board));
    my_remaining = 30;
    next_x = 0;
    next_y = 0;
    sf_received = 0;
    game_over_board_sent = 0;
    state = STATE_INIT;

    memset(opponent_checksum, 0, sizeof(opponent_checksum));
    memset(opponent_sf, 0, sizeof(opponent_sf));
    memset(row_hits, 0, sizeof(row_hits));
}

void handle_start(const char* line){
    if (strncmp(line, "HD_START", 8) == 0) {
        printf("DH_START_KROME\n");
        state = STATE_GAME;
    }
}

/// CHecksumme und Gegnerdaten ///

void send_own_checksum() {
    printf("DH_CS_");
    for (int row = 0; row < 10; row++) {
        int count = 0;
        for (int col = 0; col < 10; col++) {
            if (board[row][col] != 0) {
                count++;
            }
        }
        printf("%d", count);
    }
    printf("\n");
}

void handle_checksum(const char* line) {

    if (strncmp(line, "HD_CS_", 6) == 0) {

        for (int i = 0; i < 10; i++) {
            
            opponent_checksum[i] = line[6 + i] - '0';
        }

        send_own_checksum();
    }
}

void handle_sf_line(const char* line) {
    if (strncmp(line, "HD_SF", 5) == 0) {
        int row = line[5] - '0';
        strncpy(opponent_sf[row], &line[7], 10);
        
        sf_received++;
    }
}


/// Speillogik und aktoinen ///

void send_next_shot() {
    while (next_x < 10) {
        if (opponent_checksum[next_x] == 0) { 
            next_x++;
            next_y = 0;
            continue;
        }

        if (row_hits[next_x] >= opponent_checksum[next_x]) { // könnte man glaub ich mit der funktion drüber kombinieren 
            next_x++;
            next_y = 0;
            continue;
        }

        while (next_y < 10) {
            if (opponent_sf[next_x][next_y] == '0') {
                printf("DH_BOOM_%d_%d\n", next_x, next_y);
                next_y++;
                return;
            }
            next_y++;
        }

        next_x++;
        next_y = 0;
    }

    printf("DH_BOOM_0_0\n");
}

void handle_incoming_shot(const char* line) {
    int x, y;
    if (sscanf(line, "HD_BOOM_%d_%d", &x, &y) == 2) {
        if (x >= 0 && x < 10 && y >= 0 && y < 10 && board[x][y] != 0) {
            board[x][y] = 0;
            my_remaining--;
            if (my_remaining == 0 && !game_over_board_sent) {
                state = STATE_RESULT;
                send_game_over_board();
                return;
            }
            printf("DH_BOOM_H\n");
        } else {
            printf("DH_BOOM_M\n");
        }
        send_next_shot();
    } else if (strncmp(line, "DH_BOOM_H", 9) == 0) {
        if (next_x > 0 && next_x <= 10) {
            row_hits[next_x - 1]++;
        }
    }
}

/// Soielende ///
void send_game_over_board() {
    for (int row = 0; row < 10; row++) {
        printf("DH_SF%dD", row);
        for (int col = 0; col < 10; col++) {
            printf("%d", original_board[row][col]);
        }
        printf("\n");
    }
    game_over_board_sent = 1;
}

/// Hauptprotokoll ///

void handle_line(const char* line) {
    if (state == STATE_INIT) {
        handle_start(line); //erkennnt HD_START und antwortet mit DH_START_KROME
    } else if (state == STATE_GAME) {
        handle_checksum(line);
        handle_incoming_shot(line);

        if (strncmp(line, "HD_SF", 5) == 0) {
            state = STATE_RESULT;
        }

    } else if (state == STATE_RESULT) {
        if (strncmp(line, "HD_SF", 5) == 0) {
            handle_sf_line(line);

            if (sf_received == 10) {
                if (!game_over_board_sent) {
                    send_game_over_board();
                }
                reset_game_state();
            }
        }
    }
}
/// main ///
int main(void) {
    memcpy(original_board, board, sizeof(board));

    SystemClock_Config();
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    GPIOA->MODER |= (0b10 << (2 * 2)) | (0b10 << (3 * 2));
    GPIOA->AFR[0] |= (0b0001 << (4 * 2)) | (0b0001 << (4 * 3));
    USART2->BRR = APB_FREQ / BAUDRATE;
    USART2->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    char line[64]; //????
    int idx = 0; //???

    while (1) {
        while (!(USART2->ISR & USART_ISR_RXNE));
        char c = USART2->RDR; //init von usart 2 für serielle komunikation

        if (c == '\n') {
            line[idx] = '\0'; //????
            handle_line(line);
            idx = 0;
        } else if (idx < sizeof(line) - 1) {
            line[idx++] = c;
        }
    }
}