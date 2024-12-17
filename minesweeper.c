//Copyright - Darren Banfi 2024
#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <proto/exec.h>  // Required for Delay()
#include <devices/audio.h>
#include <exec/io.h>
#include <graphics/text.h> // Required for TextLength()

#define GRID_SIZE 10
#define CELL_SIZE 20
#define NUM_MINES 10
#define GRID_OFFSET_X 10
#define GRID_OFFSET_Y 30
#define BUTTON_WIDTH 80
#define BUTTON_HEIGHT 20
#define BUTTON_X (GRID_OFFSET_X + (GRID_SIZE * CELL_SIZE) / 2 - BUTTON_WIDTH / 2) // Centered horizontally
#define BUTTON_Y (GRID_OFFSET_Y + GRID_SIZE * CELL_SIZE + 5) // Just below the grid

//This is a queue to support the fast flood fill of the revel blocks
typedef struct {
    int x, y;
} GridPoint;

#define MAX_QUEUE_SIZE (GRID_SIZE * GRID_SIZE)

typedef struct {
    GridPoint points[MAX_QUEUE_SIZE];
    int front, rear;
} Queue;

void initQueue(Queue *q) {
    q->front = 0;
    q->rear = 0;
}

int isQueueEmpty(Queue *q) {
    return q->front == q->rear;
}

void enqueue(Queue *q, int x, int y) {
    if ((q->rear + 1) % MAX_QUEUE_SIZE != q->front) { // Check for overflow
        q->points[q->rear].x = x;
        q->points[q->rear].y = y;
        q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
    }
}

GridPoint dequeue(Queue *q) {
    GridPoint p = { -1, -1 };
    if (!isQueueEmpty(q)) {
        p = q->points[q->front];
        q->front = (q->front + 1) % MAX_QUEUE_SIZE;
    }
    return p;
}
//End of queue

// Minesweeper grid cell
typedef struct {
    int isMine;        // 1 if mine
    int isRevealed;    // 1 if revealed
    int isFlagged;     // 1 if flagged
    int adjacentMines; // Number of adjacent mines
} Cell;

// Global variables
struct Window *win;
Cell grid[GRID_SIZE][GRID_SIZE];
int running = 1;

// Function prototypes
void initGame();
void drawGrid();
void closeGame();
void handleEvents();
void revealCell(int x, int y);
void placeMines();
void Delay(ULONG ticks);
void toggleFlag(int x, int y);
void resetGame();
void checkWinCondition();
void displayMessage(const char *message);
void drawExplosion(int x, int y);
void shakeWindow(struct Window *win);
void playBeep();

int main() {
    // Open the Intuition window
win = OpenWindowTags(NULL,
    WA_Left, 20,
    WA_Top, 20,
    WA_Width, GRID_SIZE * CELL_SIZE + GRID_OFFSET_X * 2,
    WA_Height, GRID_SIZE * CELL_SIZE + GRID_OFFSET_Y * 2 + BUTTON_HEIGHT + 20, // Adjusted height
    WA_Title, (ULONG)"AmIga Minesweeper",
    WA_Flags, WFLG_CLOSEGADGET | WFLG_DEPTHGADGET | WFLG_DRAGBAR | WFLG_ACTIVATE | WFLG_RMBTRAP,
    WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS,
    TAG_DONE);


    if (!win) {
        printf("Error: Failed to open Intuition window\n");
        return 1;
    }

    // Initialize the game
    initGame();
    drawGrid();

    // Main event loop
    handleEvents();

    // Cleanup
    closeGame();
    return 0;
}
void calculateAdjacentMines() {
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            if (grid[y][x].isMine) continue;

            int count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx >= 0 && nx < GRID_SIZE && ny >= 0 && ny < GRID_SIZE) {
                        if (grid[ny][nx].isMine) count++;
                    }
                }
            }
            grid[y][x].adjacentMines = count;
        }
    }
}

void initGame() {
    memset(grid, 0, sizeof(grid));
    placeMines();
    calculateAdjacentMines();
}
void setPointerBusy(struct Window *win) {
    SetWindowPointer(win,
        WA_BusyPointer, TRUE,
        TAG_DONE);

    // Force Intuition to process the pointer change
    for (int i = 0; i < 2; i++) {
        WaitTOF(); // Wait for two vertical blanks (forces a refresh)
    }
}

void setPointerNormal(struct Window *win) {
    SetWindowPointer(win,
        WA_BusyPointer, FALSE,
        TAG_DONE);
    WaitTOF(); // Wait for pointer update
}

void placeMines() {
    int placed = 0;
    while (placed < NUM_MINES) {
        int x = rand() % GRID_SIZE;
        int y = rand() % GRID_SIZE;
        if (!grid[y][x].isMine) {
            grid[y][x].isMine = 1;
            placed++;
        }
    }
}

void drawGrid() {
    struct RastPort *rp = win->RPort;

    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            int left = GRID_OFFSET_X + x * CELL_SIZE;
            int top = GRID_OFFSET_Y + y * CELL_SIZE;
            int right = left + CELL_SIZE - 1;
            int bottom = top + CELL_SIZE - 1;

            if (grid[y][x].isRevealed) {
                SetAPen(rp, grid[y][x].isMine ? 2 : 3); // Red for mine, green for safe
                RectFill(rp, left, top, right, bottom);

                if (grid[y][x].adjacentMines > 0 && !grid[y][x].isMine) {
                    char buf[2];
                    sprintf(buf, "%d", grid[y][x].adjacentMines);
                    SetAPen(rp, 1); // Black for numbers
                    Move(rp, left + CELL_SIZE / 3, top + CELL_SIZE / 1.5);
                    Text(rp, buf, 1);
                }
            } else if (grid[y][x].isFlagged) {
                SetAPen(rp, 4); // Red for flags
                RectFill(rp, left, top, right, bottom);
                SetAPen(rp, 1); // Black for text
                Move(rp, left + CELL_SIZE / 3, top + CELL_SIZE / 1.5);
                Text(rp, "F", 1);
            } else {
                SetAPen(rp, 1); // Default unrevealed cell
                RectFill(rp, left, top, right, bottom);
            }

            // Grid lines
            SetAPen(rp, 0);
            Move(rp, left, top);
            Draw(rp, right, top);
            Draw(rp, right, bottom);
            Draw(rp, left, bottom);
            Draw(rp, left, top);
        }
    }

    // Draw Game Over message if game ended
    if (!running) {
        SetAPen(rp, 2); // Red for game over text
        Move(rp, GRID_OFFSET_X, BUTTON_Y - 10);
        Text(rp, "Game Over! Click Reset to Try Again", 33);
    }

    // Draw Reset button
    SetAPen(rp, 3); // Green for button background
    RectFill(rp, BUTTON_X, BUTTON_Y, BUTTON_X + BUTTON_WIDTH, BUTTON_Y + BUTTON_HEIGHT);
    SetAPen(rp, 1); // Black for text
    Move(rp, BUTTON_X + 10, BUTTON_Y + 15);
    Text(rp, "Reset", 5);
}

void handleEvents() {
    struct IntuiMessage *msg;

    while (running || !running) { // Keep handling events even after Game Over
        WaitPort(win->UserPort);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            if (msg->Class == IDCMP_CLOSEWINDOW) {
                running = 0; // Close the game
                return;
            } else if (msg->Class == IDCMP_MOUSEBUTTONS) {
                int gridX = (msg->MouseX - GRID_OFFSET_X) / CELL_SIZE;
                int gridY = (msg->MouseY - GRID_OFFSET_Y) / CELL_SIZE;

                if (msg->Code == SELECTDOWN) {
                    // Check for reset button click
                    if (msg->MouseX >= BUTTON_X && msg->MouseX <= BUTTON_X + BUTTON_WIDTH &&
                        msg->MouseY >= BUTTON_Y && msg->MouseY <= BUTTON_Y + BUTTON_HEIGHT) {
                        resetGame();
                        continue; // Skip further processing
                    }

                    // Handle grid clicks
                    if (running && gridX >= 0 && gridX < GRID_SIZE && gridY >= 0 && gridY < GRID_SIZE) {
                        revealCell(gridX, gridY);
                    }
                } else if (msg->Code == MENUDOWN) { // Right-click for flag
                    if (running && gridX >= 0 && gridX < GRID_SIZE && gridY >= 0 && gridY < GRID_SIZE) {
                        toggleFlag(gridX, gridY);
                    }
                }
            }
            ReplyMsg((struct Message *)msg);
        }
    }
}

void resetGame() {
    // Clear the grid and reset the game state
    memset(grid, 0, sizeof(grid));
    running = 1; // Reactivate the game loop
    initGame();  // Reinitialize mines and grid
    drawGrid();  // Redraw the grid
    displayMessage("Game Reset!"); // Show reset message
}

void displayMessage(const char *message) {
    struct RastPort *rp = win->RPort;

    // Position the message just below the reset button
    int messageTop = BUTTON_Y + BUTTON_HEIGHT + 5;
    int messageBottom = messageTop + 15;

    // Clear the message area
    SetAPen(rp, 0); // Background color
    RectFill(rp, BUTTON_X - 10, messageTop, win->Width - 10, messageBottom);

    // Calculate text width and truncate if necessary
    int textWidth = TextLength(rp, message, strlen(message));
    int maxWidth = win->Width - (BUTTON_X + 20);

    char truncatedMessage[256];
    if (textWidth > maxWidth) {
        int charsToFit = strlen(message) * maxWidth / textWidth; // Calculate how many characters fit
        snprintf(truncatedMessage, charsToFit + 1, "%s", message);
        message = truncatedMessage; // Use truncated message
    }

    // Draw the message
    SetAPen(rp, 2); // Text color
    Move(rp, BUTTON_X, messageTop + 10); // Text position
    Text(rp, message, strlen(message));
}

void checkWinCondition() {
    int flaggedMines = 0;
    int revealedCells = 0;

    // Check all cells for win condition
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            if (grid[y][x].isMine && grid[y][x].isFlagged) {
                flaggedMines++;
            }
            if (!grid[y][x].isMine && grid[y][x].isRevealed) {
                revealedCells++;
            }
        }
    }

    if (flaggedMines == NUM_MINES && revealedCells == (GRID_SIZE * GRID_SIZE - NUM_MINES)) {
        struct RastPort *rp = win->RPort; // Declare and assign RastPort
        SetAPen(rp, 3); // Green for success message
        Move(rp, BUTTON_X, BUTTON_Y - 5);
        Text(rp, "Congratulations! You Win!", 26);
        running = 0; // Stop game loop
    }
}


void toggleFlag(int x, int y) {
    if (grid[y][x].isRevealed) return; // Cannot flag revealed cells

    grid[y][x].isFlagged = !grid[y][x].isFlagged; // Toggle flag
    //printf("Cell (%d, %d) %s\n", x, y, grid[y][x].isFlagged ? "Flagged" : "Unflagged");

    drawGrid(); // Redraw the grid
    checkWinCondition(); // Check if the player has won
}

void revealCell(int startX, int startY) {
    if (grid[startY][startX].isRevealed || grid[startY][startX].isFlagged) return;

    setPointerBusy(win); // Set pointer to busy

    // Handle mine explosion
    if (grid[startY][startX].isMine) {
        playBeep();               // Play beep
        drawExplosion(startX, startY); // Show explosion
        shakeWindow(win);             // Shake the window
        displayMessage("BOOM! Game Over!");
        running = 0;
        setPointerNormal(win); // Restore pointer
        return;
    }


    // Start the flood-fill process using the queue
    Queue queue;
    initQueue(&queue);
    enqueue(&queue, startX, startY);

    while (!isQueueEmpty(&queue)) {
        GridPoint p = dequeue(&queue);
        int x = p.x, y = p.y;

        // Bounds check
        if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) continue;
        if (grid[y][x].isRevealed || grid[y][x].isFlagged) continue;

        grid[y][x].isRevealed = 1;

        // Stop spreading if this cell has adjacent mines
        if (grid[y][x].adjacentMines > 0) continue;

        // Enqueue all adjacent cells
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx != 0 || dy != 0) { // Avoid enqueueing the current cell
                    enqueue(&queue, x + dx, y + dy);
                }
            }
        }
    }

    drawGrid(); // Redraw the grid
    setPointerNormal(win); // Restore pointer to normal
}


void drawExplosion(int x, int y) {
    struct RastPort *rp = win->RPort;
    int left = GRID_OFFSET_X + x * CELL_SIZE;
    int top = GRID_OFFSET_Y + y * CELL_SIZE;
    int right = left + CELL_SIZE - 1;
    int bottom = top + CELL_SIZE - 1;

    for (int i = 0; i < 3; i++) { // Flash the explosion
        SetAPen(rp, 2); // Bright red
        RectFill(rp, left, top, right, bottom);
        Delay(5);

        SetAPen(rp, 0); // Clear explosion
        RectFill(rp, left, top, right, bottom);
        Delay(5);
    }

    drawGrid(); // Restore grid appearance
}

void shakeWindow(struct Window *win) {
    int originalLeft = win->LeftEdge;
    int originalTop = win->TopEdge;

    for (int i = 0; i < 6; i++) { // Shake 6 times
        MoveWindow(win, (i % 2 == 0) ? 5 : -5, 0); // Alternate left and right
        Delay(2); // Small delay
    }

    // Restore original position
    MoveWindow(win, originalLeft - win->LeftEdge, originalTop - win->TopEdge);
}

void playBeep() {
    struct MsgPort *audioPort;
    struct IOAudio *audioIO;
    BYTE beepBuffer[256];

    // Fill beep buffer with square wave
    for (int i = 0; i < 256; i++) {
        beepBuffer[i] = (i < 128) ? 64 : -64; // Simple square wave
    }

    // Create message port
    audioPort = CreateMsgPort();

    // Allocate audio IO request
    audioIO = (struct IOAudio *)AllocMem(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR);

    audioIO->ioa_Request.io_Message.mn_ReplyPort = audioPort;

    // Open audio.device
    if (OpenDevice("audio.device", 0, (struct IORequest *)audioIO, 0) != 0) {
        FreeMem(audioIO, sizeof(struct IOAudio));
        DeleteMsgPort(audioPort);
        return;
    }


    // Set up audio request
    audioIO->ioa_Request.io_Command = CMD_WRITE;
    audioIO->ioa_Data = beepBuffer;
    audioIO->ioa_Length = sizeof(beepBuffer);
    audioIO->ioa_Volume = 64;              // Max volume (0-64)
    audioIO->ioa_Period = 3579545 / 440;   // Frequency 440Hz (middle A)
    audioIO->ioa_Cycles = 5;               // Play the buffer 5 times

    // Enable audio DMA
    audioIO->ioa_Request.io_Flags = ADIOF_PERVOL; // Per-channel volume support

    DoIO((struct IORequest *)audioIO);

    // Clean up
    CloseDevice((struct IORequest *)audioIO);
    FreeMem(audioIO, sizeof(struct IOAudio));
    DeleteMsgPort(audioPort);
}


void closeGame() {
    if (win) CloseWindow(win);
}
