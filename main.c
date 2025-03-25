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

#define MUL 2
#define WIN_WIDTH (1920 * MUL)
#define WIN_HEIGHT (1080 * MUL)
#define NUM_BALLS (200 * MUL * MUL)
#define BALL_SIZE 40
#define MAX_SPEED (10 / (FPS / 60))
#define EPSILON 0.001f
#define FPS 60
#define NUM_SECONDS (20)
#define NUM_FRAMES (NUM_SECONDS * FPS)


// define RENDER to render the output
#define RENDER

// define SHOW to show the output in a window. can do both render and show at the same time.
#define SHOWj



typedef struct
{
    bool valid;
    bool attached;
    int attached_to;

    float size;
    float x, y;
    float vx, vy;
    unsigned long color;
    unsigned long original_color;
} Ball;



Ball balls[NUM_BALLS];
Display *display;
Window window;
XColor vscode_gray;
GC gc;
struct timespec start = {0}, end = {0}; 
bool colliding = false;
int core_ball_idx = -1;
bool shell_mode = false;
int shell_ejection_remaining = 0;
float initial_gravity = 0.05f;
float current_gravity = 0.05f;

FILE *ffmpeg;


// merge the music and audio
// ffmpeg -i out.mp4 -i music.mp3 -c:v copy -c:a aac -shortest Balls.mp4


void apply_mutual_gravity()
{
    if (colliding)
        return;

    current_gravity *= 1.001f;

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

            float force = current_gravity * mass_a * mass_b / dist_sqr;

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

float random_speed()
{
    return (float) ((float)(rand() % 2000) / 1000.0f - 1.0f) * MAX_SPEED;
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
    ball->vx = random_speed();
    ball->vy = random_speed();

    for (int j = 0; j < i; j++)
        if (is_overlapping(&balls[i], &balls[j]))
            goto MAKE_RANDOM_POSITION;
    
    ball->valid = True;
    ball->size = BALL_SIZE;
    ball->attached = false;
    ball->attached_to = -1;
}

void generate_random_color(const int i)
{
    uint8_t r, g, b;
    int loop_count = 0;

    MAKE_NEW_COLOR:

    if (loop_count++ > 1<<16)
    {
        PR("%s", "Could not find a random color for a ball.");
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
    balls[i].original_color = balls[i].color;
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

void fade_to_background_if_small(Ball *ball)
{
    if (ball->size >= BALL_SIZE)
        return;

    float t = ball->size / BALL_SIZE; // 1 → full color, 0 → full fade
    if (t < 0.3f) t = 0.3f;
    if (t > 1.0f) t = 1.0f;

    unsigned long orig = ball->original_color;
    uint8_t r0 = (orig >> 16) & 0xFF;
    uint8_t g0 = (orig >> 8) & 0xFF;
    uint8_t b0 = orig & 0xFF;

    // Background: 30,30,30
    uint8_t r = (uint8_t)(r0 * t + 30 * (1.0f - t));
    uint8_t g = (uint8_t)(g0 * t + 30 * (1.0f - t));
    uint8_t b = (uint8_t)(b0 * t + 30 * (1.0f - t));

    ball->color = (r << 16) | (g << 8) | b;
}

void handle_suction(const int i, const int j)
{
    Ball *a = &balls[i];
    Ball *b = &balls[j];

    if (!a->valid || !b->valid)
        return;

    if (!is_overlapping(a, b))
        return;

    Ball *larger = a->size >= b->size ? a : b;
    Ball *smaller = a->size < b->size ? a : b;

    fade_to_background_if_small(smaller);

    // Compute masses (area)
    float mass_large = larger->size * larger->size;
    float mass_small = smaller->size * smaller->size;

    // Momentum transfer
    larger->vx = (larger->vx * mass_large + smaller->vx * mass_small) / (mass_large + mass_small);
    larger->vy = (larger->vy * mass_large + smaller->vy * mass_small) / (mass_large + mass_small);

    // Freeze the smaller ball and attach it
    smaller->vx = 0;
    smaller->vy = 0;
    smaller->attached = true;
    smaller->attached_to = (larger == a) ? i : j;
}

void handle_collision(const int i, const int j)
{
    if (!is_overlapping(&balls[i], &balls[j]))
        return;

    if ((i == core_ball_idx || j == core_ball_idx) && shell_mode)
    {
        Ball *core = &balls[core_ball_idx];
        Ball *other = (i == core_ball_idx) ? &balls[j] : &balls[i];

        // Compute direction from core to other
        float dx = other->x - core->x;
        float dy = other->y - core->y;
        float dist = sqrtf(dx * dx + dy * dy) + EPSILON;
        float nx = dx / dist;
        float ny = dy / dist;
    
        // Project velocity onto normal
        float dot = other->vx * nx + other->vy * ny;
    
        // Reflect velocity
        other->vx -= 2.0f * dot * nx;
        other->vy -= 2.0f * dot * ny;
    
        return;
    }

    Ball *ball_a = &balls[i];
    Ball *ball_b = &balls[j];

    float dx = ball_a->x - ball_b->x;
    float dy = ball_a->y - ball_b->y;
    float dist = sqrtf(dx * dx + dy * dy);

    // just in case the two circles are perfectly overlapping.
    if (dist < 1e-6f)
    {
        dx = 1.0f;
        dy = 0.0f;
        dist = 1.0f;
    }

    const float nx = dx / dist;
    const float ny = dy / dist;

    // --- Positional correction only ---
    const float overlap = BALL_SIZE - dist;
    const float separation = overlap / 2.0f;

    ball_a->x += nx * separation;
    ball_a->y += ny * separation;
    ball_b->x -= nx * separation;
    ball_b->y -= ny * separation;

    // --- Velocity bounce only if approaching ---
    const float rvx = ball_a->vx - ball_b->vx;
    const float rvy = ball_a->vy - ball_b->vy;
    const float velAlongNormal = rvx * nx + rvy * ny;

    if (velAlongNormal < 0.0f)
    {
        const float restitution = 1.0f;
        const float impulse = -(1.0f + restitution) * velAlongNormal / 2.0f;

        const float impulseX = impulse * nx;
        const float impulseY = impulse * ny;

        ball_a->vx += impulseX;
        ball_a->vy += impulseY;
        ball_b->vx -= impulseX;
        ball_b->vy -= impulseY;
    }
}

void explode_shell_from_ball()
{
    Ball *core = &balls[core_ball_idx];
    float area = core->size * core->size;
    int num_shell_balls = (int)(area / (BALL_SIZE * BALL_SIZE));

    if (num_shell_balls < 1) return;

    shell_ejection_remaining = num_shell_balls;
}

void emit_multiple_random_balls()
{
    if (shell_ejection_remaining <= 0)
        return;

    Ball *core = &balls[core_ball_idx];
    float R = core->size / 2.0f;
    float small_r = BALL_SIZE / 2.0f;
    int balls_per_frame = MUL * MUL;
    int emitted = 0;

    for (int emit_try = 0; emit_try < NUM_BALLS * 2 && emitted < balls_per_frame && shell_ejection_remaining > 0; emit_try++)
    {
        int j;
        for (j = 0; j < NUM_BALLS; j++)
            if (!balls[j].valid && j != core_ball_idx)
                break;

        if (j == NUM_BALLS)
            break; // No free slot

        float angle = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)M_PI;
        float dx = cosf(angle);
        float dy = sinf(angle);
        float spawn_distance = R + small_r + 1.0f;

        float px = core->x + dx * spawn_distance;
        float py = core->y + dy * spawn_distance;

        // Avoid overlapping other balls
        bool overlaps = false;
        for (int k = 0; k < NUM_BALLS; k++)
        {
            if (!balls[k].valid || k == j)
                continue;

            float dx2 = balls[k].x - px;
            float dy2 = balls[k].y - py;
            float dist2 = dx2 * dx2 + dy2 * dy2;
            float min_dist = (balls[k].size + BALL_SIZE) / 2.0f + 1.0f;

            if (dist2 < min_dist * min_dist)
            {
                overlaps = true;
                break;
            }
        }

        if (overlaps)
            continue;

        float speed = (((float)(rand() % 1000) / 1000.0f) * MAX_SPEED / 2.0f) + MAX_SPEED / 2.0f;

        balls[j].x = px;
        balls[j].y = py;
        balls[j].vx = dx * speed;
        balls[j].vy = dy * speed;
        balls[j].size = BALL_SIZE;
        balls[j].valid = true;
        balls[j].attached = false;
        balls[j].attached_to = -1;
        balls[j].color = balls[j].original_color;

        // Shrink core by area
        float core_area = core->size * core->size;
        float lost_area = BALL_SIZE * BALL_SIZE;
        core_area = fmaxf(core_area - lost_area, 1.0f);
        core->size = sqrtf(core_area);

        shell_ejection_remaining--;
        emitted++;
    }
}

void update_positions()
{
    void (*handle_ball_interaction)(const int, const int) = (colliding ? handle_collision : handle_suction);

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
                handle_ball_interaction(i, j);
    }


    for (int i = 0; i < NUM_BALLS; i++)
    {
        Ball *ball = &balls[i];
        if (!ball->valid || !ball->attached)
            continue;

        Ball *host = &balls[ball->attached_to];
        if (!host->valid) {
            ball->attached = false;
            ball->attached_to = -1;
            continue;
        }

        // Slowly transfer area
        float area_small = ball->size * ball->size;
        float area_host = host->size * host->size;

        float absorption_rate = 0.1f;
        float transfer = area_small * absorption_rate;

        area_small -= transfer;
        area_host += transfer;

        if (area_small < 1.0f) {
            ball->valid = false;
        } else {
            ball->size = sqrtf(area_small);
            host->size = sqrtf(area_host);

            // Stick ball to surface of host
            float dx = ball->x - host->x;
            float dy = ball->y - host->y;
            float len = sqrtf(dx * dx + dy * dy) + EPSILON;
            float radius = host->size / 2.0f + ball->size / 2.0f;// + 1.0f;

            ball->x = host->x + dx / len * radius;
            ball->y = host->y + dy / len * radius;
        }
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
    static uint8_t rgb_buffer[WIN_WIDTH * WIN_HEIGHT * 3];

    // Fill background with vscode gray: #1e1e1e (30,30,30)
    memset(rgb_buffer, 30, sizeof(rgb_buffer));

    for (int i = 0; i < NUM_BALLS; i++)
    {
        if (!balls[i].valid) continue;

        float radius_f = balls[i].size / 2.0f;
        int radius = (int)(radius_f + 0.5f); // Rounded radius
        int cx = (int)(balls[i].x);          // Already center x
        int cy = (int)(balls[i].y);          // Already center y
        int radius2 = radius * radius;

        // Extract RGB from packed color
        uint32_t color = (uint32_t)balls[i].color;
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;

        for (int dy = -radius; dy <= radius; dy++)
        {
            for (int dx = -radius; dx <= radius; dx++)
            {
                if (dx * dx + dy * dy <= radius2)
                {
                    int px = cx + dx;
                    int py = cy + dy;

                    if (px >= 0 && px < WIN_WIDTH && py >= 0 && py < WIN_HEIGHT)
                    {
                        int idx = (py * WIN_WIDTH + px) * 3;
                        rgb_buffer[idx + 0] = r;
                        rgb_buffer[idx + 1] = g;
                        rgb_buffer[idx + 2] = b;
                    }
                }
            }
        }
    }

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
    int colliding_frames = 0;

    LOOP:
    clock_gettime(CLOCK_MONOTONIC, &start);

    #ifdef SHOW
    if (close_on_key_press())
        return;
    #endif

    draw_screen();
    apply_mutual_gravity();
    update_positions();

    if (existing_balls() == 1)
    {
        wait_frames++;
    }

    if (colliding)
    {
        colliding_frames++;
    }

    if (colliding_frames > 60*8 + MUL*60*1.75)
    {
        colliding_frames = 0;
        colliding = false;
        shell_mode = false;
    }


    if (shell_mode) 
    {
        if (core_ball_idx >= 0 && balls[core_ball_idx].valid)
        {
            emit_multiple_random_balls();
            if (shell_ejection_remaining <= 0)
            {
                shell_mode = false;
                balls[core_ball_idx].size = BALL_SIZE;
                current_gravity = initial_gravity;
            }
        }
        else
        {
            shell_mode = false;
        }
    } 
    else if (wait_frames > 60) 
    {
        wait_frames = 0;
        colliding = true;
        for (int i = 0; i < NUM_BALLS; i++) 
        {
            if (balls[i].valid) 
            {
                core_ball_idx = i;
                shell_mode = true;
                explode_shell_from_ball(&balls[core_ball_idx]);
                break;
            }
        }
    }

    if (!shell_mode)
        core_ball_idx = -1;
    

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
    char *fname = "/home/pi/Documents/Youtube/Balls/Frames/out.mp4";
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


