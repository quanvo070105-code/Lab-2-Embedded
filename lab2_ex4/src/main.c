/*
 * BKIT ARM4 - Lab 2 - Exercise 4: Collecting Data from an ISR through a
 * Message Queue
 *
 * - Producer: a k_timer with a 200 ms period. Its callback (ISR) builds a
 *   struct sample and sends it with k_msgq_put(..., K_NO_WAIT). Success ->
 *   n_sent++, queue full -> n_drop++ (atomic counters).
 * - Consumer: thread_consumer takes each sample and "processes" it for
 *   PROC_MS (Y0 on PROC_MS/2, off PROC_MS/2), then n_recv++.
 * - thread_monitor prints sent / received / dropped / in_queue every 5 s and
 *   turns LED3 on as soon as one sample has been dropped.
 *
 * Part a: PROC_MS = 500 -> consumer (2 samples/s) slower than producer (5/s)
 * Part b: PROC_MS = 150 -> consumer (6.7 samples/s) faster -> no drop
 */
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#define PROC_MS         500     /* part a: 500, part b: 150 */
#define SAMPLE_PERIOD   200     /* ms */
#define STAT_PERIOD     5000    /* ms */
#define STACK_SIZE      1024
#define PRIORITY        7

static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios); /* PE4 */
static const struct gpio_dt_spec y0   = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios); /* PE5 */

struct sample {
    uint32_t seq;
    uint32_t t_ms;
};

K_MSGQ_DEFINE(sample_q, sizeof(struct sample), 8, 4);

static atomic_t n_sent;
static atomic_t n_drop;
static atomic_t n_recv;

/* Timer callback = ISR: build a sample and send it without waiting */
static void sample_timer_isr(struct k_timer *timer)
{
    static uint32_t seq;
    struct sample s = {
        .seq  = ++seq,
        .t_ms = k_uptime_get_32(),
    };

    if (k_msgq_put(&sample_q, &s, K_NO_WAIT) == 0) {
        atomic_inc(&n_sent);
    } else {
        atomic_inc(&n_drop);            /* -ENOMSG: queue full, sample lost */
    }
}
K_TIMER_DEFINE(sample_timer, sample_timer_isr, NULL);

static void thread_consumer(void *p1, void *p2, void *p3)
{
    struct sample s;

    gpio_pin_configure_dt(&y0, GPIO_OUTPUT_INACTIVE);
    while (1) {
        k_msgq_get(&sample_q, &s, K_FOREVER);   /* empty -> wait */

        gpio_pin_set_dt(&y0, 1);                /* "processing" */
        k_msleep(PROC_MS / 2);
        gpio_pin_set_dt(&y0, 0);
        k_msleep(PROC_MS / 2);

        atomic_inc(&n_recv);
    }
}

static void thread_monitor(void *p1, void *p2, void *p3)
{
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    while (1) {
        k_msleep(STAT_PERIOD);

        long sent = atomic_get(&n_sent);
        long recv = atomic_get(&n_recv);
        long drop = atomic_get(&n_drop);

        printf("[%6u ms] sent: %3ld | received: %3ld | dropped: %3ld | in_queue: %u\n",
               k_uptime_get_32(), sent, recv, drop, k_msgq_num_used_get(&sample_q));

        if (drop > 0) {
            gpio_pin_set_dt(&led3, 1);          /* data loss detected */
        }
    }
}

K_THREAD_DEFINE(consumer_tid, STACK_SIZE, thread_consumer, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(monitor_tid,  STACK_SIZE, thread_monitor,  NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
    printf("Lab 2 - Exercise 4: ISR -> message queue -> thread (PROC_MS = %d)\n", PROC_MS);
    k_timer_start(&sample_timer, K_MSEC(SAMPLE_PERIOD), K_MSEC(SAMPLE_PERIOD));
    return 0;
}
