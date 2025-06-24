#include <stm32f0xx.h>  // CMSIS_Header Zugriff auf Register
#include "clock_.h"     // bezieht clock_h 
#include <stdio.h>      // standart funktion z.B printf
#include <string.h>


#define BAUDRATE 115200
#define APB_FREQ 48000000
#define SPEAKER_PIN 8


typedef enum {
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

uint8_t original_board[10][10]; // speichert originals board   unsighn integer der 8 bit lang ist uint8_t???

// init Variabeln
int opponent_checksum[10] = {0};
int row_hits[10] = {0}; // Treffer pro Zeile zählen
int my_remaining = 30;

int current_row = 0;
int current_col = 0;

int sf_received = 0;
char opponent_sf[10][11];
int game_over_board_sent = 0;

char line[64];  // Puffer für empfangende Textzeile über UART
int idx = 0;    // Index um empfangede Zeichen im Puffer 


//Ausgabe nicht per Terminal sonder über UART schnittstelle
int _write(int handle, char* data, int size) { // wenn printf ausgeführt wird, ruft sie intern diese Funktion auf
    for (int i = 0; i < size; i++) {
        while (!(USART2->ISR & USART_ISR_TXE));
        USART2->TDR = data[i];
    }
    return size;
}
// -------- speaker -------------//

//Hohes Piepsen (Gibt kurzen hohen piebsto aus)
void speaker_high(void) {
    uint32_t arr = (APB_FREQ / 1047) - 1;  
    TIM3->ARR = arr;
    TIM3->CCR3 = arr / 2;
    for (volatile int i = 0; i < APB_FREQ / 80; i++) __NOP();  
    TIM3->CCR3 = 0;
}


//Tiefes Piepsen (Gibt kurzen tieferen Piebston aus)
void speaker_low(void) {
    uint32_t arr = (APB_FREQ ) - 1;   
    TIM3->ARR = arr;
    TIM3->CCR3 = arr / 2;
    for (volatile int i = 0; i < APB_FREQ / 80; i++) __NOP();  
    TIM3->CCR3 = 0;
}





// -------------------------------- START --------------------//



void handshake(void){  // nachteil das ich arguemnte immer mit übergeben muss 
    if (strncmp(line, "HD_START", 8) == 0) {
        printf("DH_START_KROME\n");
        state = STATE_GAME;
    }
}

//  ---------------------- Checksum austausch -------------------------- //

//verarbeitet gegner checksum
void read_checksum(void) {
    
        for (int i = 0; i < 10; i++) {
            opponent_checksum[i] = line[6 + i] - '0'; //checksum starte ab dem 7. Zeichen
        }                                             //string line werden die Ziffern als ASCII zeichen gespecichert. -0 heißt minus 48 erzeugt wieder den richtigen zaheln wert im arraay gespeichert

}

//erstellt und senden meine CS
void send_own_checksum() {                          
    printf("DH_CS_");
    for (int row = 0; row < 10; row++) {        //durchläuft die Zeilen 
        int possible_targets = 0;               //damit wir beim nächsten durchlauf ganz links anfangen
        for (int col = 0; col < 10; col++) {    //durchläuft die Reihen 
            if (board[row][col] != 0) {         //zählt pro zeile die Felder ungelich 0
                possible_targets++;
            }
        }
        printf("%d", possible_targets);         //ausgabe mögliche treffer wird ginter DH_CS_gehahngen
    }
    printf("\n");       //Ende der der Nachricht
}




//----------------------------------------------------------- Schussabfolge -----------------------------------------------------------------------//


// werde beschossen
void handle_incoming_shot(void) {
    int x, y;
    if (sscanf(line, "HD_BOOM_%d_%d", &x, &y) == 2) {                        //Nachricht lesen und zwei Werte aus eingabe speichern
        if (x >= 0 && x < 10 && y >= 0 && y < 10 && board[x][y] != 0) { //prüft ob xy in spielfeld größe und ob das welt ungleich NUll ist
            board[x][y] = 0; //wenn ungleich dann FEld auf Nullsetzen, weil SCHiffsteil wurde getroffen
            my_remaining--;   // meine schiffsteile runterzäglen
            if (my_remaining == 0 && !game_over_board_sent) { // ist der auslöser dafür das ich verloren habe und mein board schicke
                speaker_high();     // Tiefer Ton bei Niederlage
                send_game_over_board(); // sendet sofort mein board 
                
                state = STATE_RESULT; 
                return; // verhindert das ich noch schieße obwohl ich verloren habe weil sonst der code hier srutner ausgeführt wird
            }
            printf("DH_BOOM_H\n");
        } else {
            printf("DH_BOOM_M\n");
        }
        send_next_shot(); //antowrte mit schuss von mir
    }
}

// mein Schuss mit Schussstrategie
void send_next_shot() {
    while (current_row < 10) {  //geht Zeile für Zeile durch
        if (opponent_checksum[current_row] == 0) { //prüft ob die Zeile ziffer Null in der CS hat, dann wird übersprungen
            current_row++; 
            current_col = 0;
            continue; //Überspringt den nächsten schleifen Punkt also das schißen 
        }

        if (row_hits[current_row] >= opponent_checksum[current_row]) { //prüft ob getroffene Felder = anzahl möglicher felder 
            current_row++;
            current_col = 0;
            continue;
        }

        if (current_col < 10) { // Schuss muss im SPielfeld bleiben
            printf("DH_BOOM_%d_%d\n", current_row, current_col); //es wird nach einander Hchgezählt 
            current_col++; //damit wird ein wert höher für den nächsten schuss gewäht
            return;
        } else { //wenn wir durch alle 10 felder in der zeile druch sind in nääcshte zeile springen
            current_col = 0;
            current_row++;
        }
    }

    // falls kein weiterer sinnvoller schuss aber kann nicht passieren 
    //printf("DH_BOOM_1_1\n");
}

// -------------------------------------------- Spiel vorbei --------------------------------------------//


void handle_sf_line(void) {
    int row = line[5] - '0'; // -48 wegen ASCI 
    strncpy(opponent_sf[row], &line[7], 10); //Ziel: String row im 2D Array opponent_sf QUelle: Adressendesn8. Zeichens Länge: genau 10 Zeichen
    opponent_sf[row][10] = '\0';
    sf_received++;
}

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


//setzt alle Spielvariablen in Anfangszusatnd zurück
void reset_game_state() {
    memcpy(board, original_board, sizeof(board));

    
    my_remaining = 30;
    current_row = 0;
    current_col = 0;
    sf_received = 0;
    game_over_board_sent = 0;
    state = STATE_INIT;
    
    
    //memset() sorgt dafür das keine Datenreste zurück bleiben 
    memset(opponent_checksum, 0, sizeof(opponent_checksum));  // sagen das mich da auf die idee gebracht hat
    memset(opponent_sf, 0, sizeof(opponent_sf));
    memset(row_hits, 0, sizeof(row_hits));
}



// ------------ Schummeln ------------------ //

// prüft ob Board von Gegner zu Checksum gegner passt
void verify_opponent_board() {
    for (int row = 0; row < 10; row++) {
        int count = 0;
        for (int col = 0; col < 10; col++) {
            if (opponent_sf[row][col] != '0') {
                count++;
            }
        }

        if (count != opponent_checksum[row]) {
            printf("es wurde geschummelt"); // falsche Nachricht Programm bricht ab
        } 
    }
}




void nachricht_handler(void) {  //
    if (state == STATE_INIT) {
        handshake(); //verarbeitet Start Nachroht sendet start
    }

    else if (state == STATE_GAME) {

        if (strncmp(line, "HD_CS_", 6) == 0) {          // CS Austausch
            read_checksum();
            send_own_checksum();
        }

        else if (strncmp(line, "HD_BOOM_", 8) == 0) {
            handle_incoming_shot();                     // verarbeitet Schussabfolge
        }
                
        else if (strncmp(line, "HD_SF", 5) == 0) {      // Gegner hat verloren und schickt sein Spielfeld
            //sf_received = 0;                            //zähler allgemin zurücksetzten
            state = STATE_RESULT;
            handle_sf_line();                           //verarbeitet Schlussabfolge speichert erste erhaltene HD_SF zeile ud setzt sf_received auf 1
        }                                               //Zeile 0 wird sofort gespeichert in oppoent_sf Row
    }

    else if (state == STATE_RESULT) {                   //my remaining = 0 oder HD_SF Zeile empfagnen
        
        if (strncmp(line, "HD_SF", 5) == 0) {           //erkennt sf zeilen
            handle_sf_line();                           // speichert wert und erhöht sf_received

            if (sf_received == 10) {                    //ganzes gegener board erhalten?
                
                                                        //Nur wenn ich gewonnen habe, habe ich noch kein Board geschickt. Also ganes gegner Board erhalten also schicke ich mein
                if (!game_over_board_sent) {
                    speaker_low();    // Hoher Ton bei Sieg            //verhindert wenn ich gewonnen habe das nochmal ein board geschcikt wird
                    send_game_over_board();
                    
                }

                verify_opponent_board();
                reset_game_state();                     // alles auf anfang: Arrays zurück, zählernulle sate auf state init setzten 
            }
        }
    }
}

int main(void) {
    //board wird gesichert
    memcpy(original_board, board, sizeof(board)); // Ziel: Array quelle: board, alles kopieren

    SystemClock_Config();
    // Initialisiert USART2 für UART-Kommunikation auf PA2 (TX) und PA3 (RX)

    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;                          // Takt für GPIOA aktivieren
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;                       // Takt für USART2 aktivieren

    // PA2 (TX) konfigurieren:
    GPIOA->MODER |= 0b10 << (2 * 2);                            // AF-Modus (10) für PA2
    GPIOA->AFR[0] |= 0b0001 << (4 * 2);                         // AF1 = USART2_TX auf PA2

    // PA3 (RX) konfigurieren:
    GPIOA->MODER |= 0b10 << (3 * 2);                            // AF-Modus (10) für PA3
    GPIOA->AFR[0] |= 0b0001 << (4 * 3);                         // AF1 = USART2_RX auf PA3

    USART2->BRR = APB_FREQ / BAUDRATE;                          // Baudrate einstellen (z. B. 48000000 / 115200)
    USART2->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;  // Transmitter aktivieren, Receiver aktivieren, USART aktivieren

    // init speaker
    
    RCC->AHBENR  |= RCC_AHBENR_GPIOCEN;         // Takt für GPIOC aktivieren
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;         // Takt für Timer 3 aktivieren

    // PC8 in Alternativfunktionsmodus setzen (AF = 10)
    GPIOC->MODER &= ~(0b11 << (2 * SPEAKER_PIN)); // Bits löschen
    GPIOC->MODER |=  (0b10 << (2 * SPEAKER_PIN)); // AF-Modus setzen

    // PC8 auf AF0 (Timer3_CH3) konfigurieren
    GPIOC->AFR[1]  &= ~(0xF << ((SPEAKER_PIN - 8) * 4)); // AF-Feld löschen
    GPIOC->AFR[1]  |=  (0x0 << ((SPEAKER_PIN - 8) * 4)); // AF0 setzen

    GPIOC->OSPEEDR |= 0b11 << (2 * SPEAKER_PIN); // hohe Ausgangsgeschwindigkeit

    TIM3->PSC = 0;                    // Prescaler auf 0 → 48 MHz Timer-Frequenz
    TIM3->CCMR2 &= ~(0xFF);          // Kanal-Register löschen (Channel 3 & 4)
    TIM3->CCMR2 |= 0b110 << 4;       // PWM Mode 1 für Channel 3 (OC3M = 110)
    TIM3->CCER |= 1 << 8;            // Channel 3 Output Enable (CC3E = 1)
    TIM3->CR1 |= 1;                  // Timer starten (CEN = 1)



    

    while (1) {  //Endlosschleife
        while (!(USART2->ISR & USART_ISR_RXNE));  //wartet auf Zeichen per UART
        char c = USART2->RDR;                     // Zeichen aus UART register lesen

        if (c == '\n') {                          // \n gibt das Ende der Nachricht an 
            line[idx] = '\0';                     // Nullterminierung macht aus Array ein string
            nachricht_handler();
            idx = 0;
        }
        //solange kein Zeilenumbruch, speicher die Zeichen im Puffer
        else if (idx < sizeof(line) - 1) { //SChutz gegen Überlauf
            line[idx++] = c;
        }
    }
}