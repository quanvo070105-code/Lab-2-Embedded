/*
 * BKIT ARM4 - Lab 2 - Exercise 5: Traffic Light with a Pedestrian Button
 *
 * Threads
 *   thread_traffic_light : Y0 = RED, Y1 = GREEN, both = AMBER
 *                          RED 7 s -> GREEN 5 s -> AMBER 2 s -> RED ...
 *   thread_led_blinky    : LED3 1 s on / 1 s off ("system alive")
 *   thread_logger        : the ONLY thread that calls printf (lowest priority)
 * ISR
 *   ped_timer_isr        : simulated pedestrian button (one-shot k_timer,
 *                          first at 10 s, then every 14 s, 8 s, 17 s, ...):
 *                          gives ped_sem and sends an event to log_q.
 *
 * Pedestrian rule: GREEN always lasts at least 2 s. After those 2 s, old
 * requests are discarded with k_sem_reset() and the thread waits for a new
 * request for the remaining green time (k_sem_take with a timeout). A request
 * ends GREEN immediately (-> AMBER). Requests during RED / AMBER have no effect.
 *
 * Centralized logging: other threads and the ISR only put struct evt into
 * log_q with K_NO_WAIT; thread_logger takes the events and prints them.
 */
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#define STACK_SIZE      1024
#define PRIO_LIGHT      7
#define PRIO_BLINKY     7
#define PRIO_LOGGER     10          /* lowest priority of the application */
#define GREEN_MIN_MS    2000

static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios); /* PE4 */
static const struct gpio_dt_spec y0   = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios); /* PE5 RED   */
static const struct gpio_dt_spec y1   = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios); /* PE6 GREEN */

/* ---- Traffic-light phases (constant table, as in Lab 1 Exercise 5) ---- */
enum phase_id { PH_RED, PH_GREEN, PH_AMBER };

struct phase {
    const char *name;
    int y0;
    int y1;
    int ms;
};

static const struct phase phases[] = {
    [PH_RED]   = { "RED",   1, 0, 7000 },
    [PH_GREEN] = { "GREEN", 0, 1, 5000 },
    [PH_AMBER] = { "AMBER", 1, 1, 2000 },
};

static atomic_t cur_phase = ATOMIC_INIT(PH_RED);   /* read by the ISR */

/* ---- Events for the logger ---- */
enum evt_type { EVT_PHASE, EVT_PED_REQUEST, EVT_GREEN_SHORTENED };

struct evt {
    uint8_t  type;      /* enum evt_type */
    uint8_t  phase;     /* enum phase_id */
    uint32_t t_ms;
};

K_MSGQ_DEFINE(log_q, sizeof(struct evt), 16, 4);
K_SEM_DEFINE(ped_sem, 0, 1);                        /* pedestrian request */

static void log_event(uint8_t type, uint8_t phase)  /* thread or ISR */
{
    struct evt e = { .type = type, .phase = phase, .t_ms = k_uptime_get_32() };

    (void)k_msgq_put(&log_q, &e, K_NO_WAIT);        /* never wait */
}

/* ---- Simulated pedestrian button: one-shot timer, ISR context ---- */
static const uint32_t ped_interval_ms[] = { 14000, 8000, 17000 };
#define PED_FIRST_MS    10000

static void ped_timer_isr(struct k_timer *timer)
{
    static unsigned int idx;

    k_sem_give(&ped_sem);
    log_event(EVT_PED_REQUEST, (uint8_t)atomic_get(&cur_phase));

    k_timer_start(timer, K_MSEC(ped_interval_ms[idx]), K_NO_WAIT);
    idx = (idx + 1) % ARRAY_SIZE(ped_interval_ms);
}
K_TIMER_DEFINE(ped_timer, ped_timer_isr, NULL);

/* ---- Threads ---- */
static void set_phase(enum phase_id ph)
{
    atomic_set(&cur_phase, ph);
    gpio_pin_set_dt(&y0, phases[ph].y0);
    gpio_pin_set_dt(&y1, phases[ph].y1);
    log_event(EVT_PHASE, ph);
}

static void thread_traffic_light(void *p1, void *p2, void *p3)
{
    gpio_pin_configure_dt(&y0, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&y1, GPIO_OUTPUT_INACTIVE);

    while (1) {
        /* RED */
        set_phase(PH_RED);
        k_msleep(phases[PH_RED].ms);

        /* GREEN: at least 2 s, then can be shortened by a NEW request */
        set_phase(PH_GREEN);
        k_msleep(GREEN_MIN_MS);
        k_sem_reset(&ped_sem);                       /* discard old requests */
        if (k_sem_take(&ped_sem, K_MSEC(phases[PH_GREEN].ms - GREEN_MIN_MS)) == 0) {
            log_event(EVT_GREEN_SHORTENED, PH_GREEN);
        }

        /* AMBER */
        set_phase(PH_AMBER);
        k_msleep(phases[PH_AMBER].ms);
    }
}

static void thread_led_blinky(void *p1, void *p2, void *p3)
{
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    while (1) {
        gpio_pin_set_dt(&led3, 1);
        k_msleep(1000);
        gpio_pin_set_dt(&led3, 0);
        k_msleep(1000);
    }
}

static void thread_logger(void *p1, void *p2, void *p3)
{
    struct evt e;
    uint32_t green_start = 0;

    printf("Lab 2 - Exercise 5: Traffic light with a pedestrian button\n");
    while (1) {
        k_msgq_get(&log_q, &e, K_FOREVER);

        switch (e.type) {
        case EVT_PHASE:
            if (e.phase == PH_GREEN) {
                green_start = e.t_ms;
            }
            printf("[%6u ms] [LIGHT] %s - %d s\n",
                   e.t_ms, phases[e.phase].name, phases[e.phase].ms / 1000);
            break;
        case EVT_PED_REQUEST:
            if (e.phase != PH_GREEN) {
                printf("[%6u ms] [PED] request during %s -> no effect\n",
                       e.t_ms, phases[e.phase].name);
            } else if (e.t_ms - green_start < GREEN_MIN_MS) {
                printf("[%6u ms] [PED] request in the first 2 s of GREEN -> discarded\n",
                       e.t_ms);
            } else {
                printf("[%6u ms] [PED] request during GREEN\n", e.t_ms);
            }
            break;
        case EVT_GREEN_SHORTENED:
            printf("[%6u ms] [PED] GREEN shortened after %u ms -> AMBER\n",
                   e.t_ms, e.t_ms - green_start);
            break;
        default:
            break;
        }
    }
}

K_THREAD_DEFINE(light_tid,  STACK_SIZE, thread_traffic_light, NULL, NULL, NULL, PRIO_LIGHT,  0, 0);
K_THREAD_DEFINE(blinky_tid, STACK_SIZE, thread_led_blinky,    NULL, NULL, NULL, PRIO_BLINKY, 0, 0);
K_THREAD_DEFINE(logger_tid, 2048,       thread_logger,        NULL, NULL, NULL, PRIO_LOGGER, 0, 0);

int main(void)
{
    /* First pedestrian request at 10 s, the ISR schedules the next ones */
    k_timer_start(&ped_timer, K_MSEC(PED_FIRST_MS), K_NO_WAIT);
    return 0;
}
