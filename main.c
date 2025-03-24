#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <omp.h>

#include "macros.h"

#define MUL 3
#define WIN_WIDTH (1920 * MUL)
#define WIN_HEIGHT (1080 * MUL)
#define NUM_BALLS (200 * MUL * MUL)
#define BALL_SIZE 40
#define MAX_SPEED (10 / (FPS / 60))
#define EPSILON 0.001f
#define FPS 60
#define NUM_SECONDS (30 * 60)
#define NUM_FRAMES (NUM_SECONDS * FPS)


// define RENDER to render the output
#define RENDERj

// define SHOW to show the output in a window. can do both render and show at the same time.
#define SHOW



typedef struct
{
    bool valid;
    float size;
    float x, y;
    float vx, vy;
    unsigned long color;
} Ball;



Ball balls[NUM_BALLS];
Display *display;
Window window;
XColor vscode_gray;
GC gc;
struct timespec start = {0}, end = {0}; 


FILE *ffmpeg;

// merge the music and audio
// ffmpeg -i out.mp4 -i music.mp3 -c:v copy -c:a aac -shortest Balls.mp4


void apply_mutual_gravity()
{
    const float G = 0.05f; // Gravitational constant (tweak for desired strength)

    for (int i = 0; i < NUM_BALLS; i++)
    {
        Ball *a = &balls[i];
        if (!a->valid) continue;

        float ax = 0.0f;
        float ay = 0.0f;

        for (int j = 0; j < NUM_BALLS; j++)
        {
            if (i == j) continue;

            Ball *b = &balls[j];
            if (!b->valid) continue;

            float dx = b->x - a->x;
            float dy = b->y - a->y;
            float dist_sqr = dx * dx + dy * dy + EPSILON; // Add EPSILON to avoid divide-by-zero
            float dist = sqrtf(dist_sqr);

            float mass_a = a->size * a->size;
            float mass_b = b->size * b->size;

            float force = G * mass_a * mass_b / dist_sqr;

            ax += force * dx / dist; // Normalize direction
            ay += force * dy / dist;
        }

        a->vx += ax / (a->size * a->size); // F = ma => a = F/m
        a->vy += ay / (a->size * a->size);
    }
}

bool is_overlapping(const Ball *a, const Ball *b)
{
    const float dx = a->x - b->x;
    const float dy = a->y - b->y;
    const float center_dist = sqrtf(dx * dx + dy * dy);
    const float min_center_dist = (a->size + b->size)/2 + EPSILON;
    return center_dist < min_center_dist;
}

void set_ball_position(const int i)
{
    Ball *ball = &balls[i];
    int loop_count = 0;

    MAKE_RANDOM_POSITION:

    if (loop_count++ > 1<<16)
    {
        PR("%s", "Could not find a random position for a ball.");
        exit(EXIT_FAILURE);
    }

    ball->x =  (float) (rand() % (WIN_WIDTH - BALL_SIZE*2)) + BALL_SIZE;
    ball->y =  (float) (rand() % (WIN_HEIGHT - BALL_SIZE*2)) + BALL_SIZE;
    ball->vx = (float) ((float)(rand() % 200) / 100.0f - 1.0f) * MAX_SPEED;
    ball->vy = (float) ((float)(rand() % 200) / 100.0f - 1.0f) * MAX_SPEED;

    for (int j = 0; j < i; j++)
        if (is_overlapping(&balls[i], &balls[j]))
            goto MAKE_RANDOM_POSITION;
    
    ball->valid = True;
    ball->size = BALL_SIZE;
}

void generate_random_color(const int i)
{
    uint8_t r, g, b;
    int loop_count = 0;

    MAKE_NEW_COLOR:

    if (loop_count++ > 1<<16)
    {
        PR("%s", "Could not find a random position for a ball.");
        exit(EXIT_FAILURE);
    }

    r = (uint8_t)rand();
    g = (uint8_t)rand();
    b = (uint8_t)rand();

    // too dark
    if ((int)r + (int)g + (int)b < 150)
        goto MAKE_NEW_COLOR;

    // same color as the another ball.
    for (int j = 0; j < i; ++j)
        if (balls[i].color == balls[j].color)
            goto MAKE_NEW_COLOR;

    balls[i].color = (r << 16) | (g << 8) | b;   
}

void make_balls()
{
    srand((unsigned int)time(NULL));

    #pragma omp parallel for schedule (static)
    for (int i = 0; i < NUM_BALLS; i++)
    {
        generate_random_color(i);
        set_ball_position(i);
    }
}

bool fsame(const float a, const float b)
{
    const float diff = a - b;
    return (diff > 0 ? diff < 1e-6 : diff > -1e-6);
}

void handle_collision(const int i, const int j)
{
    if (!is_overlapping(&balls[i], &balls[j]))
        return;

    Ball *ball_a = &balls[i];
    Ball *ball_b = &balls[j];

    const float ma = ball_a->size * ball_a->size;
    const float mb = ball_b->size * ball_b->size;
    const float new_size = sqrtf(ma + mb);

    const float kgvax = ma * ball_a->vx;
    const float kgvay = ma * ball_a->vy;
    const float kgvbx = mb * ball_b->vx;
    const float kgvby = mb * ball_b->vy;

    const float new_xv = (kgvbx + kgvax) / (mb + ma);
    const float new_yv = (kgvby + kgvay) / (mb + ma);

    if (fsame(ball_a->size, ball_b->size))
    {
        if ((ball_a->vx * ball_a->vx + ball_a->vy * ball_a->vy) >
            (ball_b->vx * ball_b->vx + ball_b->vy * ball_b->vy))
            ball_b->valid = false;
        else
            ball_a->valid = false;
    }
    else if (ball_a->size > ball_b->size)
        ball_b->valid = false;
    else
        ball_a->valid = false;

    Ball *merged = ball_a->valid ? ball_a : ball_b;
    merged->size = new_size;
    merged->vx = new_xv;
    merged->vy = new_yv;
}

void update_positions()
{
    for (int i = 0; i < NUM_BALLS; i++)
    {
        Ball *ball = &balls[i];
        if (!ball->valid)
            continue;
    
        ball->x += ball->vx;
        ball->y += ball->vy;
    
        float radius = ball->size / 2.0f;
    
        // Bounce off the left or right wall
        if (ball->x - radius < 0)
        {
            ball->x = radius;
            ball->vx *= -1;
        }
        else if (ball->x + radius > WIN_WIDTH)
        {
            ball->x = WIN_WIDTH - radius;
            ball->vx *= -1;
        }
    
        // Bounce off the top or bottom wall
        if (ball->y - radius < 0)
        {
            ball->y = radius;
            ball->vy *= -1;
        }
        else if (ball->y + radius > WIN_HEIGHT)
        {
            ball->y = WIN_HEIGHT - radius;
            ball->vy *= -1;
        }
    }
    

    for (int i = 0; i < NUM_BALLS; i++)
    {
        if (! balls[i].valid) continue;
        for (int j = i + 1; j < NUM_BALLS; j++)
            if (balls[j].valid) 
                handle_collision(i, j);
    }
}

void setup_display()
{
    display = XOpenDisplay(NULL);

    if (!display)
    {
        PR("%s", "Cannot open display");
        exit(EXIT_FAILURE);
    }

    int screen = DefaultScreen(display);
    Colormap colormap = DefaultColormap(display, screen);
    XParseColor(display, colormap, "#1e1e1e", &vscode_gray);
    XAllocColor(display, colormap, &vscode_gray);

    window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                        100, 100, WIN_WIDTH, WIN_HEIGHT, 1,
                                        BlackPixel(display, screen), vscode_gray.pixel);

    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    gc = XCreateGC(display, window, 0, NULL);
}

int timer()
{
    // static double total_time = 0;
    // static int frame_count = 0;

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Always do the math, don't check start.tv_sec == 0 — it's always set
    double elapsed =
        (double)(end.tv_sec - start.tv_sec) * 1000.0 +
        (double)(end.tv_nsec - start.tv_nsec) / 1e6;

    // total_time += elapsed;
    // frame_count++;

    // if (frame_count % (FPS * 2) == 0 && elapsed > 3)
    // {
    //     PR("Avg frame time: %.3f ms (%.1f FPS)", elapsed, 1000.0 / elapsed);
    //     return 0;
    // }
    return (int) (elapsed * 1000 + 67);
}

bool close_on_key_press()
{
    while (XPending(display))
    {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == KeyPress)
        {
            PR("%s", "Key pressed, exiting...");
            return true;
        }
    }
    return false;
}

void pipe_to_ffmpeg()
{

    // --- 2. Manually draw into an RGB24 pixel buffer ---
    static uint8_t rgb_buffer[WIN_WIDTH * WIN_HEIGHT * 3];

    // Fill background with vscode gray: #1e1e1e (30,30,30)
    memset(rgb_buffer, 30, sizeof(rgb_buffer));

    // --- 3. Draw circles into the buffer ---
    for (int i = 0; i < NUM_BALLS; i++)
    {
        int cx = (int)(balls[i].x + BALL_SIZE / 2.0f);
        int cy = (int)(balls[i].y + BALL_SIZE / 2.0f);
        int radius = BALL_SIZE / 2;
        int radius2 = radius * radius;

        // Extract color from X11 color (ARGB) to RGB
        uint32_t color = (uint32_t)balls[i].color;
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;

        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= radius2) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px >= 0 && px < WIN_WIDTH && py >= 0 && py < WIN_HEIGHT) {
                        int idx = (py * WIN_WIDTH + px) * 3;
                        rgb_buffer[idx + 0] = r;
                        rgb_buffer[idx + 1] = g;
                        rgb_buffer[idx + 2] = b;
                    }
                }
            }
        }
    }

    // --- 4. Write frame to ffmpeg pipe ---
    fwrite(rgb_buffer, 1, WIN_WIDTH * WIN_HEIGHT * 3, ffmpeg);
}

void stop_recording()
{
    if (ffmpeg) {
        fflush(ffmpeg);
        pclose(ffmpeg);
        ffmpeg = NULL;
        printf("Recording stopped and file finalized.\n");
    }
}

void draw_screen()
{
    #ifdef SHOW
    XClearWindow(display, window);
    for (int i = 0; i < NUM_BALLS; i++)
    {
        Ball *b = &balls[i];
        if (!b->valid) continue;
        XSetForeground(display, gc, b->color);
        XFillArc(display, window, gc,
                    (int)(b->x - b->size/2), (int)(b->y - b->size/2),
                    (unsigned int)b->size, (unsigned int)b->size, 0, 360 * 64);
    }
    XFlush(display);
    #endif

    #ifdef RENDER
    pipe_to_ffmpeg();
    #endif
}


int existing_balls()
{
    int sum = 0;
    for (int i = 0; i < NUM_BALLS; ++i)
        sum += (int) balls[i].valid;
    return sum;
}

void simulate()
{
    int num_frames = 0;
    int wait_frames = 0;

    LOOP:
    clock_gettime(CLOCK_MONOTONIC, &start);

    #ifdef SHOW
    if (close_on_key_press())
        return;
    #endif

    apply_mutual_gravity();
    update_positions();
    draw_screen();


    if (existing_balls() == 1)
    {
        wait_frames++;
    }
    if (wait_frames > 60)
    {
        wait_frames = 0;
        memset(balls, 0, sizeof(balls));
        make_balls();
    }

    const int delay  = (1000000 / FPS) - timer();
    if (delay > 0) usleep((__useconds_t)delay); 
    timer();

    if (++num_frames > NUM_FRAMES)
        return;

    goto LOOP;
}

int main()
{
    #ifdef RENDER
    char *fname = "out.mp4";///home/pi/Documents/Youtube/Balls/Frames/out.mp4";
    char command[256];
    sprintf(command, "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size %dx%d -framerate 60 "
        "-i - -vf format=yuv420p -c:v libx264 -preset fast %s", WIN_WIDTH, WIN_HEIGHT, fname);
    ffmpeg = popen(command, "w");
    #endif


    #ifdef SHOW
    setup_display();
    #endif


    make_balls();
    simulate();

    #ifdef SHOW
    XFreeGC(display, gc);
    XCloseDisplay(display);
    #endif

    #ifdef RENDER
    stop_recording();
    #endif

    exit(EXIT_SUCCESS);
}


